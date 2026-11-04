#ifndef NV_KEY_H_
#define NV_KEY_H_

#include <stdbool.h>
#include <stdint.h>

/******************************* Init and Deinit ******************************/

bool keyInit(void);
void keyQuit(void);

/********************************* Key events *********************************/

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

Key keyGet(void);

/********************************** Key Binds *********************************/

// Create a bind sequence: appends Key_None at the end to end the sequence.
#define BindSeq(...) ((BindKeys){ __VA_ARGS__, Key_None })
// Create a bind callback with no arguments.
#define BindCBNoArgs(fn)                                                       \
    ((BindCallback){                                                           \
        .kind = BindCallback_NoArgs,                                           \
        .func.noArgs = (fn)                                                    \
    })
// Create a bind callback with a pointer argument.
#define BindCBPtr(fn, usr)                                                     \
    ((BindCallback){                                                           \
        .kind = BindCallback_Ptr,                                              \
        .func.ptrArg = {                                                       \
            .func = (void(*)(void *))(fn),                                     \
            .arg = (void *)(usr)                                               \
        }                                                                      \
    })
// Create a bind callback with an int argument.
#define BindCBInt(fn, usr)                                                     \
    ((BindCallback){                                                           \
        .kind = BindCallback_Ptr,                                              \
        .func.intArg = { .func = (void(*)(int))(fn), .arg = (usr) }            \
    })
// Create a bind callback with a BindKeys argument.
#define BindCBKeys(fn)                                                         \
    ((BindCallback){                                                           \
        .kind = BindCallback_Keys,                                             \
        .func.keysArg = (fn)                                                   \
    })
// Create a bind callback with pointer and BindKeys arguments.
#define BindCBPtrKeys(fn, usr)                                                 \
    ((BindCallback){                                                           \
        .kind = BindCallback_PtrKeys,                                          \
        .func.ptrArg = {                                                       \
            .func = ((void)(*)(void *, BindKeys))(fn),                         \
            .arg = (void *)(usr) }                                             \
    })

typedef int32_t BindKeys[];

typedef union BindCallbackFunc {
    void (*noArgs)(void);
    struct {
        void (*func)(void *);
        void *arg;
    } ptrArg;
    struct {
        void (*func)(int);
        int arg;
    } intArg; // for enums
    void (*keysArg)(BindKeys);
    struct {
        void (*func)(void *, BindKeys);
        void *arg;
    } ptrKeys;
} BindCallbackFunc;

typedef enum BindCallbackKind {
    BindCallback_NoArgs,
    BindCallback_Ptr,
    BindCallback_Int,
    BindCallback_Keys,
    BindCallback_PtrKeys
} BindCallbackKind;

typedef struct BindCallback {
    BindCallbackKind kind;
    BindCallbackFunc func;
} BindCallback;

typedef struct Bind {
    BindCallback callback;
    bool hasSelection;
} Bind;

typedef struct BindMap {
    Bind *value;
    int32_t key;

    uint16_t cap;
    uint16_t len;
    struct BindMap *nodes;
} BindMap;

// NOTE: the relative order of match results must not be changed as it is
// used to simplify the code in the implementation

typedef enum BindMatchResult {
    // No sequence matches this binding.
    BindMatch_NotFound = 0,
    // The sequence can continue but it currently leads to no binding.
    BindMatch_Incomplete = 1,
    // The sequence unambiguously matches a binding.
    BindMatch_Found = 2,
    // The sequence can continue but it is already a valid binding.
    BindMatch_Partial = 3
} BindMatchResult;

// Note that if two sequences are only distinguished by a wildcard, the one
// without 'BindAny' takes precedence and is reported as found. For example
// say that:
// x,y triggers A
// x,BindAny triggers B
// The sequence x,y will find result in BindMatch_Found with A as the action.

// Add a root map for the bindings and return its ID.
int32_t bindAddRootMap(void);
// Check if a root map exists. Using other functions with a non-existent root
// will silently fail.
bool bindRootMapExists(int32_t id);

// Add a key bind, override the existing one if present.
void bindAdd(int32_t root, BindKeys seq, Bind keyBind);
// Remove a key bind, return `true` if a key bind was removed and `false` if no
// action was taken. `BindAny` is matched only to itself.
bool bindRemove(int32_t root, BindKeys seq);
// Check if a bind exists.
bool bindExists(int32_t root, BindKeys seq);
// Find the best-match for a sequence.
BindMatchResult bindMatch(int32_t root, BindKeys seq, Bind *outBind);

// Print a root tree. Useful for debugging.
void bindPrintRoot(int32_t root);

// Invoke the callback of a key bind.
void bindInvokeCallback(Bind bind, BindKeys seq);

#endif // !NV_KEY_H_
