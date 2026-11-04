#include "nv_test.h"
#include "nv_key.h"

void test_bindRootMapExists(void) {
    int32_t id = bindAddRootMap();
    testAssert(bindRootMapExists(id));
    testAssert(!bindRootMapExists(id + 1));
    keyQuit();
}

testList(
    testMake(test_bindRootMapExists)
)
