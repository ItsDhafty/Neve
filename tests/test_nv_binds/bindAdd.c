#include "nv_test.h"
#include "nv_binds.h"

static Bind emptyBind = { 0 };

void test_bindAddToRoot(void) {
    int32_t root = bindAddRootMap();
    bindAdd(root, BindSeq('a'), emptyBind);
    bindAdd(root, BindSeq('b'), emptyBind);
    bindAdd(root, BindSeq(BindAny), emptyBind);
    testAssert(bindExists(root, BindSeq('a')));
    testAssert(bindExists(root, BindSeq('b')));
    testAssert(bindExists(root, BindSeq(BindAny)));

    bindQuit();
}

void test_bindAddExtending(void) {
    int32_t root = bindAddRootMap();
    bindAdd(root, BindSeq('a'), emptyBind);
    bindAdd(root, BindSeq('a', 'b'), emptyBind);
    bindAdd(root, BindSeq('a', 'b', 'c'), emptyBind);
    bindAdd(root, BindSeq('a', 'c'), emptyBind);
    testAssert(bindExists(root, BindSeq('a')));
    testAssert(bindExists(root, BindSeq('a', 'b')));
    testAssert(bindExists(root, BindSeq('a', 'b', 'c')));
    testAssert(bindExists(root, BindSeq('a', 'c')));
    bindQuit();
}

testList(
    testMake(test_bindAddToRoot),
    testMake(test_bindAddExtending),
)
