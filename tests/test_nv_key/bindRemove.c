#include "nv_test.h"
#include "nv_key.h"

void test_bindRemove(void) {
    int32_t root = bindAddRootMap();

    bindAdd(root, BindSeq('a'), (Bind){ 0 });
    testAssert(bindExists(root, BindSeq('a')));
    bindRemove(root, BindSeq('a'));
    testAssert(!bindExists(root, BindSeq('a')));
    keyQuit();
}

testList(
    testMake(test_bindRemove)
)
