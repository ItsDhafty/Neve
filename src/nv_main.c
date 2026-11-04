#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "nv_editor.h"
#include "nv_error.h"
#include "nv_escapes.h"
#include "nv_key.h"
#include "nv_logging.h"
#include "nv_term.h"

static bool g_neveIsInit = false;

bool initNeve(void) {
    if (!termInit()) {
        errLog("failed to initialize the terminal");
        return false;
    }

    if (!keyInit()) {
        errLog("failed to initialize input handling");
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

    keyQuit();
    editorQuit();
    termQuit();
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
        editorHandleKey(keyGet());
        editorRefresh();
    }

exit:
    quitNeve();
    logQuit();
    return ret;
}
