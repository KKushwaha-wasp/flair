#ifndef flair_ui_Keyboard_h
#define flair_ui_Keyboard_h

namespace flair {
  namespace ui {

     class Keyboard
     {
     public:
        
        // KeyCodes
        enum {
           BACKSPACE = 8,
           TAB = 9,
           
           ENTER = 13,
           
           COMMAND = 15,
           SHIFT = 16,
           CONTROL = 17,
           ALTERNATE = 18,
           
           CAPS_LOCK = 20,
           NUMPAD = 21,
           
           ESCAPE = 27,
           
           SPACE = 32,
           PAGE_UP = 33,
           PAGE_DOWN = 34,
           END = 35,
           HOME = 36,
           LEFT = 37,
           UP = 38,
           RIGHT = 39,
           DOWN = 40,
           
           NUMBER_0 = 48,
           NUMBER_1 = 49,
           NUMBER_2 = 50,
           NUMBER_3 = 51,
           NUMBER_4 = 52,
           NUMBER_5 = 53,
           NUMBER_6 = 54,
           NUMBER_7 = 55,
           NUMBER_8 = 56,
           NUMBER_9 = 57,
           
           A = 65,
           B = 66,
           C = 67,
           D = 68,
           E = 69,
           F = 70,
           G = 71,
           H = 72,
           I = 73,
           J = 74,
           K = 75,
           L = 76,
           M = 77,
           N = 78,
           O = 79,
           P = 80,
           Q = 81,
           R = 82,
           S = 83,
           T = 84,
           U = 85,
           V = 86,
           W = 87,
           X = 88,
           Y = 89,
           Z = 90,
           
           F1 = 112,
           F2 = 113,
           F3 = 114,
           F4 = 115,
           F5 = 116,
           F6 = 117,
           F7 = 118,
           F8 = 119,
           F9 = 120,
           F10 = 121,
           F11 = 122,
           F12 = 123,
           F13 = 124,
           F14 = 125,
           F15 = 126,
           
           SEMICOLON = 186,
           EQUAL = 187,
           COMMA = 188,
           MINUS = 189,
           PERIOD = 190,
           SLASH = 191,
           BACKQUOTE = 192,
           
           LEFTBRACKET = 219,
           BACKSLASH = 220,
           RIGHTBRACKET = 221,
           QUOTE = 222,
           
           _KEY_COUNT // Internal for sizing the key array
        };
      
     // Properties
     public:
        static bool capsLock();
        
        static bool hasVirtualKeyboard();
        
        static bool numLock();
        
        static int physicalKeyboardType();
        
     private:
        Keyboard();

     };

  }
}
#endif
