#ifndef flair_internal_ConcurrentQueue_h
#define flair_internal_ConcurrentQueue_h

#include <atomic>
#include <type_traits>
#include <utility>
#include <cassert>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <new>


// A lock-free queue for a single-consumer, single-producer architecture.
// The queue is also wait-free in the common path (except if more memory
// needs to be allocated, in which case malloc is called).
// Allocates memory sparingly (O(lg(n) times, amortized), and only once if
// the original maximum size estimate is never exceeded.
// Tested on x86/x64 processors, but semantics should be correct for all
// architectures (given the right implementations in atomicops.h), provided
// that aligned integer and pointer accesses are naturally atomic.
// Note that there should only be one consumer thread and producer thread;
// Switching roles of the threads, or using multiple consecutive threads for
// one role, is not safe unless properly synchronized.
// Using the queue exclusively from one thread is fine, though a bit silly.

#define CACHE_LINE_SIZE 64

namespace flair {
   namespace internal {
      template<typename T, size_t MAX_BLOCK_SIZE = 512>
      class ConcurrentQueue
      {
        // Design: Based on a queue-of-queues. The low-level queues are just
        // circular buffers with front and tail indices indicating where the
        // next element to dequeue is and where the next element can be enqueued,
        // respectively. Each low-level queue is called a "block". Each block
        // wastes exactly one element's worth of space to keep the design simple
        // (if front == tail then the queue is empty, and can't be full).
        // The high-level queue is a circular linked list of blocks; again there
        // is a front and tail, but this time they are pointers to the blocks.
        // The front block is where the next element to be dequeued is, provided
        // the block is not empty. The back block is where elements are to be
        // enqueued, provided the block is not full.
        // The producer thread owns all the tail indices/pointers. The consumer
        // thread owns all the front indices/pointers. Both threads read each
        // other's variables, but only the owning thread updates them. E.g. After
        // the consumer reads the producer's tail, the tail may change before the
        // consumer is done dequeuing an object, but the consumer knows the tail
        // will never go backwards, only forwards.
        // If there is no room to enqueue an object, an additional block (of
        // equal size to the last block) is added. Blocks are never removed.

      public:
        // Constructs a queue that can hold maxSize elements without further
        // allocations. If more than MAX_BLOCK_SIZE elements are requested,
        // then several blocks of MAX_BLOCK_SIZE each are reserved (including
        // at least one extra buffer block).
        explicit ConcurrentQueue(size_t maxSize = 15) : enqueuing(false), dequeuing(false)
        {
            assert(maxSize > 0);
            assert(MAX_BLOCK_SIZE == ceilToPow2(MAX_BLOCK_SIZE) && "MAX_BLOCK_SIZE must be a power of 2");
            assert(MAX_BLOCK_SIZE >= 2 && "MAX_BLOCK_SIZE must be at least 2");

            block* firstBlock = nullptr;

            largestBlockSize = ceilToPow2(maxSize + 1);		// We need a spare slot to fit maxSize elements in the block
            if (largestBlockSize > MAX_BLOCK_SIZE * 2) {
                // We need a spare block in case the producer is writing to a different block the consumer is reading from, and
                // wants to enqueue the maximum number of elements. We also need a spare element in each block to avoid the ambiguity
                // between front == tail meaning "empty" and "full".
                // So the effective number of slots that are guaranteed to be usable at any time is the block size - 1 times the
                // number of blocks - 1. Solving for maxSize and applying a ceiling to the division gives us (after simplifying):
                size_t initialBlockCount = (maxSize + MAX_BLOCK_SIZE * 2 - 3) / (MAX_BLOCK_SIZE - 1);
                largestBlockSize = MAX_BLOCK_SIZE;
                block* lastBlock = nullptr;
                for (size_t i = 0; i != initialBlockCount; ++i) {
                    auto block = make_block(largestBlockSize);
                    if (block == nullptr) {
                        throw std::bad_alloc();
                    }
                    if (firstBlock == nullptr) {
                        firstBlock = block;
                    }
                    else {
                        lastBlock->next = block;
                    }
                    lastBlock = block;
                    block->next = firstBlock;
                }
            }
            else {
                firstBlock = make_block(largestBlockSize);
                if (firstBlock == nullptr) {
                    throw std::bad_alloc();
                }
                firstBlock->next = firstBlock;
            }
            frontBlock = firstBlock;
            tailBlock = firstBlock;

            // Make sure the reader/writer threads will have the initialized memory setup above:
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }

        // Note: The queue should not be accessed concurrently while it's
        // being deleted. It's up to the user to synchronize this.
        ~ConcurrentQueue()
        {
            // Make sure we get the latest version of all variables from other CPUs:
            std::atomic_thread_fence(std::memory_order_seq_cst);

            // Destroy any remaining objects in queue and free memory
            block* frontBlock_ = frontBlock;
            block* blck = frontBlock_;
            do {
                block* nextBlock = blck->next;
                size_t blockFront = blck->front;
                size_t blockTail = blck->tail;

                for (size_t i = blockFront; i != blockTail; i = (i + 1) & blck->sizeMask) {
                    auto element = reinterpret_cast<T*>(blck->data + i * sizeof(T));
                    element->~T();
                    (void)element;
                }

                auto rawBlock = blck->rawThis;
                blck->~block();
                std::free(rawBlock);
                blck = nextBlock;
            } while (blck != frontBlock_);
        }


        // Enqueues a copy of element if there is room in the queue.
        // Returns true if the element was enqueued, false otherwise.
        // Does not allocate memory.
        inline bool try_enqueue(T const& element)
        {
            return inner_enqueue<CannotAlloc>(element);
        }

        // Enqueues a moved copy of element if there is room in the queue.
        // Returns true if the element was enqueued, false otherwise.
        // Does not allocate memory.
        inline bool try_enqueue(T&& element)
        {
            return inner_enqueue<CannotAlloc>(std::forward<T>(element));
        }


        // Enqueues a copy of element on the queue.
        // Allocates an additional block of memory if needed.
        // Only fails (returns false) if memory allocation fails.
        inline bool enqueue(T const& element)
        {
            return inner_enqueue<CanAlloc>(element);
        }

        // Enqueues a moved copy of element on the queue.
        // Allocates an additional block of memory if needed.
        // Only fails (returns false) if memory allocation fails.
        inline bool enqueue(T&& element)
        {
            return inner_enqueue<CanAlloc>(std::forward<T>(element));
        }


        // Attempts to dequeue an element; if the queue is empty,
        // returns false instead. If the queue has at least one element,
        // moves front to result using operator=, then returns true.
        template<typename U>
        bool try_dequeue(U& result)
        {
            reentrant_guard guard(this->dequeuing);

            // High-level pseudocode:
            // Remember where the tail block is
            // If the front block has an element in it, dequeue it
            // Else
            //     If front block was the tail block when we entered the function, return false
            //     Else advance to next block and dequeue the item there

            // Note that we have to use the value of the tail block from before we check if the front
            // block is full or not, in case the front block is empty and then, before we check if the
            // tail block is at the front block or not, the producer fills up the front block *and
            // moves on*, which would make us skip a filled block. Seems unlikely, but was consistently
            // reproducible in practice.
            // In order to avoid overhead in the common case, though, we do a double-checked pattern
            // where we have the fast path if the front block is not empty, then read the tail block,
            // then re-read the front block and check if it's not empty again, then check if the tail
            // block has advanced.

            block* frontBlock_ = frontBlock.load();
            size_t blockTail = frontBlock_->localTail;
            size_t blockFront = frontBlock_->front.load();

            if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
                std::atomic_thread_fence(std::memory_order_acquire);

            non_empty_front_block:
                // Front block not empty, dequeue from here
                auto element = reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
                result = std::move(*element);
                element->~T();

                blockFront = (blockFront + 1) & frontBlock_->sizeMask;

                std::atomic_thread_fence(std::memory_order_release);
                frontBlock_->front = blockFront;
            }
            else if (frontBlock_ != tailBlock.load()) {
                std::atomic_thread_fence(std::memory_order_acquire);

                frontBlock_ = frontBlock.load();
                blockTail = frontBlock_->localTail = frontBlock_->tail.load();
                blockFront = frontBlock_->front.load();
                std::atomic_thread_fence(std::memory_order_acquire);

                if (blockFront != blockTail) {
                    // Oh look, the front block isn't empty after all
                    goto non_empty_front_block;
                }

                // Front block is empty but there's another block ahead, advance to it
                block* nextBlock = frontBlock_->next;
                // Don't need an acquire fence here since next can only ever be set on the tailBlock,
                // and we're not the tailBlock, and we did an acquire earlier after reading tailBlock which
                // ensures next is up-to-date on this CPU in case we recently were at tailBlock.

                size_t nextBlockFront = nextBlock->front.load();
                size_t nextBlockTail = nextBlock->localTail = nextBlock->tail.load();
                std::atomic_thread_fence(std::memory_order_acquire);

                // Since the tailBlock is only ever advanced after being written to,
                // we know there's for sure an element to dequeue on it
                assert(nextBlockFront != nextBlockTail);
                (void)(nextBlockTail);

                // We're done with this block, let the producer use it if it needs
                std::atomic_thread_fence(std::memory_order_release);		// Expose possibly pending changes to frontBlock->front from last dequeue
                frontBlock = frontBlock_ = nextBlock;

                std::atomic_signal_fence(std::memory_order_release);	// Not strictly needed

                auto element = reinterpret_cast<T*>(frontBlock_->data + nextBlockFront * sizeof(T));

                result = std::move(*element);
                element->~T();

                nextBlockFront = (nextBlockFront + 1) & frontBlock_->sizeMask;

                std::atomic_thread_fence(std::memory_order_release);
                frontBlock_->front = nextBlockFront;
            }
            else {
                // No elements in current block and no other block to advance to
                return false;
            }

            return true;
        }


        // Returns a pointer to the front element in the queue (the one that
        // would be removed next by a call to `try_dequeue` or `pop`). If the
        // queue appears empty at the time the method is called, nullptr is
        // returned instead.
        // Must be called only from the consumer thread.
        T* peek()
        {
            reentrant_guard guard(this->dequeuing);
            // See try_dequeue() for reasoning

            block* frontBlock_ = frontBlock.load();
            size_t blockTail = frontBlock_->localTail;
            size_t blockFront = frontBlock_->front.load();

            if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
                std::atomic_thread_fence(std::memory_order_acquire);
            non_empty_front_block:
                return reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
            }
            else if (frontBlock_ != tailBlock.load()) {
                std::atomic_thread_fence(std::memory_order_acquire);
                frontBlock_ = frontBlock.load();
                blockTail = frontBlock_->localTail = frontBlock_->tail.load();
                blockFront = frontBlock_->front.load();
                std::atomic_thread_fence(std::memory_order_acquire);

                if (blockFront != blockTail) {
                    goto non_empty_front_block;
                }

                block* nextBlock = frontBlock_->next;

                size_t nextBlockFront = nextBlock->front.load();
                std::atomic_thread_fence(std::memory_order_acquire);

                assert(nextBlockFront != nextBlock->tail.load());
                return reinterpret_cast<T*>(nextBlock->data + nextBlockFront * sizeof(T));
            }

            return nullptr;
        }

        // Removes the front element from the queue, if any, without returning it.
        // Returns true on success, or false if the queue appeared empty at the time
        // `pop` was called.
        bool pop()
        {
               reentrant_guard guard(this->dequeuing);
               // See try_dequeue() for reasoning

               block* frontBlock_ = frontBlock.load();
               size_t blockTail = frontBlock_->localTail;
               size_t blockFront = frontBlock_->front.load();

               if (blockFront != blockTail || blockFront != (frontBlock_->localTail = frontBlock_->tail.load())) {
                   std::atomic_thread_fence(std::memory_order_acquire);

               non_empty_front_block:
                   auto element = reinterpret_cast<T*>(frontBlock_->data + blockFront * sizeof(T));
                   element->~T();

                   blockFront = (blockFront + 1) & frontBlock_->sizeMask;

                   std::atomic_thread_fence(std::memory_order_acquire);
                   frontBlock_->front = blockFront;
               }
               else if (frontBlock_ != tailBlock.load()) {
                   std::atomic_thread_fence(std::memory_order_acquire);
                   frontBlock_ = frontBlock.load();
                   blockTail = frontBlock_->localTail = frontBlock_->tail.load();
                   blockFront = frontBlock_->front.load();
                   std::atomic_thread_fence(std::memory_order_acquire);

                   if (blockFront != blockTail) {
                       goto non_empty_front_block;
                   }

                   // Front block is empty but there's another block ahead, advance to it
                   block* nextBlock = frontBlock_->next;

                   size_t nextBlockFront = nextBlock->front.load();
                   size_t nextBlockTail = nextBlock->localTail = nextBlock->tail.load();
                   std::atomic_thread_fence(std::memory_order_acquire);

                   assert(nextBlockFront != nextBlockTail);
                   (void)(nextBlockTail);

                   std::atomic_signal_fence(std::memory_order_release);
                   frontBlock = frontBlock_ = nextBlock;

                   std::atomic_signal_fence(std::memory_order_release);

                   auto element = reinterpret_cast<T*>(frontBlock_->data + nextBlockFront * sizeof(T));
                   element->~T();

                   nextBlockFront = (nextBlockFront + 1) & frontBlock_->sizeMask;

                   std::atomic_thread_fence(std::memory_order_release);
                   frontBlock_->front = nextBlockFront;
               }
               else {
                   // No elements in current block and no other block to advance to
                   return false;
               }

               return true;
           }

           // Returns the approximate number of items currently in the queue.
           // Safe to call from both the producer and consumer threads.
           inline size_t size_approx() const
           {
               size_t result = 0;
               block* frontBlock_ = frontBlock.load();
               block* block = frontBlock_;
               do {
                   std::atomic_thread_fence(std::memory_order_acquire);
                   size_t blockFront = block->front.load();
                   size_t blockTail = block->tail.load();
                   result += (blockTail - blockFront) & block->sizeMask;
                   block = block->next.load();
               } while (block != frontBlock_);
               return result;
           }


       private:
           enum AllocationMode { CanAlloc, CannotAlloc };

           template<AllocationMode canAlloc, typename U>
           bool inner_enqueue(U&& element)
           {
               reentrant_guard guard(this->enqueuing);

               // High-level pseudocode (assuming we're allowed to alloc a new block):
               // If room in tail block, add to tail
               // Else check next block
               //     If next block is not the head block, enqueue on next block
               //     Else create a new block and enqueue there
               //     Advance tail to the block we just enqueued to

               block* tailBlock_ = tailBlock.load();
               size_t blockFront = tailBlock_->localFront;
               size_t blockTail = tailBlock_->tail.load();

               size_t nextBlockTail = (blockTail + 1) & tailBlock_->sizeMask;
               if (nextBlockTail != blockFront || nextBlockTail != (tailBlock_->localFront = tailBlock_->front.load())) {
                   std::atomic_thread_fence(std::memory_order_acquire);
                   // This block has room for at least one more element
                   char* location = tailBlock_->data + blockTail * sizeof(T);
                   new (location)T(std::forward<U>(element));

                   std::atomic_thread_fence(std::memory_order_release);
                   tailBlock_->tail = nextBlockTail;
               }
               else {
                   std::atomic_thread_fence(std::memory_order_acquire);
                   if (tailBlock_->next.load() != frontBlock) {
                       // Note that the reason we can't advance to the frontBlock and start adding new entries there
                       // is because if we did, then dequeue would stay in that block, eventually reading the new values,
                       // instead of advancing to the next full block (whose values were enqueued first and so should be
                       // consumed first).

                       std::atomic_thread_fence(std::memory_order_acquire);		// Ensure we get latest writes if we got the latest frontBlock

                       // tailBlock is full, but there's a free block ahead, use it
                       block* tailBlockNext = tailBlock_->next.load();
                       size_t nextBlockFront = tailBlockNext->localFront = tailBlockNext->front.load();
                       nextBlockTail = tailBlockNext->tail.load();
                       std::atomic_signal_fence(std::memory_order_acquire);

                       // This block must be empty since it's not the head block and we
                       // go through the blocks in a circle
                       assert(nextBlockFront == nextBlockTail);
                       tailBlockNext->localFront = nextBlockFront;

                       char* location = tailBlockNext->data + nextBlockTail * sizeof(T);
                       new (location)T(std::forward<U>(element));

                       tailBlockNext->tail = (nextBlockTail + 1) & tailBlockNext->sizeMask;

                       std::atomic_thread_fence(std::memory_order_release);
                       tailBlock = tailBlockNext;
                   }
                   else if (canAlloc == CanAlloc) {
                       // tailBlock is full and there's no free block ahead; create a new block
                       auto newBlockSize = largestBlockSize >= MAX_BLOCK_SIZE ? largestBlockSize : largestBlockSize * 2;
                       auto newBlock = make_block(newBlockSize);
                       if (newBlock == nullptr) {
                           // Could not allocate a block!
                           return false;
                       }
                       largestBlockSize = newBlockSize;

                       new (newBlock->data) T(std::forward<U>(element));

                       assert(newBlock->front == 0);
                       newBlock->tail = newBlock->localTail = 1;

                       newBlock->next = tailBlock_->next.load();
                       tailBlock_->next = newBlock;

                       // Might be possible for the dequeue thread to see the new tailBlock->next
                       // *without* seeing the new tailBlock value, but this is OK since it can't
                       // advance to the next block until tailBlock is set anyway (because the only
                       // case where it could try to read the next is if it's already at the tailBlock,
                       // and it won't advance past tailBlock in any circumstance).

                       std::atomic_thread_fence(std::memory_order_release);
                       tailBlock = newBlock;
                   }
                   else if (canAlloc == CannotAlloc) {
                       // Would have had to allocate a new block to enqueue, but not allowed
                       return false;
                   }
                   else {
                       assert(false && "Should be unreachable code");
                       return false;
                   }
               }

               return true;
           }


           // Disable copying
           ConcurrentQueue(ConcurrentQueue const&) = delete;

           // Disable assignment
           ConcurrentQueue& operator=(ConcurrentQueue const&) = delete;

           inline static size_t ceilToPow2(size_t x)
           {
               // From http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
               --x;
               x |= x >> 1;
               x |= x >> 2;
               x |= x >> 4;
               for (size_t i = 1; i < sizeof(size_t); i <<= 1) {
                   x |= x >> (i << 3);
               }
               ++x;
               return x;
           }

           template<typename U>
           static inline char* align_for(char* ptr)
           {
               const std::size_t alignment = std::alignment_of<U>::value;
               return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
           }
       private:
           struct reentrant_guard
           {
               reentrant_guard(bool& _inSection)
                   : inSection(_inSection)
               {
                   assert(!inSection);
                   if (inSection) {
                       throw std::runtime_error("ReaderWriterQueue does not support enqueuing or dequeuing elements from other elements' ctors and dtors");
                   }

                   inSection = true;
               }

               ~reentrant_guard() { inSection = false; }

           private:
               reentrant_guard& operator=(reentrant_guard const&);

           private:
               bool& inSection;
           };

           struct block
           {
               // Avoid false-sharing by putting highly contended variables on their own cache lines
               std::atomic< size_t> front;	// (Atomic) Elements are read from here
               size_t localTail;			// An uncontended shadow copy of tail, owned by the consumer

               char cachelineFiller0[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)];
               std::atomic<size_t> tail;	// (Atomic) Elements are enqueued here
               size_t localFront;

               char cachelineFiller1[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>) - sizeof(size_t)];	// next isn't very contended, but we don't want it on the same cache line as tail (which is)
               std::atomic<block*> next;	// (Atomic)

               char* data;		// Contents (on heap) are aligned to T's alignment

               const size_t sizeMask;


               // size must be a power of two (and greater than 0)
               block(size_t const& _size, char* _rawThis, char* _data)
                   : front(0), localTail(0), tail(0), localFront(0), next(nullptr), data(_data), sizeMask(_size - 1), rawThis(_rawThis)
               {
               }

           private:
               // C4512 - Assignment operator could not be generated
               block& operator=(block const&);

           public:
               char* rawThis;
           };


           static block* make_block(size_t capacity)
           {
               // Allocate enough memory for the block itself, as well as all the elements it will contain
               auto size = sizeof(block) + std::alignment_of<block>::value - 1;
               size += sizeof(T) * capacity + std::alignment_of<T>::value - 1;
               auto newBlockRaw = static_cast<char*>(std::malloc(size));
               if (newBlockRaw == nullptr) {
                   return nullptr;
               }

               auto newBlockAligned = align_for<block>(newBlockRaw);
               auto newBlockData = align_for<T>(newBlockAligned + sizeof(block));
               return new (newBlockAligned)block(capacity, newBlockRaw, newBlockData);
           }

       private:
           std::atomic<block*> frontBlock;		// (Atomic) Elements are enqueued to this block

           char cachelineFiller[CACHE_LINE_SIZE - sizeof(std::atomic<block*>)];
           std::atomic<block*> tailBlock;		// (Atomic) Elements are dequeued from this block

           size_t largestBlockSize;

           bool enqueuing;
           bool dequeuing;
       };
   }
}

/* The code in this file is based on Cameron Desrocher's readerwriterqueue
 * (https://github.com/cameron314/readerwriterqueue) and is used under the
 * following license.
 *
 * Simplified BSD License:
 *
 * Copyright (c) 2013-2015, Cameron Desrochers. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#endif
