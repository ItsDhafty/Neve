#include <stdio.h>

#include "clib_threads.h"
#include "nv_error.h"
#include "nv_key.h"
#include "nv_logging.h"
#include "nv_pool.h"
#include "nv_term.h"

#define QueueNextIdx(idx) (((idx) + 1) % KeyQueueSize)
#define KeyQueueSize 4096

static Thread g_inputThread;
static bool g_inputThreadRun = true;

BindMap g_bindRoots = { 0 };

typedef struct Queue {
    uint16_t front, back;
    Key keys[KeyQueueSize];
} Queue;

// Queue of raw terminal input to be parsed.
static Queue g_rawQueue = { 0 };
// Queue of parsed terminal input to be handled.
static Queue g_parsedQueue = { 0 };

static const Key recurringKeys[] = {
    [  2] = Key_Insert,
    [  3] = Key_Delete,
    [  5] = Key_PageUp,
    [  6] = Key_PageDown,
    [  7] = Key_Home,
    [ 11] = Key_F1,
    [ 12] = Key_F2,
    [ 13] = Key_F3,
    [ 14] = Key_F4,
    [ 15] = Key_F5,
    [ 17] = Key_F6,
    [ 18] = Key_F7,
    [ 19] = Key_F8,
    [ 20] = Key_F9,
    [ 21] = Key_F10,
    [ 23] = Key_F11,
    [ 24] = Key_F12,
    ['A'] = Key_Up,
    ['B'] = Key_Down,
    ['C'] = Key_Right,
    ['D'] = Key_Left,
    ['F'] = Key_End,
    ['H'] = Key_Home,
    ['P'] = Key_F1,
    ['Q'] = Key_F2,
    ['R'] = Key_F3,
    ['S'] = Key_F4
};

static Key _tryParseKey(void);
static Key _charToKey(Key k);
static size_t _readEscapeSeq(uint32_t *outBuf, size_t bufCap);
void _keyQEnq(Queue *queue, Key key);
bool _keyQDeq(Queue *queue, Key *outKey);
bool _keyQAt(const Queue *queue, size_t idx, Key *outKey);
void _keyQDrop(Queue *queue, size_t count);
size_t _keyQIsFull(Queue *queue);

void _keyQEnq(Queue *queue, Key key) {
    if (QueueNextIdx(queue->back) == queue->front) {
        return;
    }
    queue->keys[queue->back] = key;
    queue->back = QueueNextIdx(queue->back);
}

bool _keyQDeq(Queue *queue, Key *outKey) {
    if (queue->front == queue->back) {
        if (outKey) *outKey = Key_None;
        return false;
    }
    if (outKey) *outKey = queue->keys[queue->front];
    queue->front = QueueNextIdx(queue->front);
    return true;
}

bool _keyQAt(const Queue *queue, size_t idx, Key *outKey) {
    size_t len = queue->front <= queue->back
        ? queue->back - queue->front
        : (queue->back + KeyQueueSize) - queue->front;

    if (idx >= len) {
        if (outKey) {
            *outKey = Key_None;
        }
        return false;
    } else {
        if (outKey) {
            *outKey = queue->keys[(queue->front + idx) % KeyQueueSize];
        }
        return true;
    }
}

void _keyQDrop(Queue *queue, size_t count) {
    if (count == 0) return;
    size_t len = queue->front <= queue->back
        ? queue->back - queue->front
        : (queue->back + KeyQueueSize) - queue->front;
    if (count > len) count = len;

    queue->front = (queue->front + count) % KeyQueueSize;
}

ThreadRet _inputThreadRoutine(void *arg) {
    (void)arg;
    while (g_inputThreadRun) {
        UcdCP rawInput = termGetInput();
        if (rawInput < 0) {
            errLog("reading input failed");
            errClear();
        }
        _keyQEnq(&g_rawQueue, rawInput);
        Key key = _tryParseKey();
        if (key == Key_None) {
            continue;
        }
        _keyQEnq(&g_parsedQueue, key);
    }
    return 0;
}

Key keyGet(void) {
    Key k;
    (void)_keyQDeq(&g_parsedQueue, &k);
    return k;
}

static Key _tryParseKey(void) {
    Key k;
    if (!_keyQAt(&g_rawQueue, 0, &k)) return Key_None;

    if (k != Key_Escape) {
        _keyQDeq(&g_rawQueue, NULL);
        return _charToKey(k);
    }

#define EscapeBufSize 6 // Allow at most up to 4 arguments for the escape seq.
    uint32_t buf[EscapeBufSize];
    size_t len = _readEscapeSeq(buf, EscapeBufSize);
#undef EscapeBufSize

    if (len == 1) {
        return _charToKey(buf[0]);
    }

    uint32_t endCh = buf[len - 1];
    size_t argCount = len - 2;
    Key ret = 0;

    if (
        (endCh <= 'S' && recurringKeys[endCh] != Key_None)
        || (
            endCh == '~'
            && buf[0] == '['
            && (argCount == 1 || argCount == 2)
            && buf[1] <= 24
            && recurringKeys[buf[1]] != Key_None
        )
    ) {
        if (endCh == '~') {
            ret = recurringKeys[buf[1]];
            if (argCount == 1) return ret;
        } else {
            ret = recurringKeys[endCh];
        }

        if (argCount == 0) {
            return ret;
        } else if (argCount == 1) {
            return ret | ((buf[1] - 1) << 24);
        } else {
            return ret | ((buf[2] - 1) << 24);
        }
    }

    logFmt(Log_Warning, "unrecognized input escape sequence");
    return Key_None;
}

static Key _charToKey(Key k) {
    if ((k >= ' ' && k <= '~') || k > 0x7f) {
        return k;
    }

    if (k == 0x7f) {
        return Key_Backspace; // The actual Delete key is usually ^[[3~
    } else if (k == 0x8) {
        return KeyCtrl(Key_Backspace);
    } else if (k == 0 || k > 26) {
        return KeyCtrl('@' + k);
    } else if (k >= 1 && k <= 26) {
        return KeyCtrl('a' + k - 1);
    }

    nvUnreachable;
}

// Parse escape sequences as the regular expression
// \033[O[](\d+(;\d+)*)?.
// In the buffer the first and last occupied slots are the characters
// themselves and the numbers in the middle are parsed as actual numbers.
static size_t _readEscapeSeq(uint32_t *outBuf, size_t bufSize) {
    nvAssertExpr(bufSize >= 3);
    Key key;
    (void)_keyQAt(&g_rawQueue, 0, &key);
    nvAssertExpr(key == Key_Escape);

    if (!_keyQAt(&g_rawQueue, 1, &key)) {
        goto notAnEscape;
    }
    if (key != '[' && key != 'O') {
        // Alt+Key usually sends ESC Key
        _keyQDrop(&g_rawQueue, 2);
        outBuf[0] = KeyAlt(_charToKey(key));
        return 1;
    }

    // The initial ESC is omitted in the buffer.
    outBuf[0] = key;

    size_t numIdx = 0;
    size_t outBufIdx = 1;
    size_t queueIdx = 2;
    char numBuf[6] = { 0 };
    for (; _keyQAt(&g_rawQueue, queueIdx, &key); queueIdx++) {
        // Ignore escape sequences that become too long.
        if (queueIdx + 1 >= KeyQueueSize || queueIdx > 2 + (bufSize - 2) * 6) {
            goto notAnEscape;
        }

        if (key == Key_None) {
            goto notAnEscape;
        }

        // Add digits to numBuf to be parsed later
        if (key >= '0' && key <= '9') {
            if (numIdx < sizeof(numBuf) - 1) {
                numBuf[numIdx++] = (char)key;
            } else {
                goto notAnEscape;
            }
            continue;
        }

        // Escape sequences must end with a printable character
        if (key < ' ' || key > '~') {
            goto notAnEscape;
        }

        // When the digits end parse the number
        numBuf[numIdx] = '\0';
        unsigned long num = strtoul(numBuf, NULL, 10);
        memset(numBuf, 0, sizeof(numBuf));

        if (num == 0) {
            num = 1;
        } else if (num > INT16_MAX) {
            goto notAnEscape;
        }

        if (outBufIdx < bufSize) {
            outBuf[outBufIdx++] = num;
        } else {
            goto notAnEscape;
        }

        // Expect more arguments if there is a semicolon
        if (key != ';') {
            break;
        }

    }

    if (outBufIdx < bufSize) {
        outBuf[outBufIdx++] = key;
        _keyQDrop(&g_rawQueue, queueIdx);
        return outBufIdx;
    }

notAnEscape:
    (void)_keyQDeq(&g_rawQueue, &key);
    outBuf[0] = _charToKey(key);
    return 1;
}

/******************************************************************************/

#define _minMapCap 4

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
    while (*seq != Key_None) {
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

    BindMap *lastValidNode = map;
    int32_t branch = *seq;
    while (*seq != Key_None) {
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
    while (*seq != Key_None && map != NULL) {
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
    if (seq[0] == Key_None) {
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

/******************************************************************************/

bool keyInit(void) {
    if (!threadCreate(&g_inputThread, _inputThreadRoutine, NULL)) {
        errSetErrno();
        return false;
    }
    return true;
}

void keyQuit(void) {
    _mapDestroy(&g_bindRoots);
    g_bindRoots = (BindMap){ 0 };
    poolDestroy(&g_bindPool);

    g_inputThreadRun = false;
    (void)threadJoin(g_inputThread, NULL);
}
