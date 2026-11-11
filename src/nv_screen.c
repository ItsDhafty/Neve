#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "nv_escapes.h"
#include "clib_mem.h"
#include "nv_screen.h"
#include "nv_string.h"
#include "nv_term.h"
#include "nv_utils.h"

void screenInit(Screen *screen) {
    screen->w = 0;
    screen->h = 0;
    strInit(&screen->buf, 0);
    strInit(&screen->tempBuf, 0);
    screen->editRows = NULL;
    screen->displayRows = NULL;
    screen->editStyles = NULL;
    screen->displayStyles = NULL;
}

void screenDestroy(Screen *screen) {
    strDestroy(&screen->buf);
    strDestroy(&screen->tempBuf);

    memFree(screen->editRows);
    memFree(screen->displayRows);
    memFree(screen->editStyles);
    memFree(screen->displayStyles);
    screen->w = 0;
    screen->h = 0;
    screen->editRows = NULL;
    screen->displayRows = NULL;
}

ScreenStyle *_resizeStyles(ScreenStyle *styles, size_t oldLen, size_t newLen) {
    if (oldLen == newLen) {
        return styles;
    }
    if (oldLen > newLen) {
        return memShrink(styles, newLen, sizeof(*styles));
    } else {
        ScreenStyle *newStyles = memChange(styles, newLen, sizeof(*styles));
        memset(newStyles + oldLen, 0, (newLen - oldLen) * sizeof(*styles));
        return newStyles;
    }
}

ScreenRows *_resizeRows(ScreenRows *rows, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) {
        return NULL; // not a failure just the value of screen->*Rows when empty
    }
    // Memory layout:
    // [ rows struct | sBufs array | sBufs buffers ]
    // Alignment is fine since ScreenRows contains pointers
    size_t newSize = sizeof(*rows)
                   + sizeof(*rows->sBufs) * h
                   + w * 4 * h; // max of 4 bytes per column
    ScreenRows *newRows = memChangeBytes(rows, newSize);

    newRows->sBufs = (StrBuf *)(newRows + 1);
    newRows->buffer = (Utf8Ch *)&newRows->sBufs[h];

    for (uint16_t i = 0; i < h; i++) {
        newRows->sBufs[i] = (StrBuf) {
            .buf = (char *)newRows->buffer + (w * 4 * i),
            .bufSize = w * 4,
            .len = 0
        };
    }
    return newRows;
}

void screenResize(Screen *screen, uint16_t w, uint16_t h) {
    if (w == screen->w && h == screen->h) {
        return;
    }
    screen->resized = true;

    screen->editRows = _resizeRows(screen->editRows, w, h);
    screen->displayRows = _resizeRows(screen->displayRows, w, h);
    screen->editStyles = _resizeStyles(
        screen->editStyles,
        screen->w * screen->h,
        w * h
    );
    screen->displayStyles = _resizeStyles(
        screen->displayStyles,
        screen->w * screen->h,
        w * h
    );

    screen->w = w;
    screen->h = h;
}

static uint16_t _cutStr(StrView *sv, size_t aim, uint16_t w, bool canOvershoot) {
    ptrdiff_t i = 0;
    uint8_t cpWidth = 0;
    UcdCP cp = 0;
    for (
        i = strViewNext(sv, -1, &cp);
        i != -1 && w < aim;
        i = strViewNext(sv, i, &cp)
    ) {
        // Assume tabs have been filtered out
        cpWidth = ucdCPWidth(cp, 1, w);

        if (!canOvershoot && w + cpWidth > aim) {
            break;
        }

        w += cpWidth;
    }

    if (i != -1) {
        sv->len = i;
    }
    return w;
}

void screenWrite(
    Screen *screen,
    int16_t x, int16_t y,
    const Utf8Ch *str, size_t len
) {
    if (x >= screen->w || y >= screen->h || y < 0) {
        return;
    }

    StrView sv = { .buf = str, .len = len };

    // Make the x >= 0
    if (x < 0) {
        uint16_t w = _cutStr(&sv, (size_t)(-x), 0, true);
        if (sv.len == len || w < -x) { // The whole string is out of view
            return;
        }
        str = sv.buf + sv.len;
        len = len - sv.len;
        x += w;
    }

    uint16_t w = 0;
    uint16_t screenW = screen->w;
    StrBuf *row = &screen->editRows->sBufs[y];

    // Find the index in the string where the new string begins
    UcdCP cp = 0;
    uint8_t cpWidth = 0;
    ptrdiff_t startIdx = 0;
    for (
        startIdx = strViewNext((StrView *)row, -1, &cp);
        x != 0 && startIdx != -1;
        startIdx = strViewNext((StrView *)row, startIdx, &cp)
    ) {
        // Assume tabs have been filtered out
        cpWidth = ucdCPWidth(cp, 0, w);
        if (w + cpWidth > x) {
            break;
        }
        w += cpWidth;
    }

    uint16_t rowW = startIdx == -1 ? w : w + cpWidth; // Needed below

    // Append the part before the string to the new row
    sv = (StrView){
        .buf = (Utf8Ch *)row->buf,
        .len = startIdx < 0 ? row->len : (size_t)startIdx
    };
    strAppend(&screen->buf, &sv);
    // Pad with spaces if the boundary was in the middle of a character
    strRepeat(&screen->buf, ' ', x - w);

    // Add the string without going over the width of the screen
    sv = (StrView) { .buf = str, .len = len };
    w = (uint16_t)_cutStr(&sv, screenW, w, false);
    strAppend(&screen->buf, &sv);

    if (w == screenW || startIdx == -1) {
        goto replaceRow;
    }

    // Add any characters that were after the string
    ptrdiff_t rowIdx = 0;
    for (
        rowIdx = strViewNext((StrView *)row, startIdx, &cp);
        rowIdx != -1 && rowW < w;
        rowIdx = strViewNext((StrView *)row, rowIdx, &cp)
    ) {
        rowW += ucdCPWidth(cp, 0, rowW);
    }

    if (rowIdx == -1) {
        goto replaceRow;
    }

    strRepeat(&screen->buf, ' ', rowW - w);

    sv.buf = (Utf8Ch *)row->buf + rowIdx;
    sv.len = row->len - rowIdx;
    (void)_cutStr(&sv, screenW, rowW, false);
    strAppend(&screen->buf, &sv);

replaceRow:
    strBufClear(row);
    strBufAppend(row, (StrView *)&screen->buf);
    strClear(&screen->buf, screen->buf.cap);
}

nvUnixFmt(4, 5) void screenWriteFmt(
    Screen *screen,
    int16_t x, int16_t y,
    nvWinFmt const char *fmt, ...
) {
    char buf[2048] = { 0 };
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, nvArrlen(buf), fmt, args);
    va_end(args);
    if (len < 0) {
        return;
    }

    screenWrite(screen, x, y, (Utf8Ch *)buf, len);
}

void screenClear(Screen *screen, int32_t line) {
    if (line > screen->h) {
        return;
    }

    if (line >= 0) {
        strBufClear(&screen->editRows->sBufs[line]);
        memset(
            &screen->editStyles[screen->w * line],
            0,
            screen->w * sizeof(*screen->editStyles)
        );
        return;
    }
    for (uint16_t y = 0; y < screen->h; y++) {
        strBufClear(&screen->editRows->sBufs[y]);
    }
    memset(
        screen->editStyles,
        0,
        screen->w * screen->h * sizeof(*screen->editStyles)
    );
}

static bool _rowChanged(Screen *screen, uint16_t idx) {
    StrBuf *editRow = &screen->editRows->sBufs[idx];
    StrBuf *displayRow = &screen->displayRows->sBufs[idx];

    if (editRow->len != displayRow->len) {
        return true;
    }

    if (memcmp(editRow->buf, displayRow->buf, editRow->len) != 0) {
        return true;
    }
    return memcmp(
        &screen->editStyles[idx * screen->w],
        &screen->displayStyles[idx * screen->w],
        sizeof(*screen->editStyles) * screen->w
    );
}

void screenSetFg(
    Screen *screen,
    ScreenColor fg,
    int16_t x,
    int16_t y,
    uint16_t width
) {
    ScreenStyle style = {
        .fg.r = fg.col.r,
        .fg.g = fg.col.g,
        .fg.b = fg.col.b,
        .bgMode = fg.mode,
        .style = screenStyleNoBg | screenStyleNoFmt,
    };
    screenSetStyle(screen, style, x, y, width);
}

void screenSetBg(
    Screen *screen,
    ScreenColor bg,
    int16_t x,
    int16_t y,
    uint16_t width
) {
    ScreenStyle style = {
        .bg.r = bg.col.r,
        .bg.g = bg.col.g,
        .bg.b = bg.col.b,
        .bgMode = bg.mode,
        .style = screenStyleNoFg | screenStyleNoFmt,
    };
    screenSetStyle(screen, style, x, y, width);
}

void screenSetTextFmt(
    Screen *screen,
    ScreenTextFmt textFmt,
    int16_t x,
    int16_t y,
    uint16_t width
) {
    ScreenStyle style = {
        .textFmt = textFmt,
        .style = screenStyleNoFg | screenStyleNoBg,
    };
    screenSetStyle(screen, style, x, y, width);
}

// Set the full style of a region.
void screenSetStyle(
    Screen *screen,
    ScreenStyle style,
    int16_t x,
    int16_t y,
    uint16_t width
) {
    if (
        y >= (int16_t)screen->h
        || x >= (int16_t)screen->w
        || y < 0
        || x + (int16_t)width < 0
    ) {
        return;
    } else if (x < 0) {
        width = (uint16_t)(x + (int16_t)width);
        x = 0;
    }

    // From here x >= 0, y >= 0

    width = nvMin(width, screen->w - x);

    ScreenStyle *styles = &screen->editStyles[y * screen->w + x];
    for (uint16_t i = 0; i < width; i++) {
        if (!(style.style & screenStyleNoFg)) {
            styles[i].fg = style.fg;
            styles[i].fgMode = style.fgMode;
        }
        if (!(style.style & screenStyleNoBg)) {
            styles[i].bg = style.bg;
            styles[i].bgMode = style.bgMode;
        }
        if (!(style.style & screenStyleNoFmt)) {
            styles[i].textFmt = style.textFmt;
        }
        styles[i].style = 0;
    }
}

static bool _screenStyleEq(ScreenStyle st1, ScreenStyle st2) {
    return memcmp(&st1, &st2, sizeof(st1)) == 0;
}

static void _screenChangeStyle(Screen *screen, ScreenStyle st) {
    strAppendC(&screen->buf, "\x1b[0");
    if (st.textFmt & screenFmtBold) {
        strAppendC(&screen->buf, ";1");
    }
    if (st.textFmt & screenFmtDim) {
        strAppendC(&screen->buf, ";2");
    }
    if (st.textFmt & screenFmtItalic) {
        strAppendC(&screen->buf, ";3");
    }
    if (st.textFmt & screenFmtUnderline) {
        strAppendC(&screen->buf, ";4");
    }
    if (st.textFmt & screenFmtInverse) {
        strAppendC(&screen->buf, ";7");
    }
    if (st.textFmt & screenFmtStrike) {
        strAppendC(&screen->buf, ";9");
    }

    if (st.fgMode == screenColModeT16 && st.fg.r != 0) {
        strAppendFmt(&screen->buf, ";%d", st.fg.r + 29);
    } else if (st.fgMode == screenColModeT256) {
        strAppendFmt(&screen->buf, ";38;5;%d", st.fg.r);
    } else if (st.fgMode == screenColModeRGB) {
        strAppendFmt(&screen->buf, ";38;2;%d;%d;%d", st.fg.r, st.fg.g, st.fg.b);
    }

    if (st.bgMode == screenColModeT16 && st.bg.r != 0) {
        strAppendFmt(&screen->buf, ";%d", st.bg.r + 39);
    } else if (st.bgMode == screenColModeT256) {
        strAppendFmt(&screen->buf, ";48;5;%d", st.bg.r);
    } else if (st.bgMode == screenColModeRGB) {
        strAppendFmt(&screen->buf, ";48;2;%d;%d;%d", st.bg.r, st.bg.g, st.bg.b);
    }
    strAppendC(&screen->buf, "m");
}

static void _writeLine(Screen *screen, uint16_t idx) {
    StrView *editRow = (StrView *)&screen->editRows->sBufs[idx];
    strAppendFmt(
        &screen->buf,
        escCursorSetPos("%u", "%u") escLineClear,
        idx + 1, 1
    );

    ScreenStyle currSt = { 0 };
    StrView span = {
        .buf = editRow->buf,
        .len = 0
    };
    size_t width = 0;
    UcdCP cp = 0;
    for (
        ptrdiff_t i = strViewNext(editRow, -1, &cp);
        i != -1;
        i = strViewNext(editRow, i, &cp)
    ) {
        ScreenStyle cellSt = screen->editStyles[idx * screen->w + width];
        width += ucdCPWidth(cp, 0, 0);
        if (!_screenStyleEq(cellSt, currSt)) {
            span.len = i - (span.buf - editRow->buf);
            strAppend(&screen->buf, &span);
            span.buf = editRow->buf + i;
            span.len = 0;
            currSt = cellSt;
            _screenChangeStyle(screen, cellSt);
        }
    }
    span.len = editRow->len - (span.buf - editRow->buf);
    strAppend(&screen->buf, &span);

    if (width >= screen->w) {
        goto end;
    }

    for (size_t i = 0, n = screen->w - width; i < n; i++) {
        ScreenStyle cellSt = screen->editStyles[idx * screen->w + width + i];
        if (!_screenStyleEq(cellSt, currSt)) {
            currSt = cellSt;
            _screenChangeStyle(screen, cellSt);
        }
        strAppendRaw(&screen->buf, (Utf8Ch *)" ", 1);
    }

end:
    _screenChangeStyle(screen, (ScreenStyle) { 0 });
}

bool screenRefresh(Screen *screen) {
    strAppendC(
        &screen->buf,
        escCursorSetPos("", "")
    );

    bool resized = screen->resized;

    if (resized) {
        strAppendC(&screen->buf, escScreenClear);
        screen->resized = false;
    }

    for (uint16_t i = 0; i < screen->h; i++) {
        if (!resized && !_rowChanged(screen, i)) {
            continue;
        }
        _writeLine(screen, i);
    }

    ScreenRows *tempRows = screen->editRows;
    screen->editRows = screen->displayRows;
    screen->displayRows = tempRows;

    ScreenStyle *tempStyles = screen->editStyles;
    screen->editStyles = screen->displayStyles;
    screen->displayStyles = tempStyles;

    for (uint16_t y = 0; y < screen->h; y++) {
        strBufClear(&screen->editRows->sBufs[y]);
    }
    memset(
        screen->editStyles,
        0,
        sizeof(*screen->editStyles) * screen->w * screen->h
    );

    uint16_t curX = 0, curY = 0;

    strAppendFmt(&screen->buf, escCursorSetPos("%u", "%u"), curY + 1, curX + 1);

    if (!termWrite(screen->buf.buf, screen->buf.len)) {
        return false;
    }
    strClear(&screen->buf, screen->buf.len);

    return true;
}
