#ifndef NV_EDITOR_H_
#define NV_EDITOR_H_

#include "nv_buffer.h"
#include "nv_command.h"
#include "nv_key.h"
#include "nv_screen.h"
#include "nv_tui.h"

#define EditorMaxBindLen 32

typedef enum EditorMode {
    EditorMode_Normal,
    EditorMode_Edit,
    EditorMode_Selection,
    EditorMode_Command
} EditorMode;

// The state of the editor.
typedef struct Editor {
    Screen screen;
    uint64_t lastUpdate;
    BufMap buffers;
    CmdMap commands;
    CmdResult *cmdResult;
    UI ui;

    EditorMode _mode;
    EditorMode _prevMode;
    bool running;

    int32_t keys[EditorMaxBindLen + 1];
    uint8_t keyCount;
    Bind onBindTimeout;
    uint64_t keyTimeoutEnd;

    struct {
        uint64_t keyTimeoutDuration;
    } opt; // options
} Editor;

// Global editor variable.
extern Editor g_ed;

// Initialize the editor.
void editorInit(void);
// Deinitialize the editor.
void editorQuit(void);
// Query the size of the terminal and update the editor accordingly.
bool editorUpdateSize(void);

// Exit the editor if all changes are saved.
bool editorTryExit(void);
// Force exit the editor.
void editorForceExit(void);

// Handle a key press.
void editorHandleKey(Key key);
// Refresh the editor screen.
bool editorRefresh(void);

// Open a file or create a new buffer with the path.
bool editorOpen(const char *path);
// Create a new file.
void editorNewBuf(void);

// Get the current active panel, it may be NULL.
UIBufPanel *editorActivePanel(void);
// Get the current active buffer, it may be NULL.
BufHandle editorActiveBuffer(void);
// Get the current active context, it may be NULL.
Ctx *editorActiveContext(void);
// Set the mode of the editor.
void editorSetMode(EditorMode mode);
// Get the mode of the editor.
EditorMode editorGetMode(void);

#endif // !NV_EDITOR_H_
