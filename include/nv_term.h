#ifndef NV_TERM_H_
#define NV_TERM_H_

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "unicode/nv_utf.h"

/******************** Initialization and deinitialization *********************/

// Initialize the terminal.
bool termInit(void);
// Enable raw mode.
// `getInputTimeoutDSec` sets the timeout for `termGetInput` in tenths of a
// second, set to 0 disables timeout (can cause issues with `termGetKey`).
bool termEnableRawMode(uint8_t getInputTimeoutDSec);
// Deinitialize the terminal, restoring its previous state.
void termQuit(void);
// Check if the terminal is initialized.
bool termIsInit(void);

/*********************************** Input ************************************/

// Get raw input characters, returns a negative value on error.
UcdCP termGetInput(void);
// Read from the terminal.
int64_t termRead(Utf8Ch *buf, size_t bufSize);


/*********************************** Output ***********************************/

// Write to the terminal.
bool termWrite(const void *buf, size_t size);

/************************************ Info ************************************/

// Get the size of the terminal.
bool termSize(uint16_t *outRows, uint16_t *outCols);
// Get the cursor position, (0, 0) is the top left corner.
bool termCursorGetPos(uint16_t *outX, uint16_t *outY);
// Set the cursor position, (0, 0) is the top left corner.
bool termCursorSetPos(uint16_t x, uint16_t y);

#endif // !NV_TERM_H_
