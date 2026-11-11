#include <math.h>
#include "nv_editor.h"
#include "nv_tui.h"

static void _uiElemInit(
    UIElement *elem,
    UIUpdater updater
);

static void _uiUpdater(UI *ui);
static void _uiBufPanelUpdater(UIBufPanel *panel);
static void _uiCmdInputUpdater(UICmdInput *cmdInput);

static void _uiElemInit(
    UIElement *elem,
    UIUpdater updater
) {
    elem->x = 0;
    elem->y = 0;
    elem->w = 0;
    elem->h = 0;
    elem->updater = updater;
}

void uiUpdate(UIElement *elem) {
    if (elem->updater != NULL) {
        elem->updater(elem);
    }
}

void uiInit(UI *ui) {
    _uiElemInit(&ui->elem, (UIUpdater)_uiUpdater);
    _uiElemInit(&ui->statusBar, NULL);
    uiBufPanelInit(&ui->bufPanel);
    uiCmdInputInit(&ui->cmdInput);
}

void uiDestroy(UI *ui) {
    uiCmdInputDestroy(&ui->cmdInput);
}

void uiResize(UI *ui, uint16_t w, uint16_t h) {
    ui->elem.w = w;
    ui->elem.h = h;
}

void uiBufPanelInit(UIBufPanel *panel) {
    _uiElemInit(&panel->elem, (UIUpdater)_uiBufPanelUpdater);

    panel->bufHd = bufInvalidHandle;
    panel->scrollX = 0;
    panel->scrollY = 0;
}

void uiBufPanelMoveHalfUp(UIBufPanel *panel) {
    Buf *buf = bufRef(&g_ed.buffers, panel->bufHd);
    if (buf == NULL) return;
    for (uint16_t i = 0; i < panel->elem.h / 2; i++) {
        ctxCurMoveUp(&buf->ctx);
    }
}

void uiBufPanelMoveHalfDown(UIBufPanel *panel) {
    Buf *buf = bufRef(&g_ed.buffers, panel->bufHd);
    if (buf == NULL) return;
    for (uint16_t i = 0; i < panel->elem.h / 2; i++) {
        ctxCurMoveDown(&buf->ctx);
    }
}

void uiCmdInputInit(UICmdInput *cmdInput) {
    _uiElemInit(&cmdInput->elem, (UIUpdater)_uiCmdInputUpdater);
    ctxInit(&cmdInput->ctx, false);
    cmdInput->state = UICmdInput_Canceled;
}

void uiCmdInputDestroy(UICmdInput *cmdInput) {
    ctxDestroy(&cmdInput->ctx);
}

static void _uiUpdater(UI *ui) {
    if (ui->elem.h == 0) {
        return;
    }

    ui->bufPanel.elem.w = ui->elem.w;
    if (ui->cmdInput.state == UICmdInput_Inserting) {
        ui->bufPanel.elem.h = ui->elem.h - 2;
    } else {
        ui->bufPanel.elem.h = ui->elem.h - 1;
    }

    ui->cmdInput.elem.w = ui->elem.w;
    ui->cmdInput.elem.h = 1;
    ui->cmdInput.elem.y = ui->elem.h - 2;

    ui->statusBar.w = ui->elem.w;
    ui->statusBar.h = 1;
    ui->statusBar.y = ui->elem.h - 1;

    uiUpdate(&ui->bufPanel.elem);
    uiUpdate(&ui->cmdInput.elem);
    uiUpdate(&ui->statusBar);
}

static void _uiBufPanelUpdater(UIBufPanel *panel) {
    Buf *buf = bufRef(&g_ed.buffers, panel->bufHd);
    if (buf == NULL) {
        return;
    }

    Ctx *ctx = &buf->ctx;
    size_t lines = ctxLineCount(ctx);
    // log10(lines) + 1 is the width of the number, +1 for a space after
    uint8_t numColWidth = (uint8_t)log10((double)lines) + 2;

    if (panel->scrollY > lines) {
        panel->scrollY = lines - 1;
    }

    if (ctx->cursors.len != 1) {
        return;
    }

    size_t line, col;
    ctxPosAt(ctx, ctx->cursors.items[0].idx, &line, &col);

    if (panel->scrollX > col) {
        panel->scrollX = col;
    } else if (panel->scrollX + panel->elem.w - numColWidth <= col) {
        panel->scrollX = col - panel->elem.w + numColWidth + 1;
    }

    if (panel->scrollY > line) {
        panel->scrollY = line;
    } else if (panel->scrollY + panel->elem.h <= line) {
        panel->scrollY = line - panel->elem.h + 1;
    }
}

static void _uiCmdInputUpdater(UICmdInput *cmdInput) {
    if (cmdInput->ctx.cursors.len != 1) {
        return;
    }

    size_t col;
    ctxPosAt(&cmdInput->ctx, cmdInput->ctx.cursors.items[0].idx, NULL, &col);

    if (cmdInput->scroll > col) {
        cmdInput->scroll = col;
    } else if (cmdInput->scroll + cmdInput->elem.w - 1 <= col) {
        cmdInput->scroll = col - cmdInput->elem.w + 2;
    }
}
