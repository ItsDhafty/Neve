#ifndef NV_RENDER_H_
#define NV_RENDER_H_

#include "nv_string.h"
#include "nv_utils.h"

// 16-colors color mode
#define screenColModeT16 0
// 256-colors color mode
#define screenColModeT256 1
// 24-bit RGB colors color mode
#define screenColModeRGB 2

#define screenColRGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }
#define screenColT16(col) { .r = (col) }
#define screenColT256(col) { .r = (col) }

#define screenFmtBold ((uint8_t)0x01)
#define screenFmtDim ((uint8_t)0x02)
#define screenFmtItalic ((uint8_t)0x04)
#define screenFmtUnderline ((uint8_t)0x08)
#define screenFmtInverse ((uint8_t)0x10)
#define screenFmtStrike ((uint8_t)0x20)

#define screenStyleNoFg ((uint8_t)0x1)
#define screenStyleNoBg ((uint8_t)0x2)
#define screenStyleNoFmt ((uint8_t)0x4)

// The color of one cell
// The IDs for the TERM16 color mode are the following:
// 0 - default
// 1 - black
// 2 - red
// 3 - green
// 4 - yellow
// 5 - blue
// 6 - magenta
// 7 - cyan
// 8 - white
// 61..68 are the bright variants.
// This is to make the zeroed value the default color.
// Set `col` with the `screenCol*` macros
typedef struct {
    struct {
        uint8_t r, g, b;
    } col;
    uint8_t mode;
} ScreenColor;

// Bitwise or of `screenFmt*` flags.
typedef uint8_t ScreenTextFmt;

// The style of one of the cells of the terminal
typedef struct {
    struct {
        uint8_t r, g, b;
    } fg, bg;
    uint8_t fgMode : 2;
    uint8_t bgMode : 2;
    uint8_t style : 4;
    ScreenTextFmt textFmt;
} ScreenStyle;

// NOTE: when changing ScreenRows check the alignment

typedef struct ScreenRows {
    StrBuf *sBufs;
    Utf8Ch *buffer;
} ScreenRows;

// The painting surface.
typedef struct {
    uint16_t w, h;
    Str buf;
    Str tempBuf;
    ScreenRows *editRows;
    ScreenRows *displayRows;
    ScreenStyle *editStyles;
    ScreenStyle *displayStyles;
    bool resized;
} Screen;

// Initialize the screen.
void screenInit(Screen *screen);
// Destroy the contents of the screen.
void screenDestroy(Screen *screen);

// Resize the screen. If the size changes the screen is cleared.
void screenResize(Screen *screen, uint16_t w, uint16_t h);

// Write a string to the screen.
void screenWrite(
    Screen *screen,
    int16_t x, int16_t y,
    const Utf8Ch *str, size_t len
);

// Write a string to the screen using printf-style formatting.
nvUnixFmt(4, 5) void screenWriteFmt(
    Screen *screen,
    int16_t x, int16_t y,
    nvWinFmt const char *fmt, ...
);

// Set the foreground color.
void screenSetFg(
    Screen *screen,
    ScreenColor fg,
    int16_t x,
    int16_t y,
    uint16_t width
);

// Set the background color.
void screenSetBg(
    Screen *screen,
    ScreenColor bg,
    int16_t x,
    int16_t y,
    uint16_t width
);

// Set the text formatting.
void screenSetTextFmt(
    Screen *screen,
    ScreenTextFmt textFmt,
    int16_t x,
    int16_t y,
    uint16_t width
);

// Set the full style of a region.
void screenSetStyle(
    Screen *screen,
    ScreenStyle style,
    int16_t x,
    int16_t y,
    uint16_t width
);

// Clear a line of the screen.
// If `line == -1` all lines are cleared.
void screenClear(Screen *screen, int32_t line);

// Display any draw calls to the screen.
bool screenRefresh(Screen *screen);

#endif // !NV_RENDER_H_
