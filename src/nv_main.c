#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "nv_editor.h"
#include "nv_error.h"
#include "nv_escapes.h"
#include "nv_key_queue.h"
#include "nv_logging.h"
#include "nv_term.h"
#include "clib_threads.h"

static KeyQueue g_keyQ = { 0 };
static Thread g_inputThread;
static bool g_inputThreadRun = true;
static bool g_neveIsInit = false;

ThreadRet inputThreadRoutine(void *arg);

bool initNeve(void) {
    if (!termInit()) {
        errLog("failed to initialize the terminal");
        return false;
    }

    if (!threadCreate(&g_inputThread, inputThreadRoutine, NULL)) {
        errSetErrno();
        errLog("failed to create thread");
        return false;
    }

    if (!termEnableRawMode(1)) {
        errLog("failed to enable raw mode");
        return false;
    }

    editorInit();
    g_neveIsInit = true;
    return true;
}

void quitNeve(void) {
    if (!g_neveIsInit) {
        return;
    }

    termWrite(sLen(
        escScreenClear
        escDisableAltBuffer
        escCursorShow
        escCursorShapeDefault
    ));

    g_inputThreadRun = false;
    (void)threadJoin(g_inputThread, NULL);

    editorQuit();
    termQuit();
}

ThreadRet inputThreadRoutine(void *arg) {
    (void)arg;
    while (g_inputThreadRun) {
        int32_t key = termGetKey();
        if (key == 0) {
            continue;
        }
        keyQueueEnq(&g_keyQ, key);
    }
    return 0;
}

int keyLogMode(void) {
    if (!termInit()) {
        errLog("failed to initialize the terminal");
        return 1;
    }

    if (!termEnableRawMode(1)) {
        errLog("failed to enable raw mode");
        return 1;
    }

    while (true) {
        UcdCP ch = termGetInput();
        if (ch <= 0) {
            continue;
        }
        if (ch > 127) {
            printf("U+%04X ", ch);
        } else if (ch == ' ') {
            printf("<sp>");
        } else if (ch > ' ' && ch <= '~') {
            printf("%c", ch);
        } else {
            printf("^%c", ch ^ 0x40);
        }
        fflush(stdout);
        if (ch == ('C' ^ 0x40)) { // Ctrl-C
            break;
        }
    }

    termQuit();

    return 0;
}

// TODO: use wmain on Windows
int main(int argc, char **argv) {
    int ret = 0;
    if (!logInit(NULL)) {
        printf("Failed to generate log.\n");
        ret = 1;
        goto exit;
    }

    if (argc > 2) {
        printf("Usage: neve [file]\n");
        ret = 1;
        goto exit;
    }

    if (argc == 2 && strcmp(argv[1], "--keys") == 0) {
        ret = keyLogMode();
        goto exit;
    }

    if (!initNeve()) {
        ret = 1;
        goto exit;
    }

    if (argc == 2) {
        editorOpen(argv[1]);
    } else {
        editorNewBuf();
    }

    while (g_ed.running) {
        int32_t key = keyQueueDeq(&g_keyQ);
        if (key < 0) {
            errLog("failed to read the key");
            goto exit;
        }
        editorHandleKey((uint32_t)key);
        editorRefresh();
    }

exit:
    quitNeve();
    logQuit();
    return ret;
}
