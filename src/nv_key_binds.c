#include <stdio.h>
#include "nv_key_binds.h"
#include "nv_pool.h"

#define _minMapCap 4

BindMap g_bindRoots = { 0 };

Pool g_bindPool = {
    .blockSize = sizeof(Bind),
    .pageSize = sizeof(Bind) * 2048
};

static BindMap *_mapInsert(BindMap *map, BindMap entry);
static BindMap *_mapGet(BindMap *map, int32_t key);
static bool _mapRemove(BindMap *map, int32_t key);
static BindMatchResult _bindMatchRec(
    BindMap *map,
    int32_t *seq,
    Bind *outBind
);

int32_t bindAddRootMap(void) {
    BindMap map = { .key = g_bindRoots.len + 1 };
    _mapInsert(&g_bindRoots, map);
    return map.key;
}

bool bindRootMapExists(int32_t id) {
    return _mapGet(&g_bindRoots, id) != NULL;
}

void _printMap(BindMap *map, uint32_t indent) {
    printf(
        "%*s[%d] %x: %p\n",
        indent * 2, "",
        map->len,
        map->key,
        (void *)map->value
    );
    for (uint16_t i = 0; i < map->cap; i++) {
        if (map->nodes[i].key != Key_None) {
            _printMap(&map->nodes[i], indent + 1);
        }
    }
}

void bindPrintRoot(int32_t root) {
    BindMap *map = _mapGet(&g_bindRoots, root);
    if (map == NULL) {
        printf("NULL\n");
        return;
    }
    _printMap(map, 0);
}

void bindAdd(int32_t root, BindKeys seq, Bind keyBind) {
    BindMap *map = _mapGet(&g_bindRoots, root);
    if (map == NULL) return;
    while (*seq != BindEnd) {
        BindMap *newMap = _mapGet(map, *seq);
        if (newMap == NULL) {
            newMap = _mapInsert(map, (BindMap){ .key = *seq });
        }
        map = newMap;
        seq++;
    }

    if (map->value != NULL) {
        poolFree(&g_bindPool, map->value);
    }
    Bind *bind = poolAlloc(&g_bindPool);
    *bind = keyBind;
    map->value = bind;
}

bool bindRemove(int32_t root, BindKeys seq) {
    BindMap *map = _mapGet(&g_bindRoots, root);
    if (map == NULL) return false;

    // When deleting a key binding, if it is a leaf node of the tree, the leaf
    // must also be deleted. However this could lead to another leaf that has
    // no binding breaking the invariant of the data structure. To do this we
    // store the last valid node along the sequence along the part of the
    // sequence after it.

    BindMap *lastValidNode = map;
    int32_t branch = *seq;
    while (*seq != BindEnd) {
        map = _mapGet(map, *seq);
        seq++;

        // If the sequence does not exist.
        if (map == NULL) return false;

        if (map->len >= 2 || (map->len == 1 && map->value != NULL)) {
            lastValidNode = map;
            branch = *seq;
        }
    }
    // If the sequence does not end at a key bind.
    if (map->value == NULL) return false;
    poolFree(&g_bindPool, map->value);
    map->value = NULL;
    // If the sequence ends on a valid node 'branch' is equal to BindEnd and
    // nothing is deleted.
    _mapRemove(lastValidNode, branch);
    return true;
}

bool bindExists(int32_t root, BindKeys seq) {
    BindMap *map = _mapGet(&g_bindRoots, root);
    if (map == NULL) return false;
    while (*seq != BindEnd && map != NULL) {
        map = _mapGet(map, *seq);
        seq++;
    }
    return map != NULL && map->value != NULL;
}

BindMatchResult bindMatch(int32_t root, BindKeys seq, Bind *outBind) {
    BindMap *rootMap = _mapGet(&g_bindRoots, root);
    if (rootMap == NULL) return BindMatch_NotFound;
    return _bindMatchRec(rootMap, seq, outBind);
}

static BindMatchResult _bindMatchRec(
    BindMap *map,
    int32_t *seq,
    Bind *outBind
) {
    if (seq[0] == BindEnd) {
        if (map->value == NULL) {
            return map->len == 0 ? BindMatch_NotFound : BindMatch_Incomplete;
        } else {
            *outBind = *map->value;
            return map->len == 0 ? BindMatch_Found : BindMatch_Partial;
        }
    }

    BindMap *wildcard = _mapGet(map, BindAny);
    BindMap *specific = _mapGet(map, seq[0]);

    if (!wildcard && !specific) {
        return BindMatch_NotFound;
    } else if ((wildcard && !specific) || (!wildcard && specific)) {
        return _bindMatchRec(wildcard ? wildcard : specific, seq + 1, outBind);
    }

    // At this point we know that we are not at the end of the sequence and
    // need to explore both branches.

    /*
     * A table that shows the outcome based on the result of exploring the two
     * possible branches. In parenthesis is the key bind taken where it applies.
     *
     *                               Specific
     *                Not F.     Incom.     Found      Part.
     *              +----------+----------+----------+----------+
     *   W   Not F. | Not F.   | Incom.   | Found(S) | Part.(S) |
     *   i          +----------+----------+----------+----------+
     *   l   Incom. | Incom.   | Incom.   | Part.(S) | Part.(S) |
     *   d          +----------+----------+----------+----------+
     *   c   Found  | Found(W) | Part.(W) | Found(S) | Part.(S) |
     *   a          +----------+----------+----------+----------+
     *   r   Part.  | Part.(W) | Part.(W) | Part.(S) | Part.(S) |
     *   d          +----------+----------+----------+----------+
     *
     */

    Bind wildBind = { 0 };
    Bind specBind = { 0 };

    BindMatchResult wildRes = _bindMatchRec(wildcard, seq + 1, &wildBind);
    BindMatchResult specRes = _bindMatchRec(specific, seq + 1, &specBind);

    *outBind = specRes >= BindMatch_Found ? specBind : wildBind;
    // If the values lie in the diagonal
    if (wildRes + specRes == BindMatch_Partial) {
        return BindMatch_Partial;
    }
    return nvMax(wildRes, specRes);
}

static int32_t _keyHash(int32_t key) {
    uint32_t ukey = (uint32_t)key;
    // Use some bits from outside the unicode range
    return (int32_t)((ukey >> 21) ^ ukey);
}

static uint16_t _mapIdx(BindMap *nodes, uint16_t cap, int32_t key) {
    int32_t hash = _keyHash(key);
    for (uint16_t i = 0; i < cap - 1; i++) {
        uint16_t idx = (hash + i) & (cap - 1);
        if (nodes[idx].key == Key_None || nodes[idx].key == key) {
            return idx;
        }
    }
    return (hash + cap - 1) & (cap - 1);
}

static void _mapDestroy(BindMap *map) {
    if (map->value != NULL) {
        poolFree(&g_bindPool, map->value);
    }
    if (map->nodes == NULL) return;
    for (uint16_t i = 0; i < map->cap; i++) {
        if (map->nodes[i].key != Key_None) {
            _mapDestroy(&map->nodes[i]);
        }
    }
    memFree(map->nodes);
}

static void _mapExpand(BindMap *map) {
    uint16_t newCap = nvMax(map->cap * 2, _minMapCap);
    BindMap *newMapNodes = memAllocZeroed(newCap, sizeof(*newMapNodes));

    for (uint16_t i = 0; i < map->cap; i++) {
        if (map->nodes[i].key == Key_None) continue;
        uint16_t newIdx = _mapIdx(newMapNodes, newCap, map->nodes[i].key);
        newMapNodes[newIdx] = map->nodes[i];
    }
    memFree(map->nodes);
    map->nodes = newMapNodes;
    map->cap = newCap;
}

static BindMap *_mapInsert(BindMap *map, BindMap entry) {
    nvAssertExpr(map != NULL);
    if (map->len >= (map->cap >> 2) + (map->cap >> 1)) {
        _mapExpand(map);
    }
    uint16_t idx = _mapIdx(map->nodes, map->cap, entry.key);
    if (map->nodes[idx].key != Key_None) {
        _mapDestroy(&map->nodes[idx]);
    } else {
        map->len++;
    }
    map->nodes[idx] = entry;
    return &map->nodes[idx];
}

static BindMap *_mapGet(BindMap *map, int32_t key) {
    if (map->len == 0) return NULL;
    uint16_t idx = _mapIdx(map->nodes, map->cap, key);
    return map->nodes[idx].key == Key_None ? NULL : &map->nodes[idx];
}

static bool _mapRemove(BindMap *map, int32_t key) {
    uint16_t idx = _mapIdx(map->nodes, map->cap, key);
    if (map->nodes[idx].key == Key_None) return false;
    _mapDestroy(&map->nodes[idx]);
    uint16_t mask = map->cap - 1;
    idx = (idx + 1) & mask;
    while (
        map->nodes[idx].key != Key_None
        && (_keyHash(map->nodes[idx].key) & mask) != idx
    ) {
        uint16_t prevIdx = (idx - 1) & mask;
        map->nodes[prevIdx] = map->nodes[idx];
        map->nodes[idx] = (BindMap){ 0 };
        idx = (idx + 1) & mask;
    }
    return true;
}

void bindQuit(void) {
    _mapDestroy(&g_bindRoots);
    g_bindRoots = (BindMap){ 0 };
    poolDestroy(&g_bindPool);
}

void bindInvokeCallback(Bind bind, BindKeys seq) {
    switch (bind.callback.kind) {
    case BindCallback_NoArgs:
        bind.callback.func.noArgs();
        break;
    case BindCallback_Ptr:
        bind.callback.func.ptrArg.func(bind.callback.func.ptrArg.arg);
        break;
    case BindCallback_Int:
        bind.callback.func.intArg.func(bind.callback.func.intArg.arg);
        break;
    case BindCallback_Keys:
        bind.callback.func.keysArg(seq);
        break;
    case BindCallback_PtrKeys:
        bind.callback.func.ptrKeys.func(bind.callback.func.ptrKeys.arg, seq);
        break;
    }
}
