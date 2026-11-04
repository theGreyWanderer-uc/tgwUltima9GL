#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "travelprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define MAP_OF_MANAGER  0x20u
#define REC_MAP         0x58u
#define REC_X           0x5Cu
#define TEXT_LO         0x00401000u
#define TEXT_HI         0x00740000u

typedef uint32_t (U9CALL *MapLoadFn)(void *self, uint32_t arg);

static MapLoadFn o_load;
static unsigned  g_loads;
static DWORD     g_t0;

static void tlog(const char *fmt, ...)
{
    char buf[480];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("TRAVEL %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static void avatar_text(char *out, size_t cap)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!rec || !readable4(rec + REC_X + 8)) { snprintf(out, cap, "record 0 unreadable"); return; }
    snprintf(out, cap, "map %d at %d,%d,%d", (int)rd32(rec + REC_MAP), (int)rd32(rec + REC_X),
             (int)rd32(rec + REC_X + 4), (int)(int16_t)(rd32(rec + REC_X + 8) & 0xFFFFu));
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

static uint32_t U9CALL h_load(void *self, uint32_t arg)
{
    uintptr_t m = (uintptr_t)self;
    char before[80], after[80], chain[200];
    uint32_t r, mapNow;
    unsigned seq = ++g_loads;

    mapNow = readable4(m + MAP_OF_MANAGER) ? rd32(m + MAP_OF_MANAGER) : 0xFFFFFFFFu;
    avatar_text(before, sizeof before);
    chain_text((const uint32_t *)__builtin_frame_address(0), chain, sizeof chain);
    tlog("#%u Map_Load of map %d (manager %08X, stack argument %08X) - the Avatar's record: %s",
         seq, (int)mapNow, (unsigned)m, (unsigned)arg, before);
    tlog("#%u   called through:%s", seq, chain);

    r = o_load(self, arg);

    avatar_text(after, sizeof after);
    tlog("#%u   returned %u; the Avatar's record now %s", seq, (unsigned)r, after);
    return r;
}

void u9travel_install(void)
{
    static const uint8_t expect[7] = { 0x6A, 0xFF, 0x68, 0x1E, 0xD1, 0x72, 0x00 };
    static const U9PatchSpec spec = {
        "TRAVEL_MAP_LOAD", U9PATCH_INLINE, U9SYM_MAP_LOAD, 7, expect, 7, U9PF_EXCLUSIVE
    };
    g_t0 = u9time_now();
    o_load = (MapLoadFn)u9patch_install(&spec, (void *)h_load);
    tlog("travel watch: Map_Load %s - every region change logged with its caller chain",
         o_load ? "hooked" : "REFUSED (see the manifest)");
}

void u9travel_report(void)
{
    if (g_t0) u9log_line("TRAVEL watch: %u map loads", g_loads);
}
