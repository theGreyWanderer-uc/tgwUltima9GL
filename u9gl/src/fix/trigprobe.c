#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "trigprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define OBJ_LINK        0x1Au
#define REC_MAP         0x58u
#define REC_X           0x5Cu
#define PROP_PHASE01    0x3Eu
#define PROP_PHASE23    0x3Bu
#define DATUM_COUNT     0x400u
#define TEXT_LO         0x00401000u
#define TEXT_HI         0x00740000u

#define TRIGGER_IDS     10000u
#define RUNS_LOGGED     25u
#define LINE_CAP        20000u
#define DEPTH_MAX       16
#define WORLD           0x00850228u
#define WORLD_MAP_MGR   0xCCu
#define SHRINE_LINK_LO  0xFFF8u
#define SHRINE_NEAR     2500
#define SHRINE_SCAN     6000
#define SHRINE_SCAN_MS  2000u
#define SHRINES_KEPT    8

typedef uint32_t (U9CALL *ExecFn)(void *sys, uintptr_t obj, uint32_t phase, uint32_t label);
typedef void     (U9CALL *DatumSetFn)(void *store, int index, uint32_t value);
typedef uint8_t  (U9CALL *DatumGetFn)(void *store, int index);
typedef uint32_t (U9CALL *GetPropFn)(void *obj, uint32_t tag);
typedef uint32_t (__attribute__((fastcall)) *ActionFn)(uint32_t action, uintptr_t target);
typedef int      (U9CALL *CollectFn)(void *iter, uintptr_t *out, int max, void *mgr,
                                     const int32_t *pos, int halfExtent, uint32_t link);

static ExecFn     o_exec;
static DatumSetFn o_dset;
static DatumGetFn o_dget;
static ActionFn   o_action;
static int        g_shrineOnly;
static unsigned   g_actions, g_outOfScope;
static int32_t    g_shrine[SHRINES_KEPT][3];
static unsigned   g_shrineMap[SHRINES_KEPT], g_nShrine;
static DWORD      g_lastScan;
static DWORD      g_t0;
static unsigned   g_lines;

static uint32_t   g_runs[TRIGGER_IDS];
static unsigned   g_seq, g_noTrigger, g_overflowIds;
static unsigned   g_writes, g_readsOutside, g_readsInside;

static int        g_depth;
static unsigned   g_stackSeq[DEPTH_MAX];
static unsigned   g_stackId[DEPTH_MAX];
static uint8_t    g_stackScope[DEPTH_MAX];

static void tlog(const char *tag, const char *fmt, ...)
{
    char buf[480];
    va_list ap;

    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) {
            g_lines++;
            u9log_line("TRIGGER line cap reached - counting continues");
        }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("%s %7lu %s", tag, (unsigned long)(u9time_now() - g_t0), buf);
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
    for (i = 0; i < 200 && found < 4 && n + 12 < cap; i++) {
        if (!readable4((uintptr_t)(sp + i))) break;
        if (!after_call(sp[i])) continue;
        n += (size_t)snprintf(out + n, cap - n, " %08X", (unsigned)sp[i]);
        found++;
    }
    if (!found) snprintf(out, cap, " (none found)");
}

static int obj_pos(uintptr_t obj, int *x, int *y, int *z)
{
    uintptr_t page;
    uint32_t xy;

    if (!obj || !readable4(obj + OBJ_FLAGS) || (rd32(obj + OBJ_FLAGS) & 0x200u)) return 0;
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(obj + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + 0x10);
    *z = (int16_t)(rd32(obj + 8) & 0xFFFFu);
    return 1;
}

static void avatar_text(char *out, size_t cap)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!rec || !readable4(rec + REC_X + 8)) { snprintf(out, cap, "?"); return; }
    snprintf(out, cap, "map %d at %d,%d,%d", (int)rd32(rec + REC_MAP), (int)rd32(rec + REC_X),
             (int)rd32(rec + REC_X + 4), (int)(int16_t)(rd32(rec + REC_X + 8) & 0xFFFFu));
}

static unsigned trigger_of(uintptr_t obj, uint32_t phase)
{
    GetPropFn get = (GetPropFn)u9sym(U9SYM_OBJECT_GET_PROPERTY);
    uint32_t v;

    if (!get || !obj || phase > 3 || !readable4(obj + OBJ_LINK)) return 0;
    v = get((void *)obj, phase < 2 ? PROP_PHASE01 : PROP_PHASE23);
    return (phase & 1) ? (v >> 16) : (v & 0xFFFFu);
}

static const uint16_t SHRINE_TYPES[] = {
    2940, 2941, 2942, 2943, 2944, 2945, 2946, 2947,
    4883, 4884, 4885, 4886, 4887, 4888, 4889, 4890,
    4956, 4938, 5472, 5496, 5508, 5468, 5291, 5288,
    0x111E, 0xCA7,
};
static const uint16_t VIRTUE_LINKS[] = { 1034, 1280, 1300, 1310, 1320, 1330, 1340, 1350 };

static int avatar_at(int32_t p[3], unsigned *map)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!rec || !readable4(rec + REC_X + 8)) return 0;
    p[0] = (int32_t)rd32(rec + REC_X); p[1] = (int32_t)rd32(rec + REC_X + 4);
    p[2] = (int16_t)(rd32(rec + REC_X + 8) & 0xFFFFu);
    *map = rd32(rec + REC_MAP);
    return 1;
}

static void shrine_learn(uintptr_t obj, unsigned link)
{
    int32_t p[3], av[3];
    unsigned i, map;
    int x, y, z;
    if (!obj_pos(obj, &x, &y, &z) || !avatar_at(av, &map)) return;
    p[0] = x; p[1] = y; p[2] = z;
    for (i = 0; i < g_nShrine; i++)
        if (g_shrineMap[i] == map && abs(g_shrine[i][0] - x) < 200 && abs(g_shrine[i][1] - y) < 200) return;
    i = g_nShrine < SHRINES_KEPT ? g_nShrine++ : (g_nShrine - 1);
    memcpy(g_shrine[i], p, sizeof p);
    g_shrineMap[i] = map;
    tlog("SHRINE", "shrine (link %04X, type %u) on map %u at %d,%d,%d - triggers within %d of it are logged",
         link, (unsigned)*(const volatile uint16_t *)(obj + OBJ_TYPE), map, x, y, z, SHRINE_NEAR);
}

static void shrine_scan(void)
{
    CollectFn collect = (CollectFn)u9sym(U9SYM_WORLD_COLLECT_BY_LINK);
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0, found[8];
    void *mgr = (world && readable4(world + WORLD_MAP_MGR)) ? (void *)rd32(world + WORLD_MAP_MGR) : NULL;
    uint32_t iter[32];
    int32_t av[3];
    unsigned link, map;
    int n, i;
    DWORD now = u9time_now();
    if (now - g_lastScan < SHRINE_SCAN_MS) return;
    g_lastScan = now;
    if (!collect || !mgr || !avatar_at(av, &map)) return;
    for (link = SHRINE_LINK_LO; link <= 0xFFFFu; link++) {
        memset(iter, 0, sizeof iter);
        n = collect(iter, found, 8, mgr, av, SHRINE_SCAN, link);
        for (i = 0; i < n && found[i]; i++) shrine_learn(found[i], link);
    }
}

static int near_shrine(void)
{
    int32_t av[3];
    unsigned i, map;
    if (!avatar_at(av, &map)) return 0;
    for (i = 0; i < g_nShrine; i++) {
        double dx = av[0] - g_shrine[i][0], dy = av[1] - g_shrine[i][1];
        if (g_shrineMap[i] == map && dx * dx + dy * dy < (double)SHRINE_NEAR * SHRINE_NEAR) return 1;
    }
    return 0;
}

static int in_scope(uintptr_t obj)
{
    unsigned i, link, type;
    if (g_depth > 0 && g_depth <= DEPTH_MAX && g_stackScope[g_depth - 1]) return 1;
    if (obj && readable4(obj + OBJ_FLAGS)) {
        link = (rd32(obj + OBJ_FLAGS) & 1u) ? 0u : *(const volatile uint16_t *)(obj + OBJ_LINK);
        type = *(const volatile uint16_t *)(obj + OBJ_TYPE);
        if (link >= SHRINE_LINK_LO) { shrine_learn(obj, link); return 1; }
        for (i = 0; i < sizeof VIRTUE_LINKS / sizeof VIRTUE_LINKS[0]; i++) if (link == VIRTUE_LINKS[i]) return 1;
        for (i = 0; i < sizeof SHRINE_TYPES / sizeof SHRINE_TYPES[0]; i++) if (type == SHRINE_TYPES[i]) return 1;
    }
    shrine_scan();
    return near_shrine();
}

static uint32_t U9CALL h_exec(void *sys, uintptr_t obj, uint32_t phase, uint32_t label)
{
    unsigned id = trigger_of(obj, phase), seq = 0;
    int scope = g_shrineOnly ? in_scope(obj) : 0;
    uint32_t r;

    if (g_shrineOnly && !scope) {
        g_outOfScope++;
        if (g_depth < DEPTH_MAX) { g_stackSeq[g_depth] = 0; g_stackId[g_depth] = id; g_stackScope[g_depth] = 0; }
        g_depth++;
        r = o_exec(sys, obj, phase, label);
        g_depth--;
        return r;
    }

    if (!id) {
        g_noTrigger++;
    } else {
        seq = ++g_seq;
        if (id < TRIGGER_IDS) g_runs[id]++; else g_overflowIds++;
        if (id >= TRIGGER_IDS || g_runs[id] <= RUNS_LOGGED) {
            char where[48], av[64], chain[64];
            int x, y, z;
            uint32_t flags = rd32(obj + OBJ_FLAGS);
            unsigned link = (flags & 1u) ? 0u : *(const volatile uint16_t *)(obj + OBJ_LINK);

            if (obj_pos(obj, &x, &y, &z)) snprintf(where, sizeof where, "%d,%d,%d", x, y, z);
            else snprintf(where, sizeof where, "(contained)");
            avatar_text(av, sizeof av);
            chain_text((const uint32_t *)__builtin_frame_address(0), chain, sizeof chain);
            tlog("TRIGGER", "#%u depth %d: trigger %u, phase %u label %u, on type %u link %u at %s"
                 " (obj %08X) - Avatar %s - from%s",
                 seq, g_depth, id, (unsigned)phase, (unsigned)label,
                 (unsigned)*(const volatile uint16_t *)(obj + OBJ_TYPE), link, where,
                 (unsigned)obj, av, chain);
            if (id < TRIGGER_IDS && g_runs[id] == RUNS_LOGGED)
                tlog("TRIGGER", "trigger %u has run %u times - further runs are counted only",
                     id, RUNS_LOGGED);
        }
    }

    if (id && id < TRIGGER_IDS && g_runs[id] > RUNS_LOGGED) seq = 0;
    if (g_depth < DEPTH_MAX) { g_stackSeq[g_depth] = seq; g_stackId[g_depth] = id; g_stackScope[g_depth] = (uint8_t)scope; }
    g_depth++;
    r = o_exec(sys, obj, phase, label);
    g_depth--;
    return r;
}

static void running_text(char *out, size_t cap)
{
    int d = g_depth < DEPTH_MAX ? g_depth : DEPTH_MAX;
    while (d-- > 0)
        if (g_stackId[d]) {
            if (g_stackSeq[d]) snprintf(out, cap, "in trigger %u (#%u)", g_stackId[d], g_stackSeq[d]);
            else snprintf(out, cap, "in trigger %u (a counted run)", g_stackId[d]);
            return;
        }
    snprintf(out, cap, "outside a trigger");
}

static void U9CALL h_dset(void *store, int index, uint32_t value)
{
    uintptr_t at = (uintptr_t)store + (uintptr_t)index;
    int old = (index >= 0 && (unsigned)index < DATUM_COUNT && readable4(at & ~(uintptr_t)3u))
              ? *(const volatile uint8_t *)at : -1;
    char running[48];

    g_writes++;
    if (g_shrineOnly && !(g_depth > 0 && g_depth <= DEPTH_MAX && g_stackScope[g_depth - 1]) && (g_depth || !near_shrine())) {
        o_dset(store, index, value);
        return;
    }
    running_text(running, sizeof running);
    tlog("DATUM", "write %d: %d -> %u, from %08X, %s", index, old, (unsigned)(value & 0xFFu),
         (unsigned)(uintptr_t)__builtin_return_address(0), running);
    o_dset(store, index, value);
}

static uint8_t U9CALL h_dget(void *store, int index)
{
    uint8_t v = o_dget(store, index);
    char running[48];

    if (!g_depth) { g_readsOutside++; return v; }
    g_readsInside++;
    if (g_depth <= DEPTH_MAX && g_stackId[g_depth - 1] && !g_stackSeq[g_depth - 1]) return v;
    running_text(running, sizeof running);
    tlog("DATUM", "read %d = %u, %s", index, (unsigned)v, running);
    return v;
}

static uint32_t __attribute__((fastcall)) h_action(uint32_t action, uintptr_t target)
{
    char running[48], where[48];
    int x, y, z, scope = g_depth > 0 && g_depth <= DEPTH_MAX && g_stackScope[g_depth - 1];
    g_actions++;
    if (!g_shrineOnly || scope || action == 0) {
        running_text(running, sizeof running);
        if (target && readable4(target + OBJ_FLAGS)) {
            if (obj_pos(target, &x, &y, &z)) snprintf(where, sizeof where, "%d,%d,%d", x, y, z);
            else snprintf(where, sizeof where, "(contained)");
            tlog("ACTION", "special action %u%s on type %u link %u at %s (obj %08X), %s", (unsigned)action,
                 action == 0 ? " (start a shrine cleansing)" : "",
                 (unsigned)*(const volatile uint16_t *)(target + OBJ_TYPE),
                 (rd32(target + OBJ_FLAGS) & 1u) ? 0u : (unsigned)*(const volatile uint16_t *)(target + OBJ_LINK),
                 where, (unsigned)target, running);
        } else {
            tlog("ACTION", "special action %u, target %08X, %s", (unsigned)action, (unsigned)target, running);
        }
    }
    return o_action(action, target);
}

void u9trig_install(int shrineOnly)
{
    static const uint8_t expExec[7] = { 0x6A, 0xFF, 0x68, 0xA7, 0x0C, 0x73, 0x00 };
    static const uint8_t expSet[6]  = { 0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0 };
    static const uint8_t expGet[6]  = { 0x8B, 0x44, 0x24, 0x04, 0x85, 0xC0 };
    static const U9PatchSpec specExec = {
        "TRIGGER_EXECUTE", U9PATCH_INLINE, U9SYM_TRIGGER_EXECUTE, 7, expExec, 7, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specSet = {
        "TRIGGER_DATUM_SET", U9PATCH_INLINE, U9SYM_SHARED_DATUM_SET, 6, expSet, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specGet = {
        "TRIGGER_DATUM_GET", U9PATCH_INLINE, U9SYM_SHARED_DATUM_GET, 6, expGet, 6, U9PF_EXCLUSIVE
    };
    static const uint8_t expAction[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const U9PatchSpec specAction = {
        "TRIGGER_SPECIAL_ACTION", U9PATCH_INLINE, U9SYM_TRIGGER_SPECIAL_ACTION, 6, expAction, 6, U9PF_EXCLUSIVE
    };

    g_t0 = u9time_now();
    o_exec = (ExecFn)u9patch_install(&specExec, (void *)h_exec);
    o_dset = (DatumSetFn)u9patch_install(&specSet, (void *)h_dset);
    o_dget = (DatumGetFn)u9patch_install(&specGet, (void *)h_dget);
    o_action = (ActionFn)u9patch_install(&specAction, (void *)h_action);
    g_shrineOnly = shrineOnly;
    tlog("TRIGGER", "trigger watch%s: executor %s, datum writes %s, datum reads %s, special actions %s",
         shrineOnly ? " (shrine scope only - diag_shrine_watch)" : "",
         o_exec ? "hooked" : "REFUSED (see the manifest)",
         o_dset ? "hooked" : "REFUSED", o_dget ? "hooked" : "REFUSED", o_action ? "hooked" : "REFUSED");
}

void u9trig_report(void)
{
    unsigned i, k, distinct = 0;

    if (!g_t0) return;
    for (i = 1; i < TRIGGER_IDS; i++) if (g_runs[i]) distinct++;
    u9log_line("TRIGGER watch: %u runs of %u triggers (%u above %u), %u calls with no trigger"
               " for the phase; %u datum writes, %u reads in triggers, %u elsewhere",
               g_seq, distinct, g_overflowIds, TRIGGER_IDS - 1, g_noTrigger,
               g_writes, g_readsInside, g_readsOutside);
    u9log_line("TRIGGER watch: %u special actions; %u shrines learned%s", g_actions, g_nShrine,
               g_shrineOnly ? "; runs outside the shrine scope not logged" : "");

    {
        static uint8_t taken[TRIGGER_IDS];
        memset(taken, 0, sizeof taken);
        for (k = 0; k < 10; k++) {
            unsigned best = 0;
            for (i = 1; i < TRIGGER_IDS; i++)
                if (!taken[i] && g_runs[i] > g_runs[best]) best = i;
            if (!best || !g_runs[best]) break;
            taken[best] = 1;
            u9log_line("TRIGGER   trigger %u ran %u times", best, g_runs[best]);
        }
    }
}
