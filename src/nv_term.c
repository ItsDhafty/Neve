#include "nv_term.h"

#if (defined(NV_TERM_W) && !defined(NV_TERM_H))                                \
    || (!defined(NV_TERM_W) && defined(NV_TERM_H))
#error Both NV_TERM_W and NV_TERM_H must be defined
#endif

#ifdef _WIN32
#include "win32/nv_term_win32.c"
#else
#include "unix/nv_term_unix.c"
#endif // !_WIN32

bool termIsInit(void) {
    return g_initialized;
}
