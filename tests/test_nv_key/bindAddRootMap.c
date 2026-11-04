#include "nv_test.h"
#include "nv_key.h"

void test_bindAddRootMap(void) {
    int32_t id0 = bindAddRootMap();
    int32_t id1 = bindAddRootMap();
    testAssert(id0 != id1);
    keyQuit();
}

testList(
    testMake(test_bindAddRootMap)
)
