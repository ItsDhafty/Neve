#ifndef NV_BINDS_H_
#define NV_BINDS_H_

#include <stdint.h>
#include <stdbool.h>
#include "nv_key.h"

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

void bindQuit(void);

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

#endif // !NV_BINDS_H_
