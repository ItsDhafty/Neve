#include <stdlib.h>

#include "nv_key.h"
#include "nv_utils.h"
#include "unicode/nv_utf.h"

// Read a possible escape sequence from the terminal.
// Return the number of elements written to outBuf.
// If no character is available return 0.
// If there is only a simple character return 1 and the character is in the
// first slot of outBuf.
// Otherwise parse an escape sequence. The first and last filled slots are the
// First and last characters of the escape sequence. Any number in the mmiddle
// Is an already parsed arguent (the numbers are already integers instead of
// being strings of digit characters).
static size_t _readEscapeSeq(int32_t *outBuf, size_t bufSize) {
    nvAssertExpr(bufSize >= 3);
    UcdCP key = _getKey();
    if (key == Key_None) {
        return 0;
    }

    if (key != Key_Escape) {
        outBuf[0] = key;
        return 1;
    }

    key = _getKey();
    if (key == Key_Escape) {
        // ESC ESC is the same as a single ESC
        outBuf[0] = key;
        return 1;
    }
    if (key != '[' && key != 'O') {
        outBuf[0] = Key_Escape;
        g_queuedKey = key;
        return 1;
    }
    // The initial ESC is omitted in the buffer.
    outBuf[0] = key;

    size_t i = 1;
    size_t numIdx = 0;
    char numBuf[6] = { 0 };
    for (;;) {
        key = _getKey();

        // Ignore any incomplete sequences
        if (key == Key_None) {
            return 0;
        }

        // Add digits to numBuf to be parsed later
        if (key >= '0' && key <= '9') {
            if (numIdx < sizeof(numBuf) - 1) {
                numBuf[numIdx++] = (char)key;
            }
            continue;
        }

        // When the digits end parse the number
        numBuf[numIdx] = '\0';
        unsigned long num = strtoul(numBuf, NULL, 10);
        memset(numBuf, 0, sizeof(numBuf));

        if (num == 0) {
            num = 1;
        } else if (num > INT16_MAX) {
            num = INT16_MAX;
        }

        if (i < bufSize) {
            outBuf[i++] = num;
        }

        // Expect more arguments if there is a semicolon
        if (key == ';') {
            continue;
        }

        // Otherwise add the last character of the sequence to outBuf
        if (i < bufSize) {
            outBuf[i++] = key;
        }
        // No early return for sequences that are too long for the buffer,
        // this prevents any extra characters to be interpreted as keystrokes
        // by themselves instead of as being part of an escape sequence.

        break;
    }

    // Ignore sequences too long for the buffer
    return i < bufSize ? i : 0;
}

Key keyGet(void) {
#define _SeqBufLen 32
    int32_t seq[_SeqBufLen] = { 0 };
    size_t seqLen = _readEscapeSeq(seq, _SeqBufLen);
#undef _SeqBufLen

    if (seqLen == 0) {
        return 0;
    } else if (seqLen == 1) {
        return seq[0];
    }

    if (seq[0] == 'O') {
        if (seqLen != 2) {
            return 0;
        }
        switch (seq[1]) {
        case 'A': return Key_Up;
        case 'B': return Key_Down;
        case 'C': return Key_Right;
        case 'D': return Key_Left;
        case 'F': return Key_End;
        case 'H': return Key_Home;
        case 'P': return Key_F1;
        case 'Q': return Key_F2;
        case 'R': return Key_F3;
        case 'S': return Key_F4;
        case 't': return Key_F5;
        case 'u': return Key_F6;
        case 'v': return Key_F7;
        case 'l': return Key_F8;
        case 'w': return Key_F9;
        case 'x': return Key_F10;
        default: return 0;
        }
    }
    nvAssertExpr(seq[0] == '[');
    switch (seq[seqLen - 1]) {
    case 'A': return Key_Up;
    case 'B': return Key_Down;
    case 'C': return Key_Right;
    case 'D': return Key_Left;
    case 'H': return Key_Home;
    case 'F': return Key_End;
    case '~':
        if (seqLen < 2) {
            return 0 ;
        }
        switch (seq[1]) {
            case 1:
            case 7:
                return Key_Home;
            case 3:
                return Key_Delete;
            case 4:
                return Key_End;
            case 11: return Key_F1;
            case 12: return Key_F2;
            case 13: return Key_F3;
            case 14: return Key_F4;
            case 15: return Key_F5;
            case 17: return Key_F6;
            case 18: return Key_F7;
            case 19: return Key_F8;
            case 20: return Key_F9;
            case 21: return Key_F10;
            case 23: return Key_F11;
            case 24: return Key_F12;
            default: return 0;
        }
    default: return 0;
    }
}
