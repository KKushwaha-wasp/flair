#include "flair/display/DisplayObject.h"
#include "flair/display/DisplayObjectContainer.h"
#include "flair/display/Stage.h"

#include <stdexcept>

using flair::geom::Rectangle;
using flair::geom::Matrix;
using flair::geom::Point;

namespace flair {
   namespace display {
      
      DisplayObject::DisplayObject() : _x(0.0f), _y(0.0f), _rotation(0.0f), _scaleX(1.0f), _scaleY(1.0f), _alpha(1.0f), _width(0.0f), _height(0.0f)
      {
         _parent = std::weak_ptr<DisplayObjectContainer>();
      }
      
      DisplayObject::~DisplayObject()
      {
         
      }
      
      std::string DisplayObject::name() const
      {
         return _name;
      }
      
      std::string DisplayObject::name(std::string name)
      {
         return _name = name;
      }
      
      float DisplayObject::alpha() const
      {
         return _alpha;
      }
      
      float DisplayObject::alpha(float alpha)
      {
         return _alpha = alpha;
      }
      
      const Rectangle DisplayObject::bounds() const
      {
         return _bounds;
      }
      
      bool DisplayObject::hasVisibleArea() const
      {
         return _hasVisibleArea;
      }
      
      float DisplayObject::height() const
      {
         return _height * _scaleY;
      }
      
      float DisplayObject::height(float height)
      {
         if (_height > 0.0f) {
            _scaleY = height / _height;
         }
         return this->height();
      }
      
      float DisplayObject::width() const
      {
         return _width * _scaleY;
      }
      
      float DisplayObject::width(float width)
      {
         if (_width > 0.0f) {
            _scaleX = width / _width;
         }
         return this->width();
      }
      
      float DisplayObject::x() const
      {
         return _x;
      }
      
      float DisplayObject::x(float x)
      {
         return _x = x;
      }
      
      float DisplayObject::y() const
      {
         return _y;
      }
      
      float DisplayObject::y(float y)
      {
         return _y = y;
      }
      
      std::shared_ptr<Stage> DisplayObject::stage() const
      {
         return std::dynamic_pointer_cast<Stage>(root());
      }
      
      const std::shared_ptr<DisplayObjectContainer> DisplayObject::root() const
      {
         std::shared_ptr<DisplayObject> currentObject = std::const_pointer_cast<DisplayObject>(std::dynamic_pointer_cast<const DisplayObject>(shared_from_this()));
         while (currentObject->parent())
         {
            auto root = std::dynamic_pointer_cast<Stage>(currentObject->parent());
            if (root) {
               return root;
            }
            else {
               currentObject = currentObject->parent();
            }
         }
         
         return std::shared_ptr<DisplayObjectContainer>();
      }
      
      const std::shared_ptr<DisplayObjectContainer> DisplayObject::parent() const
      {
            return _parent.lock();
      }
      
      Matrix DisplayObject::transformationMatrix() const
      {
         geom::Matrix transform;
         transform.rotate(_rotation);
         transform.scale(_scaleX, _scaleY);
         transform.translate(_x, _y);
         return transform;
      }
      
      Matrix DisplayObject::transformationMatrix(Matrix m)
      {
         // TODO: Set the x, y, rotation, scale, skew
         return _transformationMatrix = m;
      }
      
      float DisplayObject::pivotX() const
      {
         return _pivotX;
      }
      
      float DisplayObject::pivotX(float pivotX)
      {
         return _pivotY;
      }
      
      float DisplayObject::rotation() const
      {
         return _rotation;
      }
      
      float DisplayObject::rotation(float rotation)
      {
         return _rotation = rotation;
      }
      
      float DisplayObject::scaleX() const
      {
         return _scaleX;
      }
      
      float DisplayObject::scaleX(float scaleX)
      {
         return _scaleX = scaleX;
      }
      
      float DisplayObject::scaleY() const
      {
         return _scaleY;
      }
      
      float DisplayObject::scaleY(float scaleY)
      {
         return _scaleY = scaleY;
      }
      
      float DisplayObject::skewX() const
      {
         return _skewX;
      }
      
      float DisplayObject::skewX(float skewX)
      {
         return _skewX = skewX;
      }
      
      float DisplayObject::skewY() const
      {
         return _skewY;
      }
      
      float DisplayObject::skewY(float skewY)
      {
         return _skewY = skewY;
      }
      
      bool DisplayObject::touchable() const
      {
         return _touchable;
      }
      
      bool DisplayObject::touchable(bool touchable)
      {
         return _touchable = touchable;
      }
      
      bool DisplayObject::visible() const
      {
         return _visible;
      }
      
      bool DisplayObject::visible(bool visible)
      {
         return _visible = visible;
      }
      
      Rectangle DisplayObject::getBounds(std::shared_ptr<DisplayObject> targetSpace) const
      {
         Rectangle r;
         return r;
      }
      
      Matrix DisplayObject::getTransformationMatrix(std::shared_ptr<DisplayObject> targetSpace) const
      {
         Matrix m;
         return m;
      }
      
      Point DisplayObject::globalToLocal(Point localPoint) const
      {
         Point p;
         return p;
      }
      
      std::shared_ptr<DisplayObject> DisplayObject::hitTest(Point localPoint, bool forTouch) const
      {
         return std::shared_ptr<DisplayObject>();
      }
      
      void DisplayObject::setParent(std::shared_ptr<DisplayObjectContainer> parent)
      {
         std::shared_ptr<DisplayObject> ancestor = parent;
         while (ancestor && ancestor.get() != this) {
            ancestor = ancestor->parent();
         }
         
         if (ancestor.get() == this) {
            throw std::invalid_argument("An object cannot be added as a child to itself or one of its children (or children's children, etc.)");
         }
         else {
            _parent = parent;
         }
      }
      
      void DisplayObject::render(RenderSupport* support, float parentAlpha, geom::Matrix parentTransform)
      {
         
      }
      
   }
}
