#ifndef NV_KEY_PARSER_H_
#define NV_KEY_PARSER_H_

#include <stdint.h>

// Ctrl modifier
#define KeyCtrl(k) (0x10000000 | (k))
// Shift modifier
#define KeyShift(k) (0x20000000 | (k))
// Alt modifier
#define KeyAlt(k) (0x40000000 | (k))
// Meta modifier
#define KeyMeta(k) (0x80000000 | (k))
// Remove the modifiers from a key
#define KeyRaw(k) (0x0fffffff & (k))

// Check if a key has a modifier, k is the key and mod is the modifier macro.
#define KeyHasMod(k, mod) (mod(k) ^ (k) == mod(0))

// Special keys
enum {
    Key_None = 0,
    Key_Tab = KeyCtrl('i'),
    Key_Enter = KeyCtrl('m'),
    Key_Escape = 0x1b,
    Key_Backspace = 0x7f,

    // From here on the keys use values that cannot be in Unicode

    Key_ArrowLeft = 0x110000,
    Key_ArrowRight,
    Key_ArrowUp,
    Key_ArrowDown,
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
    Key_Delete,

    Key_MAX
};

typedef uint32_t Key;

#endif // !NV_KEY_PARSER_H_
