#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "shrinewatch.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define PROC_STAGE      0xD8u
#define PROC_SHRINE     0xDCu
#define PROC_GLYPH      0xE0u
#define PROC_ITEM       0xE4u
#define PROC_MARK       0xECu
#define PROC_PEDESTAL   0xF0u
#define PROC_LINKED     0xF4u
#define PROC_VIRTUE     0x14Cu
#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define OBJ_LINK        0x1Au
#define OBJ_HIDDEN      0x40100u
#define OBJ_CONTAINED   0x200u
#define SHRINE_BITS     0xC000u
#define STAGE_REVEAL    13u
#define STAGE_LAST      15u
#define MAX_PROCS       8
#define OBJTYPES_ADDR      0x00AE7C7Cu
#define WORLD           0x00850228u
#define WORLD_MAP_MGR   0xCCu
#define ACT_SELF        0x18u
#define RUNE_BASE       0xCA7u
#define MAP_REACH       120000
#define MAX_LINES       2000u

typedef void (U9CALL *TickFn)(void *self);
typedef int  (U9CALL *CollectFn)(void *iter, uintptr_t *out, int max, void *mgr,
                                 const int32_t *pos, int halfExtent, uint32_t link);
typedef uintptr_t (U9CALL *ContentFn)(uintptr_t obj);
typedef uintptr_t (U9CALL *TakeOutFn)(uintptr_t item, int a, int32_t x, int32_t y, uint32_t z, int b);
typedef uint32_t *(__attribute__((fastcall)) *ItemCreateFn)(uint32_t *out, int type, void *loc, int flags);
typedef uint32_t (__attribute__((stdcall)) *HandleAllocFn)(uintptr_t obj);
typedef void (U9CALL *PlaceAtFn)(uint32_t *handle, const void *loc, int zero);

static const uint8_t expTick[4] = { 0x00, 0xFF, 0x62, 0x00 };
static const U9PatchSpec specTick = { "SHRINE_CLEANSE_TICK", U9PATCH_VTABLE,
    U9SYM_SHRINE_CLEANSE_TICK_SLOT, 4, expTick, 4, U9PF_EXCLUSIVE };

static TickFn   o_tick;
static int      g_on, g_fix;
static unsigned g_lines, g_seen, g_ended, g_nulls, g_found, g_pulled, g_made, g_failed, g_homed;
static struct { void *self; unsigned stage, bits; } g_proc[MAX_PROCS];

#define TRACK_MAX      48
#define TRACK_STEP     3
#define TRACK_LINES    8000u
#define TRACK_LINKS    9
#define TRACK_REACH    3000
#define PROC_ANIM       0x12Cu
#define PROC_WALK_LINK  0x150u
#define PROC_BOLTS      0x154u
#define PROC_BOLT_COUNT 0x604u
#define BOLTS_MAX       100
#define CLEANSE_FADE    0x01320754u
#define WALK_MARKER     1134u
#define WALK_REACH      0x500
static struct {
    void     *self;
    DWORD     t0;
    unsigned  n, lines, anim, bolts;
    int       walk[3], hasWalk;
    struct { uintptr_t o; uint16_t type, link; char role[8]; int p[3], has; } obj[TRACK_MAX];
} g_trk;

static const char *const VIRTUE[9] = { "?", "valor", "sacrifice", "compassion", "justice",
                                       "honesty", "honor", "spirituality", "humility" };
static const struct { uint16_t link, rune, item; } VIRTUE_OBJ[9] = {
    { 0, 0, 0 },
    { 1330, 2947, 5288 }, { 1310, 2945, 5468 }, { 1034, 2940, 4956 }, { 1340, 2944, 5508 },
    { 1280, 2941, 4938 }, { 1300, 2942, 5472 }, { 1350, 2946, 5291 }, { 1320, 2943, 5496 },
};

static const struct { int16_t rune[2], item[2]; } HOME[9] = {
    { { 0, 0 }, { 0, 0 } },
    { { -29, -10 }, { 14, -14 } },
    { { 9, -29 }, { 14, 24 } },
    { { 11, 31 }, { -16, -10 } },
    { { -2, -34 }, { 3, 24 } },
    { { -6, -24 }, { 7, 26 } },
    { { 30, 2 }, { -25, 11 } },
    { { 21, -2 }, { -23, 32 } },
    { { -30, 8 }, { 10, -21 } },
};
#define HOME_Z_RUNE   93
#define HOME_Z_ITEM   78
#define ALTAR_MARGIN  65
#define ALTAR_Z_BELOW 400
#define ALTAR_Z_ABOVE 1200
static const struct { uint8_t virtue; int32_t x, y; } DECOY[] = {
    { 5, 80476, 22516 }, { 3, 80467, 22556 }, { 6, 80520, 22524 }, { 2, 80513, 22477 },
    { 7, 80501, 22450 }, { 1, 80431, 22505 }, { 8, 80433, 22457 }, { 4, 80432, 22481 },
    { 7, 2807, 4127 },
};
#define DECOY_REACH   64

static uintptr_t handle_object(uint32_t handle)
{
    uintptr_t tbl, slot;
    if (!handle || !readable4(ENTITY_TABLE)) return 0;
    tbl = rd32(ENTITY_TABLE);
    if (!tbl) return 0;
    slot = tbl + (uintptr_t)handle * 0x0Cu + 8;
    if (!readable4(slot)) return 0;
    slot = rd32(slot);
    return (slot && readable4(slot + OBJ_FLAGS)) ? slot : 0;
}

static int obj_where(uintptr_t o, int p[3])
{
    uintptr_t page = o & ~(uintptr_t)0xFFFu;
    uint32_t xy;
    if ((rd32(o + OBJ_FLAGS) & OBJ_CONTAINED) || !readable4(page + 0x10)) return 0;
    xy = rd32(o + 4);
    p[0] = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    p[1] = (int16_t)(xy >> 16) + (int32_t)rd32(page + 0x10);
    p[2] = (int16_t)(rd32(o + 8) & 0xFFFFu);
    return 1;
}

#define line(...) do { if (g_lines < MAX_LINES) { g_lines++; u9log_line(__VA_ARGS__); } } while (0)

static int describe(void *self, unsigned off, const char *role)
{
    uint32_t  h = rd32((uintptr_t)self + off);
    uintptr_t o = handle_object(h);
    unsigned  f;
    int       p[3];
    if (!o) {
        line("SHRINE %08X   +%02X %-8s handle %u -> NOTHING", (unsigned)self, off, role, (unsigned)h);
        return 0;
    }
    f = rd32(o + OBJ_FLAGS);
    if (obj_where(o, p))
        line("SHRINE %08X   +%02X %-8s handle %u type %u link %u flags %08X%s at %d,%d,%d",
             (unsigned)self, off, role, (unsigned)h, *(const uint16_t *)(o + OBJ_TYPE),
             *(const uint16_t *)(o + OBJ_LINK), f, (f & OBJ_HIDDEN) == OBJ_HIDDEN ? " hidden" : "",
             p[0], p[1], p[2]);
    else
        line("SHRINE %08X   +%02X %-8s handle %u type %u link %u flags %08X%s contained",
             (unsigned)self, off, role, (unsigned)h, *(const uint16_t *)(o + OBJ_TYPE),
             *(const uint16_t *)(o + OBJ_LINK), f, (f & OBJ_HIDDEN) == OBJ_HIDDEN ? " hidden" : "");
    return 1;
}

static void roster(void *self, const char *why)
{
    unsigned v = rd32((uintptr_t)self + PROC_VIRTUE);
    int pedestal, linked;
    line("SHRINE %08X %s: %s, stage %d", (unsigned)self, why, VIRTUE[v <= 8 ? v : 0],
         rd32((uintptr_t)self + PROC_STAGE));
    describe(self, PROC_SHRINE, "shrine");
    describe(self, PROC_GLYPH, "glyph");
    describe(self, PROC_ITEM, "item");
    describe(self, PROC_MARK, "0x111E");
    pedestal = describe(self, PROC_PEDESTAL, "0xCA7");
    linked   = describe(self, PROC_LINKED, "linked");
    if (!pedestal || !linked) {
        g_nulls++;
        line("SHRINE %08X WARNING: %s resolve%s to nothing - stage 13 reveals it unchecked and"
             " faults at 0x004BEB00 (F-18)", (unsigned)self,
             !pedestal && !linked ? "+F0 and +F4" : !pedestal ? "+F0" : "+F4",
             !pedestal && !linked ? "" : "s");
    }
}

static unsigned base_type(uintptr_t o)
{
    uintptr_t tbl = readable4(OBJTYPES_ADDR) ? rd32(OBJTYPES_ADDR) : 0;
    unsigned t = *(const volatile uint16_t *)(o + OBJ_TYPE);
    return (tbl && readable4(tbl + t * 16u + 4)) ? *(const volatile uint16_t *)(tbl + t * 16u + 4) : t;
}

static int is_wanted(uintptr_t o, unsigned base, unsigned link)
{
    if (!o || !readable4(o + OBJ_LINK) || (rd32(o + OBJ_FLAGS) & 1u)) return 0;
    return *(const volatile uint16_t *)(o + OBJ_LINK) == link && base_type(o) == base;
}

static uintptr_t avatar_object(void)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t act = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    return (act && readable4(act + ACT_SELF)) ? handle_object(rd32(act + ACT_SELF)) : 0;
}

static uintptr_t find_inside(uintptr_t box, unsigned base, unsigned link, int depth, int *budget)
{
    ContentFn first = (ContentFn)u9sym(U9SYM_OBJECT_FIRST_CONTENT);
    ContentFn next  = (ContentFn)u9sym(U9SYM_OBJECT_NEXT_CONTENT);
    uintptr_t it, got;
    if (!first || !next || depth > 3) return 0;
    for (it = first(box); it && (*budget)-- > 0; it = next(it)) {
        if (!readable4(it + OBJ_FLAGS)) break;
        if (is_wanted(it, base, link)) return it;
        if ((got = find_inside(it, base, link, depth + 1, budget)) != 0) return got;
    }
    return 0;
}

static void home_spot(unsigned v, int rune, const int sp[3], int32_t at[3])
{
    const int16_t *d = rune ? HOME[v].rune : HOME[v].item;
    at[0] = sp[0] + d[0]; at[1] = sp[1] + d[1]; at[2] = sp[2] + (rune ? HOME_Z_RUNE : HOME_Z_ITEM);
}

static int on_altar(unsigned v, const int sp[3], const int p[3])
{
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0, k, dx = p[0] - sp[0], dy = p[1] - sp[1], dz = p[2] - sp[2];
    for (k = 0; k < 2; k++) {
        const int16_t *d = k ? HOME[v].item : HOME[v].rune;
        if (d[0] < x0) x0 = d[0];
        if (d[0] > x1) x1 = d[0];
        if (d[1] < y0) y0 = d[1];
        if (d[1] > y1) y1 = d[1];
    }
    return dx >= x0 - ALTAR_MARGIN && dx <= x1 + ALTAR_MARGIN && dy >= y0 - ALTAR_MARGIN &&
           dy <= y1 + ALTAR_MARGIN && dz >= -ALTAR_Z_BELOW && dz <= ALTAR_Z_ABOVE;
}

static int is_decoy(unsigned v, const int p[3])
{
    unsigned i;
    for (i = 0; i < sizeof DECOY / sizeof DECOY[0]; i++)
        if (DECOY[i].virtue == v && p[0] - DECOY[i].x <= DECOY_REACH && DECOY[i].x - p[0] <= DECOY_REACH &&
            p[1] - DECOY[i].y <= DECOY_REACH && DECOY[i].y - p[1] <= DECOY_REACH)
            return 1;
    return 0;
}

static void repair_slot(void *self, unsigned off, uintptr_t shrine, unsigned v)
{
    int rune = off == PROC_PEDESTAL, sp[3], budget = 2000, n, i, decoys = 0;
    unsigned link = VIRTUE_OBJ[v].link, base = rune ? RUNE_BASE : VIRTUE_OBJ[v].item;
    unsigned type = rune ? VIRTUE_OBJ[v].rune : VIRTUE_OBJ[v].item;
    const char *what = rune ? "rune" : "item";
    HandleAllocFn alloc = (HandleAllocFn)u9sym(U9SYM_HANDLE_ALLOC);
    CollectFn collect = (CollectFn)u9sym(U9SYM_WORLD_COLLECT_BY_LINK);
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0, objs[256], o = 0, av;
    void *mgr = (world && readable4(world + WORLD_MAP_MGR)) ? (void *)rd32(world + WORLD_MAP_MGR) : NULL;
    uint32_t iter[32], h = 0;
    int32_t at[3];
    struct { int32_t x, y; int16_t z, pad; } loc;

    if (!alloc || !obj_where(shrine, sp)) { g_failed++; return; }
    home_spot(v, rune, sp, at);
    memset(&loc, 0, sizeof loc);
    loc.x = at[0]; loc.y = at[1]; loc.z = (int16_t)at[2];

    if (collect && mgr) {
        memset(iter, 0, sizeof iter);
        n = collect(iter, objs, 256, mgr, sp, MAP_REACH, link);
        for (i = 0; i < n && objs[i] && !o; i++) {
            int p[3];
            if (!is_wanted(objs[i], base, link) || !obj_where(objs[i], p)) continue;
            if (is_decoy(v, p)) { decoys++; continue; }
            o = objs[i];
        }
        if (decoys)
            u9log_line("SHRINE fix F-18: %s's %s - %d copy at a decoy's place (Lord British's display or "
                       "map 22's second ankh) left alone", VIRTUE[v], what, decoys);
        if (o) {
            int p[3] = { 0, 0, 0 }, q[3] = { 0, 0, 0 };
            unsigned t = *(const uint16_t *)(o + OBJ_TYPE);
            obj_where(o, p);
            h = alloc(o);
            ((PlaceAtFn)u9sym(U9SYM_WORLDOBJ_PLACE_AT))(&h, &loc, 0);
            if (handle_object(h)) obj_where(handle_object(h), q);
            g_found++;
            u9log_line("SHRINE fix F-18: %s's %s (type %u, link %u) found on the map at %d,%d,%d - moved to "
                       "the shrine, now at %d,%d,%d", VIRTUE[v], what, t, link, p[0], p[1], p[2], q[0], q[1], q[2]);
        }
    }
    if (!h && (av = avatar_object()) != 0 && (o = find_inside(av, base, link, 0, &budget)) != 0) {
        unsigned t = *(const uint16_t *)(o + OBJ_TYPE);
        uintptr_t moved = ((TakeOutFn)u9sym(U9SYM_ITEM_TAKE_OUT_TO_WORLD))(o, 1, at[0], at[1],
                                                                         (uint32_t)(uint16_t)at[2], 1);
        if (moved && readable4(moved + OBJ_FLAGS)) {
            h = alloc(moved);
            g_pulled++;
            u9log_line("SHRINE fix F-18: %s's %s (type %u, link %u) was in the Avatar's pack - taken out "
                       "to the shrine at %d,%d,%d", VIRTUE[v], what, t, link, at[0], at[1], at[2]);
        }
    }
    if (!h) {
        ((ItemCreateFn)u9sym(U9SYM_ITEM_CREATE))(&h, (int)type, &loc, 0);
        if (h) {
            g_made++;
            u9log_line("SHRINE fix F-18: %s's %s nowhere to be found - a new one (type %u) made at the "
                       "shrine, %d,%d,%d", VIRTUE[v], what, type, at[0], at[1], at[2]);
        }
    }
    if (h && handle_object(h)) {
        *(volatile uint32_t *)((uintptr_t)self + off) = h;
    } else {
        g_failed++;
        u9log_line("SHRINE fix F-18: %s's %s could not be found or made - stage 13 will fault", VIRTUE[v], what);
    }
}

static void home_slot(void *self, unsigned off, uintptr_t shrine, unsigned v)
{
    int rune = off == PROC_PEDESTAL, sp[3], p[3], q[3] = { 0, 0, 0 };
    uint32_t *slot = (uint32_t *)((uintptr_t)self + off);
    uintptr_t o = handle_object(*slot);
    const char *what = rune ? "rune" : "item";
    int32_t at[3];
    struct { int32_t x, y; int16_t z, pad; } loc;

    if (!o || !obj_where(shrine, sp)) return;
    home_spot(v, rune, sp, at);
    memset(&loc, 0, sizeof loc);
    loc.x = at[0]; loc.y = at[1]; loc.z = (int16_t)at[2];
    if (!obj_where(o, p)) {
        uintptr_t moved = ((TakeOutFn)u9sym(U9SYM_ITEM_TAKE_OUT_TO_WORLD))(o, 1, loc.x, loc.y,
                                                                         (uint32_t)(uint16_t)loc.z, 1);
        if (!moved || !readable4(moved + OBJ_FLAGS)) return;
        if (handle_object(*slot) != moved) *slot = ((HandleAllocFn)u9sym(U9SYM_HANDLE_ALLOC))(moved);
        g_homed++;
        u9log_line("SHRINE fix F-18: %s's %s was in a container - taken out to the shrine at %d,%d,%d",
                   VIRTUE[v], what, loc.x, loc.y, loc.z);
        return;
    }
    if (on_altar(v, sp, p)) return;
    ((PlaceAtFn)u9sym(U9SYM_WORLDOBJ_PLACE_AT))(slot, &loc, 0);
    if (handle_object(*slot)) obj_where(handle_object(*slot), q);
    g_homed++;
    u9log_line("SHRINE fix F-18: %s's %s was away from the altar at %d,%d,%d - moved to the shrine, now at %d,%d,%d",
               VIRTUE[v], what, p[0], p[1], p[2], q[0], q[1], q[2]);
}

static void trk_note(int stage, uintptr_t o, const char *role, int force)
{
    int p[3], i, k = -1;
    uint16_t type, link;
    DWORD t = u9time_now() - g_trk.t0;
    if (!o || !obj_where(o, p)) return;
    type = *(const volatile uint16_t *)(o + OBJ_TYPE);
    link = *(const volatile uint16_t *)(o + OBJ_LINK);
    for (i = 0; i < (int)g_trk.n; i++)
        if (!strcmp(g_trk.obj[i].role, role) && g_trk.obj[i].type == type && g_trk.obj[i].link == link &&
            (strcmp(role, "link") || g_trk.obj[i].o == o)) { k = i; break; }
    if (k < 0) {
        if (g_trk.n >= TRACK_MAX) return;
        k = (int)g_trk.n++;
        g_trk.obj[k].type = type; g_trk.obj[k].link = link; g_trk.obj[k].has = 0;
        lstrcpynA(g_trk.obj[k].role, role, sizeof g_trk.obj[k].role);
    }
    g_trk.obj[k].o = o;
    if (g_trk.obj[k].has && !force && abs(p[0] - g_trk.obj[k].p[0]) < TRACK_STEP &&
        abs(p[1] - g_trk.obj[k].p[1]) < TRACK_STEP && abs(p[2] - g_trk.obj[k].p[2]) < TRACK_STEP)
        return;
    if (g_trk.lines < TRACK_LINES) {
        g_trk.lines++;
        if (g_trk.obj[k].has)
            u9log_line("SHRINE TRACK %6lu ms stage %2d %-6s type %4u link %5u at %d,%d,%d (moved %+d,%+d,%+d)",
                       (unsigned long)t, stage, role, type, link, p[0], p[1], p[2],
                       p[0] - g_trk.obj[k].p[0], p[1] - g_trk.obj[k].p[1], p[2] - g_trk.obj[k].p[2]);
        else
            u9log_line("SHRINE TRACK %6lu ms stage %2d %-6s type %4u link %5u at %d,%d,%d (first)",
                       (unsigned long)t, stage, role, type, link, p[0], p[1], p[2]);
        if (g_trk.lines == TRACK_LINES) u9log_line("SHRINE TRACK line budget spent - the rest is not logged");
    }
    memcpy(g_trk.obj[k].p, p, sizeof p);
    g_trk.obj[k].has = 1;
}

static const char *anim_name(unsigned a)
{
    static char buf[4][24];
    static int k;
    switch (a) {
    case 0x1D1: return "0x1D1 (stage 3, kneel)";
    case 0x1D0: return "0x1D0 (stage 5, kneeling)";
    case 0x1D2: return "0x1D2 (stage 6 or 10, stand)";
    case 0x1A7: return "0x1A7 (stage 11, lifted)";
    case 0xFFFFFFFFu: return "none";
    }
    k = (k + 1) & 3;
    wsprintfA(buf[k], "0x%X", a);
    return buf[k];
}

static void walk_target(void *self, uintptr_t shrine)
{
    CollectFn collect = (CollectFn)u9sym(U9SYM_WORLD_COLLECT_BY_LINK);
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0, objs[64];
    void *mgr = (world && readable4(world + WORLD_MAP_MGR)) ? (void *)rd32(world + WORLD_MAP_MGR) : NULL;
    unsigned link = rd32((uintptr_t)self + PROC_WALK_LINK);
    uint32_t iter[32];
    int sp[3], n, j;
    if (!collect || !mgr || !shrine || !obj_where(shrine, sp)) return;
    memset(iter, 0, sizeof iter);
    n = collect(iter, objs, 64, mgr, sp, WALK_REACH, link);
    for (j = 0; j < n && objs[j]; j++)
        if (readable4(objs[j] + OBJ_FLAGS) && *(const volatile uint16_t *)(objs[j] + OBJ_TYPE) == WALK_MARKER &&
            obj_where(objs[j], g_trk.walk)) {
            g_trk.hasWalk = 1;
            line("SHRINE %08X walk target: the type %u marker on link %u at %d,%d,%d (%+d,%+d from the shrine)",
                 (unsigned)self, WALK_MARKER, link, g_trk.walk[0], g_trk.walk[1], g_trk.walk[2],
                 g_trk.walk[0] - sp[0], g_trk.walk[1] - sp[1]);
            return;
        }
    line("SHRINE %08X walk target: no type %u marker on link %u within %d - no walk (stage 0 goes to 2)",
         (unsigned)self, WALK_MARKER, link, WALK_REACH);
}

static void bolts(void *self, int stage)
{
    unsigned n = rd32((uintptr_t)self + PROC_BOLT_COUNT), i;
    if (n == g_trk.bolts) return;
    line("SHRINE %08X bolts queued %u -> %u at stage %d, %lu ms", (unsigned)self, g_trk.bolts, n, stage,
         (unsigned long)(u9time_now() - g_trk.t0));
    g_trk.bolts = n;
    for (i = 0; i < n && i < BOLTS_MAX && i < 12; i++) {
        uintptr_t e = (uintptr_t)self + PROC_BOLTS + i * 12u;
        uint32_t a = rd32(e), b = rd32(e + 4);
        int pa[3] = { 0, 0, 0 }, pb[3] = { 0, 0, 0 };
        uintptr_t oa = a != 0xFFFFFFFFu ? handle_object(a) : 0, ob = handle_object(b);
        if (oa) obj_where(oa, pa);
        if (ob) obj_where(ob, pb);
        line("SHRINE %08X   bolt %u: %d ms left, from handle %u (type %u) at %d,%d,%d to %s", (unsigned)self, i,
             (int)rd32(e + 8), (unsigned)b, ob ? *(const uint16_t *)(ob + OBJ_TYPE) : 0u, pb[0], pb[1], pb[2],
             a == 0xFFFFFFFFu ? "1000 straight up" : oa ? "the other object" : "nothing");
        if (oa)
            line("SHRINE %08X            the other: handle %u (type %u) at %d,%d,%d", (unsigned)self, (unsigned)a,
                 *(const uint16_t *)(oa + OBJ_TYPE), pa[0], pa[1], pa[2]);
    }
}

static void track(void *self, int stage, int stageChanged, uintptr_t shrine)
{
    static const struct { unsigned off; const char *role; } SLOT[] = {
        { PROC_GLYPH, "glyph" }, { PROC_ITEM, "item" }, { PROC_MARK, "+EC" },
        { PROC_PEDESTAL, "+F0" }, { PROC_LINKED, "+F4" },
    };
    unsigned v = rd32((uintptr_t)self + PROC_VIRTUE), i, l;
    CollectFn collect = (CollectFn)u9sym(U9SYM_WORLD_COLLECT_BY_LINK);
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0, objs[64];
    void *mgr = (world && readable4(world + WORLD_MAP_MGR)) ? (void *)rd32(world + WORLD_MAP_MGR) : NULL;
    int sp[3], n, j;

    if (g_trk.self != self) {
        memset(&g_trk, 0, sizeof g_trk);
        g_trk.self = self;
        g_trk.t0 = u9time_now();
        g_trk.anim = rd32((uintptr_t)self + PROC_ANIM);
        line("SHRINE %08X track: the Avatar, the process's objects and everything on links %u..%u within %d"
             " of the shrine, a TRACK line per %d units moved; times from now", (unsigned)self,
             v <= 8 ? VIRTUE_OBJ[v].link : 0, v <= 8 ? VIRTUE_OBJ[v].link + TRACK_LINKS - 1 : 0, TRACK_REACH,
             TRACK_STEP);
        walk_target(self, shrine);
    }
    trk_note(stage, avatar_object(), "avatar", stageChanged);
    if (stageChanged && stage == 2 && g_trk.hasWalk) {
        int p[3];
        uintptr_t av = avatar_object();
        if (av && obj_where(av, p))
            line("SHRINE %08X walk ended at %d,%d,%d - %d units from the marker (%+d,%+d)", (unsigned)self,
                 p[0], p[1], p[2], (int)sqrt((double)(p[0] - g_trk.walk[0]) * (p[0] - g_trk.walk[0]) +
                                             (double)(p[1] - g_trk.walk[1]) * (p[1] - g_trk.walk[1])),
                 p[0] - g_trk.walk[0], p[1] - g_trk.walk[1]);
    }
    {
        unsigned a = rd32((uintptr_t)self + PROC_ANIM);
        if (a != g_trk.anim) {
            line("SHRINE %08X anim %s -> %s at stage %d, %lu ms", (unsigned)self, anim_name(g_trk.anim),
                 anim_name(a), stage, (unsigned long)(u9time_now() - g_trk.t0));
            g_trk.anim = a;
        }
    }
    bolts(self, stage);
    if (stageChanged && readable4(CLEANSE_FADE)) {
        float f = *(const volatile float *)CLEANSE_FADE;
        line("SHRINE %08X stage %d: DAT_01320754 = %.3f", (unsigned)self, stage, (double)f);
    }
    for (i = 0; i < sizeof SLOT / sizeof SLOT[0]; i++)
        trk_note(stage, handle_object(rd32((uintptr_t)self + SLOT[i].off)), SLOT[i].role, 0);
    if (!collect || !mgr || v < 1 || v > 8 || !shrine || !obj_where(shrine, sp)) return;
    for (l = 0; l < TRACK_LINKS; l++) {
        uint32_t iter[32];
        memset(iter, 0, sizeof iter);
        n = collect(iter, objs, 64, mgr, sp, TRACK_REACH, VIRTUE_OBJ[v].link + l);
        for (j = 0; j < n && objs[j]; j++)
            if (readable4(objs[j] + OBJ_FLAGS)) trk_note(stage, objs[j], "link", 0);
    }
}

#define STAGE_RISE      11u
#define BEAM_STALE_MS   3000u
#define STAGE_GLOW      12u
#define STRIKE_AFTER_12 7500u
#define PEAK_RISE_MS    250u
static struct { void *self; int virtue, test, sp[3]; DWORD on, off, last, t12; unsigned stage; int peakLogged; } g_beam;
DWORD u9snd_t0(void);

static void beam_tick(void *self, unsigned stage, uintptr_t shrine)
{
    DWORD now = u9time_now();
    unsigned v = rd32((uintptr_t)self + PROC_VIRTUE);
    if (g_beam.self != self) {
        g_beam.self = self;
        g_beam.on = g_beam.off = g_beam.t12 = 0;
        g_beam.peakLogged = 0;
    }
    g_beam.last = now;
    if (g_beam.on && stage != g_beam.stage)
        u9log_line("SHRINE beam stage %d at %lu ms (SND clock %lu ms)", (int)stage,
                   (unsigned long)(now - g_beam.on), (unsigned long)(now - u9snd_t0()));
    g_beam.stage = stage;
    if (stage == STAGE_RISE && !g_beam.on && v >= 1 && v <= 8 && shrine && obj_where(shrine, g_beam.sp)) {
        g_beam.on = now ? now : 1;
        g_beam.virtue = (int)v;
        u9log_line("SHRINE beam on: %s, the shrine at %d,%d,%d", VIRTUE[v], g_beam.sp[0], g_beam.sp[1], g_beam.sp[2]);
    }
    if (stage == STAGE_GLOW && g_beam.on && !g_beam.t12) g_beam.t12 = now ? now : 1;
    if (stage == STAGE_REVEAL && g_beam.on && !g_beam.off) {
        g_beam.off = now ? now : 1;
        u9log_line("SHRINE beam fading: the reveal (stage 13), %lu ms after it came on",
                   (unsigned long)(now - g_beam.on));
    }
}

int u9shrine_beam(int fadeMs, int delayMs, int *virtue, float base[3], float foot[3], float *level)
{
    DWORD now = u9time_now(), start;
    uintptr_t av = avatar_object();
    int p[3], k;
    float lv = 1.0f, f = (float)(fadeMs > 1 ? fadeMs : 1);
    if (!av || !obj_where(av, p)) return 0;
    if (g_beam.test) {
        *virtue = g_beam.test;
        for (k = 0; k < 3; k++) base[k] = foot[k] = (float)p[k];
        *level = 1.0f;
        return 1;
    }
    if (!g_beam.on || (LONG)(now - g_beam.last) > (LONG)BEAM_STALE_MS) return 0;
    start = g_beam.on + (DWORD)(delayMs > 0 ? delayMs : 0);
    if ((LONG)(now - start) < 0) return 0;
    if ((float)(now - start) < f) lv = (float)(now - start) / f;
    if (g_beam.off) {
        float out = 1.0f - (float)(now - g_beam.off) / f;
        if (out <= 0.0f) return 0;
        if (out < lv) lv = out;
    }
    *virtue = g_beam.virtue;
    for (k = 0; k < 3; k++) { base[k] = (float)g_beam.sp[k]; foot[k] = (float)p[k]; }
    *level = lv;
    return 1;
}

float u9shrine_beam_peak(int leadMs)
{
    DWORD now = u9time_now(), from;
    float p;
    if (g_beam.test || !g_beam.on) return 0.0f;
    if (g_beam.t12) {
        from = g_beam.t12 + STRIKE_AFTER_12 - (DWORD)(leadMs > 0 ? leadMs : 0);
        if (g_beam.off && (LONG)(g_beam.off - from) < 0) from = g_beam.off;
    } else if (g_beam.off) {
        from = g_beam.off;
    } else {
        return 0.0f;
    }
    if ((LONG)(now - from) < 0) return 0.0f;
    if (!g_beam.peakLogged) {
        g_beam.peakLogged = 1;
        u9log_line("SHRINE beam peak rising at %lu ms after it came on (%s)", (unsigned long)(now - g_beam.on),
                   g_beam.off && from == g_beam.off ? "at stage 13, not predicted" : "predicted from stage 12");
    }
    p = (float)(now - from) / (float)PEAK_RISE_MS;
    return p > 1.0f ? 1.0f : p;
}

void u9shrine_beam_test(int v)
{
    g_beam.test = (v >= 1 && v <= 8) ? v : 0;
}

static void U9CALL h_tick(void *self)
{
    unsigned  stage = rd32((uintptr_t)self + PROC_STAGE), bits = 0;
    uintptr_t shrine = handle_object(rd32((uintptr_t)self + PROC_SHRINE));
    int i, k = -1, freeSlot = -1, changed = 1;

    if (shrine) bits = rd32(shrine + OBJ_FLAGS) & SHRINE_BITS;
    beam_tick(self, stage, shrine);
    if (g_fix && stage == STAGE_REVEAL && shrine) {
        unsigned v = rd32((uintptr_t)self + PROC_VIRTUE);
        if (v >= 1 && v <= 8) {
            if (!handle_object(rd32((uintptr_t)self + PROC_PEDESTAL))) repair_slot(self, PROC_PEDESTAL, shrine, v);
            else                                                       home_slot(self, PROC_PEDESTAL, shrine, v);
            if (!handle_object(rd32((uintptr_t)self + PROC_LINKED)))   repair_slot(self, PROC_LINKED, shrine, v);
            else                                                       home_slot(self, PROC_LINKED, shrine, v);
        }
    }
    if (!g_on) { o_tick(self); return; }
    for (i = 0; i < MAX_PROCS; i++) {
        if (g_proc[i].self == self) { k = i; break; }
        if (!g_proc[i].self && freeSlot < 0) freeSlot = i;
    }
    if (k < 0) {
        g_seen++;
        roster(self, "new cleansing");
        if (freeSlot >= 0) {
            k = freeSlot;
            g_proc[k].self = self; g_proc[k].stage = stage; g_proc[k].bits = bits;
        }
    } else {
        changed = stage != g_proc[k].stage;
        if (stage != g_proc[k].stage)
            line("SHRINE %08X stage %d -> %d", (unsigned)self, (int)g_proc[k].stage, (int)stage);
        if (bits != g_proc[k].bits)
            line("SHRINE %08X shrine bits %04X -> %04X (stage %d)", (unsigned)self,
                 g_proc[k].bits, bits, stage);
        if (stage == STAGE_REVEAL && g_proc[k].stage != STAGE_REVEAL)
            roster(self, "about to reveal");
        g_proc[k].stage = stage; g_proc[k].bits = bits;
    }
    track(self, (int)stage, changed, shrine);
    if (stage == STAGE_LAST && (bits & 0x8000u)) {
        g_ended++;
        line("SHRINE %08X ends: stage 15 saw shrine bit 8000", (unsigned)self);
        if (k >= 0) g_proc[k].self = NULL;
        if (g_trk.self == self) g_trk.self = NULL;
    }
    o_tick(self);
}

void u9shrine_install(void)
{
    g_on  = u9cfg_int("diag_shrine_watch", 0);
    g_fix = u9cfg_int("fix_shrine_reveal", 1);
    if (!g_on && !g_fix) {
        u9log_line("F-18 shrine fix disabled by config - a cleansing whose rune or item is gone crashes as stock");
        return;
    }
    o_tick = (TickFn)u9patch_install(&specTick, (void *)h_tick);
    if (g_fix)
        u9log_line("F-18 shrine fix %s: before a cleansing reveals its rune and item, a missing one is "
                   "found on the map, taken back from the pack, or made anew", o_tick ? "on" : "REFUSED");
    if (!g_on) return;
    u9log_line("SHRINE watch: cleansing tick %s; a roster of each process's objects, its"
               " stages and the shrine's 4000/8000 bits, and a WARNING for a reveal of nothing",
               o_tick ? "hooked" : "REFUSED");
}

void u9shrine_report(void)
{
    if (g_fix && (g_found || g_pulled || g_made || g_failed || g_homed))
        u9log_line("F-18 shrine fix: %u found on the map, %u taken back from the pack, %u made, %u failed, "
                   "%u brought back to the altar", g_found, g_pulled, g_made, g_failed, g_homed);
    if (g_on)
        u9log_line("SHRINE totals: %u cleansings seen, %u ended, %u with a NULL reveal", g_seen,
                   g_ended, g_nulls);
}
