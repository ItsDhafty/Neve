#include <stdarg.h>
#include <stdlib.h>
#include "clib_threads.h"
#include "nv_logging.h"
#include "nv_file.h"

#ifdef NV_DEBUG
static LogLevel g_minLevel = Log_Debug;
#else
static LogLevel g_minLevel = Log_Warning;
#endif
static File g_logFile = { 0 };
static ThreadMutex g_logMutex = ThreadMutexInitializer;

#ifdef _WIN32

#define DEST_ENV "LOCALAPPDATA"
#define SEP '\\'
#define ALT_SEP '/'
#define FILE_NAME "neve.log"

#else

#define DEST_ENV "HOME"
#define SEP '/'
#define ALT_SEP '\0'
#define FILE_NAME ".neve.log"

#endif

bool logInit(const char *filePath) {
    if (filePath != NULL) {
        return fileOpen(&g_logFile, filePath, FileMode_Write)
            == FileIOResult_Success;
    }

    char buf[1024];
    StrBuf sb = strBufMake(buf, nvArrlen(buf));
#ifdef NV_DEBUG
    const char *dest = ".";
#else
    const char *dest = getenv(DEST_ENV);
    if (dest == NULL) {
        return false;
    }
#endif
    const char sepStr[] = { SEP, '\0' };
    strBufAppendC(&sb, dest);
    if (sb.buf[sb.len - 1] != SEP && sb.buf[sb.len - 1] != ALT_SEP) {
        strBufAppendC(&sb, sepStr);
    }
    strBufAppendC(&sb, FILE_NAME);
    return fileOpen(&g_logFile, sb.buf, FileMode_Write) == FileIOResult_Success;
}

bool logIsInit(void) {
    return g_logFile.fp != NULL;
}

#undef DEST_ENV
#undef SEP
#undef ALT_SEP
#undef FILE_NAME

void logQuit(void) {
    fileClose(&g_logFile);
}

nvUnixFmt(2, 3) void logFmt(LogLevel lvl, nvWinFmt const char *fmt, ...) {
    if (lvl < g_minLevel) return;
    switch (lvl) {
    case Log_Debug:
        fputs("[DEBUG]   ", g_logFile.fp);
        break;
    case Log_Info:
        fputs("[INFO]    ", g_logFile.fp);
        break;
    case Log_Warning:
        fputs("[WARNING] ", g_logFile.fp);
        break;
    case Log_Error:
        fputs("[ERROR]   ", g_logFile.fp);
        break;
    }
    va_list args;
    threadMutexLock(&g_logMutex);
    va_start(args, fmt);
    (void)vfprintf(g_logFile.fp, fmt, args);
    va_end(args);
    putc('\n', g_logFile.fp);
    fflush(g_logFile.fp);
    threadMutexUnlock(&g_logMutex);
}
