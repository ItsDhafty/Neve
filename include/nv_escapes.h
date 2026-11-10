#ifndef NV_ESCAPES_H_
#define NV_ESCAPES_H_

// Maximum value for numbers in escape sequences
#define escNumMax 32767

// Expand to a string literal and its length comma separated
#define sLen(strLit) (Utf8Ch *)(strLit), sizeof(strLit) - 1

// Clear the screen
#define escScreenClear "\x1b[2J\x1b[H"

// Clear the line the cursor is on
#define escLineClear "\x1b[2K\x1b[G"

#ifdef NV_DISABLE_ALT
#define escEnableAltBuffer ""
#define escDisableAltBuffer ""
#else
// Enable the alternative screen buffer
#define escEnableAltBuffer "\x1b[?1049h"
// Disable the alternative screen buffer
#define escDisableAltBuffer "\x1b[?1049l"
#endif

// Get supported flags for kitty keyboard protocol
#define escKeyGetFlags "\x1b[?u"
// Push flags to enable kitty keyboard protocol
#define escKeyPushFlags(f) "\x1b[>" f "u"
// Pop flags for kitty keyobard protocol
#define escKeyPopFlags "\x1b[<u"

/***************************** Cursor positioning *****************************/

// Move cursor up
#define escCursorUp(n) "\x1b[" n "A"
// Move cursor down
#define escCursorDown(n) "\x1b[" n "B"
// Move cursor right
#define escCursorRight(n) "\x1b[" n "C"
// Move cursor left
#define escCursorLeft(n) "\x1b[" n "D"
// Move cursor to the next line
#define escCursorNextLn(n) "\x1b[" n "E"
// Move cursor to the previous line
#define escCursorPrevLn(n) "\x1b[" n "F"
// Set the X coordinate of the cursor (begins from 1)
#define escCursorSetX(n) "\x1b[" n "G"
// Set the Y coordinate of the cursor (begins from 1)
#define escCursorSetY(n) "\x1b[" n "d"

// Set the position of the cursor
#define escCursorSetPos(x, y) "\x1b[" y ";" x "H"
// Get the cursor position
#define escCursorGetPos "\x1b[6n"

/******************************** Cursor looks ********************************/

// Show the cursor
#define escCursorShow "\x1b[?25h"
// Hide the cursor
#define escCursorHide "\x1b[?25l"
// Start blinking
#define escCursorBlink "\x1b[?12h"
// Stop blinking
#define escCursorStill "\x1b[?12l"

// Set the cursor shape to the user's default
#define escCursorShapeDefault "\x1b[0 q"
// Set the cursor shape to be a blinking block
#define escCursorShapeBlinkBlock "\x1b[1 q"
// Set the cursor shape to be a steady block
#define escCursorShapeStillBlock "\x1b[2 q"
// Set the cursor shape to be a blinking underline
#define escCursorShapeBlinkLine "\x1b[3 q"
// Set the cursor shape to be a steady underline
#define escCursorShapeStillLine "\x1b[4 q"
// Set the cursor shape to be a blinking vertical bar
#define escCursorShapeBlinkBar "\x1b[5 q"
// Set the cursor shape to be a steady vertical bar
#define escCursorShapeStillBar "\x1b[6 q"

/********************************* Text Style *********************************/

// Set the style of the text, multiple styles can be separated by ';'
// Example: `escSetStyle(styleUnderlineOn ";" colorBrightBlueFg)`
#define escSetStyle(c) "\x1b[" c "m"

// Use with `escSetStyle`
#define styleDefault "0"
// Use with `escSetStyle`
#define styleBoldOn "1"
// Use with `escSetStyle`
#define styleBoldOff "22"
// Use with `escSetStyle`
#define styleUnderlineOn "4"
// Use with `escSetStyle`
#define styleUnderlineOff "24"
// Swap foregrand and background colors
// Use with `escSetStyle`
#define colorSwapOn "7"
// Use with `escSetStyle`
#define colorSwapOff "27"
// Use with `escSetStyle`
#define colorBlackFg "30"
// Use with `escSetStyle`
#define colorRedFg "31"
// Use with `escSetStyle`
#define colorGreenFg "32"
// Use with `escSetStyle`
#define colorYellowFg "33"
// Use with `escSetStyle`
#define colorBlueFg "34"
// Use with `escSetStyle`
#define colorMagentaFg "35"
// Use with `escSetStyle`
#define colorCyanFg "36"
// Use with `escSetStyle`
#define colorWhyteFg "37"
// Use with `escSetStyle`
#define colorRGBFg(r, g, b) "38;2;" r ";" g ";" b
// Use with `escSetStyle`
#define colorDefaultFg "39"

// Use with `escSetStyle`
#define colorBlackBg "40"
// Use with `escSetStyle`
#define colorRedBg "41"
// Use with `escSetStyle`
#define colorGreenBg "42"
// Use with `escSetStyle`
#define colorYellowBg "43"
// Use with `escSetStyle`
#define colorBlueBg "44"
// Use with `escSetStyle`
#define colorMagentaBg "45"
// Use with `escSetStyle`
#define colorCyanBg "46"
// Use with `escSetStyle`
#define colorWhyteBg "47"
// Use with `escSetStyle`
#define colorRGBBg(r, g, b) "48;2;" r ";" g ";" b
// Use with `escSetStyle`
#define colorDefaultBg "49"

// Use with `escSetStyle`
#define colorBrightBlackFg "90"
// Use with `escSetStyle`
#define colorBrightRedFg "91"
// Use with `escSetStyle`
#define colorBrightGreenFg "92"
// Use with `escSetStyle`
#define colorBrightYellowFg "93"
// Use with `escSetStyle`
#define colorBrightBlueFg "94"
// Use with `escSetStyle`
#define colorBrightMagentaFg "95"
// Use with `escSetStyle`
#define colorBrightCyanFg "96"
// Use with `escSetStyle`
#define colorBrightWhiteFg "97"

// Use with `escSetStyle`
#define colorBrightBlackBg "100"
// Use with `escSetStyle`
#define colorBrightRedBg "101"
// Use with `escSetStyle`
#define colorBrightGreenBg "102"
// Use with `escSetStyle`
#define colorBrightYellowBg "103"
// Use with `escSetStyle`
#define colorBrightBlueBg "104"
// Use with `escSetStyle`
#define colorBrightMagentaBg "105"
// Use with `escSetStyle`
#define colorBrightCyanBg "106"
// Use with `escSetStyle`
#define colorBrightWhiteBg "107"

#endif // !NV_ESCAPES_H_
