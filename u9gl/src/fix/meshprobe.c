#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "meshprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define MESH_TABLE      0x00926DF0u
#define MESH_ENTRY      12u
#define MESH_ENTRIES    8000
#define TEXT_LO         0x00401000u
#define TEXT_HI         0x00740000u

typedef void (U9CALL *HandleSetFn)(void *handle, int app, int arg);
typedef void (U9CALL *LoadQueueFn)(void *loader, uint32_t a, int app, int *b, uint32_t c);
typedef void (U9CALL *LoadNowFn)(void *loader, void *out, int app);

static HandleSetFn o_set;
static LoadQueueFn o_queue;
static LoadNowFn   o_now;

typedef struct {
    const char *name;
    U9Symbol    sym;
    uintptr_t   call;
    unsigned    acquired, reassigned, allCalls;
    unsigned    lastAcquired, lastReassigned;
    int         chainLogged;
} Site;

static Site g_site[] = {
    { .name = "MESHREF_SET_5A5D90",   .sym = U9SYM_MESHREF_SET_5A5D90 },
    { .name = "MESHREF_SET_5762C0",   .sym = U9SYM_MESHREF_SET_5762C0 },
    { .name = "MESHREF_SET_5A5830",   .sym = U9SYM_MESHREF_SET_5A5830 },
    { .name = "MESHREF_QUEUE_57578C", .sym = U9SYM_MESHREF_QUEUE_57578C },
    { .name = "MESHREF_QUEUE_57586A", .sym = U9SYM_MESHREF_QUEUE_57586A },
    { .name = "MESHREF_NOW_4116E9",   .sym = U9SYM_MESHREF_NOW_4116E9 },
    { .name = "MESHREF_NOW_411726",   .sym = U9SYM_MESHREF_NOW_411726 },
};
#define SITES ((int)(sizeof g_site / sizeof g_site[0]))

static int       g_app = -1;
static uintptr_t g_entry;
static DWORD     g_every, g_next, g_t0;
static int       g_on, g_haveLast;
static uint32_t  g_lastCount;
static unsigned  g_lines;

static void mlog(const char *fmt, ...)
{
    char buf[480];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("MESHREF %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static int after_call(uintptr_t ret)
{
    const uint8_t *p = (const uint8_t *)ret;
    if (ret < TEXT_LO + 6 || ret >= TEXT_HI) return 0;
    if (p[-5] == 0xE8) return 1;
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return 1;
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return 1;
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return 1;
    return 0;
}

static void chain_text(const uint32_t *sp, char *out, size_t cap)
{
    size_t n = 0;
    unsigned i, found = 0;
    out[0] = 0;
    for (i = 0; i < 400 && found < 12 && n + 12 < cap; i++) {
        if (!readable4((uintptr_t)(sp + i))) break;
        if (!after_call(sp[i])) continue;
        n += (size_t)snprintf(out + n, cap - n, " %08X", (unsigned)sp[i]);
        found++;
    }
    if (!found) snprintf(out, cap, " (none found)");
}

static Site *site_of(uintptr_t ret)
{
    int i;
    for (i = 0; i < SITES; i++)
        if (g_site[i].call + 5 == ret) return &g_site[i];
    return NULL;
}

static void acquired(Site *s, int app, const uint32_t *frame)
{
    if (!s) return;
    s->allCalls++;
    if (app != g_app) return;
    s->acquired++;
    if (!s->chainLogged) {
        char chain[200];
        s->chainLogged = 1;
        chain_text(frame, chain, sizeof chain);
        mlog("first handle on appearance %d from %s (CALL %08X), called through:%s",
             g_app, s->name, (unsigned)s->call, chain);
    }
}

static void U9CALL h_set(void *handle, int app, int arg)
{
    Site *s = site_of((uintptr_t)__builtin_return_address(0));
    if (s && handle && readable4((uintptr_t)handle) &&
        *(const uint32_t *)handle == g_entry)
        s->reassigned++;
    acquired(s, app, (const uint32_t *)__builtin_frame_address(0));
    o_set(handle, app, arg);
}

static void U9CALL h_queue(void *loader, uint32_t a, int app, int *b, uint32_t c)
{
    acquired(site_of((uintptr_t)__builtin_return_address(0)), app,
             (const uint32_t *)__builtin_frame_address(0));
    o_queue(loader, a, app, b, c);
}

static void U9CALL h_now(void *loader, void *out, int app)
{
    acquired(site_of((uintptr_t)__builtin_return_address(0)), app,
             (const uint32_t *)__builtin_frame_address(0));
    o_now(loader, out, app);
}

static const uint8_t CALL_E8[1] = { 0xE8 };

void u9mesh_install(void)
{
    int i, ok = 0;

    g_app = u9cfg_int("diag_mesh_watch", -1);
    if (g_app < 0) return;
    g_t0 = u9time_now();
    if (g_app >= MESH_ENTRIES) {
        mlog("mesh handle watch: appearance %d is past the table's %d - off",
             g_app, MESH_ENTRIES);
        return;
    }
    g_entry = MESH_TABLE + (uintptr_t)g_app * MESH_ENTRY;
    g_every = (DWORD)u9cfg_int("diag_mesh_watch_ms", 5000);
    if (g_every < 500) g_every = 500;

    for (i = 0; i < SITES; i++) {
        Site *s = &g_site[i];
        U9PatchSpec spec = { s->name, U9PATCH_CALLSITE, s->sym, 5, CALL_E8, 1,
                             U9PF_EXCLUSIVE };
        void *repl = i < 3 ? (void *)h_set : i < 5 ? (void *)h_queue : (void *)h_now;
        void *prev;
        s->call = u9sym(s->sym);
        prev = u9patch_install(&spec, repl);
        if (!prev) { mlog("%s at %08X REFUSED (see the manifest)", s->name, (unsigned)s->call); continue; }
        if (i < 3)      o_set   = (HandleSetFn)prev;
        else if (i < 5) o_queue = (LoadQueueFn)prev;
        else            o_now   = (LoadNowFn)prev;
        ok++;
    }
    g_on = ok > 0;
    g_next = u9time_now();
    mlog("mesh handle watch: appearance %d, entry %08X; %d of %d call sites hooked;"
         " a line every %lu ms when its count or its acquisitions move",
         g_app, (unsigned)g_entry, ok, SITES, (unsigned long)g_every);
}

void u9mesh_tick(void)
{
    DWORD now;
    uint32_t count, mesh, idle;
    unsigned acq = 0, reas = 0, i;
    int delta;
    char per[200];
    size_t n = 0;

    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;
    if (!readable4(g_entry + 8)) return;

    mesh  = rd32(g_entry);
    count = rd32(g_entry + 4);
    idle  = rd32(g_entry + 8);
    per[0] = 0;
    for (i = 0; i < (unsigned)SITES; i++) {
        Site *s = &g_site[i];
        unsigned a = s->acquired - s->lastAcquired, r = s->reassigned - s->lastReassigned;
        s->lastAcquired = s->acquired;
        s->lastReassigned = s->reassigned;
        acq += a; reas += r;
        if (a && n + 24 < sizeof per)
            n += (size_t)snprintf(per + n, sizeof per - n, " %08X:+%u", (unsigned)s->call, a);
    }
    delta = g_haveLast ? (int)(count - g_lastCount) : 0;
    if (g_haveLast && !acq && !reas && !delta) return;
    g_lines++;
    if (!g_haveLast)
        mlog("appearance %d: %u handles, mesh %08X, idle %u (first reading)",
             g_app, (unsigned)count, (unsigned)mesh, (unsigned)idle);
    else
        mlog("appearance %d: %u handles (%+d), mesh %08X, idle %u | acquired %u%s%s,"
             " reassigned away %u -> released elsewhere %d",
             g_app, (unsigned)count, delta, (unsigned)mesh, (unsigned)idle, acq,
             acq ? " at" : "", per, reas, (int)acq - (int)reas - delta);
    g_lastCount = count;
    g_haveLast = 1;
}

void u9mesh_report(void)
{
    int i;
    if (!g_on) return;
    mlog("mesh handle watch, appearance %d: %u lines; the entry holds %u handles at exit",
         g_app, g_lines, readable4(g_entry + 4) ? (unsigned)rd32(g_entry + 4) : 0u);
    for (i = 0; i < SITES; i++)
        mlog("  %s %08X: %u handles on it, %u reassigned away, %u calls for any appearance",
             g_site[i].name, (unsigned)g_site[i].call, g_site[i].acquired,
             g_site[i].reassigned, g_site[i].allCalls);
}
