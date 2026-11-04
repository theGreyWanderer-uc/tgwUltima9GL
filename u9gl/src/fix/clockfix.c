#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "clockfix.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

typedef DWORD (WINAPI *ClockFn)(void);
typedef BOOL  (WINAPI *PeekFn)(LPMSG, HWND, UINT, UINT, UINT);
typedef BOOL  (WINAPI *GetMsgFn)(LPMSG, HWND, UINT, UINT);

static volatile DWORD g_base;
static ClockFn  o_tick, o_time;
static PeekFn   o_peek;
static GetMsgFn o_get;

static DWORD WINAPI h_tick(void) { return o_tick() - g_base; }

static DWORD WINAPI h_time(void);
static void check_slots(void);

static DWORD WINAPI h_time(void)
{
    static unsigned n;
    if (g_base && (++n & 0xFFu) == 0) check_slots();
    return o_time() - g_base;
}

static BOOL WINAPI h_peek(LPMSG m, HWND w, UINT lo, UINT hi, UINT rm)
{
    BOOL r = o_peek(m, w, lo, hi, rm);
    if (r && m) m->time -= g_base;
    return r;
}

static BOOL WINAPI h_get(LPMSG m, HWND w, UINT lo, UINT hi)
{
    BOOL r = o_get(m, w, lo, hi);
    if (r != -1 && m) m->time -= g_base;
    return r;
}

typedef struct {
    const char *name, *dll, *api;
    U9Symbol    sym;
    void       *hook;
    void      **orig;
    uint8_t     expect[4];
    FARPROC     real;
} Slot;

static Slot g_slots[4] = {
    { "F13_GETTICKCOUNT", "kernel32.dll", "GetTickCount", U9SYM_IAT_GETTICKCOUNT, (void *)h_tick, (void **)&o_tick, {0}, 0 },
    { "F13_TIMEGETTIME",  "winmm.dll",    "timeGetTime",  U9SYM_IAT_TIMEGETTIME,  (void *)h_time, (void **)&o_time, {0}, 0 },
    { "F13_PEEKMESSAGEA", "user32.dll",   "PeekMessageA", U9SYM_IAT_PEEKMESSAGEA, (void *)h_peek, (void **)&o_peek, {0}, 0 },
    { "F13_GETMESSAGEA",  "user32.dll",   "GetMessageA",  U9SYM_IAT_GETMESSAGEA,  (void *)h_get,  (void **)&o_get,  {0}, 0 },
};

static void check_slots(void)
{
    unsigned i;
    for (i = 0; i < 4; i++) {
        uintptr_t at = u9sym(g_slots[i].sym);
        uint32_t held = *(const volatile uint32_t *)at;
        if (held == (uint32_t)(uintptr_t)g_slots[i].hook) continue;
        g_base = 0;
        u9time_set_engine_base(g_base);
        u9log_line("F-13 clock rebase WITHDRAWN: u9.exe's %s slot now holds %08X, not ours -"
                   " another shim patched it after us. Every clock passes through unchanged,"
                   " so the game sees one base (Windows'), and F-13 can return",
                   g_slots[i].api, (unsigned)held);
        return;
    }
}

void u9clock_install(void)
{
    unsigned i, installed = 0;
    int start = u9cfg_int("fix_clock_start_ms", 60000);
    DWORD tick, time;

    if (!u9cfg_int("fix_clock_rebase", 1)) {
        u9log_line("F-13 clock rebase disabled by config - u9.exe sees Windows'"
                   " clocks, which go wrong after 24.86 days up");
        return;
    }
    if (start < 0) start = 60000;

    for (i = 0; i < 4; i++) {
        Slot *s = &g_slots[i];
        HMODULE m = GetModuleHandleA(s->dll);
        uintptr_t at = u9sym(s->sym);
        uint32_t held;

        s->real = m ? GetProcAddress(m, s->api) : 0;
        if (!s->real || !at) {
            u9log_line("F-13 clock rebase NOT installed: %s!%s not resolved", s->dll, s->api);
            return;
        }
        held = *(const volatile uint32_t *)at;
        if (held != (uint32_t)(uintptr_t)s->real) {
            HMODULE owner = 0;
            char path[MAX_PATH] = "";
            const char *base;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)(uintptr_t)held, &owner) || !owner ||
                owner == GetModuleHandleA(0)) {
                u9log_line("F-13 clock rebase NOT installed: u9.exe's %s slot %08X holds %08X,"
                           " not %s's %08X, and no loaded module owns it", s->api,
                           (unsigned)at, (unsigned)held, s->dll,
                           (unsigned)(uintptr_t)s->real);
                return;
            }
            GetModuleFileNameA(owner, path, sizeof path);
            base = strrchr(path, '\\');
            u9log_line("F-13 clock rebase: u9.exe's %s already goes to %s (%08X) - chained"
                       " through it", s->api, base ? base + 1 : path, (unsigned)held);
        }
        memcpy(s->expect, &held, 4);
        s->real = (FARPROC)(uintptr_t)held;
    }

    tick = ((ClockFn)g_slots[0].real)();
    time = ((ClockFn)g_slots[1].real)();
    g_base = tick - (DWORD)start;
    u9time_set_engine_base(g_base);

    for (i = 0; i < 4; i++) {
        static U9PatchSpec specs[4];
        Slot *s = &g_slots[i];
        void *prev;
        specs[i] = (U9PatchSpec){ s->name, U9PATCH_VTABLE, s->sym, 4, s->expect, 4, U9PF_EXCLUSIVE };
        prev = u9patch_install(&specs[i], s->hook);
        if (!prev) break;
        *s->orig = prev;
        installed++;
    }
    if (installed < 4) {
        g_base = 0;
        u9time_set_engine_base(g_base);
        u9log_line("F-13 clock rebase NOT in effect: %s was refused (see the manifest);"
                   " %u installed slot(s) pass time through unchanged",
                   g_slots[installed].api, installed);
        return;
    }
    u9log_line("F-13 clock rebase: Windows up %lu.%02lu days (GetTickCount %lu, timeGetTime"
               " %lu%s); u9.exe's clocks and message times now start at %d ms",
               (unsigned long)(tick / 86400000u),
               (unsigned long)(tick % 86400000u / 864000u),
               (unsigned long)tick, (unsigned long)time,
               tick >= 0x80000000u ? " - past 2^31, where the fault begins" : "", start);
}
