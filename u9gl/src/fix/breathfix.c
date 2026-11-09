#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "breathfix.h"

void u9log_line(const char *fmt, ...);

static const uint8_t BREATH_EXPECT[6] = { 0x8B, 0x86, 0x80, 0x06, 0x00, 0x00 };
static const U9PatchSpec BREATH_SPEC = {
    "I20_DEEP_BREATH", U9PATCH_STUB, U9SYM_BREATH_DEEP_REMAINDER,
    6, BREATH_EXPECT, 6, U9PF_EXCLUSIVE
};

void u9breathfix_install(void)
{
    uintptr_t site = u9sym(U9SYM_BREATH_DEEP_REMAINDER);
    uintptr_t resume = u9sym(U9SYM_BREATH_DEEP_RESUME);
    uint8_t *st, jmp[6];
    unsigned n = 0;

    if (!site || !resume) return;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("I-20 DEEP BREATH: no memory for the stub - not installed"); return; }

    memcpy(st + n, BREATH_EXPECT, 6); n += 6;
    st[n++] = 0xB9; st[n++] = 0x2C; st[n++] = 0x01; st[n++] = 0x00; st[n++] = 0x00;
    st[n++] = 0x99;
    st[n++] = 0xF7; st[n++] = 0xF9;
    st[n++] = 0x29; st[n++] = 0xD1;
    st[n++] = 0x89; st[n++] = 0x8E; st[n++] = 0x80; st[n++] = 0x06; st[n++] = 0x00; st[n++] = 0x00;
    st[n] = 0xE9;
    *(int32_t *)(st + n + 1) = (int32_t)(resume - (uintptr_t)(st + n + 5));
    FlushInstructionCache(GetCurrentProcess(), st, 64);

    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    jmp[5] = 0x90;
    if (!u9patch_install_bytes(&BREATH_SPEC, jmp, 6)) {
        VirtualFree(st, 0, MEM_RELEASE);
        u9log_line("I-20 DEEP BREATH: REFUSED (see the manifest) - deep water still drains a breath a frame");
        return;
    }
    u9log_line("I-20 DEEP BREATH: installed - in deep water the breath countdown restarts at the rest of its"
               " 300 ms, not at the overshoot (stub %08X)", (unsigned)(uintptr_t)st);
}

#define TRIGGER_SYSTEM  0x00AE7C6Cu
#define ZONE_MAX        16
#define ZONE_FAR        4000

typedef void (U9CALL *BreathSwitchFn)(void *body, int off);
static uintptr_t g_zone[ZONE_MAX];
static int       g_zones, g_zoneMap = -1;
static unsigned  g_kept, g_keptLogged, g_switches;

int u9console_current_map(void);
int u9avatar_position(int *x, int *y, int *z);

static int zone_pos(uintptr_t obj, int *x, int *y)
{
    uintptr_t page;
    uint32_t xy;
    if (!obj || !readable4(obj + 0x14) || (rd32(obj + 0x14) & 0x200u)) return 0;
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(obj + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + 0x10);
    return 1;
}

static void zones_prune(void)
{
    int map = u9console_current_map(), ax, ay, az, i, zx, zy;
    if (map != g_zoneMap) { g_zones = 0; g_zoneMap = map; return; }
    if (!u9avatar_position(&ax, &ay, &az)) return;
    for (i = 0; i < g_zones; ) {
        if (!zone_pos(g_zone[i], &zx, &zy) || abs(zx - ax) > ZONE_FAR || abs(zy - ay) > ZONE_FAR)
            g_zone[i] = g_zone[--g_zones];
        else
            i++;
    }
}

static uintptr_t running_object(void)
{
    uintptr_t sys = readable4(TRIGGER_SYSTEM) ? rd32(TRIGGER_SYSTEM) : 0;
    return (sys && readable4(sys + 8)) ? rd32(sys + 8) : 0;
}

static void U9CALL h_breath_switch(void *body, int off)
{
    uintptr_t zone = running_object();
    BreathSwitchFn fn = (BreathSwitchFn)(*(uintptr_t *)(*(uintptr_t *)body + 0x1E8));
    int i;
    g_switches++;
    zones_prune();
    if (off) {
        for (i = 0; i < g_zones && g_zone[i] != zone; i++) {}
        if (zone && i == g_zones && g_zones < ZONE_MAX) g_zone[g_zones++] = zone;
        fn(body, off);
        return;
    }
    for (i = 0; i < g_zones; i++)
        if (g_zone[i] == zone) { g_zone[i] = g_zone[--g_zones]; break; }
    if (g_zones > 0) {
        g_kept++;
        if (g_keptLogged < 40) {
            g_keptLogged++;
            u9log_line("I-20 BREATH ZONES: left the zone at %08X but still inside %d other - breath tracking stays off",
                       (unsigned)zone, g_zones);
        }
        return;
    }
    fn(body, off);
}

static const uint8_t SWITCH_ON_EXPECT[6]  = { 0xFF, 0x92, 0xE8, 0x01, 0x00, 0x00 };
static const uint8_t SWITCH_OFF_EXPECT[6] = { 0xFF, 0x90, 0xE8, 0x01, 0x00, 0x00 };
static const U9PatchSpec SWITCH_ON_SPEC = {
    "I20_BREATH_SWITCH_ON", U9PATCH_STUB, U9SYM_BREATH_SWITCH_ON_CALL, 6, SWITCH_ON_EXPECT, 6, U9PF_EXCLUSIVE
};
static const U9PatchSpec SWITCH_OFF_SPEC = {
    "I20_BREATH_SWITCH_OFF", U9PATCH_STUB, U9SYM_BREATH_SWITCH_OFF_CALL, 6, SWITCH_OFF_EXPECT, 6, U9PF_EXCLUSIVE
};

static int call_to_us(const U9PatchSpec *spec, U9Symbol sym)
{
    uintptr_t site = u9sym(sym);
    uint8_t call[6];
    if (!site) return 0;
    call[0] = 0xE8;
    *(int32_t *)(call + 1) = (int32_t)((uintptr_t)&h_breath_switch - (site + 5));
    call[5] = 0x90;
    return u9patch_install_bytes(spec, call, 6);
}

void u9breathzones_install(void)
{
    int on = call_to_us(&SWITCH_ON_SPEC, U9SYM_BREATH_SWITCH_ON_CALL);
    int off = on ? call_to_us(&SWITCH_OFF_SPEC, U9SYM_BREATH_SWITCH_OFF_CALL) : 0;
    if (on && off)
        u9log_line("I-20 BREATH ZONES: installed - leaving one breathable space for another keeps breath tracking"
                   " off; tracking switches on only when the Avatar has left them all");
    else
        u9log_line("I-20 BREATH ZONES: REFUSED (%s) - the last zone crossed decides, as stock",
                   on ? "the off call; the on call passes straight through" : "see the manifest");
}

void u9breathzones_report(void)
{
    if (g_switches)
        u9log_line("I-20 BREATH ZONES: %u switches, %u kept off because the Avatar was still inside another zone",
                   g_switches, g_kept);
}
