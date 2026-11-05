#include <assert.h>
#include <stdarg.h>

#include "nv_draw.h"
#include "nv_editor.h"
#include "nv_escapes.h"
#include "nv_term.h"
#include "nv_time.h"

Editor g_ed = { 0 };
int32_t g_mapMove;
int32_t g_mapNormal, g_mapEdit, g_mapSel, g_mapCmd, g_mapGlob;

static void _addKeyBinds(void);

void editorInit(void) {
    screenInit(&g_ed.screen);
    g_ed.lastUpdate = 0;

    bufMapInit(&g_ed.buffers);
    uiInit(&g_ed.ui);

    g_ed._prevMode = EditorMode_Normal;
    g_ed._mode = EditorMode_Normal;
    g_ed.running = true;

    arrAppend(&g_ed.commands, cmdEntryNew("q", cmdQuit));
#ifdef NV_DEBUG
    // fq is too easy to type and can lead to slip-ups but it is useful when
    // testing
    arrAppend(&g_ed.commands, cmdEntryNew("fq", cmdForceQuit));
#endif
    arrAppend(&g_ed.commands, cmdEntryNew("forcequit", cmdForceQuit));
    arrAppend(&g_ed.commands, cmdEntryNew("quit", cmdQuit));
    arrAppend(&g_ed.commands, cmdEntryNew("exit", cmdQuit));
    arrAppend(&g_ed.commands, cmdEntryNew("w", cmdSave));
    arrAppend(&g_ed.commands, cmdEntryNew("wq", cmdSaveAndQuit));
    arrAppend(&g_ed.commands, cmdEntryNew("write", cmdSave));
    arrAppend(&g_ed.commands, cmdEntryNew("s", cmdSave));
    arrAppend(&g_ed.commands, cmdEntryNew("sq", cmdSaveAndQuit));
    arrAppend(&g_ed.commands, cmdEntryNew("save", cmdSave));
    arrAppend(&g_ed.commands, cmdEntryNew("saveas", cmdSaveAs));
    arrAppend(&g_ed.commands, cmdEntryNew("pwd", cmdWorkingDir));

    _addKeyBinds();

    g_ed.opt.keyTimeoutDuration = 5 * TimeSecond;

    termWrite(sLen(
        escEnableAltBuffer
        escCursorHide
    ));
}

void editorQuit(void) {
    screenDestroy(&g_ed.screen);
    bufMapDestroy(&g_ed.buffers);
    bindQuit();
}

bool editorUpdateSize(void) {
    uint16_t rows, cols;
    if (!termSize(&rows, &cols)) {
        return false;
    }

    screenResize(&g_ed.screen, cols, rows);
    uiResize(&g_ed.ui, cols, rows);

    return true;
}

bool editorTryExit(void) {
    Buf *buf = bufRef(&g_ed.buffers, g_ed.ui.bufPanel.bufHd);
    if (buf == NULL) {
        g_ed.running = false;
        return true;
    }
    if (buf->ctx.edited) {
        return false;
    }
    g_ed.running = false;
    return true;
}

void editorForceExit(void) {
    g_ed.running = false;
}

void _handleKeysWithFallbacks(int32_t maps[], uint8_t count) {
    // TODO: immediate selection handling

    Bind bind = { 0 };
    BindMatchResult res = { 0 };
    g_ed.keys[g_ed.keyCount] = Key_None;

    bool incompleteFound = false;
    uint64_t timeoutDuration = g_ed.opt.keyTimeoutDuration;

    uint8_t i;
    for (i = 0; i < g_ed.keyCount && !incompleteFound; i++) {
        for (uint8_t j = 0; j < count; j++) {
            res = bindMatch(maps[j], g_ed.keys + i, &bind);

            switch (res) {
            case BindMatch_NotFound:
                break;
            case BindMatch_Incomplete:
                incompleteFound = true;
                break;
            case BindMatch_Found:
                if (!incompleteFound) {
                    timeoutDuration = 0;
                }
                goto setTimeout;
            case BindMatch_Partial:
                goto setTimeout;
            }
        }
    }

    if (!incompleteFound) g_ed.keyCount = 0;
    return;

setTimeout:
    g_ed.onBindTimeout = bind;
    g_ed.keyTimeoutEnd = g_ed.lastUpdate + timeoutDuration;
    g_ed.keyCount -= i;
    // NOTE: `i` here is valid because we can only get to this branch from
    // inside the inner loop and as a consequence `i++` does not execute once
    // too many times
    for (uint8_t j = 0; j < g_ed.keyCount - i; j++) {
        g_ed.keys[j] = g_ed.keys[j + i];
    }
    g_ed.keys[g_ed.keyCount] = Key_None;
}

void editorHandleKey(Key key) {
    if (key == Key_None) return;

    if (g_ed.keyCount < EditorMaxBindLen) {
        g_ed.keys[g_ed.keyCount++] = key;
    }

    switch (g_ed._mode) {
    case EditorMode_Normal:
        _handleKeysWithFallbacks((int32_t[]){ g_mapNormal, g_mapMove, g_mapGlob }, 3);
        break;
    case EditorMode_Edit:
        _handleKeysWithFallbacks((int32_t[]){ g_mapEdit }, 1);
        break;
    case EditorMode_Selection:
        _handleKeysWithFallbacks((int32_t[]){ g_mapSel, g_mapMove, g_mapGlob }, 3);
        break;
    case EditorMode_Command:
        _handleKeysWithFallbacks((int32_t[]){ g_mapCmd, g_mapEdit }, 2);
        break;
    default:
        nvUnreachable;
    }
}

void _runCmd(void) {
    StrView cmd = ctxGetContent(&g_ed.ui.cmdInput.ctx);
    if (cmd.len == 0) {
        return;
    }
    StrView cmdName = cmd;
    StrView cmdArgs = { .buf = NULL, .len = 0 };
    for (size_t i = 0; i < cmd.len; i++) {
        if (cmd.buf[i] == ' ') {
            cmdName.len = i;
            cmdArgs = (StrView) {
                .buf = &cmd.buf[i + 1],
                .len = cmd.len - i - 1
            };
            break;
        }
    }

    for (size_t i = 0; i < g_ed.commands.len; i++) {
        CmdEntry *cmdEntry = g_ed.commands.items[i];
        if (cmdEntry->nameLen != cmdName.len) {
            continue;
        }
        if (memcmp(cmdEntry->name, cmdName.buf, cmdName.len) != 0) {
            continue;
        }
        if (g_ed.cmdResult != NULL) {
            memFree(g_ed.cmdResult);
        }
        g_ed.cmdResult = cmdEntry->cmd(cmdArgs);
        return;
    }
    if (g_ed.cmdResult != NULL) {
        memFree(g_ed.cmdResult);
    }
    g_ed.cmdResult = cmdResultFailed(
        "command '"StrFmt"' not found",
        StrArg(&cmdName)
    );
}

bool editorRefresh(void) {
    uint64_t time = timeRelNs();
    if (time - g_ed.lastUpdate < 16000000) {
        timeSleep(16000000 + g_ed.lastUpdate - time);
    }
    g_ed.lastUpdate = timeRelNs();

    if (g_ed.keyTimeoutEnd != 0 && g_ed.lastUpdate >= g_ed.keyTimeoutEnd) {
        bindInvokeCallback(g_ed.onBindTimeout, g_ed.keys);
        g_ed.keyCount = 0;
        g_ed.keyTimeoutEnd = 0;
    }

    if (g_ed._mode == EditorMode_Command) {
        switch (g_ed.ui.cmdInput.state) {
        case UICmdInput_Inserting:
            break;
        case UICmdInput_Confirmed:
            _runCmd();
            editorSetMode(g_ed._prevMode);
            break;
        case UICmdInput_Canceled:
            editorSetMode(g_ed._prevMode);
            break;
        }
    }

    if (!editorUpdateSize()) {
        return false;
    }
    uiUpdate(&g_ed.ui.elem);

    screenClear(&g_ed.screen, -1);
    drawUI(&g_ed.screen, &g_ed.ui);

    return screenRefresh(&g_ed.screen);
}

bool editorOpen(const char *path) {
    File file;
    BufHandle newBuf = bufInvalidHandle;
    bool success = true;
    switch (fileOpen(&file, path, FileMode_Read)) {
    case FileIOResult_Success:
        bufClose(&g_ed.buffers, g_ed.ui.bufPanel.bufHd);
        if (
            bufInitFromFile(
                &g_ed.buffers,
                &file,
                &newBuf
            ).kind != BufResult_Success
        ) {
            success = false;
fileClose(&file);
            break;
        }
        fileClose(&file);
        break;
    case FileIOResult_FileNotFound:
        bufClose(&g_ed.buffers, g_ed.ui.bufPanel.bufHd);
        newBuf = bufInitEmpty(&g_ed.buffers);
        bufSetPathC(&g_ed.buffers, newBuf, path);
        break;
    default:
        success = false;
    }
    g_ed.ui.bufPanel.bufHd = newBuf;
    return success;
}

void editorNewBuf(void) {
    bufClose(&g_ed.buffers, g_ed.ui.bufPanel.bufHd);
    BufHandle newBuf = bufInitEmpty(&g_ed.buffers);
    g_ed.ui.bufPanel.bufHd = newBuf;
}

UIBufPanel *editorActivePanel(void) {
    return g_ed._mode == EditorMode_Command ? NULL : &g_ed.ui.bufPanel;
}

BufHandle editorActiveBuffer(void) {
    if (g_ed._mode == EditorMode_Command) {
        return bufInvalidHandle;
    } else {
        return g_ed.ui.bufPanel.bufHd;
    }
}

Ctx *editorActiveContext(void) {
    if (g_ed._mode == EditorMode_Command) {
        return &g_ed.ui.cmdInput.ctx;
    } else {
        return &bufRef(&g_ed.buffers, g_ed.ui.bufPanel.bufHd)->ctx;
    }
}

// --------------------------- Default key binds ---------------------------- //

#define mkCtxBind(map, fn, ...)                                                \
    bindAdd(                                                                   \
        map,                                                                   \
        BindSeq(__VA_ARGS__),                                                  \
        (Bind){ .callback = BindCBPtr(_callCtxCB, (fn)) }                      \
    )

#define mkBind(map, cb, ...)                                                   \
    bindAdd(map, BindSeq(__VA_ARGS__), (Bind) { .callback = (cb) })

// Global map functions
static void _addMovementMap(void);
static void _addGenericMovementKeys(int32_t map);

static void _addNormalMode(void);
static void _addEditMode(void);
static void _addSelMode(void);
static void _addCmdMode(void);
static void _addGlobBinds(void);

static void _addKeyBinds(void) {
    _addMovementMap();
    _addNormalMode();
    _addEditMode();
    _addSelMode();
    _addCmdMode();
    _addGlobBinds();
}

// General bind callbacks
static void _callCtxCB(void *func);
static void _callPanelCB(void *user);
static void _normalModeCB(BindKeys keys);
static void _editModeCB(BindKeys keys);
static void _insertTextCB(BindKeys keys);
static void _goToNormalModeCB(void);
static void _selModeCB(BindKeys keys);
static void _cmdSetInputCB(int user);

static void _addMovementMap(void) {
    g_mapMove = bindAddRootMap();

    mkCtxBind(g_mapMove, ctxCurMoveUp, 'i');
    mkCtxBind(g_mapMove, ctxCurMoveDown, 'k');
    mkCtxBind(g_mapMove, ctxCurMoveLeft, 'j');
    mkCtxBind(g_mapMove, ctxCurMoveRight, 'l');
    mkCtxBind(g_mapMove, ctxCurMoveToLineStart, 'u');
    mkCtxBind(g_mapMove, ctxCurMoveToLineEnd, 'o');

    mkCtxBind(g_mapMove, ctxCurMoveToPrevParagraph, 'I');
    mkCtxBind(g_mapMove, ctxCurMoveToNextParagraph, 'K');
    mkCtxBind(g_mapMove, ctxCurMoveToPrevWordStart, 'J');
    mkCtxBind(g_mapMove, ctxCurMoveToNextWordStart, 'L');
    mkCtxBind(g_mapMove, ctxCurMoveToTextStart, 'U');
    mkCtxBind(g_mapMove, ctxCurMoveToTextEnd, 'O');

    bindAdd(g_mapMove, BindSeq(KeyCtrl('i')), (Bind){
        .callback = BindCBPtr(_callPanelCB, uiBufPanelMoveHalfUp)
    });
    bindAdd(g_mapMove, BindSeq(KeyCtrl('k')), (Bind){
        .callback = BindCBPtr(_callPanelCB, uiBufPanelMoveHalfDown)
    });
    mkCtxBind(g_mapMove, ctxCurMoveToPrevWordEnd, KeyCtrl('j'));
    mkCtxBind(g_mapMove, ctxCurMoveToNextWordEnd, KeyCtrl('l'));
    // TODO: Key_CtrlU
    // TODO: Key_CtrlO

    // TODO: [b]c
    // TODO: [B]c
    // TODO: [^B]
    // TODO: [a]c
    // TODO: [A]c
    // TODO: [^A]

    // TODO: [f]
    // TODO: [F]
    // TODO: [n]
    // TODO: [N]

    // TODO: [space][f]s
    // TODO: [space][F]s
    //
    // TODO: [space][p]

    _addGenericMovementKeys(g_mapMove);
}

static void _addGenericMovementKeys(int32_t map) {
    // TODO: Many more useful movement keys
    mkCtxBind(map, ctxCurMoveBack, Key_Left);
    mkCtxBind(map, ctxCurMoveFwd, Key_Right);
    mkCtxBind(map, ctxCurMoveUp, Key_Up);
    mkCtxBind(map, ctxCurMoveDown, Key_Down);

    mkCtxBind(g_mapEdit, ctxCurMoveToLineStart, KeyCtrl('a')); // Home
    mkCtxBind(g_mapEdit, ctxCurMoveToLineEnd, KeyCtrl('e')); // End
}

static void _addNormalMode(void) {
    g_mapNormal = bindAddRootMap();

    mkCtxBind(g_mapNormal, ctxUndo, 'z');
    mkCtxBind(g_mapNormal, ctxRedo, 'Z');
    // TODO: [^Z]

    // TODO: [y]s
    mkCtxBind(g_mapNormal, ctxRemoveLine, 'Y');
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), KeyCtrl('y'));
    mkCtxBind(g_mapNormal, ctxRemoveBack, 'q');
    mkCtxBind(g_mapNormal, ctxRemoveFwd, 'Q');

    // TODO: [^Q][j]c
    // TODO: [^Q][i]c
    mkCtxBind(g_mapNormal, ctxIndent, ' ', 'd');
    mkCtxBind(g_mapNormal, ctxDedent, ' ', 'D');
    // TODO: [space][b]
    // TODO: [space][j]

    // TODO: [space][s]
    // TODO: [space][S]

    // TODO: [x]s
    // TODO: [X]
    // TODO: [^X]
    // TODO: [c]s
    // TODO: [C]
    // TODO: [^C]
    // TODO: [v]
    // TODO: [V]
    // TODO: [^V]c


    // TODO: [d][i]
    // TODO: [d][k]
    // TODO: [d][J]
    // TODO: [d][L]
    // TODO: [d][^J]
    // TODO: [d][^K]
    // TODO: [d][u]
    // TODO: [d][o]
    // TODO: [d][b]c
    // TODO: [d][B]c
    // TODO: [d][a]c
    // TODO: [d][A]c

    // TODO: [d][f]
    // TODO: [d][F]
    // TODO: [d][n]
    // TODO: [d][N]
    // TODO: [d][space][f]s
    // TODO: [d][space][F]s
    // TODO: [d][space][p]

    // TODO: [D]
    // TODO: [^D][u]
    // TODO: [^D][o]
    // TODO: [^D][^D]

    // TODO: [m]c
    // TODO: [M]c
    // TODO: [space][m]

    // TODO: [w][j]
    // TODO: [w][l]
    // TODO: [w][i]
    // TODO: [w][k]

    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'e');
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'E');
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), KeyCtrl('e'));
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'h');
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'H');

    // TODO: [r]s
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'R');
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), KeyCtrl('r'));

    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 's'); // TODO: with imm. sel
    mkBind(g_mapNormal, BindCBKeys(_normalModeCB), 'S');

    // TODO: [^S]
    // TODO: [^D]
}

static void _addEditMode(void) {
    g_mapEdit = bindAddRootMap();
    _addGenericMovementKeys(g_mapEdit);

    mkCtxBind(g_mapEdit, ctxCurMoveFwd, KeyCtrl('f'));
    mkCtxBind(g_mapEdit, ctxCurMoveBack, KeyCtrl('b'));
    mkCtxBind(g_mapEdit, ctxCurMoveToPrevWordStart, KeyCtrl('k'));
    mkCtxBind(g_mapEdit, ctxCurMoveToNextWordEnd, KeyCtrl('l'));
    mkCtxBind(g_mapEdit, ctxCurMoveUp, KeyCtrl('p'));
    mkCtxBind(g_mapEdit, ctxCurMoveDown, KeyCtrl('n'));

    mkCtxBind(g_mapEdit, ctxRemoveBack, KeyCtrl('z'));
    mkCtxBind(g_mapEdit, ctxRemoveBack, Key_Backspace);
    mkCtxBind(g_mapEdit, ctxRemoveFwd, KeyCtrl('x'));
    mkCtxBind(g_mapEdit, ctxRemoveFwd, Key_Delete);
    mkBind(g_mapEdit, BindCBKeys(_editModeCB), KeyCtrl('w'));
    mkBind(g_mapEdit, BindCBKeys(_editModeCB), KeyCtrl('r'));
    mkBind(g_mapEdit, BindCBKeys(_editModeCB), KeyCtrl('t'));
    mkBind(g_mapEdit, BindCBKeys(_editModeCB), KeyCtrl('y'));

    mkCtxBind(g_mapEdit, ctxInsertLineAbove, KeyCtrl('o'));
    mkCtxBind(g_mapEdit, ctxInsertLineBelow, KeyCtrl('u'));
    mkCtxBind(g_mapEdit, ctxIndent, KeyCtrl('d'));
    mkCtxBind(g_mapEdit, ctxDedent, KeyCtrl('s'));

    // TODO: [^H]?
    // TODO: [^V]
    mkBind(g_mapEdit, BindCBKeys(_insertTextCB), BindAny);

    mkBind(g_mapEdit, BindCBNoArgs(_goToNormalModeCB), KeyCtrl('c'));
    mkBind(g_mapEdit, BindCBNoArgs(_goToNormalModeCB), KeyCtrl('q'));
    mkBind(g_mapEdit, BindCBNoArgs(_goToNormalModeCB), Key_Escape);
}

static void _addSelMode(void) {
    g_mapSel = bindAddRootMap();

    mkBind(g_mapSel, BindCBKeys(_selModeCB), 'h');
    mkBind(g_mapSel, BindCBKeys(_selModeCB), 'H');

    // TODO: [c]
    // TODO: [x]
    // TODO: [v]
    mkBind(g_mapSel, BindCBKeys(_selModeCB), 'y');
    mkBind(g_mapSel, BindCBKeys(_selModeCB), 'r');
    mkCtxBind(g_mapSel, ctxIndent, 'd');
    mkCtxBind(g_mapSel, ctxDedent, 'D');

    // TODO: [space][j]
    // TODO: [s]c
    // TODO: [s][y]c

    mkBind(g_mapSel, BindCBNoArgs(_goToNormalModeCB), KeyCtrl('c'));
    mkBind(g_mapSel, BindCBNoArgs(_goToNormalModeCB), KeyCtrl('q'));
    mkBind(g_mapSel, BindCBNoArgs(_goToNormalModeCB), Key_Escape);
}

static void _addCmdMode(void) {
    g_mapCmd = bindAddRootMap();

    Bind cancel = {
        .callback = BindCBInt(_cmdSetInputCB, UICmdInput_Canceled)
    };
    bindAdd(g_mapCmd, BindSeq(KeyCtrl('c')), cancel);
    bindAdd(g_mapCmd, BindSeq(KeyCtrl('q')), cancel);
    bindAdd(g_mapCmd, BindSeq(Key_Escape), cancel);

    Bind confirm = {
        .callback = BindCBInt(_cmdSetInputCB, UICmdInput_Confirmed)
    };
    bindAdd(g_mapCmd, BindSeq(Key_Enter), confirm);
    bindAdd(g_mapCmd, BindSeq(KeyCtrl('j')), confirm);
}

static void _addGlobBinds(void) {
    g_mapGlob = bindAddRootMap();

    bindAdd(g_mapGlob, BindSeq('p'), (Bind){
        .callback = BindCBInt(editorSetMode, EditorMode_Command)
    });
}

static void _callCtxCB(void *user) {
    Ctx *ctx = editorActiveContext();
    if (ctx != NULL) {
        ((void (*)(Ctx *))user)(ctx);
    }
}

static void _callPanelCB(void *user) {
    UIBufPanel *panel = editorActivePanel();
    if (panel != NULL) {
        ((void (*)(UIBufPanel *))user)(panel);
    }
}

static void _normalModeCB(BindKeys keys) {
    Ctx *ctx = editorActiveContext();

    switch (keys[0]) {
    case 'e':
        editorSetMode(EditorMode_Edit);
        break;
    case 'E':
        ctxCurMoveToLineEnd(ctx);
        editorSetMode(EditorMode_Edit);
        break;
    case KeyCtrl('e'):
        ctxCurMoveToLineStart(ctx);
        editorSetMode(EditorMode_Edit);
        break;
    case 'h':
        ctxInsertLineBelow(ctx);
        editorSetMode(EditorMode_Edit);
        break;
    case 'H':
        ctxInsertLineAbove(ctx);
        editorSetMode(EditorMode_Edit);
        break;
    case 's':
        ctxSelBegin(ctx, false);
        editorSetMode(EditorMode_Selection);
        break;
    case 'S':
        ctxSelBegin(ctx, true);
        editorSetMode(EditorMode_Selection);
        break;
    case 'Y':
        ctxSelBegin(ctx, true);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    case KeyCtrl('y'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToLineEnd(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    case 'R':
        ctxSelBegin(ctx, true);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        editorSetMode(EditorMode_Edit);
        break;
    case KeyCtrl('r'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToLineEnd(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    }
}

static void _editModeCB(BindKeys keys) {
    Ctx *ctx = editorActiveContext();
    if (ctx == NULL) return;

    switch (keys[0]) {
    case KeyCtrl('w'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToPrevWordStart(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    case KeyCtrl('r'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToNextWordStart(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    case KeyCtrl('t'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToLineStart(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    case KeyCtrl('y'):
        ctxSelBegin(ctx, false);
        ctxCurMoveToLineEnd(ctx);
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        break;
    }
}

static void _insertTextCB(BindKeys keys) {
    Ctx *ctx = editorActiveContext();
    if (ctx == NULL) return;

    Key cp = keys[0];
    if (cp == Key_Enter || cp == KeyCtrl('j')) {
        cp = '\n';
    }
    if (cp != '\n' || ctx->multiline) {
        ctxInsertCP(ctx, (UcdCP)cp);
    }
}

static void _goToNormalModeCB(void) {
    UIBufPanel *panel = editorActivePanel();
    Ctx *ctx = editorActiveContext();
    if (panel != NULL) {
        ctxSelCancel(ctx);
        editorSetMode(EditorMode_Normal);
    }
}

static void _selModeCB(BindKeys keys) {
    UIBufPanel *panel = editorActivePanel();
    if (panel == NULL) return;

    Buf *buf = bufRef(&g_ed.buffers, panel->bufHd);
    if (buf == NULL) return;
    Ctx *ctx = &buf->ctx;

    switch (keys[0]) {
    case 'y':
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        editorSetMode(EditorMode_Normal);
        break;
    case 'r':
        ctxSelEnd(ctx);
        if (ctxSelHas(ctx)) {
            ctxRemoveBack(ctx);
        }
        editorSetMode(EditorMode_Edit);
        break;
    case 'h':
        if (ctxSelIsActive(ctx)) {
            ctxSelEnd(ctx);
        } else {
            ctxSelBegin(ctx, false);
        }
        break;
    case 'H':
        if (ctxSelIsActive(ctx)) {
            ctxSelEnd(ctx);
        } else {
            ctxSelBegin(ctx, true);
        }
        break;
    }
}

static void _cmdSetInputCB(int user) {
    g_ed.ui.cmdInput.state = user;
}

void editorSetMode(EditorMode mode) {
    if (g_ed._mode != EditorMode_Command) {
        g_ed._prevMode = g_ed._mode;
    }

    g_ed._mode = mode;
    g_ed.keyCount = 0;

    switch (mode) {
    case EditorMode_Normal:
    case EditorMode_Edit: {
        Ctx *ctx = editorActiveContext();
        if (ctx != NULL) {
            ctxSelCancel(ctx);
        }
        break;
    }
    case EditorMode_Selection:
        break;
    case EditorMode_Command:
        g_ed.ui.cmdInput.state = UICmdInput_Inserting;
        ctxDestroy(&g_ed.ui.cmdInput.ctx);
        ctxInit(&g_ed.ui.cmdInput.ctx, false);
        ctxCurAdd(&g_ed.ui.cmdInput.ctx, 0);
        if (g_ed.cmdResult != NULL) {
            memFree(g_ed.cmdResult);
            g_ed.cmdResult = NULL;
        }
        break;
    default:
        nvUnreachable;
    }
}

EditorMode editorGetMode(void) {
    return g_ed._mode;
}
