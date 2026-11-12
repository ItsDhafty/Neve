#include <stdio.h>
#include <inttypes.h>

#include "clib_threads.h"
#include "nv_escapes.h"
#include "nv_error.h"
#include "nv_key.h"
#include "nv_logging.h"
#include "nv_pool.h"
#include "nv_term.h"
#include "nv_time.h"
#include "nv_utils.h"

#define QueueNextIdx(idx) (((idx) + 1) % KeyQueueSize)
#define KeyQueueSize 4096

static Thread g_inputThread;
static volatile bool g_inputThreadRun = true;
static volatile bool g_requestPause = false; // Set outside the thread
static volatile bool g_eventsPaused = false; // Set inside the thread
static bool g_pushedKittyFlags = false;

typedef struct Queue {
    uint16_t front, back;
    Key keys[KeyQueueSize];
} Queue;

enum KittyFlags {
    KF_Disambiguate  = 0x01, // Disambiguate escape codes
    KF_Events        = 0x02, // Report event types
    KF_AlternateKeys = 0x04, // Report alternate keys
    KF_AllAsEscapes  = 0x08, // Report all keys as escape codes
    KF_Text          = 0x10, // Report associated text
};
uint32_t _getKittyFlags(void);

// Queue of raw terminal input to be parsed.
static Queue g_rawQueue = { 0 };

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
        if (g_requestPause) {
            g_eventsPaused = true;
            timeSleep(TimeMilliSec);
            continue;
        }
        g_eventsPaused = false;

        UcdCP rawInput = termGetInput();
        if (rawInput < 0) {
            errLog("reading input failed");
            errClear();
        }
        _keyQEnq(&g_rawQueue, rawInput);
    }
    return 0;
}

Key keyGet(void) {
    return _tryParseKey();
}

static Key _tryParseKey(void) {
    Key k;
    if (!_keyQAt(&g_rawQueue, 0, &k)) return Key_None;

    if (k != 0x1b) {
        _keyQDeq(&g_rawQueue, NULL);
        return _charToKey(k);
    }

#define EscapeBufSize 16
    uint32_t buf[EscapeBufSize];
    size_t len = _readEscapeSeq(buf, EscapeBufSize);
#undef EscapeBufSize

    if (len == 0) {
        return Key_None;
    } else if (len == 1) {
        return _charToKey(buf[0]);
    }

    // logBeginLine(Log_Debug);
    // logContLine("%c", buf[0]);
    // for (size_t i = 1; i < len - 1; i++) {
    //     logContLine("%s%"PRIu32, i == 1 ? "" : ";", buf[i]);
    // }
    // logContLine("%c", buf[len - 1]);
    // logEndLine();

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
        } else if (argCount == 2) {
            return ret | ((buf[2] - 1) << 24);
        }
    } else if (endCh == 'u' && buf[0] == '[') {
        // Assume that events and alternate keys are not reported.
        switch (argCount) {
        case 0: return Key_None;
        case 1: return _charToKey(buf[1]);
        case 2: return buf[1] | ((buf[2] - 1) << 24);
        case 3:
            if (buf[2] != 0 && ((buf[2] - 1) << 24) == KeyCtrl(0))
                return buf[1] | ((buf[2] - 1) << 24);
            else
                return buf[3];
        default: break;
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
    nvAssertExpr(key == 0x1b);

    if (!_keyQAt(&g_rawQueue, 1, &key)) {
        return 0;
    }
    if (key != '[' && key != 'O') {
        // Alt+Key usually sends ESC Key
        _keyQDrop(&g_rawQueue, 2);
        outBuf[0] = key == Key_None ? Key_Escape : KeyAlt(_charToKey(key));
        return 1;
    }

    // The initial ESC is omitted in the buffer.
    outBuf[0] = key;

    size_t numIdx = 0;
    size_t outBufIdx = 1;
    size_t queueIdx = 2;
    char numBuf[6] = { 0 };
    bool addNumber = false;
    for (;;) {
        // Ignore escape sequences that become too long.
        if (queueIdx + 1 >= KeyQueueSize || queueIdx > 2 + (bufSize - 2) * 6) {
            logFmt(Log_Debug, "index too big");
            goto notAnEscape;
        }

        if (!_keyQAt(&g_rawQueue, queueIdx, &key)) {
            return 0;
        }
        queueIdx++;

        if (key == Key_None) {
            logFmt(Log_Debug, "Key_None inside");
            goto notAnEscape;
        }

        // Add digits to numBuf to be parsed later
        if (key >= '0' && key <= '9') {
            if (numIdx < sizeof(numBuf) - 1) {
                numBuf[numIdx++] = (char)key;
            } else {
                logFmt(Log_Debug, "num too long");
                goto notAnEscape;
            }
            addNumber = true;
            continue;
        }

        // Escape sequences must end with a printable character
        if (key < ' ' || key > '~') {
            goto notAnEscape;
        }

        if (addNumber) {
            // When the digits end parse the number
            numBuf[numIdx] = '\0';
            unsigned long num = strtoul(numBuf, NULL, 10);
            memset(numBuf, 0, sizeof(numBuf));
            numIdx = 0;

            if (outBufIdx < bufSize) {
                outBuf[outBufIdx++] = num;
            } else {
                logFmt(Log_Debug, "buf too small");
                goto notAnEscape;
            }
        }

        // Expect more arguments if there is a semicolon or a colon
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
    logFmt(Log_Debug, "not an escape");
    (void)_keyQDeq(&g_rawQueue, &key);
    outBuf[0] = _charToKey(key);
    return 1;
}

void keyPauseEvents(void) {
    g_requestPause = true;
    while (!g_eventsPaused) {}
}

void keyResumeEvents(void) {
    g_requestPause = false;
    while (g_eventsPaused) {}
}

bool keyInit(void) {
    // Set the kitty keyboard protocol with the following flags:
    // - 8: report all keys as escape codes
    // - 16: report associated text

    g_pushedKittyFlags = true;
    termWrite(sLen(escKeyPushFlags("24")));
    uint32_t flags = _getKittyFlags();
    if (flags == 0) {
        g_pushedKittyFlags = false;
        goto startThread;
    }

    flags ^= flags & KF_Disambiguate; // Ignore KF_Disambiguate

    if (flags == (KF_AllAsEscapes | KF_Text)) {
        goto startThread;
    }
    termWrite(sLen(escKeyPopFlags));
    termWrite(sLen(escKeyPushFlags("1")));
    flags = _getKittyFlags();
    if (flags != KF_Disambiguate) {
        g_pushedKittyFlags = false;
        termWrite(sLen(escKeyPopFlags));
    }
    flags = 0;

startThread:
    logFmt(Log_Info, "Input flags: %"PRIu32, flags);
    if (!threadCreate(&g_inputThread, _inputThreadRoutine, NULL)) {
        errSetErrno();
        return false;
    }
    return true;
}

void keyQuit(void) {
    g_inputThreadRun = false;
    (void)threadJoin(g_inputThread, NULL);
    if (g_pushedKittyFlags) {
        termWrite(sLen(escKeyPopFlags));
    }
}

uint32_t _getKittyFlags(void) {
    char buf[7] = { 0 };
    if (!termWrite(sLen(escKeyGetFlags))) {
        return 0;
    }

    UcdCP ch = termGetInput();
    if (ch != '\x1b') return 0;
    ch = termGetInput();
    if (ch != '[') return 0;
    ch = termGetInput();
    if (ch != '?') return 0;

    ch = termGetInput();
    for (size_t i = 0; i < nvArrlen(buf); i++) {
        if (ch < '0' || ch > '9') {
            break;
        }
        buf[i] = (char)ch;
        ch = termGetInput();
    }

    if (ch != 'u') return 0;
    return (uint32_t)strtol(buf, NULL, 10);
}
