#include <stdarg.h>
#include <stdlib.h>
#include "nv_logging.h"
#include "nv_file.h"

static File logFile = { 0 };

#ifdef _WIN32

#define DEST_ENV "LOCALAPPDATA"
#define SEP '\\'
#define ALT_SEP '/'
#define FILE_NAME "neve.log"

#else

#define DEST_ENV "HOME"
#define SEP '/'
#define ALT_SEP '\0' // POSIX only supports /
#define FILE_NAME ".neve.log"

#endif

bool logInit(const char *filePath) {
    if (filePath != NULL) {
        return fileOpen(&logFile, filePath, FileMode_Write)
            == FileIOResult_Success;
    }

    char buf[1024];
    StrBuf sb = strBufMake(buf, nvArrlen(buf));
    const char *dest = getenv(DEST_ENV);
    if (dest == NULL) {
        return false;
    }
    const char sepStr[] = { SEP, '\0' };
    strBufAppendC(&sb, dest);
    if (sb.buf[sb.len - 1] != SEP && sb.buf[sb.len - 1] != ALT_SEP) {
        strBufAppendC(&sb, sepStr);
    }
    strBufAppendC(&sb, FILE_NAME);
    return fileOpen(&logFile, sb.buf, FileMode_Write) == FileIOResult_Success;
}

bool logIsInit(void) {
    return logFile.fp != NULL;
}

#undef DEST_ENV
#undef SEP
#undef ALT_SEP
#undef FILE_NAME

void logQuit(void) {
    fileClose(&logFile);
}

nvUnixFmt(2, 3) void logFmt(LogLevel lvl, nvWinFmt const char *fmt, ...) {
    switch (lvl) {
    case Log_Info:
        fputs("[INFO]    ", logFile.fp);
        break;
    case Log_Warning:
        fputs("[WARNING] ", logFile.fp);
        break;
    case Log_Error:
        fputs("[ERROR]   ", logFile.fp);
        break;
    }
    va_list args;
    va_start(args, fmt);
    (void)vfprintf(logFile.fp, fmt, args);
    va_end(args);
    putc('\n', logFile.fp);
    fflush(logFile.fp);
}
