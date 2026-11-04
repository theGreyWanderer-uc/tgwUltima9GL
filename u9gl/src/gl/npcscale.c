#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fix/fixmem.h"
#include "npcscale.h"

#define NPC_MGR         0x0098F320u
#define MAP_ITEMS_CUR   0x008515E8u
#define NPC_STORE       0x0091F0E0u
#define MAP_ITEMS       0xD0u
#define ITEMS_BASE      0x34u
#define ARENA_SIZE      0x00C00000u
#define FA_MGR          0x00AD127Cu

#define OBJ_XY          0x04u
#define OBJ_Z           0x08u
#define OBJ_FLAGS       0x14u
#define OBJ_PROPS       0x1Cu
#define PAGE_BASE_X     0x0Cu
#define PAGE_BASE_Y     0x10u

#define REC_NAME        0x04u
#define REC_SCALE       0x6Cu
#define SCENE_OBJ       0x14u
#define SCENE_SCALE     0x1A4u

#define P_UNIFORM       0x42u
#define P_AXIS_X        0x49u

typedef struct {
    unsigned  npc;
    char      name[33];
    uintptr_t obj;
    unsigned  base[3];
    unsigned  uni, axis[3];
    int       uniHas, axisHas[3], oddCell, noPool;
    float     live[3];
    int       liveOk;
    const char *liveWhy, *pool;
} Scale;

static uintptr_t items_base(uintptr_t items)
{
    return (items && readable4(items + ITEMS_BASE)) ? rd32(items + ITEMS_BASE) : 0;
}

static uintptr_t map_base(void)
{
    return items_base(readable4(MAP_ITEMS_CUR) ? rd32(MAP_ITEMS_CUR) : 0);
}

static uintptr_t store_base(void)
{
    uintptr_t m = readable4(NPC_STORE) ? rd32(NPC_STORE) : 0;
    return items_base((m && readable4(m + MAP_ITEMS)) ? rd32(m + MAP_ITEMS) : 0);
}

static uintptr_t object_base(uintptr_t obj)
{
    uintptr_t b[2], i;
    b[0] = map_base();
    b[1] = store_base();
    for (i = 0; i < 2; i++)
        if (b[i] && obj >= b[i] && obj - b[i] < ARENA_SIZE) return b[i];
    return 0;
}

static uintptr_t npc_object(unsigned npc, uintptr_t base)
{
    uintptr_t mgr = readable4(NPC_MGR) ? rd32(NPC_MGR) : 0, recs, h;
    if (mgr && readable4(mgr + 4 + npc * 4) && rd32(mgr + 4 + npc * 4))
        return rd32(mgr + 4 + npc * 4);
    recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!recs || !readable4(recs + npc * RECORD_STRIDE)) return 0;
    h = rd32(recs + npc * RECORD_STRIDE);
    return (h > 1 && base) ? base + h : 0;
}

static int prop_get(uintptr_t obj, uintptr_t base, unsigned id, unsigned *val, int *odd)
{
    uintptr_t p;
    unsigned guard = 0;
    *val = 100;
    if (!readable4(obj + OBJ_PROPS) || (rd32(obj + OBJ_FLAGS) & 1u)) return 0;
    p = rd32(obj + OBJ_PROPS) ? base + rd32(obj + OBJ_PROPS) : 0;
    while (p && guard++ < 256 && readable4(p) && readable4(p + 12)) {
        uint32_t head = rd32(p);
        unsigned n = head & 0xFFu, c;
        if (n < 4) {
            uintptr_t next = 0;
            for (c = 0; c < n; c++) {
                unsigned cid = (head >> (8 * (c + 1))) & 0xFFu;
                uint32_t v = rd32(p + 4 + 4 * c);
                if (cid == id) { *val = v; return 1; }
                if (cid == 0xFFu) next = v ? base + v : 0;
            }
            p = next;
        } else {
            if (n == id) { *val = rd32(p + 4); *odd = 1; return 1; }
            p = (head >> 8) ? base + (head >> 8) : 0;
        }
    }
    return 0;
}

static uintptr_t scene_record(uintptr_t obj, const char **why)
{
    uint32_t flags = rd32(obj + OBJ_FLAGS), xy;
    unsigned ref = flags >> 21;
    uintptr_t mgr, grid, cell, slot, rec, page;
    int x, y, cx, cy;

    if (ref == 0 || ref == 0x7FEu || ref == 0x7FFu) { *why = "not in the fast area"; return 0; }
    if (flags & 0x200u) { *why = "contained"; return 0; }
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + PAGE_BASE_Y)) { *why = "unreadable"; return 0; }
    xy = rd32(obj + OBJ_XY);
    x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + PAGE_BASE_X);
    y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + PAGE_BASE_Y);
    mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0;
    if (!mgr || !readable4(mgr + 0x64)) { *why = "no fast area"; return 0; }
    grid = rd32(mgr + 0x64);
    if (!grid || !readable4(grid + 0x30)) { *why = "no fast area"; return 0; }
    cx = x >> 10; cy = y >> 10;
    if (cx < 0 || cy < 0 || cx >= ((int32_t)rd32(grid + 0x30) >> 3) ||
        cy >= ((int32_t)rd32(grid + 0x2C) >> 3)) { *why = "off the area grid"; return 0; }
    cell = rd32(mgr + 0x44) + ((uintptr_t)rd32(mgr + 0x4C) * (unsigned)cy + (unsigned)cx) * 0x28u;
    if (!readable4(cell + 0x10) || !rd32(cell + 0x10)) { *why = "cell not set up"; return 0; }
    slot = rd32(cell + 0x10) + ref * 0x10u;
    rec = readable4(slot + 4) ? rd32(slot + 4) : 0;
    if (!rec || !readable4(rec + SCENE_OBJ) || rd32(rec + SCENE_OBJ) != obj) { *why = "no scene record yet"; return 0; }
    if (!readable4(rec + SCENE_SCALE + 8)) { *why = "unreadable"; return 0; }
    *why = "ok";
    return rec;
}

static int read_scale(unsigned npc, uintptr_t store, Scale *s)
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0, r, rec, base;
    unsigned i;

    memset(s, 0, sizeof *s);
    s->npc = npc;
    s->liveWhy = "no world object";
    if (!recs || npc >= ACTOR_COUNT) return 0;
    r = recs + npc * RECORD_STRIDE;
    if (!readable4(r + REC_SCALE)) return 0;
    memcpy(s->name, (const void *)(r + REC_NAME), 32);
    s->name[32] = 0;
    for (i = 0; i < 3; i++) s->base[i] = *(const volatile uint8_t *)(r + REC_SCALE + i);
    s->uni = 100;
    for (i = 0; i < 3; i++) s->axis[i] = 100;

    s->obj = npc_object(npc, store);
    if (!s->obj || !readable4(s->obj + OBJ_PROPS)) { s->obj = 0; return 1; }
    base = object_base(s->obj);
    if (!base) { s->noPool = 1; return 1; }
    s->pool = base == map_base() ? "current map" : "NPC store";
    s->uniHas = prop_get(s->obj, base, P_UNIFORM, &s->uni, &s->oddCell);
    for (i = 0; i < 3; i++) s->axisHas[i] = prop_get(s->obj, base, P_AXIS_X + i, &s->axis[i], &s->oddCell);
    rec = scene_record(s->obj, &s->liveWhy);
    if (rec) {
        memcpy(s->live, (const void *)(rec + SCENE_SCALE), sizeof s->live);
        s->liveOk = 1;
    }
    return 1;
}

static void registration(const Scale *s, float out_[3])
{
    unsigned i;
    for (i = 0; i < 3; i++) {
        out_[i] = (s->base[i] ? (float)s->base[i] : 100.0f) * 0.01f;
        if (s->uni != 0 && s->uni != 100) out_[i] *= (float)s->uni * 0.01f;
    }
}

static unsigned saved_pct(float v) { return (unsigned)(int)(v * 100.0f + 0.5f); }

static void reader(const Scale *s, float out_[3])
{
    unsigned i;
    for (i = 0; i < 3; i++) {
        out_[i] = (s->uni != 0 && s->uni != 100) ? (float)s->uni * 0.01f : 1.0f;
        if (s->axis[i] == 0) out_[i] = 1.0f;
        else if (s->axis[i] != 100) out_[i] = (float)s->axis[i] * 0.01f;
    }
}

static const char *verdict(const Scale *s)
{
    float reg[3], rd[3];
    int axes = s->axisHas[0] || s->axisHas[1] || s->axisHas[2], i, liveReg = 1, liveRd = 1;
    if (!s->obj) return "no world object";
    if (s->noPool) return "object in neither pool";
    if (s->oddCell) return "odd property cell";
    registration(s, reg);
    reader(s, rd);
    if (axes)
        for (i = 0; i < 3; i++)
            if (s->axis[i] != saved_pct(reg[i])) return "axes differ from npc x uniform";
    if (s->liveOk) {
        for (i = 0; i < 3; i++) {
            if (fabsf(s->live[i] - reg[i]) > 0.0005f) liveReg = 0;
            if (fabsf(s->live[i] - rd[i]) > 0.0005f)  liveRd = 0;
        }
        if (!liveReg && !liveRd) return "live differs from both";
        if (!liveReg) return "live = property reader";
    }
    return axes ? "ok" : "ok, axes not saved yet";
}

static int is_plain(const Scale *s)
{
    unsigned i;
    if (s->uniHas && s->uni != 0 && s->uni != 100) return 0;
    for (i = 0; i < 3; i++) {
        if (s->base[i] && s->base[i] != 100) return 0;
        if (s->axisHas[i] && s->axis[i] != 100) return 0;
    }
    return !strncmp(verdict(s), "ok", 2);
}

static void fmt_axes(const Scale *s, char *b, size_t cap)
{
    char p[3][8];
    unsigned i;
    for (i = 0; i < 3; i++) {
        if (s->axisHas[i]) snprintf(p[i], sizeof p[i], "%u", s->axis[i]);
        else snprintf(p[i], sizeof p[i], "-");
    }
    snprintf(b, cap, "%s/%s/%s", p[0], p[1], p[2]);
}

static void line(const Scale *s, U9ConsoleOut out)
{
    char ax[24], uni[8], live[32];
    fmt_axes(s, ax, sizeof ax);
    if (s->uniHas) snprintf(uni, sizeof uni, "%u", s->uni); else snprintf(uni, sizeof uni, "-");
    if (s->liveOk) snprintf(live, sizeof live, "%.3f/%.3f/%.3f", s->live[0], s->live[1], s->live[2]);
    else snprintf(live, sizeof live, "-");
    out("%3u %-16.16s npc %u/%u/%u uni %s axes %s live %s  %s",
        s->npc, s->name, s->base[0], s->base[1], s->base[2], uni, ax, live, verdict(s));
}

static void detail(const Scale *s, U9ConsoleOut out)
{
    float reg[3], rd[3];
    char ax[24];
    out("%u %s  (world object %08X)", s->npc, s->name, (unsigned)s->obj);
    out("  NPC record +0x6C..6E   %u/%u/%u %%  (0 counts as 100)", s->base[0], s->base[1], s->base[2]);
    if (!s->obj) { out("  no world object on this map - nothing else to read"); return; }
    if (s->noPool) { out("  object lies in neither the map's pool nor the NPC store's"); return; }
    out("  properties read from   the %s pool", s->pool);
    if (s->uniHas) out("  uniform 0x42           %u %%", s->uni);
    else           out("  uniform 0x42           absent (100)");
    fmt_axes(s, ax, sizeof ax);
    out("  axes 0x49/0x4A/0x4B    %s %%  (- = absent, reads as 100)", ax);
    registration(s, reg);
    reader(s, rd);
    out("  registration gives     %.3f/%.3f/%.3f, saved as %u/%u/%u",
        reg[0], reg[1], reg[2], saved_pct(reg[0]), saved_pct(reg[1]), saved_pct(reg[2]));
    out("  property reader gives  %.3f/%.3f/%.3f", rd[0], rd[1], rd[2]);
    if (s->liveOk) out("  live (scene +0x1A4)    %.3f/%.3f/%.3f", s->live[0], s->live[1], s->live[2]);
    else           out("  live                   - (%s)", s->liveWhy);
    out("  -> %s", verdict(s));
}

static int name_has(const char *name, const char *want)
{
    size_t n = strlen(want), i;
    for (i = 0; name[i]; i++)
        if (!_strnicmp(name + i, want, n)) return 1;
    return 0;
}

void u9npcscale_command(const char *arg, U9ConsoleOut out)
{
    uintptr_t base = store_base();
    Scale s;
    unsigned npc, shown = 0, loaded = 0, matched = 0, last = 0;
    char *end;

    if (!map_base() || !base) { out("no world is loaded"); return; }

    if (*arg && (npc = (unsigned)strtoul(arg, &end, 10), !*end)) {
        if (npc >= ACTOR_COUNT) { out("NPC numbers run 0..%u", ACTOR_COUNT - 1); return; }
        if (read_scale(npc, base, &s)) detail(&s, out);
        else out("NPC %u is unreadable", npc);
        return;
    }
    if (*arg && strcmp(arg, "all")) {
        for (npc = 0; npc < ACTOR_COUNT; npc++)
            if (read_scale(npc, base, &s) && s.name[0] && name_has(s.name, arg)) { matched++; last = npc; }
        if (!matched) { out("no NPC name contains '%s'", arg); return; }
        if (matched == 1) { read_scale(last, base, &s); detail(&s, out); return; }
        for (npc = 0; npc < ACTOR_COUNT; npc++)
            if (read_scale(npc, base, &s) && s.name[0] && name_has(s.name, arg)) line(&s, out);
        out("%u NPCs match - size <number> for one", matched);
        return;
    }
    for (npc = 0; npc < ACTOR_COUNT; npc++) {
        if (!read_scale(npc, base, &s) || !s.obj) continue;
        loaded++;
        if (!*arg && is_plain(&s)) continue;
        line(&s, out);
        shown++;
    }
    if (*arg) out("%u NPCs with a world object on this map", loaded);
    else out("%u of %u NPCs on this map shown; the rest are plain 100%% and agree (size all lists them)",
             shown, loaded);
}

#define REC_REGION      0x58u
#define REC_X           0x5Cu
#define REC_Y           0x60u
#define REC_Z           0x64u

static int object_pos(uintptr_t obj, int *x, int *y, int *z)
{
    uintptr_t page;
    uint32_t xy;
    if (!obj || !readable4(obj + OBJ_FLAGS) || (rd32(obj + OBJ_FLAGS) & 0x200u)) return 0;
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + PAGE_BASE_Y) || !readable4(obj + OBJ_Z)) return 0;
    xy = rd32(obj + OBJ_XY);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + PAGE_BASE_X);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + PAGE_BASE_Y);
    *z = (int16_t)(rd32(obj + OBJ_Z) & 0xFFFFu);
    return 1;
}

static int npc_place(unsigned npc, int curMap, U9NpcPlace *p)
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0, r, obj;
    memset(p, 0, sizeof *p);
    p->npc = npc;
    if (!recs || npc >= ACTOR_COUNT) return 0;
    r = recs + npc * RECORD_STRIDE;
    if (!readable4(r + REC_Z)) return 0;
    memcpy(p->name, (const void *)(r + REC_NAME), 32);
    p->name[32] = 0;
    if (rd32(r) == 0) return 0;
    p->map = (int32_t)rd32(r + REC_REGION);
    p->x   = (int32_t)rd32(r + REC_X);
    p->y   = (int32_t)rd32(r + REC_Y);
    p->z   = *(const volatile int16_t *)(r + REC_Z);
    if (p->map == curMap) {
        obj = npc_object(npc, store_base());
        if (obj && object_base(obj) == map_base() && object_pos(obj, &p->x, &p->y, &p->z))
            p->live = 1;
    }
    return p->map >= 0;
}

static void place_line(const U9NpcPlace *p, U9ConsoleOut out)
{
    out("%3u %-20.20s at %d,%d,%d", p->npc, p->name, p->x, p->y, p->z);
}

static void squash(const char *s, char *d, size_t cap)
{
    size_t n = 0;
    for (; *s && n + 1 < cap; s++)
        if (*s != ' ' && *s != '_' && *s != '-') d[n++] = (char)tolower((unsigned char)*s);
    d[n] = 0;
}

static int name_match(const char *name, const char *want)
{
    char a[40], b[64];
    squash(name, a, sizeof a);
    squash(want, b, sizeof b);
    if (!b[0]) return 0;
    if (!strcmp(a, b)) return 2;
    return strstr(a, b) != NULL;
}

int u9npc_locate(const char *arg, int curMap, U9NpcPlace *found, U9ConsoleOut out)
{
    U9NpcPlace p;
    unsigned npc, exact = 0, part = 0, lastExact = 0, lastPart = 0, shown = 0, away = 0;
    char *end;

    if (!readable4(ACTOR_RECORDS) || !rd32(ACTOR_RECORDS) || curMap < 0) { out("no world is loaded"); return 0; }
    if (!*arg) { out("usage: tppos npc <name> | tppos npc <number>"); return 0; }

    npc = (unsigned)strtoul(arg, &end, 10);
    if (end != arg && !*end) {
        if (npc >= ACTOR_COUNT) { out("NPC numbers run 0..%u", ACTOR_COUNT - 1); return 0; }
        if (npc_place(npc, curMap, found) && found->live) return 1;
        if (found->map >= 0 && found->name[0] && found->map != curMap)
            out("NPC %u %s is on map %d, not this one", npc, found->name, found->map);
        else
            out("NPC %u%s%s is not standing on this map", npc, found->name[0] ? " " : "", found->name);
        return 0;
    }

    for (npc = 0; npc < ACTOR_COUNT; npc++) {
        int m;
        if (!npc_place(npc, curMap, &p) || !p.name[0] || !(m = name_match(p.name, arg))) continue;
        if (!p.live) { away++; continue; }
        if (m == 2) { exact++; lastExact = npc; }
        part++; lastPart = npc;
    }
    if (exact == 1) return npc_place(lastExact, curMap, found);
    if (part == 1)  return npc_place(lastPart, curMap, found);
    if (!part) {
        if (!away) { out("no NPC's name contains '%s'", arg); return 0; }
        out("no NPC on this map matches '%s'; elsewhere:", arg);
        for (npc = 0; npc < ACTOR_COUNT && shown < 6; npc++)
            if (npc_place(npc, curMap, &p) && p.name[0] && !p.live && name_match(p.name, arg)) {
                out("  %3u %-20.20s map %d", p.npc, p.name, p.map);
                shown++;
            }
        return 0;
    }

    for (npc = 0; npc < ACTOR_COUNT && shown < 24; npc++) {
        int m;
        if (!npc_place(npc, curMap, &p) || !p.live || !p.name[0] || !(m = name_match(p.name, arg))) continue;
        if (exact && m != 2) continue;
        place_line(&p, out);
        shown++;
    }
    out("%u NPCs on this map match%s - tppos npc <number> for one", exact ? exact : part,
        shown < (exact ? exact : part) ? " (first 24 shown)" : "");
    return 0;
}

typedef struct { unsigned npc; double d2; } Near;

static int near_cmp(const void *a, const void *b)
{
    double d = ((const Near *)a)->d2 - ((const Near *)b)->d2;
    return d < 0 ? -1 : d > 0;
}

void u9npc_list_near(int curMap, int x, int y, int z, int rxy, int rz, unsigned max, U9ConsoleOut out)
{
    static Near nr[ACTOR_COUNT];
    U9NpcPlace p;
    unsigned npc, n = 0, i;

    if (!readable4(ACTOR_RECORDS) || !rd32(ACTOR_RECORDS) || curMap < 0) { out("no world is loaded"); return; }
    for (npc = 1; npc < ACTOR_COUNT; npc++) {
        double dx, dy, dz;
        if (!npc_place(npc, curMap, &p) || !p.live) continue;
        if (abs(p.x - x) > rxy || abs(p.y - y) > rxy || abs(p.z - z) > rz) continue;
        dx = p.x - x; dy = p.y - y; dz = p.z - z;
        nr[n].npc = npc;
        nr[n].d2 = dx * dx + dy * dy + dz * dz;
        n++;
    }
    if (!n) { out("no NPCs within %d x/y and %d up or down", rxy, rz); return; }
    qsort(nr, n, sizeof nr[0], near_cmp);
    for (i = 0; i < n && i < max; i++) {
        npc_place(nr[i].npc, curMap, &p);
        out("%3u %-20.20s %6.0f away, %+5d up", p.npc, p.name[0] ? p.name : "(no name)",
            sqrt(nr[i].d2), p.z - z);
    }
    out("%u NPC%s within %d x/y and %d up or down%s - tppos npc <number> goes to one", n,
        n == 1 ? "" : "s", rxy, rz, n > max ? " (nearest shown)" : "");
}

int u9npc_live_place(unsigned npc, int curMap, U9NpcPlace *p)
{
    return npc_place(npc, curMap, p) && p->live;
}

void u9npc_combat_text(unsigned npc, char *buf, size_t cap)
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0, act = 0;
    uint32_t state = 0;
    int target = -2;
    char tname[33] = "";

    if (npc >= ACTOR_COUNT) { snprintf(buf, cap, "-"); return; }
    if (recs && readable4(recs + npc * RECORD_STRIDE + 0x48)) state = rd32(recs + npc * RECORD_STRIDE + 0x48);
    if (tbl && readable4(tbl + npc * 4)) act = rd32(tbl + npc * 4);
    if (act && readable4(act) && rd32(act) >= IMAGE_LO && rd32(act) < IMAGE_HI && readable4(act + 0x104)) {
        target = (int32_t)rd32(act + 0x104);
        if (target >= 0 && target < (int)ACTOR_COUNT && recs && readable4(recs + (unsigned)target * RECORD_STRIDE + 0x24)) {
            memcpy(tname, (const void *)(recs + (unsigned)target * RECORD_STRIDE + 4), 32);
            tname[32] = 0;
        }
    }
    if (target == -2)
        snprintf(buf, cap, "state %08X (has actor %d); no actor", (unsigned)state,
                 (state & 0x00400000u) != 0);
    else if (target < 0)
        snprintf(buf, cap, "state %08X (has actor %d); target none (%d)", (unsigned)state,
                 (state & 0x00400000u) != 0, target);
    else
        snprintf(buf, cap, "state %08X (has actor %d); target %d %s", (unsigned)state,
                 (state & 0x00400000u) != 0, target, tname[0] ? tname : "(no name)");
}

uintptr_t u9npc_object(unsigned npc, int curMap)
{
    U9NpcPlace p;
    uintptr_t obj;
    if (!u9npc_live_place(npc, curMap, &p)) return 0;
    obj = npc_object(npc, store_base());
    return (obj && object_base(obj) == map_base()) ? obj : 0;
}

int u9npc_find_any(const char *arg, char *name, size_t cap, U9ConsoleOut out)
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    unsigned npc, exact = 0, part = 0, lastExact = 0, lastPart = 0, shown = 0;
    char n[33], *end;

    if (!recs) { out("no world is loaded"); return -1; }
    npc = (unsigned)strtoul(arg, &end, 10);
    if (end != arg && !*end) {
        if (npc >= ACTOR_COUNT || !readable4(recs + npc * RECORD_STRIDE + 0x24)) { out("NPC numbers run 0..%u", ACTOR_COUNT - 1); return -1; }
        memcpy(name, (const void *)(recs + npc * RECORD_STRIDE + 4), cap > 33 ? 32 : cap - 1);
        name[cap > 33 ? 32 : cap - 1] = 0;
        return (int)npc;
    }
    for (npc = 0; npc < ACTOR_COUNT; npc++) {
        int m;
        if (!readable4(recs + npc * RECORD_STRIDE + 0x24)) continue;
        memcpy(n, (const void *)(recs + npc * RECORD_STRIDE + 4), 32);
        n[32] = 0;
        if (!n[0] || !(m = name_match(n, arg))) continue;
        if (m == 2) { exact++; lastExact = npc; }
        part++; lastPart = npc;
    }
    if (exact == 1 || (!exact && part == 1)) {
        npc = exact == 1 ? lastExact : lastPart;
        memcpy(name, (const void *)(recs + npc * RECORD_STRIDE + 4), cap > 33 ? 32 : cap - 1);
        name[cap > 33 ? 32 : cap - 1] = 0;
        return (int)npc;
    }
    if (!part) { out("no NPC's name contains '%s'", arg); return -1; }
    for (npc = 0; npc < ACTOR_COUNT && shown < 24; npc++) {
        int m;
        if (!readable4(recs + npc * RECORD_STRIDE + 0x24)) continue;
        memcpy(n, (const void *)(recs + npc * RECORD_STRIDE + 4), 32);
        n[32] = 0;
        if (!n[0] || !(m = name_match(n, arg)) || (exact && m != 2)) continue;
        out("  %3u %s (map %d)", npc, n, (int)rd32(recs + npc * RECORD_STRIDE + 0x58));
        shown++;
    }
    out("%u NPCs match - use the number", exact ? exact : part);
    return -1;
}
