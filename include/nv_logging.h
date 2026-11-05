#ifndef NV_LOGGING_H_
#define NV_LOGGING_H_

#include <stdbool.h>
#include "nv_utils.h"

typedef enum LogLevel {
    Log_Debug,
    Log_Info,
    Log_Warning,
    Log_Error
} LogLevel;

bool logInit(const char *filePath);
bool logIsInit(void);
void logQuit(void);

nvUnixFmt(2, 3) void logFmt(LogLevel lvl, nvWinFmt const char *fmt, ...);

#endif // !NV_LOGGING_H_
