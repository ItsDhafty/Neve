#ifndef NV_KEY_H_
#define NV_KEY_H_

#include <stdbool.h>
#include <stdint.h>

// The top byte is used as a bitmask for key modifiers.

#define KeyShift(k) (0x01000000 | (k))
#define KeyAlt(k)   (0x02000000 | (k))
#define KeyCtrl(k)  (0x04000000 | (k))
#define KeySuper(k) (0x08000000 | (k))
#define KeyHyper(k) (0x10000000 | (k))
#define KeyMeta(k)  (0x20000000 | (k))
#define KeyCapsL(k) (0x40000000 | (k))
#define KeyNumL(k)  (0x80000000 | (k))

// Remove the modifiers from a key
#define KeyRaw(k)   (0xffffff & (k))
// Check if a key has a modifier, k is the key and mod is the modifier macro.
#define KeyHasMod(k, mod) (mod(k) ^ (k) == 0)

// Special keys
enum {
    Key_None = 0,
    Key_Tab = KeyCtrl('i'),
    Key_Enter = KeyCtrl('m'),
    Key_Escape = KeyCtrl('['),

    // From here on the keys use values that cannot be in Unicode

    Key_Left = 0x110000,
    Key_Right,
    Key_Up,
    Key_Down,
    Key_F1,
    Key_F2,
    Key_F3,
    Key_F4,
    Key_F5,
    Key_F6,
    Key_F7,
    Key_F8,
    Key_F9,
    Key_F10,
    Key_F11,
    Key_F12,
    Key_Home,
    Key_End,
    Key_Insert,
    Key_Delete,
    Key_Backspace,
    Key_PageUp,
    Key_PageDown,

    BindAny
};

typedef uint32_t Key;

bool keyInit(void);
void keyQuit(void);
Key keyGet(void);

#endif // !NV_KEY_H_
