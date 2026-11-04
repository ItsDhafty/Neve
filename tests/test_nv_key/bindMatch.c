#include "nv_test.h"
#include "nv_key.h"

typedef BindMatchResult BMR;

void test_bindMatchNotFound(void) {
    int32_t root = bindAddRootMap();

    Bind bind = { 0 };
    bindAdd(root, BindSeq('x'), bind);
    bindAdd(root, BindSeq('x', 'y'), bind);
    bindAdd(root, BindSeq('x', 'z'), bind);
    bindAdd(root, BindSeq('w'), bind);

    BMR result;
    result = bindMatch(root, BindSeq('a'), &bind);
    testAssert(result == BindMatch_NotFound);
    result = bindMatch(root, BindSeq('x', 'w'), &bind);
    testAssert(result == BindMatch_NotFound);
    result = bindMatch(root, BindSeq('x', 'y', 'z'), &bind);
    testAssert(result == BindMatch_NotFound);

    keyQuit();
}

void test_bindMatchIncomplete(void) {
    int32_t root = bindAddRootMap();

    Bind bind = { 0 };
    bindAdd(root, BindSeq('x', 'y', 'z'), bind);
    bindAdd(root, BindSeq('a', BindAny, 'c'), bind);

    BMR result;
    result = bindMatch(root, BindSeq('x', 'y'), &bind);
    testAssert(result == BindMatch_Incomplete);
    result = bindMatch(root, BindSeq('a', 'b'), &bind);
    testAssert(result == BindMatch_Incomplete);

    keyQuit();
}

void test_bindMatchFound(void) {
    int32_t root = bindAddRootMap();

    Bind bind = { 0 };
    bindAdd(root, BindSeq('x', 'y', 'z'), bind);
    bindAdd(root, BindSeq('a', BindAny, 'c'), bind);
    bindAdd(root, BindSeq('i'), (Bind){ .callback.kind = BindCallback_Ptr });
    bindAdd(root, BindSeq(BindAny), (Bind){ .callback.kind = BindCallback_Int });

    bindPrintRoot(root);

    BMR result;
    result = bindMatch(root, BindSeq('x', 'y', 'z'), &bind);
    testAssert(result == BindMatch_Found);
    result = bindMatch(root, BindSeq('a', 'b', 'c'), &bind);
    testAssert(result == BindMatch_Found);
    result = bindMatch(root, BindSeq('a', 'b', 'c'), &bind);
    testAssert(result == BindMatch_Found);
    result = bindMatch(root, BindSeq('i'), &bind);
    testAssert(result == BindMatch_Found);
    testAssert(bind.callback.kind == BindCallback_Ptr);
    result = bindMatch(root, BindSeq('j'), &bind);
    testAssert(result == BindMatch_Found);
    testAssert(bind.callback.kind == BindCallback_Int);

    keyQuit();
}

void test_bindMatchPartial(void) {
    int32_t root = bindAddRootMap();

    Bind bind = { 0 };
    bindAdd(root, BindSeq('x', 'y'), bind);
    bindAdd(root, BindSeq('x', 'y', 'z'), bind);
    bindAdd(root, BindSeq('a', 'b'), bind);
    bindAdd(root, BindSeq('a', BindAny, 'c'), bind);
    bindAdd(root, BindSeq('i', BindAny, 'k'), bind);
    bindAdd(root, BindSeq('i', 'j', 'k', 'l'), bind);

    BMR result;
    result = bindMatch(root, BindSeq('x', 'y'), &bind);
    testAssert(result == BindMatch_Partial);
    result = bindMatch(root, BindSeq('a', 'b'), &bind);
    testAssert(result == BindMatch_Partial);
    result = bindMatch(root, BindSeq('i', 'j', 'k'), &bind);
    testAssert(result == BindMatch_Partial);

    keyQuit();
}

testList(
    // testMake(test_bindMatchNotFound),
    // testMake(test_bindMatchIncomplete),
    testMake(test_bindMatchFound),
    // testMake(test_bindMatchPartial),
)
