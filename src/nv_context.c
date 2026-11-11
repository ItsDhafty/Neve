#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "clib_mem.h"
#include "nv_context.h"
#include "nv_array.h"
#include "nv_string.h"
#include "nv_utils.h"
#include "unicode/nv_utf.h"
#include "unicode/nv_ucd.h"

#ifndef _lineRefMaxGap
#define _lineRefMaxGap 65535
#endif // !_lineRefMaxGap

#define _maxIndentWidth 32
// Number of characters added/deleted before a checkpoint is added.
#define _editsForCheckpoint 20

// Get from the gap buffer
static inline Utf8Ch *_ctxBufGet(const CtxBuf *buf, size_t idx);
// Reserve space in the gap buffer
static void _ctxBufReserve(CtxBuf *buf, size_t amount);
// Shrkin if too much space is empty
static void _ctxBufShrink(CtxBuf *buf);
// Insert at the gapIdx and move it
static void _ctxBufInsert(CtxBuf *buf, const Utf8Ch *text, size_t len);
// Remove before the gap index and move it
static void _ctxBufRemove(CtxBuf *buf, size_t len);
// Change the gap index
static void _ctxBufSetGapIdx(CtxBuf *buf, size_t gapIdx);

static size_t _ctxFindNextWordStart(const Ctx *ctx, size_t idx);
static size_t _ctxFindNextWordEnd(const Ctx *ctx, size_t idx);
static size_t _ctxFindPrevWordStart(const Ctx *ctx, size_t idx);
static size_t _ctxFindPrevWordEnd(const Ctx *ctx, size_t idx);

// Replace the text in [start, end) with the contents of buf
static void _ctxReplace(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *buf,
    size_t len
);

// Get the index of the cursor at idx or of the cursor on where it should be
// inserted
static size_t _ctxCurAt(const CtxCursors *arr, size_t idx);
static void _ctxCurAddEx(CtxCursors *arr, CtxCursor cursor, bool movingBack);
static void _ctxCurMoveMany(
    Ctx *ctx,
    ptrdiff_t (*moveFunc)(Ctx *, CtxCursor, ptrdiff_t *)
);

// Join cursor selections in the sels array
static void _ctxSelJoin(Ctx *ctx);
static void _ctxSelNormalize(Ctx *ctx);

// Get info about the indentation.
static size_t _ctxIndentInfo(Ctx *ctx, size_t line, ptrdiff_t *outIdx);
// Set the indent of a line.
static void _ctxLineSetIndent(
    Ctx *ctx,
    size_t indentStart,
    size_t indentEnd,
    size_t indent,
    Str *indentBuf
);
// Indent a line.
static void _ctxLineIndent(Ctx *ctx, size_t line, Str *indentBuf);
// Dedent a line.
static void _ctxLineDedent(Ctx *ctx, size_t line, Str *indentBuf);

static bool _ctxEditIsCheckpoint(CtxEdit *edit);

void ctxInit(Ctx *ctx, bool multiline) {
    ctx->_refs = (CtxRefs){ 0 };
    ctx->_buf = (CtxBuf) {
        .bytes = NULL,
        .len = 0,
        .cap = 0,
        .gapIdx = 0
    };
    ctx->_edits = (CtxEdits){ 0 };
    strInit(&ctx->_editsBuf, 0);
    ctx->_editIdx = 0;
    // _editCount > 0 -> additions
    // _editCount < 0 -> deletions
    ctx->_editCount = 0;

    ctx->cursors = (CtxCursors){ 0 };
    ctx->_curSwap = (CtxCursors){ 0 };
    ctx->sels = (CtxSelections){ 0 };
    ctx->selKind = CtxSelection_None;
    ctx->edited = false;
    ctx->multiline = multiline;
    ctx->tabStop = 8;
    ctx->indentWidth = 4;
    ctx->mergeSpaces = false;
    ctx->_cpPending = false;
}

void ctxDestroy(Ctx *ctx) {
    arrClear(&ctx->_refs);
    arrClear(&ctx->sels);
    arrClear(&ctx->cursors);
    arrClear(&ctx->_curSwap);

    memFree(ctx->_buf.bytes);

    arrClear(&ctx->_edits);
    strClear(&ctx->_editsBuf, 0);

    // Keep the context in a valid state after deletion
    ctxInit(ctx, ctx->multiline);
}

static inline Utf8Ch *_ctxBufGet(const CtxBuf *buf, size_t idx) {
    nvAssertExpr(idx < buf->len);
    if (idx >= buf->gapIdx) {
        size_t gapSize = buf->cap - buf->len;
        idx += gapSize;
    }
    return &buf->bytes[idx];
}

static void _ctxBufReserve(CtxBuf *buf, size_t amount) {
    size_t requiredLen = buf->len + amount;
    if (requiredLen <= buf->cap) {
        return;
    }

    size_t newCap = nvMax(requiredLen + requiredLen / 2, 128);
    buf->bytes = memChange(
        buf->bytes,
        newCap,
        sizeof(*buf->bytes)
    );

    size_t newGapSize = newCap - buf->len;
    size_t gapSize = buf->cap - buf->len;

    memmove(
        buf->bytes + buf->gapIdx + newGapSize,
        buf->bytes + buf->gapIdx + gapSize,
        (buf->len - buf->gapIdx) * sizeof(*buf->bytes)
    );
    buf->cap = newCap;
}

static void _ctxBufShrink(CtxBuf *buf) {
    if (buf->len >= buf->cap / 4) {
        return;
    }
    size_t newCap = nvMax(buf->len * 2, 128);
    size_t newGapSize = newCap - buf->len;
    size_t gapSize = buf->cap - buf->len;
    memmove(
        buf->bytes + buf->gapIdx + newGapSize,
        buf->bytes + buf->gapIdx + gapSize,
        (buf->len - buf->gapIdx) * sizeof(*buf->bytes)
    );

    buf->bytes = memShrink(
        buf->bytes,
        newCap,
        sizeof(*buf->bytes)
    );

    buf->cap = newCap;
}

static void _ctxBufInsert(CtxBuf *buf, const Utf8Ch *text, size_t len) {
    _ctxBufReserve(buf, len);
    memcpy(buf->bytes + buf->gapIdx, text, len * sizeof(*text));
    buf->gapIdx += len;
    buf->len += len;
}

static void _ctxBufRemove(CtxBuf *buf, size_t len) {
    if (len > buf->gapIdx) {
        len = buf->gapIdx;
    }

    buf->gapIdx -= len;
    buf->len -= len;

    nvAssertExpr(
        buf->gapIdx == buf->len
        || utf8ChIsStart(*_ctxBufGet(buf, buf->gapIdx))
    );

    _ctxBufShrink(buf);
}

static void _ctxBufSetGapIdx(CtxBuf *buf, size_t gapIdx) {
    nvAssertExpr(gapIdx <= buf->len);
    nvAssertExpr(
        gapIdx == buf->len
        || utf8ChIsStart(*_ctxBufGet(buf, gapIdx))
    );
    if (buf->gapIdx == gapIdx) {
        return;
    }

    size_t gapSize = buf->cap - buf->len;

    if (gapIdx > buf->gapIdx) {
        size_t moveSize = gapIdx - buf->gapIdx;
        memmove(
            buf->bytes + buf->gapIdx,
            buf->bytes + buf->gapIdx + gapSize,
            moveSize * sizeof(*buf->bytes)
        );
    } else {
        size_t moveSize = buf->gapIdx - gapIdx;
        memmove(
            buf->bytes + gapIdx + gapSize,
            buf->bytes + gapIdx,
            moveSize * sizeof(*buf->bytes)
        );
    }
    buf->gapIdx = gapIdx;
}

// Get the block where line lineNo starts, -1 means the beginning of the file
static ptrdiff_t _ctxGetLineRefBlock(const Ctx *ctx, size_t lineNo) {
    size_t refsLen = ctx->_refs.len;
    CtxRef *refs = ctx->_refs.items;

    if (lineNo == 0 || refsLen == 0 || refs[0].line >= lineNo) {
        return -1;
    }

    size_t lo = 0;
    size_t hi = refsLen;

    while (lo < hi) {
        size_t mid = (hi + lo) / 2;

        if (refs[mid].line >= lineNo) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    return lo - 1;
}

// Get the block that contains idx
static ptrdiff_t _ctxGetIdxRefBlock(const Ctx *ctx, size_t idx) {
    size_t refsLen = ctx->_refs.len;
    CtxRef *refs = ctx->_refs.items;

    if (idx == 0 || refsLen == 0 || refs[0].idx > idx) {
        return -1;
    }

    size_t lo = 0;
    size_t hi = refsLen;

    while (lo < hi) {
        size_t mid = (hi + lo) / 2;

        if (refs[mid].idx == idx) {
            return mid;
        } else if (refs[mid].idx > idx) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }

    return lo - 1;
}

ptrdiff_t ctxLineStart(const Ctx *ctx, size_t lineNo) {
    if (lineNo == 0) {
        return 0;
    }

    ptrdiff_t refsIdx = _ctxGetLineRefBlock(ctx, lineNo);
    size_t i;
    size_t lineCount;

    if (refsIdx == -1) {
        i = 0;
        lineCount = 0;
    } else {
        i = ctx->_refs.items[refsIdx].idx;
        lineCount = ctx->_refs.items[refsIdx].line;
    }

    size_t endIdx = (size_t)(refsIdx + 1) < ctx->_refs.len
        ? ctx->_refs.items[refsIdx + 1].idx
        : ctx->_buf.len;

    // Get the precise index
    for (; i < endIdx; i++) {
        if (*_ctxBufGet(&ctx->_buf, i) != '\n') {
            continue;
        }
        lineCount++;
        if (lineCount == lineNo) {
            return i + 1;
        }
    }

    // The line does not exist
    return -1;
}

ptrdiff_t ctxLineEnd(const Ctx *ctx, size_t lineNo) {
    ptrdiff_t refsIdx = _ctxGetLineRefBlock(ctx, lineNo + 1);
    size_t i;
    size_t lineCount;

    if (refsIdx == -1) {
        i = 0;
        lineCount = 0;
    } else {
        i = ctx->_refs.items[refsIdx].idx;
        lineCount = ctx->_refs.items[refsIdx].line;
    }

    size_t endIdx = (size_t)(refsIdx + 1) < ctx->_refs.len
        ? ctx->_refs.items[refsIdx + 1].idx
        : ctx->_buf.len;

    for (; i < endIdx; i++) {
        if (*_ctxBufGet(&ctx->_buf, i) != '\n') {
            continue;
        }
        lineCount++;
        if (lineCount > lineNo) {
            break;
        }
    }

    if (lineCount < lineNo) {
        return  -1;
    } else {
        return i;
    }
}

ptrdiff_t ctxLineEndInc(const Ctx *ctx, size_t lineNo) {
    ptrdiff_t end = ctxLineEnd(ctx, lineNo);
    if (end < 0 || (size_t)end == ctx->_buf.len) {
        return end;
    } else {
        return end + 1;
    }
}

void ctxPosAt(const Ctx *ctx, size_t idx, size_t *outLine, size_t *outCol) {
    nvAssertExpr(idx <= ctx->_buf.len);

    CtxRef *refs = ctx->_refs.items;

    size_t line = 0;
    size_t startIdx = 0;
    size_t col = 0;

    ptrdiff_t refIdx = _ctxGetIdxRefBlock(ctx, idx);
    if (refIdx >= 0) {
        startIdx = refs[refIdx].idx;
        line = refs[refIdx].line;
        col = refs[refIdx].col;
    }

    for (size_t i = startIdx; i < idx; i++) {
        if (*_ctxBufGet(&ctx->_buf, i) == '\n') {
            line++;
            startIdx = i + 1;
            col = 0;
        }
    }

    if (outLine) {
        *outLine = line;
    }

    if (outCol == NULL) {
        return;
    }

    UcdCP cp = 0;
    for (
        ptrdiff_t j = ctxNext(ctx, (ptrdiff_t)startIdx - 1, &cp);
        j >= 0;
        j = ctxNext(ctx, j, &cp)
    ) {
        if ((size_t)j >= idx) {
            break;
        }
        col += ucdCPWidth(cp, ctx->tabStop, col);
    }
    *outCol = col;
}

ptrdiff_t ctxIdxAt(
    const Ctx *ctx,
    size_t line,
    size_t col,
    size_t *outTrueCol
) {
    ptrdiff_t refsIdx = _ctxGetLineRefBlock(ctx, line);
    ptrdiff_t i;
    size_t refLine;
    size_t refCol;
    UcdCP cp;

    refsIdx++;
    while (
        refsIdx < (ptrdiff_t)ctx->_refs.len
        && ctx->_refs.items[refsIdx].line == line
        && ctx->_refs.items[refsIdx].col <= col
    ) {
        refsIdx++;
    }
    refsIdx--;

    if (refsIdx < 0) {
        i = 0;
        refLine = 0;
        refCol = 0;
    } else {
        i = ctx->_refs.items[refsIdx].idx;
        refLine = ctx->_refs.items[refsIdx].line;
        refCol = ctx->_refs.items[refsIdx].col;
    }

    size_t endIdx = (size_t)(refsIdx + 1) < ctx->_refs.len
        ? ctx->_refs.items[refsIdx + 1].idx
        : ctx->_buf.len;

    // Get the precise index
    for (; refLine < line && i < (ptrdiff_t)endIdx; i++) {
        if (*_ctxBufGet(&ctx->_buf, i) != '\n') {
            continue;
        }
        refLine++;
        refCol = 0;
        if (refLine == line) {
            i++;
            break;
        }
    }

    if (refLine != line) {
        // The line does not exist
        return -1;
    }

    for (i = ctxNext(ctx, i - 1, &cp); i != -1; i = ctxNext(ctx, i, &cp)) {
        if (cp == '\n') {
            break;
        }
        uint8_t chWidth = ucdCPWidth(cp, ctx->tabStop, refCol);
        if (refCol + chWidth < col) {
            refCol += chWidth;
            continue;
        }
        // If it is closer to the end of the character do another iteration
        if ((ptrdiff_t)(refCol + chWidth - col) < (ptrdiff_t)(col - refCol)) {
            refCol += chWidth;
            continue;
        }
        break;
    }
    if (i == -1) {
        i = ctx->_buf.len;
    }
    if (outTrueCol != NULL) {
        *outTrueCol = refCol;
    }
    return i;
}

ptrdiff_t ctxNext(const Ctx *ctx, ptrdiff_t idx, UcdCP *outCP) {
    if (idx < 0) {
        idx = 0;
    } else if ((size_t)idx < ctx->_buf.len) {
        uint8_t offset = utf8ChRunLen(*_ctxBufGet(&ctx->_buf, idx));
        // If idx is not on a character boundary find the next one
        if (offset == 0) {
            idx++;
            while (
                (size_t)idx < ctx->_buf.len
                && !utf8ChIsStart(*_ctxBufGet(&ctx->_buf, idx))
            ) {
                idx++;
            }
        } else {
            idx += offset;
        }
    }

    if ((size_t)idx >= ctx->_buf.len) {
        if (outCP != NULL) {
            *outCP = -1;
        }
        return -1;
    }

    if (outCP != NULL) {
        *outCP = utf8ChToCP(_ctxBufGet(&ctx->_buf, idx));
    }
    return idx;
}

ptrdiff_t ctxPrev(const Ctx *ctx, ptrdiff_t idx, UcdCP *outCP) {
    if (idx == 0 || ctx->_buf.len == 0) {
        goto endReached;
    } else if (idx < 0) {
        idx = ctx->_buf.len;
    }
    idx--;
    Utf8Ch *chPtr = _ctxBufGet(&ctx->_buf, idx);
    while (idx >= 0 && !utf8ChIsStart(*chPtr)) {
        idx--;
        chPtr--;
    }
    if (idx < 0) {
        goto endReached;
    }

    if (outCP != NULL) {
        *outCP = utf8ChToCP(chPtr);
    }
    return idx;

endReached:
    if (outCP != NULL) {
        *outCP = -1;
    }
    return -1;
}

ptrdiff_t ctxLineNextStart(const Ctx *ctx, size_t lineIdx, UcdCP *outCP) {
    ptrdiff_t i = ctxLineStart(ctx, lineIdx);
    if (i == (ptrdiff_t)ctx->_buf.len || i < 0) {
        goto noLine;
    }
    if (*_ctxBufGet(&ctx->_buf, i) == '\n') {
        goto noLine;
    }
    if (outCP) {
        *outCP = utf8ChToCP(_ctxBufGet(&ctx->_buf, i));
    }
    return i;

noLine:
    if (outCP) {
        *outCP = -1;
    }
    return -1;
}

ptrdiff_t ctxLinePrevStart(const Ctx *ctx, size_t lineIdx, UcdCP *outCP) {
    ptrdiff_t i = ctxLineEnd(ctx, lineIdx);
    if (i <= 0) {
        goto noLine;
    }
    i--;
    Utf8Ch *chPtr = _ctxBufGet(&ctx->_buf, i);
    if (*chPtr == '\n') {
        goto noLine;
    }
    while (i >= 0 && !utf8ChIsStart(*chPtr)) {
        i--;
        chPtr--;
    }
    if (outCP) {
        *outCP = utf8ChToCP(chPtr);
    }
    return i;

noLine:
    if (outCP) {
        *outCP = -1;
    }
    return -1;
}

ptrdiff_t ctxLineNext(const Ctx *ctx, ptrdiff_t idx, UcdCP *outCP) {
    if (idx < 0) {
        idx = 0;
    } else if ((size_t)idx < ctx->_buf.len) {
        uint8_t offset = utf8ChRunLen(*_ctxBufGet(&ctx->_buf, idx));
        // If idx is not on a character boundary find the next one
        if (offset == 0) {
            idx++;
            while (
                (size_t)idx < ctx->_buf.len
                && !utf8ChIsStart(*_ctxBufGet(&ctx->_buf, idx))
            ) {
                idx++;
            }
        } else {
            idx += offset;
        }
    }

    if ((size_t)idx >= ctx->_buf.len) {
        goto endReached;
    }

    Utf8Ch *chPtr = _ctxBufGet(&ctx->_buf, idx);

    if (*chPtr == '\n') {
        goto endReached;
    }

    if (outCP != NULL) {
        *outCP = utf8ChToCP(chPtr);
    }
    return idx;

endReached:
    if (outCP != NULL) {
        *outCP = -1;
    }
    return -1;
}

ptrdiff_t ctxLinePrev(const Ctx *ctx, ptrdiff_t idx, UcdCP *outCP) {
    if (idx < 0) {
        idx = ctx->_buf.len;
    }
    if (idx == 0) {
        goto endReached;
    }
    idx--;
    Utf8Ch *chPtr = _ctxBufGet(&ctx->_buf, idx);
    while (idx >= 0 && !utf8ChIsStart(*chPtr)) {
        idx--;
        chPtr = _ctxBufGet(&ctx->_buf, idx);
    }
    if (idx < 0 || *chPtr == '\n') {
        goto endReached;
    }

    if (outCP != NULL) {
        *outCP = utf8ChToCP(chPtr);
    }
    return idx;

endReached:
    if (outCP != NULL) {
        *outCP = -1;
    }
    return -1;
}

static UcdCP _ctxGetChAfter(const Ctx *ctx, size_t idx) {
    if (idx >= ctx->_buf.len) {
        return -1;
    }
    return utf8ChToCP(_ctxBufGet(&ctx->_buf, idx));
}

static UcdCP _ctxGetChBefore(const Ctx *ctx, size_t idx) {
    UcdCP cp;
    (void)ctxPrev(ctx, idx, &cp);
    return cp;
}

StrView ctxGetContent(Ctx *ctx) {
    _ctxBufSetGapIdx(&ctx->_buf, ctx->_buf.len);
    return (StrView) { .buf = ctx->_buf.bytes, .len = ctx->_buf.len };
}

static size_t _ctxFindNextWordStart(const Ctx *ctx, size_t idx) {
    if (idx >= ctx->_buf.len) {
        return ctx->_buf.len;
    }
    ptrdiff_t i = idx;
    UcdCP cp = _ctxGetChAfter(ctx, idx);

    if (ucdIsCPAlphanumeric(cp)) {
        for (
            i = ctxNext(ctx, i, &cp);
            i != -1 && ucdIsCPAlphanumeric(cp);
            i = ctxNext(ctx, i, &cp)
        ) { }
    } else {
        for (
            i = ctxNext(ctx, i, &cp);
            i != -1 && !ucdIsCPWhiteSpace(cp) && !ucdIsCPAlphanumeric(cp);
            i = ctxNext(ctx, i, &cp)
        ) { }
    }

    // Skip white space
    for (; i != -1 && ucdIsCPWhiteSpace(cp); i = ctxNext(ctx, i, &cp)) { }

    if (i == -1) {
        i = ctx->_buf.len;
    }
    return (size_t)i;
}

static size_t _ctxFindNextWordEnd(const Ctx *ctx, size_t idx) {
    if (idx >= ctx->_buf.len) {
        return ctx->_buf.len;
    }
    ptrdiff_t i = idx;
    UcdCP cp = _ctxGetChAfter(ctx, idx);

    // Skip white space
    for (; i != -1 && ucdIsCPWhiteSpace(cp); i = ctxNext(ctx, i, &cp)) { }

    if (ucdIsCPAlphanumeric(cp) && i != -1) {
        for (
            i = ctxNext(ctx, i, &cp);
            i != -1 && ucdIsCPAlphanumeric(cp);
            i = ctxNext(ctx, i, &cp)
        ) { }
    } else if (i != -1) {
        for (
            i = ctxNext(ctx, i, &cp);
            i != -1 && !ucdIsCPWhiteSpace(cp) && !ucdIsCPAlphanumeric(cp);
            i = ctxNext(ctx, i, &cp)
        ) { }
    }

    if (i == -1) {
        i = ctx->_buf.len;
    }
    return (size_t)i;
}

static size_t _ctxFindPrevWordStart(const Ctx *ctx, size_t idx) {
    if (idx == 0) {
        return 0;
    }
    ptrdiff_t i = idx;
    UcdCP cp = _ctxGetChBefore(ctx, idx);

    // Skip white space
    for (; i != -1 && ucdIsCPWhiteSpace(cp); i = ctxPrev(ctx, i, &cp)) { }

    if (ucdIsCPAlphanumeric(cp) && i != -1) {
        for (
            i = ctxPrev(ctx, i, &cp);
            i != -1 && ucdIsCPAlphanumeric(cp);
            i = ctxPrev(ctx, i, &cp)
        ) { }
    } else if (i != -1) {
        for (
            i = ctxPrev(ctx, i, &cp);
            i != -1 && !ucdIsCPWhiteSpace(cp) && !ucdIsCPAlphanumeric(cp);
            i = ctxPrev(ctx, i, &cp)
        ) { }
    }
    if (i == -1) {
        i = 0;
    } else {
        i = ctxNext(ctx, i, NULL);
    }
    return i;
}

static size_t _ctxFindPrevWordEnd(const Ctx *ctx, size_t idx) {
    if (idx == 0) {
        return 0;
    }
    ptrdiff_t i = idx;
    UcdCP cp = _ctxGetChBefore(ctx, idx);

    if (ucdIsCPAlphanumeric(cp) && i != -1) {
        for (
            i = ctxPrev(ctx, i, &cp);
            i != -1 && ucdIsCPAlphanumeric(cp);
            i = ctxPrev(ctx, i, &cp)
        ) { }
    } else if (i != -1) {
        for (
            i = ctxPrev(ctx, i, &cp);
            i != -1 && !ucdIsCPWhiteSpace(cp) && !ucdIsCPAlphanumeric(cp);
            i = ctxPrev(ctx, i, &cp)
        ) { }
    }

    // Skip white space
    for (; i != -1 && ucdIsCPWhiteSpace(cp); i = ctxPrev(ctx, i, &cp)) { }

    if (i == -1) {
        i = 0;
    } else {
        i = ctxNext(ctx, i, NULL);
    }
    return i;
}

// Minimize the changed span by removing common prefixes/suffixes
static inline void _ctxReplace_MinimizeSpan(
    Ctx *ctx,
    size_t *inoutStart,
    size_t *inoutEnd,
    const Utf8Ch **inoutData,
    size_t *inoutLen
);

// Add an edit to the edits array
static inline void _ctxReplace_StoreEdit(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *data,
    size_t len
);

// Replace the text that has already been checked & the edit has been added
static inline void _ctxReplace_Raw(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *data,
    size_t len
);

// Move all the cursors inside the changed span to the end
static inline void _ctxReplace_MoveCursorsOutOfSpan(
    Ctx *ctx,
    size_t start,
    size_t end
);

// Update the cache to match the new text
static inline void _ctxReplace_UpdateRefCache(
    Ctx *ctx,
    size_t firstInvalidRefBlock,
    ptrdiff_t lenDiff,
    ptrdiff_t lineDiff,
    ptrdiff_t colDiff,
    size_t changedSpanEndIdx,
    size_t lastChangedLine
);

// Add/remove cache entries after the edited text to keep the distance more or
// less uniform
static inline void _ctxReplace_BalanceRefBlocks(Ctx *ctx, size_t refBlock);

// Update the position & selection (if active) of the cursors
static inline void _ctxReplace_UpdateCursors(
    Ctx *ctx,
    size_t start,
    size_t end,
    ptrdiff_t lenDiff
);

// Update the indices of the selections
static inline void _ctxReplace_UpdateSelections(
    Ctx *ctx,
    size_t start,
    size_t end,
    ptrdiff_t lenDiff
);

static void _ctxReplace(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *data,
    size_t len
) {
    nvAssertExpr(start <= end);
    nvAssertExpr(end <= ctx->_buf.len);
    nvAssertExpr(utf8Check(data, len));

    if (len == 0 && start == end) {
        return;
    }

    _ctxReplace_StoreEdit(ctx, start, end, data, len);
    _ctxReplace_Raw(ctx, start, end, data, len);
}

static inline void _ctxReplace_Raw(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *data,
    size_t len
) {
    _ctxReplace_MinimizeSpan(ctx, &start, &end, &data, &len);
    _ctxReplace_MoveCursorsOutOfSpan(ctx, start, end);

    ctx->edited = true;

    size_t spanStart = 0;
    uint8_t tabStop = ctx->tabStop;
    bool ignoreNL = !ctx->multiline;
    CtxBuf *buf = &ctx->_buf;
    CtxRefs *refs = &ctx->_refs;
    ptrdiff_t lenDiff = (ptrdiff_t)len - (ptrdiff_t)(end - start);
    ptrdiff_t refBlock = _ctxGetIdxRefBlock(ctx, start);
    size_t lastBlockIdx = refBlock < 0 ? 0 : refs->items[refBlock].idx;
    refBlock++; // Ensures that refBlock is positive

    // Keep track of line and column while iterating because the ref cache is
    // not valid after _ctxBufRemove
    size_t line, col;
    ctxPosAt(ctx, start, &line, &col);
    size_t prevLine, prevCol;
    ctxPosAt(ctx, end, &prevLine, &prevCol);

    _ctxBufSetGapIdx(buf, end);
    _ctxBufRemove(buf, end - start);
    _ctxBufReserve(buf, len);

    StrView sv = {
        .buf = data,
        .len = len
    };

    UcdCP cp;
    ptrdiff_t idxOffset = start;
    for (
        ptrdiff_t i = strViewNext(&sv, -1, &cp);
        i >= 0;
        i = strViewNext(&sv, i, &cp)
    ) {
        if (cp == '\r' || (cp == '\n' && ignoreNL)) {
            _ctxBufInsert(buf, &data[spanStart], i - spanStart);
            spanStart = i + 1;
            lenDiff--;
            idxOffset--;
            continue;
        } else if (cp == '\n') {
            line++;
            col = 0;
        } else {
            col += ucdCPWidth(cp, tabStop, col);
        }

        size_t idx = i + idxOffset + utf8CPLen(cp);
        if (idx - lastBlockIdx < _lineRefMaxGap) {
            continue;
        }

        _ctxBufInsert(buf, &data[spanStart], i - spanStart + 1);
        spanStart = i + 1;
        lastBlockIdx = idx;
        CtxRef block = { .idx = idx, .line = line, .col = col };
        if ((size_t)refBlock == refs->len) {
            arrAppend(refs, block);
        } else {
            arrInsert(refs, refBlock, block);
        }
        refBlock++;
    }

    if (spanStart < len) {
        _ctxBufInsert(buf, &data[spanStart], len - spanStart);
    }

    _ctxReplace_UpdateRefCache(
        ctx,
        (size_t)refBlock,
        lenDiff,
        line - prevLine,
        col - prevCol,
        end + lenDiff,
        line
    );
    _ctxReplace_UpdateSelections(ctx, start, end, lenDiff);
    _ctxReplace_UpdateCursors(ctx, start, end, lenDiff);
}

static inline void _ctxReplace_MinimizeSpan(
    Ctx *ctx,
    size_t *inoutStart,
    size_t *inoutEnd,
    const Utf8Ch **inoutData,
    size_t *inoutLen
) {
    size_t start = *inoutStart;
    size_t end = *inoutEnd;
    const Utf8Ch *data = *inoutData;
    size_t len = *inoutLen;

    // Find common prefix
    while (
        len > 0
        && start < end
        && data[0] == *_ctxBufGet(&ctx->_buf, start)
    ) {
        start++;
        data++;
        len--;
    }

    if (len == 0) {
        goto finish;
    }

    // Backtrack to the start of a codepoint
    while (!utf8ChIsStart(*data)) {
        start--;
        data--;
        len++;
    }

    // Find a common suffix
    while (
        len > 0
        && start < end
        && data[len - 1] == *_ctxBufGet(&ctx->_buf, end - 1)
    ) {
        end--;
        len--;
    }

    if (len == 0) {
        goto finish;
    }

    // Go foreward until the end of the codepoint
    do {
        end++;
        len++;
    }
    while (len < *inoutLen && !utf8ChIsStart(data[len - 1]));
    end--;
    len--;

finish:
    *inoutStart = start;
    *inoutEnd = end;
    *inoutData = data;
    *inoutLen = len;
}

static inline void _ctxReplace_StoreEdit(
    Ctx *ctx,
    size_t start,
    size_t end,
    const Utf8Ch *data,
    size_t len
) {
    if (ctx->_editIdx == 0) {
        ctx->_cpPending = false;
    }
    if (ctx->_editIdx < ctx->_edits.len && ctx->_editIdx != 0) {
        nvAssertExpr(_ctxEditIsCheckpoint(&ctx->_edits.items[ctx->_editIdx]));
        ctx->_editIdx++;
        ctx->_cpPending = false;
    }
    if (ctx->_editIdx < ctx->_edits.len) {
        // remove all edits after _editIdx
        nvAssertExpr(!_ctxEditIsCheckpoint(&ctx->_edits.items[ctx->_editIdx]));
        strCut(&ctx->_editsBuf, ctx->_edits.items[ctx->_editIdx].oldTextIdx);
        ctx->_edits.len = ctx->_editIdx;
    }
    if (ctx->_cpPending) {
        ctx->_cpPending = false;
        arrAppend(&ctx->_edits, (CtxEdit){ .oldLen = 0, .newLen = 0 });
        ctx->_editIdx++;
    }

    size_t oldLen = end - start;
    _ctxBufSetGapIdx(&ctx->_buf, end);
    size_t oldIdx = ctx->_editsBuf.len;
    if (start < ctx->_buf.len) {
        strAppendRaw(&ctx->_editsBuf, _ctxBufGet(&ctx->_buf, start), oldLen);
    }
    size_t newIdx = ctx->_editsBuf.len;
    strAppendRaw(&ctx->_editsBuf, data, len);

    ctx->_editIdx++;
    arrAppend(
        &ctx->_edits,
        (CtxEdit){
            .oldTextIdx = oldIdx,
            .oldLen = oldLen,
            .newTextIdx = newIdx,
            .newLen = len,
            .start = start
        }
    );
}

static inline void _ctxReplace_MoveCursorsOutOfSpan(
    Ctx *ctx,
    size_t start,
    size_t end
) {
    size_t firstCurIdx = _ctxCurAt(&ctx->cursors, start + 1);
    size_t lastCurIdx = _ctxCurAt(&ctx->cursors, end);
    if (firstCurIdx >= ctx->cursors.len || firstCurIdx >= lastCurIdx) {
        return;
    }

    // All cursors within [firstCurIdx, lastCurIdx) need to be moved to the end
    // of the replaced span, merging into one.

    size_t anchor;
    ctxPosAt(ctx, end, NULL, &anchor);
    CtxCursor cur = { .idx = end, .anchor = anchor, ._selStart = end };

    for (size_t i = 0; i < lastCurIdx - firstCurIdx; i++) {
        if (ctx->cursors.items[firstCurIdx]._selStart < cur._selStart) {
            cur._selStart = ctx->cursors.items[i]._selStart;
        }
        arrRemove(&ctx->cursors, firstCurIdx);
    }
    _ctxCurAddEx(&ctx->cursors, cur, false);
}

static inline void _ctxReplace_UpdateRefCache(
    Ctx *ctx,
    size_t firstInvalidRefBlock,
    ptrdiff_t lenDiff,
    ptrdiff_t lineDiff,
    ptrdiff_t colDiff,
    size_t changedSpanEndIdx,
    size_t lastChangedLine
) {
    // Update line and index of the invalidated blocks
    for (size_t i = firstInvalidRefBlock, n = ctx->_refs.len; i < n; i++) {
        ctx->_refs.items[i].idx += lenDiff;
        ctx->_refs.items[i].line += lineDiff;
    }

    // Update the columns until the end of the last changed line
    // If there is a tab before the end of the line subsequent columns may
    // change by a different amount, in that case change by colDiff until the
    // tab and then recalculate colDiff
    uint8_t tabStop = ctx->tabStop;
    size_t tabIdx = ctx->_buf.len;
    ptrdiff_t lineEnd = ctxLineEnd(ctx, lastChangedLine);
    nvAssertExpr(lineEnd >= 0);
    if (
        tabStop != 0
        && colDiff % tabStop != 0
        && changedSpanEndIdx < ctx->_buf.len
    ) {
        Utf8Ch *endPtr = _ctxBufGet(&ctx->_buf, changedSpanEndIdx);
        Utf8Ch *tabPtr = memchr(endPtr, '\t', lineEnd - changedSpanEndIdx);
        if (tabPtr != NULL) {
            tabIdx = tabPtr - endPtr + changedSpanEndIdx;
        }
    }

    for (size_t i = firstInvalidRefBlock, n = ctx->_refs.len; i < n; i++) {
        CtxRef *ref = &ctx->_refs.items[i];
        if (colDiff == 0 || ref->idx > (size_t)lineEnd) {
            break;
        } else if (ref->idx <= tabIdx) {
            ref->col += colDiff;
            continue;
        }
        size_t width;
        ctxPosAt(ctx, tabIdx, NULL, &width);
        colDiff += ((width - colDiff) % tabStop) - (width % tabStop);
        ref->col += colDiff;
        tabIdx = ctx->_buf.len;
    }

    _ctxReplace_BalanceRefBlocks(ctx, firstInvalidRefBlock);
}

static inline void _ctxReplace_BalanceRefBlocks(Ctx *ctx, size_t refBlock) {
    const size_t minWidth = _lineRefMaxGap / 2;
    const size_t maxWidth = _lineRefMaxGap + minWidth;

    size_t blockStart = refBlock > 0 ? ctx->_refs.items[refBlock - 1].idx : 0;
    size_t blockEnd = refBlock < ctx->_refs.len
        ? ctx->_refs.items[refBlock].idx
        : ctx->_buf.len;
    size_t nextBlockEnd = refBlock + 1 < ctx->_refs.len
        ? ctx->_refs.items[refBlock + 1].idx
        : ctx->_buf.len;
    bool isLastBlock = refBlock >= ctx->_refs.len;

    size_t width = blockEnd - blockStart;
    size_t fullWidth = nextBlockEnd - blockStart;

    if (width >= minWidth && width <= maxWidth) {
        return;
    }

    if (
        width > maxWidth
        && (isLastBlock || fullWidth > 2 * _lineRefMaxGap)
    ) {
        // Divide both to avoid overflow
        size_t newIdx = blockStart / 2 + blockEnd / 2;
        size_t line, col;
        ctxPosAt(ctx, newIdx, &line, &col);
        arrInsert(
            &ctx->_refs,
            refBlock,
            (CtxRef){ .idx = newIdx, .line = line, .col = col }
        );
    } else if (!isLastBlock && fullWidth <= maxWidth) {
        arrRemove(&ctx->_refs, refBlock);
    } else if (!isLastBlock) {
        size_t newIdx = blockStart / 2 + nextBlockEnd / 2;
        size_t line, col;
        ctxPosAt(ctx, newIdx, &line, &col);
        ctx->_refs.items[refBlock].idx = newIdx;
        ctx->_refs.items[refBlock].line = line;
        ctx->_refs.items[refBlock].col = col;
    }
}

static void _ctxReplace_UpdateCursors(
    Ctx *ctx,
    size_t start,
    size_t end,
    ptrdiff_t lenDiff
) {
    bool selecting = ctx->selKind != CtxSelection_None;
    CtxCursor *cursors = ctx->cursors.items;
    size_t changedLine;
    ctxPosAt(ctx, end + lenDiff, &changedLine, NULL);
    size_t lastBaseIdxCalc = ctxLineEnd(ctx, changedLine);

    // Move the active selections, if selecting
    // When not selecting the value of _selStart is assumed to be invalid
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        CtxCursor *cur = &cursors[i];
        if (cur->idx == end && i > 0 && cursors[i - 1].idx == end + lenDiff) {
            arrRemove(&ctx->cursors, i);
            i--;
            continue;
        }

        if (cur->idx >= end) {
            cur->idx += lenDiff;
            if (cur->idx <= lastBaseIdxCalc) {
                ctxPosAt(ctx, cur->idx, NULL, &cur->anchor);
            }
        }
        if (!selecting || cur->_selStart <= start) {
            continue;
        } else if (cur->_selStart >= end) {
            cur->_selStart += lenDiff;
        // From here we know that _selStart was inside the span
        } else if (cur->idx <= start) {
            cur->_selStart = start;
        } else {
            cur->_selStart = end + lenDiff;
        }
    }
}

static inline void _ctxReplace_UpdateSelections(
    Ctx *ctx,
    size_t start,
    size_t end,
    ptrdiff_t lenDiff
) {
    bool mayJoin = -lenDiff == (ptrdiff_t)(end - start);
    for (size_t i = 0; i < ctx->sels.len; i++) {
        CtxSelection *sel = &ctx->sels.items[i];
        if (sel->endIdx <= start) {
            continue;
        } else if (sel->startIdx >= end) {
            sel->startIdx += lenDiff;
            sel->endIdx += lenDiff;
        } else if (sel->startIdx >= start && sel->endIdx <= end) {
            arrRemove(&ctx->sels, i);
            i--;
            continue;
        } else if (sel->startIdx < start && sel->endIdx > end) {
            sel->endIdx += lenDiff;
        } else if (sel->startIdx < start) {
            sel->endIdx = start;
        } else {
            sel->startIdx = end + lenDiff;
            sel->endIdx += lenDiff;
        }
        if (!mayJoin || sel->startIdx < start) {
            continue;
        }
        if (i > 0 && sel->startIdx == ctx->sels.items[i - 1].endIdx) {
            ctx->sels.items[i - 1].endIdx = sel->endIdx;
            arrRemove(&ctx->sels, i);
            i--;
        }
        mayJoin = false;
    }
}

void ctxAppend(Ctx *ctx, const Utf8Ch *data, size_t len) {
    if (len == 0) {
        return;
    }

    ctx->edited = true;

    size_t spanStart = 0;
    bool ignoreNL = !ctx->multiline;
    CtxBuf *buf = &ctx->_buf;
    size_t lastBlockSize = ctx->_refs.len == 0
        ? buf->len
        : buf->len - ctx->_refs.items[ctx->_refs.len - 1].idx;

    _ctxBufSetGapIdx(buf, buf->len);

    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\r' || (data[i] == '\n' && ignoreNL)) {
            _ctxBufInsert(buf, &data[spanStart], i - spanStart);
            spanStart = i + 1;
            continue;
        }

        // Do not insert blocks in the middle of a UTF8 sequence
        if (
            ++lastBlockSize < _lineRefMaxGap
            || (i + 1 < len && !utf8ChIsStart(data[i + 1]))
        ) {
            continue;
        }

        _ctxBufInsert(buf, &data[spanStart], i - spanStart + 1);
        spanStart = i + 1;
        size_t line, col;
        ctxPosAt(ctx, ctx->_buf.len, &line, &col);
        arrAppend(
            &ctx->_refs,
            (CtxRef){ .idx = ctx->_buf.len, .line = line, .col = col }
        );
        lastBlockSize = 0;
    }

    if (spanStart < len) {
        _ctxBufInsert(buf, &data[spanStart], len - spanStart);
    }
}

typedef Arr(const Utf8Ch *) InsertLines;

static void _ctxInsertRepSels(
    Ctx *ctx,
    const Utf8Ch *data,
    size_t len,
    InsertLines *lines
) {
    CtxSelections *sels = &ctx->sels;
    size_t selsLen = sels->len;
    if (selsLen == 0) {
        return;
    }
    ctxAddUndoCheckpoint(ctx);

    bool useLines = lines != NULL && lines->len == selsLen + 1;
    for (size_t i = 0; i < selsLen; i++) {
        size_t idx = selsLen - i - 1;
        CtxSelection sel = sels->items[idx];
        arrRemove(sels, idx);
        if (useLines) {
            _ctxReplace(
                ctx,
                sel.startIdx,
                sel.endIdx,
                lines->items[idx],
                lines->items[idx + 1] - lines->items[idx] - 1
            );
        } else {
            _ctxReplace(ctx, sel.startIdx, sel.endIdx, data, len);
        }
    }
}

static void _ctxInsertCursors(
    Ctx *ctx,
    const Utf8Ch *data,
    size_t len,
    InsertLines *lines
) {
    if (len == 0) {
        return;
    }

    CtxCursors *cursors = &ctx->cursors;
    size_t cursorsLen = cursors->len;
    if (cursorsLen == 0) {
        return;
    }

    bool useLines = lines != NULL && lines->len == cursorsLen + 1;
    for (size_t i = 0; i < cursorsLen; i++) {
        CtxCursor *cur = &cursors->items[i];
        if (useLines) {
            _ctxReplace(
                ctx,
                cur->idx,
                cur->idx,
                lines->items[i],
                lines->items[i + 1] - lines->items[i] - 1
            );
        } else {
            _ctxReplace(ctx, cur->idx, cur->idx, data, len);
        }
    }
}

void ctxInsert(Ctx *ctx, const Utf8Ch *data, size_t len) {
    if (ctx->_editCount < 0 || ctx->_editCount >= _editsForCheckpoint) {
        ctxAddUndoCheckpoint(ctx);
    }
    ctx->_editCount += len;

    bool wasSelecting = ctxSelIsActive(ctx);
    ctxSelEnd(ctx);
    if (ctx->sels.len == 0 && ctx->cursors.len == 0) {
        return;
    }

    if (ctx->sels.len == 1) {
        _ctxInsertRepSels(ctx, data, len, NULL);
        return;
    } else if (ctx->cursors.len == 1 && !wasSelecting) {
        _ctxInsertCursors(ctx, data, len, NULL);
        return;
    }

    InsertLines lines = { 0 };
    arrAppend(&lines, data);
    const Utf8Ch *p = data;
    const Utf8Ch *end = data + len;
    while (p < end) {
        if (*p++ == '\n') {
            arrAppend(&lines, p);
        }
    }
    if (lines.len == 0 || lines.items[lines.len - 1] != p) {
        arrAppend(&lines, end + 1);
    }
    if (ctx->sels.len != 0 || wasSelecting) {
        _ctxInsertRepSels(ctx, data, len, &lines);
    } else {
        _ctxInsertCursors(ctx, data, len, &lines);
    }
    arrClear(&lines);
}

static size_t _cpToUTF8Filtered(UcdCP cp, bool allowLF, Utf8Ch *outBuf) {
    if (cp < 0 || cp > ucdCPMax) {
        return 0;
    }

    UcdCPInfo info = ucdGetCPInfo(cp);
    // Do not insert control characters
    if (
        (cp != '\n' || !allowLF)
        && cp != '\t'
        && UcdMajorCategory(info.category) == UcdCategory_C
    ) {
        return 0;
    }

    return utf8FromCP(cp, outBuf);
}

static void _ctxInsertNLRepSels(Ctx *ctx) {
    CtxSelections *sels = &ctx->sels;
    size_t selsLen = sels->len;
    Str indentBuf = { 0 };

    for (size_t i = 0; i < selsLen; i++) {
        size_t idx = selsLen - i - 1;
        CtxSelection sel = sels->items[idx];
        arrRemove(sels, idx);

        size_t line;
        ctxPosAt(ctx, sel.startIdx, &line, NULL);
        size_t indent = _ctxIndentInfo(ctx, line, NULL);
        _ctxReplace(ctx, sel.startIdx, sel.endIdx, (Utf8Ch *)"\n", 1);
        _ctxLineSetIndent(
            ctx,
            sel.startIdx + 1,
            sel.startIdx + 1,
            indent,
            &indentBuf
        );
    }
    strDestroy(&indentBuf);
}

static void _ctxInsertNLCursors(Ctx *ctx) {
    CtxCursors *cursors = &ctx->cursors;
    size_t cursorsLen = cursors->len;
    Str indentBuf = { 0 };

    for (size_t i = 0; i < cursorsLen; i++) {
        CtxCursor *cur = &cursors->items[i];
        size_t line;
        ctxPosAt(ctx, cur->idx, &line, NULL);
        size_t indent = _ctxIndentInfo(ctx, line, NULL);
        _ctxReplace(ctx, cur->idx, cur->idx, (Utf8Ch *)"\n", 1);
        _ctxLineSetIndent(ctx, cur->idx, cur->idx, indent, &indentBuf);
    }
    strDestroy(&indentBuf);
}

// Get the line the cursor is on to change the indent or -1 if the cursor is in
// the middle of the line.
static ptrdiff_t _ctxCurIndentLine(const Ctx *ctx, CtxCursor *cur) {
    size_t line;
    ctxPosAt(ctx, cur->idx, &line, NULL);
    ptrdiff_t lineStart = ctxLineStart(ctx, line);
    nvAssertExpr(lineStart >= 0);
    for (size_t i = cur->idx; i > (size_t)lineStart; i--) {
        Utf8Ch ch = *_ctxBufGet(&ctx->_buf, i - 1);
        if (ch != ' ' && ch != '\t') {
            return -1;
        }
    }
    return (ptrdiff_t)line;
}

static void _ctxInsertTabCursors(Ctx *ctx) {
    CtxCursors *cursors = &ctx->cursors;
    size_t cursorsLen = cursors->len;
    Str indentBuf = { 0 };

    for (size_t i = 0; i < cursorsLen; i++) {
        CtxCursor *cur = &cursors->items[i];
        ptrdiff_t line = _ctxCurIndentLine(ctx, cur);
        if (line >= 0) {
            _ctxLineIndent(ctx, (size_t)line, &indentBuf);
        } else {
            _ctxReplace(ctx, cur->idx, cur->idx, (Utf8Ch *)"\t", 1);
        }
    }
    strDestroy(&indentBuf);
}

void ctxInsertCP(Ctx *ctx, UcdCP cp) {
    if (cp == 0 || (cp == '\n' && !ctx->multiline)) {
        return;
    }

    ctxSelEnd(ctx);

    if (cp == '\n') {
        ctxAddUndoCheckpoint(ctx);
        if (ctx->sels.len != 0) {
            _ctxInsertNLRepSels(ctx);
        } else {
            _ctxInsertNLCursors(ctx);
        }
        return;
    } else if (cp == '\t' && ctx->sels.len == 0) {
        ctxAddUndoCheckpoint(ctx);
        _ctxInsertTabCursors(ctx);
        return;
    }

    Utf8Ch buf[4];
    size_t len = _cpToUTF8Filtered(cp, ctx->multiline, buf);
    if (len == 0) {
        return;
    }

    ctxInsert(ctx, buf, len);
}

static void _ctxRemoveSelections(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    for (size_t i = ctx->sels.len; i > 0; i--) {
        CtxSelection sel = ctx->sels.items[i - 1];
        _ctxReplace(ctx, sel.startIdx, sel.endIdx, NULL, 0);
    }
    arrClear(&ctx->sels);
}

static void _ctxRemoveBackCursors(Ctx *ctx) {
    Str indentBuf = { 0 };
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        size_t prevLen = ctx->cursors.len;
        CtxCursor *cur = &ctx->cursors.items[i];
        ptrdiff_t line = _ctxCurIndentLine(ctx, cur);
        ptrdiff_t lineStart = ctxLineStart(ctx, line);
        if (line >= 0 && lineStart != (ptrdiff_t)cur->idx) {
            _ctxLineDedent(ctx, (size_t)line, &indentBuf);
        } else {
            size_t end = cur->idx;
            ptrdiff_t start = ctxPrev(ctx, end, NULL);
            if (start == -1) {
                start = 0;
            }
            _ctxReplace(ctx, start, end, NULL, 0);
        }
        // Do not advance when cursors are deleted
        i -= prevLen - ctx->cursors.len;
    }
    strDestroy(&indentBuf);
}

static void _ctxRemoveFwdCursors(Ctx *ctx) {
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        size_t prevLen = ctx->cursors.len;
        CtxCursor *cur = &ctx->cursors.items[ctx->cursors.len - i - 1];
        size_t start = cur->idx;
        ptrdiff_t end = ctxNext(ctx, start, NULL);
        if (end == -1) {
            end = ctx->_buf.len;
        }
        _ctxReplace(ctx, start, end, NULL, 0);
        // Do not advance when cursors are deleted
        i -= prevLen - ctx->cursors.len;
    }
}

void ctxRemoveBack(Ctx *ctx) {
    if (ctx->_editCount > 0 || (-ctx->_editCount) >= _editsForCheckpoint) {
        ctxAddUndoCheckpoint(ctx);
    }
    ctxSelEnd(ctx);

    if (ctx->sels.len != 0) {
        _ctxRemoveSelections(ctx);
    } else {
        _ctxRemoveBackCursors(ctx);
        ctx->_editCount--;
    }
}

void ctxRemoveFwd(Ctx *ctx) {
    if (ctx->_editCount > 0 || (-ctx->_editCount) >= _editsForCheckpoint) {
        ctxAddUndoCheckpoint(ctx);
    }
    ctxSelEnd(ctx);

    if (ctx->sels.len != 0) {
        _ctxRemoveSelections(ctx);
    } else {
        _ctxRemoveFwdCursors(ctx);
        ctx->_editCount--;
    }
}

void _ctxRemoveSelectionLines(Ctx *ctx) {
    for (size_t i = 0; i < ctx->sels.len; i++) {
        CtxSelection *sel = &ctx->sels.items[i];
        size_t startLine, endLine;
        ctxPosAt(ctx, sel->startIdx, &startLine, NULL);
        ctxPosAt(ctx, sel->endIdx, &endLine, NULL);

        sel->startIdx = (size_t)ctxLineStart(ctx, startLine);
        sel->endIdx = (size_t)ctxLineEndInc(ctx, startLine);
    }
    _ctxSelNormalize(ctx);
    _ctxRemoveSelections(ctx);
}

void _ctxRemoveCursorLines(Ctx *ctx) {
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        size_t prevLen = ctx->cursors.len;
        CtxCursor *cur = &ctx->cursors.items[i];
        size_t lineNo;
        ctxPosAt(ctx, cur->idx, &lineNo, NULL);
        ptrdiff_t start = ctxLineStart(ctx, lineNo);
        ptrdiff_t end = ctxLineEnd(ctx, lineNo);

        if (start < 0 || end < 0) continue;

        if ((size_t)end < ctx->_buf.len) {
            end++;
        } else if (start > 0) {
            start--;
        }
        _ctxReplace(ctx, start, end, NULL, 0);
        // Do not advance when cursors are deleted
        i -= prevLen - ctx->cursors.len;
    }
}

void ctxRemoveLine(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    ctxSelEnd(ctx);

    if (ctx->sels.len != 0) {
        _ctxRemoveSelectionLines(ctx);
    } else {
        _ctxRemoveCursorLines(ctx);
    }
}

static size_t _ctxIndentInfo(Ctx *ctx, size_t line, ptrdiff_t *outIdx) {
    ptrdiff_t start = ctxLineStart(ctx, line);
    ptrdiff_t end = ctxLineEnd(ctx, line);
    size_t indent = 0;
    ptrdiff_t i = -1;

    if (start < 0 || end < 0) {
        goto exit;
    }

    for (i = start; i < end; i++) {
        Utf8Ch ch = *_ctxBufGet(&ctx->_buf, i);
        if (ch != ' ' && ch != '\t') {
            break;
        }
        indent += ucdCPWidth(ch, ctx->tabStop, indent);
    }

exit:
    if (outIdx != NULL) {
        *outIdx = i;
    }
    return indent;
}

static void _ctxLineSetIndent(
    Ctx *ctx,
    size_t indentStart,
    size_t indentEnd,
    size_t indent,
    Str *indentBuf
) {
    if (!ctx->mergeSpaces) {
        strRepeat(indentBuf, ' ', indent);
    } else {
        size_t tabs = indent / ctx->tabStop;
        size_t spaces = indent % ctx->tabStop;
        strRepeat(indentBuf, '\n', tabs);
        strRepeat(indentBuf, ' ', spaces);
    }
    _ctxReplace(
        ctx,
        indentStart,
        indentEnd,
        indentBuf->buf,
        indentBuf->len
    );
}

static void _ctxLineIndent(Ctx *ctx, size_t line, Str *indentBuf) {
    nvAssertExpr(ctx->indentWidth <= _maxIndentWidth);
    strClear(indentBuf, indentBuf->cap);
    ptrdiff_t start = ctxLineStart(ctx, line);
    ptrdiff_t end;
    size_t currIndent = _ctxIndentInfo(ctx, line, &end);
    size_t indent =
        currIndent + ctx->indentWidth - (currIndent % ctx->indentWidth);
    _ctxLineSetIndent(ctx, start, end, indent, indentBuf);
}

static void _ctxLineDedent(Ctx *ctx, size_t line, Str *indentBuf) {
    nvAssertExpr(ctx->indentWidth <= _maxIndentWidth);
    strClear(indentBuf, indentBuf->cap);
    ptrdiff_t start = ctxLineStart(ctx, line);
    ptrdiff_t end;
    size_t currIndent = _ctxIndentInfo(ctx, line, &end);
    size_t indent;
    if (currIndent < ctx->indentWidth) {
        indent = 0;
    } else {
        indent =
            currIndent - (ctx->indentWidth - (currIndent % ctx->indentWidth));
    }
    _ctxLineSetIndent(ctx, start, end, indent, indentBuf);
}

void ctxIndent(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    ptrdiff_t lastLine = -1;
    Str indentBuf = { 0 };

    ctxSelEnd(ctx);

    if (ctx->sels.len == 0) {
        // Indenting may cause some cursors to merge but all lines are still
        // correctly indented because the deleted cursor was on the same line
        for (size_t i = 0; i < ctx->cursors.len; i++) {
            CtxCursor *cur = &ctx->cursors.items[i];
            size_t line;
            ctxPosAt(ctx, cur->idx, &line, NULL);
            if ((ptrdiff_t)line != lastLine) {
                lastLine = line;
                _ctxLineIndent(ctx, line, &indentBuf);
            }
        }
    } else {
        for (size_t i = 0; i < ctx->sels.len; i++) {
            CtxSelection sel = ctx->sels.items[i];
            size_t startLine;
            size_t endLine;
            ctxPosAt(ctx, sel.startIdx, &startLine, NULL);
            ctxPosAt(ctx, sel.endIdx, &endLine, NULL);
            if (lastLine != (ptrdiff_t)startLine) {
                _ctxLineIndent(ctx, startLine, &indentBuf);
            }
            for (size_t line = startLine; line < endLine; line++) {
                _ctxLineIndent(ctx, line + 1, &indentBuf);
            }
            lastLine = endLine;
        }
    }
    strDestroy(&indentBuf);
}

void ctxDedent(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    ptrdiff_t lastLine = -1;
    Str indentBuf = { 0 };
    ctxSelEnd(ctx);

    if (ctx->sels.len == 0) {
        // Indenting may cause some cursors to merge but all lines are still
        // correctly indented because the deleted cursor was on the same line
        for (size_t i = 0; i < ctx->cursors.len; i++) {
            CtxCursor *cur = &ctx->cursors.items[i];
            size_t line;
            ctxPosAt(ctx, cur->idx, &line, NULL);
            if ((ptrdiff_t)line != lastLine) {
                lastLine = line;
                _ctxLineDedent(ctx, line, &indentBuf);
            }
        }
    } else {
        for (size_t i = 0; i < ctx->sels.len; i++) {
            CtxSelection sel = ctx->sels.items[i];
            size_t startLine;
            size_t endLine;
            ctxPosAt(ctx, sel.startIdx, &startLine, NULL);
            ctxPosAt(ctx, sel.endIdx, &endLine, NULL);
            if (lastLine != (ptrdiff_t)startLine) {
                _ctxLineDedent(ctx, startLine, &indentBuf);
            }
            for (size_t line = startLine; line < endLine; line++) {
                _ctxLineDedent(ctx, line + 1, &indentBuf);
            }
            lastLine = endLine;
        }
    }
    strDestroy(&indentBuf);
}

void ctxInsertLineAbove(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    ctxSelEnd(ctx);

    if (ctx->sels.len != 0) {
        ctxInsertCP(ctx, '\n');
        return;
    }
    // Guarantee one cursor per line
    ctxCurMoveToLineStart(ctx);
    Str indentBuf = { 0 };
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        CtxCursor *cur = &ctx->cursors.items[i];
        size_t line;
        ctxPosAt(ctx, cur->idx, &line, NULL);
        size_t indent = _ctxIndentInfo(ctx, line, NULL);
        _ctxReplace(ctx, cur->idx, cur->idx, (Utf8Ch *)"\n", 1);
        cur->idx--;
        _ctxLineSetIndent(ctx, cur->idx, cur->idx, indent, &indentBuf);
    }
    strDestroy(&indentBuf);
}

void ctxInsertLineBelow(Ctx *ctx) {
    ctxAddUndoCheckpoint(ctx);
    ctxSelEnd(ctx);

    if (ctx->sels.len != 0) {
        ctxInsertCP(ctx, '\n');
    } else {
        ctxCurMoveToLineEnd(ctx);
        // ctxInsertCP already adds the proper indent
        ctxInsertCP(ctx, '\n');
    }
}

static bool _ctxEditIsCheckpoint(CtxEdit *edit) {
    return edit->oldLen == 0 && edit->newLen == 0;
}

void ctxAddUndoCheckpoint(Ctx *ctx) {
    ctx->_editCount = 0;
    ctx->_cpPending = true;
}

void ctxUndo(Ctx *ctx) {
    while (ctx->_editIdx > 0) {
        CtxEdit *edit = &ctx->_edits.items[--ctx->_editIdx];
        if (_ctxEditIsCheckpoint(edit)) {
            break;
        }
        _ctxReplace_Raw(
            ctx,
            edit->start,
            edit->start + edit->newLen,
            &ctx->_editsBuf.buf[edit->oldTextIdx],
            edit->oldLen
        );
    }
}

void ctxRedo(Ctx *ctx) {
    if (ctx->_editIdx == ctx->_edits.len) {
        return;
    }
    CtxEdit *edit = &ctx->_edits.items[ctx->_editIdx];
    do {
        _ctxReplace_Raw(
            ctx,
            edit->start,
            edit->start + edit->oldLen,
            &ctx->_editsBuf.buf[edit->newTextIdx],
            edit->newLen
        );
        if (++ctx->_editIdx == ctx->_edits.len) {
            return;
        }
        edit = &ctx->_edits.items[ctx->_editIdx];
    } while (!_ctxEditIsCheckpoint(edit));
}

bool ctxChInSel(const Ctx *ctx, size_t idx) {
    for (size_t i = 0; i < ctx->sels.len; i++) {
        CtxSelection sel = ctx->sels.items[i];
        if (idx >= sel.startIdx && idx < sel.endIdx) {
            return true;
        }
    }

    if (!ctxSelIsActive(ctx)) {
        return false;
    }

    for (size_t i = 0; i < ctx->cursors.len; i++) {
        CtxCursor *cur = &ctx->cursors.items[i];
        CtxSelection sel = ctxCurToSel(ctx, cur);
        if (idx >= sel.startIdx && idx < sel.endIdx) {
            return true;
        }
    }
    return false;
}

size_t ctxLineCount(const Ctx *ctx) {
    size_t line;
    ctxPosAt(ctx, ctx->_buf.len, &line, NULL);
    return line + 1;
}

static size_t _ctxCurAt(const CtxCursors *arr, size_t idx) {
    size_t hi = arr->len;
    size_t lo = 0;
    CtxCursor *cursors = arr->items;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (cursors[mid].idx == idx) {
            return mid;
        } else if (cursors[mid].idx > idx) {
            hi = mid;
        } else {
            lo = mid + 1;
        }

    }

    return lo;
}

static void _ctxCurAddEx(CtxCursors *arr, CtxCursor cursor, bool movingBack) {
    size_t curIdx = _ctxCurAt(arr, cursor.idx);

    if (curIdx >= arr->len) {
        arrAppend(arr, cursor);
    } else if (arr->items[curIdx].idx != cursor.idx) {
        arrInsert(arr, curIdx, cursor);
    }
    if (curIdx >= arr->len) {
        arrAppend(arr, cursor);
    } else if (arr->items[curIdx].idx != cursor.idx) {
        arrInsert(arr, curIdx, cursor);
    } else { // if the cursor is merging with another one
        size_t *prevSelStart = &arr->items[curIdx]._selStart;
        *prevSelStart = movingBack
            ? nvMax(*prevSelStart, cursor._selStart)
            : nvMin(*prevSelStart, cursor._selStart);
    }
}

void ctxCurAdd(Ctx *ctx, size_t idx) {
    size_t col;
    ctxPosAt(ctx, idx, NULL, &col);
    _ctxCurAddEx(&ctx->cursors, (CtxCursor){ idx, col, idx }, false);
}

static void _ctxCurMoveMany(
    Ctx *ctx,
    // Arguments: the context, the cursor to move and an out pointer for the
    // resulting anchor, if not set the anchor is changed automatically.
    // Return the new index of the cursor.
    ptrdiff_t (*moveFunc)(Ctx *, CtxCursor, ptrdiff_t *)
) {
    arrClear(&ctx->_curSwap);

    for (size_t i = 0; i < ctx->cursors.len; i++) {
        CtxCursor oldCur = ctx->cursors.items[i];
        ptrdiff_t col = -1;
        ptrdiff_t newCur = moveFunc(ctx, oldCur, &col);
        if (newCur < 0) {
            newCur = oldCur.idx;
            col = oldCur.anchor;
        }

        if (col < 0) {
            ctxPosAt(ctx, (size_t)newCur, NULL, (size_t *)&col);
        }

        CtxCursor cursor = {
            .idx = newCur,
            .anchor = (size_t)col,
            ._selStart = oldCur._selStart
        };

        _ctxCurAddEx(&ctx->_curSwap, cursor, (size_t)newCur < oldCur.idx);
    }

    CtxCursors temp = ctx->cursors;
    ctx->cursors = ctx->_curSwap;
    ctx->_curSwap = temp;
    arrClear(&ctx->_curSwap);
}

static ptrdiff_t _ctxCurMoveLeftCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return ctxLinePrev(ctx, oldCur.idx, NULL);
}
void ctxCurMoveLeft(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveLeftCB);
}

static ptrdiff_t _ctxCurMoveRightCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    if (oldCur.idx == ctx->_buf.len
        || *_ctxBufGet(&ctx->_buf, oldCur.idx) == '\n'
    ) {
        return -1;
    }
    ptrdiff_t newCur = ctxNext(ctx, oldCur.idx, NULL);
    if (newCur < 0) {
        newCur = ctx->_buf.len;
    }
    return newCur;
}
void ctxCurMoveRight(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveRightCB);
}

static ptrdiff_t _ctxCurMoveUpCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    *outAnchor = oldCur.anchor;
    size_t oldLine;
    ctxPosAt(ctx, oldCur.idx, &oldLine, NULL);
    return ctxIdxAt(ctx, oldLine - 1, oldCur.anchor, NULL);
}
void ctxCurMoveUp(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveUpCB);
}

static ptrdiff_t _ctxCurMoveDownCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    *outAnchor = oldCur.anchor;
    size_t oldLine;
    ctxPosAt(ctx, oldCur.idx, &oldLine, NULL);
    return ctxIdxAt(ctx, oldLine + 1, oldCur.anchor, NULL);
}
void ctxCurMoveDown(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveDownCB);
}

static ptrdiff_t _ctxCurMoveFwdCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    ptrdiff_t newCur = ctxNext(ctx, oldCur.idx, NULL);
    if (newCur < 0) {
        newCur = ctx->_buf.len;
    }
    return newCur;
}
void ctxCurMoveFwd(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveFwdCB);
}

static ptrdiff_t _ctxCurMoveBackCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return ctxPrev(ctx, oldCur.idx, NULL);
}
void ctxCurMoveBack(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveBackCB);
}

static ptrdiff_t _ctxCurMoveToLineStartCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    size_t lineNo;
    ctxPosAt(ctx, oldCur.idx, &lineNo, NULL);
    return ctxLineStart(ctx, lineNo);
}
void ctxCurMoveToLineStart(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToLineStartCB);
}

static ptrdiff_t _ctxCurMoveToLineEndCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    size_t lineNo;
    ctxPosAt(ctx, oldCur.idx, &lineNo, NULL);
    return ctxLineEnd(ctx, lineNo);
}
void ctxCurMoveToLineEnd(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToLineEndCB);
}

static ptrdiff_t _ctxCurMoveToTextStartCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)ctx;
    (void)oldCur;
    (void)outAnchor;
    return 0;
}
void ctxCurMoveToTextStart(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToTextStartCB);
}

static ptrdiff_t _ctxCurMoveToTextEndCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)oldCur;
    (void)outAnchor;
    return ctx->_buf.len;
}
void ctxCurMoveToTextEnd(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToTextEndCB);
}

static ptrdiff_t _ctxCurMoveToNextWordStartCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return _ctxFindNextWordStart(ctx, oldCur.idx);
}
void ctxCurMoveToNextWordStart(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToNextWordStartCB);
}

static ptrdiff_t _ctxCurMoveToNextWordEndCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return _ctxFindNextWordEnd(ctx, oldCur.idx);
}
void ctxCurMoveToNextWordEnd(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToNextWordEndCB);
}

static ptrdiff_t _ctxCurMoveToPrevWordStartCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return _ctxFindPrevWordStart(ctx, oldCur.idx);
}
void ctxCurMoveToPrevWordStart(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToPrevWordStartCB);
}

static ptrdiff_t _ctxCurMoveToPrevWordEndCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    (void)outAnchor;
    return _ctxFindPrevWordEnd(ctx, oldCur.idx);
}
void ctxCurMoveToPrevWordEnd(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToPrevWordEndCB);
}

static size_t _lineCountUpperBound(const Ctx *ctx) {
    if (ctx->_refs.len == 0) {
        return ctx->_buf.len;
    } else {
        CtxRef *ref = &ctx->_refs.items[ctx->_refs.len - 1];
        return ref->line + (ctx->_buf.len - ref->idx);
    }
}

static ptrdiff_t _ctxCurMoveToNextParagraphCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    *outAnchor = oldCur.anchor;

    size_t maxLineNo = _lineCountUpperBound(ctx);
    size_t lineNo;
    ctxPosAt(ctx, oldCur.idx, &lineNo, NULL);
    ptrdiff_t newCur = -1;
    bool skippedBlankLines = false;
    for (;;) {
        ptrdiff_t lineStart = ctxLineStart(ctx, lineNo);
        if (lineStart < 0) {
            break;
        }
        newCur = ctxLineEnd(ctx, lineNo);
        nvAssertExpr(newCur >= 0);
        if (newCur - lineStart != 0) {
            skippedBlankLines = true;
        } else if (skippedBlankLines) {
            break;
        }
        lineNo++;
        // Avoid infinite loop
        if (lineNo > maxLineNo) {
            break;
        }
    }

    nvAssertExpr(newCur >= 0);
    return newCur;
}
void ctxCurMoveToNextParagraph(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToNextParagraphCB);
}

static ptrdiff_t _ctxCurMoveToPrevParagraphCB(
    Ctx *ctx,
    CtxCursor oldCur,
    ptrdiff_t *outAnchor
) {
    *outAnchor = oldCur.anchor;

    size_t lineNo;
    ctxPosAt(ctx, oldCur.idx, &lineNo, NULL);
    ptrdiff_t newCur = -1;
    bool skippedBlankLines = false;
    for (;;) {
        ptrdiff_t lineEnd = ctxLineEnd(ctx, lineNo);
        if (lineEnd < 0) {
            break;
        }
        newCur = ctxLineStart(ctx, lineNo);
        nvAssertExpr(newCur >= 0);
        if (lineEnd - newCur != 0) {
            skippedBlankLines = true;
        } else if (skippedBlankLines) {
            break;
        }
        if (lineNo == 0) {
            break;
        }
        lineNo--;
    }

    nvAssertExpr(newCur >= 0);
    return newCur;
}
void ctxCurMoveToPrevParagraph(Ctx *ctx) {
    _ctxCurMoveMany(ctx, _ctxCurMoveToPrevParagraphCB);
}

static int _selCmp(const void *a, const void *b) {
    const CtxSelection *selA = a;
    const CtxSelection *selB = b;
    ptrdiff_t diff = (ptrdiff_t)selA->startIdx - (ptrdiff_t)selB->startIdx;
    if (diff < 0) {
        return -1;
    } else if (diff > 0) {
        return 1;
    } else {
        return 0;
    }
}

static void _ctxSelJoin(Ctx *ctx) {
    // Append non-empty selections
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        CtxCursor *cursor = &ctx->cursors.items[i];
        CtxSelection sel = ctxCurToSel(ctx, cursor);
        if (sel.startIdx == sel.endIdx) {
            continue;
        }
        arrAppend(&ctx->sels, sel);
    }
    _ctxSelNormalize(ctx);
}

static void _ctxSelNormalize(Ctx *ctx) {
    // Sort the selection based on the starting index
    qsort(ctx->sels.items, ctx->sels.len, sizeof(*ctx->sels.items), _selCmp);

    // Join all overlapping or adjacent selections
    for (size_t i = 1; i < ctx->sels.len; i++) {
        if (ctx->sels.items[i - 1].endIdx < ctx->sels.items[i].startIdx) {
            continue;
        }
        if (ctx->sels.items[i - 1].endIdx < ctx->sels.items[i].endIdx) {
            ctx->sels.items[i - 1].endIdx = ctx->sels.items[i].endIdx;
        }
        arrRemove(&ctx->sels, i);
        i--;
    }
}

CtxSelection ctxCurToSel(const Ctx *ctx, CtxCursor *cur) {
    size_t idx1 = cur->idx;
    size_t idx2 = cur->_selStart;
    if (idx1 > idx2) {
        size_t temp = idx2;
        idx2 = idx1;
        idx1 = temp;
    }

    switch (ctx->selKind) {
    case CtxSelection_None:
        return (CtxSelection) { .startIdx = 0, .endIdx = 0 };
    case CtxSelection_Char:
        return (CtxSelection) { .startIdx = idx1, .endIdx = idx2 };
    case CtxSelection_Line: {
        size_t lineStart, lineEnd;
        ctxPosAt(ctx, idx1, &lineStart, NULL);
        ctxPosAt(ctx, idx2, &lineEnd, NULL);
        return (CtxSelection) {
            .startIdx = ctxLineStart(ctx, lineStart),
            .endIdx = ctxLineEndInc(ctx, lineEnd)
        };
    }
    default:
        nvUnreachable;
    }
}

void ctxSelBegin(Ctx *ctx, bool lineSelection) {
    for (size_t i = 0; i < ctx->cursors.len; i++) {
        ctx->cursors.items[i]._selStart = ctx->cursors.items[i].idx;
    }
    if (lineSelection) {
        ctx->selKind = CtxSelection_Line;
    } else {
        ctx->selKind = CtxSelection_Char;
    }
}

void ctxSelEnd(Ctx *ctx) {
    if (ctxSelIsActive(ctx)) {
        _ctxSelJoin(ctx);
        ctx->selKind = CtxSelection_None;
    }
}

void ctxSelCancel(Ctx *ctx) {
    ctx->selKind = CtxSelection_None;
    arrClear(&ctx->sels);
}

bool ctxSelIsActive(const Ctx *ctx) {
    return ctx->selKind != CtxSelection_None;
}

bool ctxSelHas(const Ctx *ctx) {
    return ctx->selKind != CtxSelection_None || ctx->sels.len > 0;
}

static void _strAppendBufSpan(
    const CtxBuf *buf,
    Str *str,
    size_t start,
    size_t end
) {
    size_t gapIdx = buf->gapIdx;

    if (gapIdx <= start || gapIdx >= end) {
        strAppendRaw(str, _ctxBufGet(buf, start), end - start);
        return;
    }

    strAppendRaw(str, _ctxBufGet(buf, start), gapIdx - start);
    strAppendRaw(str, _ctxBufGet(buf, gapIdx), end - gapIdx);
}

Str *ctxSelText(Ctx *ctx) {
    ctxSelEnd(ctx);

    Str *ret = strNew(0);
    for (size_t i = 0; i < ctx->sels.len; i++) {
        CtxSelection sel = ctx->sels.items[i];
        _strAppendBufSpan(&ctx->_buf, ret, sel.startIdx, sel.endIdx);
        if (i + 1 != ctx->sels.len) {
            strAppendC(ret, "\n");
        }
    }

    return ret;
}
