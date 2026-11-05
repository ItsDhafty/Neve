#include "nv_test.h"
#include "nv_binds.h"

void test_bindRootMapExists(void) {
    int32_t id = bindAddRootMap();
    testAssert(bindRootMapExists(id));
    testAssert(!bindRootMapExists(id + 1));
    bindQuit();
}

testList(
    testMake(test_bindRootMapExists)
)
