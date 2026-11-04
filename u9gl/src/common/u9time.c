#include <windows.h>
#include "u9time.h"
#include "../injection/config.h"

void u9log_line(const char *fmt, ...);

static volatile DWORD g_offset;
static volatile DWORD g_engineBase;

DWORD u9time_now(void)
{
    return GetTickCount() + g_offset;
}

DWORD u9time_engine(void)
{
    return GetTickCount() - g_engineBase;
}

void u9time_set_engine_base(DWORD base)
{
    g_engineBase = base;
}

void u9time_init(void)
{
    static int done;
    DWORD real = GetTickCount();
    int start;

    if (done) return;
    done = 1;
    start = u9cfg_int("diag_tick_start", 0);
    if (!start) return;
    g_offset = (DWORD)start - real;
    u9log_line("TIME our clock starts at %lu (0x%08lX) by diag_tick_start; the real clock"
               " is at %lu. u9.exe's clock is not affected: fix_clock_start_ms sets that",
               (unsigned long)(real + g_offset), (unsigned long)(real + g_offset),
               (unsigned long)real);
}
