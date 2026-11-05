#include "nv_test.h"
#include "nv_binds.h"

void test_bindAddRootMap(void) {
    int32_t id0 = bindAddRootMap();
    int32_t id1 = bindAddRootMap();
    testAssert(id0 != id1);
    bindQuit();
}

testList(
    testMake(test_bindAddRootMap)
)
