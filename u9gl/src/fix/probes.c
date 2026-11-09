#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../injection/patch.h"
#include "../injection/config.h"
#include "../diag/diag.h"
#include "fixmem.h"
#include "probes.h"
#include "teleprobe.h"
#include "pathprobe.h"
#include "bagprobe.h"
#include "birdprobe.h"
#include "portalfix.h"
#include "lightwatch.h"
#include "flyerfix.h"
#include "npcprobe.h"
#include "actorwatch.h"
#include "travelprobe.h"
#include "trigprobe.h"
#include "playerwatch.h"
#include "meshprobe.h"
#include "meshstat.h"

void u9log_line(const char *fmt, ...);

static int g_censusWanted;

static void fixlog(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfA(line, fmt, ap);
    va_end(ap);
    u9log_line("%s", line);
}

static int      g_probeIdx = -1;
static unsigned g_probeTicks, g_probeLogged;
static unsigned g_probeLists = 99, g_probeNodes = 99, g_probeListLogged;
static unsigned g_probe2BC = 99;
static int      g_probeSeen;
static int32_t  g_probeLast;

static void probe_actor(void *self)
{
    uintptr_t tbl, obj, recs;
    int32_t v174, v178;

    if (g_probeIdx < 0) return;
    tbl = rd32(ACTOR_TABLE);
    if (!tbl || !readable4(tbl + (unsigned)g_probeIdx * 4)) return;
    obj = rd32(tbl + (unsigned)g_probeIdx * 4);
    if (!obj || obj != (uintptr_t)self) return;

    g_probeTicks++;
    v174 = (int32_t)rd32(obj + 0x174);
    v178 = (int32_t)rd32(obj + 0x178);

    if (!g_probeSeen) {
        g_probeSeen = 1;
        g_probeLast = v174 + 1;
        recs = rd32(ACTOR_RECORDS);
        if (recs) {
            uintptr_t rec = recs + (unsigned)g_probeIdx * RECORD_STRIDE;
            uint32_t c4 = rd32(rec + 0xC4);
            fixlog("probe %d: record[0x34] = %d, record[0xC4] = %08X, bit 0x80"
                   " %s - %s by the active-list filter on that clause",
                   g_probeIdx, (int)(int16_t)(rd32(rec + 0x34) & 0xFFFFu), c4,
                   (c4 & 0x80) ? "SET" : "clear",
                   (c4 & 0x80) ? "EXCLUDED" : "not excluded");
        }
    }
    {
        unsigned li, lists = 0, nodes = 0;
        for (li = 0; li < 6; li++) {
            uintptr_t head = obj + 0x240 + li * 0x0C, node;
            unsigned guard = 0;
            if (!readable4(head)) continue;
            node = rd32(head);
            if (node) lists++;
            while (node && readable4(node + 4) && guard++ < 64) {
                nodes++;
                node = rd32(node + 4);
            }
        }
        {
            unsigned bc = rd32(obj + 0x2BC) & 0xFFu;
            if (bc != g_probe2BC) {
                g_probe2BC = bc;
                if (g_probeListLogged < 12) {
                    g_probeListLogged++;
                    fixlog("probe %d tick %-6u: +0x2BC now %u%s", g_probeIdx,
                           g_probeTicks, bc,
                           bc ? " - a rebuild is pending"
                              : " - the tick consumed it, slot 0x1D4 ran");
                }
            }
        }
        if (lists != g_probeLists || nodes != g_probeNodes) {
            g_probeLists = lists;
            g_probeNodes = nodes;
            if (g_probeListLogged < 12) {
                g_probeListLogged++;
                fixlog("probe %d tick %-6u: lists now %u/6 non-empty, %u nodes,"
                       " +0x2BC %u", g_probeIdx, g_probeTicks, lists, nodes,
                       rd32(obj + 0x2BC) & 0xFFu);
            }
        }
    }

    if (v174 == g_probeLast) return;
    g_probeLast = v174;
    if (g_probeLogged >= 40) return;
    g_probeLogged++;
    fixlog("probe %d tick %-6u: +0x174 %d, +0x178 %d", g_probeIdx,
           g_probeTicks, v174, v178);
}

typedef char (U9CALL *AttackFn)(void *self, int target);
static AttackFn o_attack;
static U9Patch  g_patAtkA, g_patAtkB;
static unsigned g_atkCalls, g_atkOk, g_atkLogged;

static char U9CALL h_attack(void *self, int target)
{
    uintptr_t tbl;
    int isProbe = 0;
    char r;
    void *caller = __builtin_return_address(0);

    if (g_probeIdx >= 0) {
        tbl = rd32(ACTOR_TABLE);
        if (tbl && readable4(tbl + (unsigned)g_probeIdx * 4))
            isProbe = (rd32(tbl + (unsigned)g_probeIdx * 4) == (uintptr_t)self);
    }
    r = o_attack(self, target);
    if (!isProbe) return r;

    g_atkCalls++;
    if (r) g_atkOk++;
    if (g_atkLogged < 40) {
        g_atkLogged++;
        fixlog("probe %d ATTACK from %08X: target %d -> %d  [+0x174 %d,"
               " +0x178 %d, +0x220 %d, +0x224 %d, +0x104 %d]",
               g_probeIdx, (unsigned)(uintptr_t)caller, target, (int)r,
               (int32_t)rd32((uintptr_t)self + 0x174),
               (int32_t)rd32((uintptr_t)self + 0x178),
               (int32_t)rd32((uintptr_t)self + 0x220),
               (int32_t)rd32((uintptr_t)self + 0x224),
               (int32_t)rd32((uintptr_t)self + 0x104));
    }
    return r;
}

static void attack_probe_install(void)
{
    struct { uintptr_t slot; U9Patch *p; const char *name; } rows[] = {
        { 0x00748F90u, &g_patAtkA, "0x00748F90" },
        { 0x00747EB0u, &g_patAtkB, "0x00747EB0" },
    };
    unsigned i;

    for (i = 0; i < 2; i++) {
        uint32_t now = *(const uint32_t *)rows[i].slot;
        if (now != 0x0043E0C0u) {
            fixlog("probe: attack slot %s holds %08X, expected 0043E0C0"
                   " - NOT hooked", rows[i].name, now);
            continue;
        }
        if (!u9patch_begin(rows[i].p, (void *)rows[i].slot, 1)) continue;
        o_attack = (AttackFn)u9patch_slot(rows[i].p, 0, (void *)h_attack);
        u9patch_end(rows[i].p, 1);
        fixlog("probe: attack call hooked on slot %s", rows[i].name);
    }
}

#define DECIDE_FN 0x004BA740u
typedef uint32_t (U9CALL *DecideFn)(void *self);
static DecideFn o_decide;
static unsigned g_decCalls, g_decExpired, g_decYes, g_decLogged;
static int      g_lastDecide;

#define DEC_CALLERS 16
static struct { uintptr_t addr; unsigned calls, yes; } g_decFrom[DEC_CALLERS];
static unsigned g_decFromCount;

static void note_decide_caller(uintptr_t caller, int yes)
{
    unsigned i;
    for (i = 0; i < g_decFromCount; i++) {
        if (g_decFrom[i].addr != caller) continue;
        g_decFrom[i].calls++;
        if (yes) g_decFrom[i].yes++;
        return;
    }
    if (g_decFromCount >= DEC_CALLERS) return;
    g_decFrom[g_decFromCount].addr = caller;
    g_decFrom[g_decFromCount].calls = 1;
    g_decFrom[g_decFromCount].yes = yes ? 1u : 0u;
    g_decFromCount++;
}

static uint32_t U9CALL h_decide(void *self)
{
    uintptr_t tbl;
    int isProbe = 0, expired = 0;
    int32_t v174 = 0, v228 = 0;
    uint32_t r;
    void *caller = __builtin_return_address(0);

    if (g_probeIdx >= 0) {
        tbl = rd32(ACTOR_TABLE);
        if (tbl && readable4(tbl + (unsigned)g_probeIdx * 4))
            isProbe = (rd32(tbl + (unsigned)g_probeIdx * 4) == (uintptr_t)self);
    }
    if (isProbe) {
        v174 = (int32_t)rd32((uintptr_t)self + 0x174);
        v228 = (int32_t)rd32((uintptr_t)self + 0x228);
        expired = (v174 < 1);
    }

    r = o_decide(self);

    if (!isProbe) return r;
    g_decCalls++;
    if (expired) g_decExpired++;
    if (r & 0xFFu) g_decYes++;
    note_decide_caller((uintptr_t)caller, (r & 0xFFu) != 0);
    g_lastDecide = (r & 0xFFu) != 0;
    if (g_decLogged < 40 && (expired || (r & 0xFFu))) {
        g_decLogged++;
        fixlog("probe %d DECIDE from %08X: +0x174 %d on entry (%s), +0x228 %d"
               " -> %s", g_probeIdx, (unsigned)(uintptr_t)caller, v174,
               expired ? "EXPIRED" : "still counting", v228,
               (r & 0xFFu) ? "STRIKE" : "no");
    }
    return r;
}

static char (U9CALL *o_sibA)(void *self);
static char (U9CALL *o_sibB)(void *self);
static unsigned g_sibALogged, g_sibBLogged, g_sibAYes, g_sibBYes;

static int probe_is(void *self)
{
    uintptr_t tbl;
    if (g_probeIdx < 0) return 0;
    tbl = rd32(ACTOR_TABLE);
    if (!tbl || !readable4(tbl + (unsigned)g_probeIdx * 4)) return 0;
    return rd32(tbl + (unsigned)g_probeIdx * 4) == (uintptr_t)self;
}

static char U9CALL h_sibA(void *self)
{
    char r = o_sibA(self);
    if (!probe_is(self)) return r;
    if (r) g_sibAYes++;
    if (g_sibALogged < 20 && g_lastDecide) {
        g_sibALogged++;
        fixlog("probe %d SIB_A (004DE090) -> %d   [decision STRIKE, +0x220 %d,"
               " +0x22C %08X]", g_probeIdx, (int)r,
               (int32_t)rd32((uintptr_t)self + 0x220),
               rd32((uintptr_t)self + 0x22C));
    }
    return r;
}

static char U9CALL h_sibB(void *self)
{
    char r = o_sibB(self);
    uintptr_t rec;
    int w3a = -1;

    if (!probe_is(self)) return r;
    if (r) g_sibBYes++;
    rec = rd32((uintptr_t)self + 0x0C);
    if (rec && readable4(rec + 0x38))
        w3a = (int)(rd32(rec + 0x38) >> 16);
    if (g_sibBLogged < 20 && g_lastDecide) {
        unsigned li, nodes = 0, lists = 0, usable = 0;
        for (li = 0; li < 6; li++) {
            uintptr_t head = (uintptr_t)self + 0x240 + li * 0x0C;
            uintptr_t node;
            unsigned guard = 0;
            if (!readable4(head)) continue;
            node = rd32(head);
            if (node) lists++;
            while (node && readable4(node + 8) && guard++ < 64) {
                uintptr_t item = rd32(node + 8);
                nodes++;
                if (item && readable4(item + 0x18) &&
                    (int)(rd32(item + 0x18) & 0xFFu) <= w3a) usable++;
                node = readable4(node + 4) ? rd32(node + 4) : 0;
            }
        }
        g_sibBLogged++;
        fixlog("probe %d SIB_B (004DE0E0) -> %d   [decision STRIKE, +0x220 %d,"
               " record+0x3A %d, +0x2B8 %d, lists %u/6 non-empty, %u nodes,"
               " %u usable]", g_probeIdx, (int)r,
               (int32_t)rd32((uintptr_t)self + 0x220), w3a,
               (int32_t)rd32((uintptr_t)self + 0x2B8), lists, nodes, usable);
    }
    return r;
}

static void sibling_probe_install(void)
{
    static const uint8_t expA[6]  = { 0x51, 0x56, 0x8B, 0xF1, 0x6A, 0x01 };
    static const uint8_t expB[10] = { 0x56, 0x8B, 0xF1, 0x83, 0xBE,
                                      0x20, 0x02, 0x00, 0x00, 0x0B };
    o_sibA = (char (U9CALL *)(void *))
             u9inline_hook(0x004DE090u, (void *)h_sibA, 6, expA);
    o_sibB = (char (U9CALL *)(void *))
             u9inline_hook(0x004DE0E0u, (void *)h_sibB, 10, expB);
    fixlog("probe: sibling consumers %s / %s",
           o_sibA ? "hooked" : "NOT hooked", o_sibB ? "hooked" : "NOT hooked");
}

static void decide_probe_install(void)
{
    static const uint8_t expect[6] = { 0x53, 0x56, 0x8B, 0xF1, 0x32, 0xDB };
    o_decide = (DecideFn)u9inline_hook(DECIDE_FN, (void *)h_decide, 6, expect);
    fixlog("probe: combat decision 0x004BA740 %s",
           o_decide ? "hooked" : "NOT hooked");
}

#define CENSUS_MAX 24
static struct {
    unsigned idx; uintptr_t obj, vt;
    int32_t v174, v178; unsigned lists, nodes;
} g_census[CENSUS_MAX];
static unsigned g_censusCount, g_censusTick;

static void census_take(void)
{
    uintptr_t tbl = rd32(ACTOR_TABLE);
    unsigned i, n = 0;

    if (!tbl) return;
    for (i = 0; i < ACTOR_COUNT && n < CENSUS_MAX; i++) {
        uintptr_t obj, vt;
        unsigned li, lists = 0, nodes = 0;

        if (!readable4(tbl + i * 4)) continue;
        obj = rd32(tbl + i * 4);
        if (!obj || !readable4(obj)) continue;
        vt = rd32(obj);
        if (!is_actor_vtable(vt)) continue;

        for (li = 0; li < 6; li++) {
            uintptr_t head = obj + 0x240 + li * 0x0C, node;
            unsigned guard = 0;
            if (!readable4(head)) continue;
            node = rd32(head);
            if (node) lists++;
            while (node && readable4(node + 4) && guard++ < 64) {
                nodes++;
                node = rd32(node + 4);
            }
        }
        g_census[n].idx = i;
        g_census[n].obj = obj;
        g_census[n].vt = vt;
        g_census[n].v174 = (int32_t)rd32(obj + 0x174);
        g_census[n].v178 = (int32_t)rd32(obj + 0x178);
        g_census[n].lists = lists;
        g_census[n].nodes = nodes;
        n++;
    }
    g_censusCount = n;
}

static void dump_entity(const char *what, unsigned id, uintptr_t ent);

static int      g_dumpId = -1;
static unsigned g_dumpCount;
static uintptr_t g_dumpLastEnt = 1;
static uint32_t g_dumpLast14 = 1;

static void dump_wanted_entity(void)
{
    uintptr_t tbl, ent = 0;
    uint32_t f14 = 0;

    if (g_dumpId < 0) return;
    tbl = readable4(0x008515D0u) ? rd32(0x008515D0u) : 0;
    if (!tbl) return;
    if (readable4(tbl + (unsigned)g_dumpId * 0x0Cu + 8))
        ent = rd32(tbl + (unsigned)g_dumpId * 0x0Cu + 8);
    if (ent && readable4(ent + 0x14)) f14 = rd32(ent + 0x14);

    if (ent == g_dumpLastEnt && f14 == g_dumpLast14) return;
    g_dumpLastEnt = ent;
    g_dumpLast14 = f14;
    if (g_dumpCount >= 24) return;
    g_dumpCount++;

    fixlog("");
    fixlog("entity %d changed (%u): +0x14 = %08X, top11 = %u  -> %s",
           g_dumpId, g_dumpCount, f14, f14 >> 21,
           !ent ? "GONE - slot released"
           : ((f14 >> 21) == 0 || (f14 >> 21) == 0x7FEu || (f14 >> 21) == 0x7FFu)
             ? "DESTROYED - the sentinel FUN_004CD220 tests for"
             : "live, with a resource");
    dump_entity("entity", (unsigned)g_dumpId, ent);
}

static void census_tick(void)
{
    dump_wanted_entity();
    if (!g_censusWanted) return;
    ++g_censusTick;
    if (g_censusTick != 200u && (g_censusTick % 1200u) != 0u) return;
    census_take();
}

static void census_report(void)
{
    unsigned i;

    if (!g_censusCount) {
        if (g_censusWanted)
            fixlog("census: no actor-class object was ever seen (%u actor ticks)."
                   " If the run reached play, the table was read at a moment when"
                   " nothing was registered.", g_censusTick);
        return;
    }
    fixlog("");
    fixlog("actor census (last snapshot, actor-class objects only):");
    fixlog("   index  object    vtable    +0x174  +0x178  lists  nodes");
    for (i = 0; i < g_censusCount; i++)
        fixlog("   %-5u  %08X  %08X  %6d  %6d  %4u/6  %5u",
               g_census[i].idx, (unsigned)g_census[i].obj,
               (unsigned)g_census[i].vt, g_census[i].v174, g_census[i].v178,
               g_census[i].lists, g_census[i].nodes);
    fixlog("   an actor-class NPC with 0/6 lists is in the same position as"
           " Tydus; a non-actor vtable never appears here at all");
}


#define FA_MGR        0x00AD127Cu
#define FA_SLOT       0x10u
#define FA_ALLOCATED  0x7FEu
#define FA_USABLE     0x7FDu
#define FA_CELLS      128u

static int      g_faWanted;
static int      g_diagTele;
static unsigned g_faTick;
static int      g_faChecked;
static unsigned g_faCells, g_faCellsPeak, g_faDropped;
static unsigned g_faPeakAll;
static unsigned g_faSamples;
static struct { uintptr_t cell; unsigned live, peak; } g_fa[FA_CELLS];

static unsigned fa_free_len(uintptr_t cell)
{
    uintptr_t base = rd32(cell + 0x10);
    unsigned  idx, n = 0;

    if (!base || !readable4(base)) return 0;
    idx = rd32(cell + 0x14);
    while (idx >= 1u && idx <= FA_USABLE && n <= FA_ALLOCATED) {
        uintptr_t slot = base + idx * FA_SLOT;
        if (!readable4(slot)) break;
        idx = rd32(slot);
        n++;
    }
    return n;
}

static void fa_record(uintptr_t cell, unsigned live)
{
    unsigned i;
    for (i = 0; i < g_faCells; i++)
        if (g_fa[i].cell == cell) {
            g_fa[i].live = live;
            if (live > g_fa[i].peak) g_fa[i].peak = live;
            return;
        }
    if (g_faCells >= FA_CELLS) { g_faDropped++; return; }
    g_fa[g_faCells].cell = cell;
    g_fa[g_faCells].live = live;
    g_fa[g_faCells].peak = live;
    g_faCells++;
}

static void fa_sample(void)
{
    uintptr_t mgr, cell;
    unsigned  seen = 0;

    if (!readable4(FA_MGR)) return;
    mgr = rd32(FA_MGR);
    if (!mgr || !readable4(mgr + 0x48)) return;

    for (cell = rd32(mgr + 0x48); cell && seen < 4096u;
         cell = rd32(cell + 0x20)) {
        unsigned live;
        if (!readable4(cell + 0x18) || !readable4(cell + 0x20)) break;
        seen++;
        live = rd32(cell + 0x18);
        if (live > FA_ALLOCATED) continue;
        fa_record(cell, live);

        if (live > g_faPeakAll) {
            g_faPeakAll = live;
            fixlog("fastarea: new maximum %u of %u (%u%%) in cell %08X",
                   live, FA_USABLE, live * 100u / FA_USABLE, (unsigned)cell);
        }

        if (!g_faChecked && live) {
            unsigned freeN = fa_free_len(cell);
            g_faChecked = 1;
            fixlog("fastarea: cell %08X holds %u live and %u free, total %u,"
                   " expected %u - %s", (unsigned)cell, live, freeN,
                   live + freeN, FA_USABLE,
                   live + freeN == FA_USABLE
                     ? "the 2045 reading holds"
                     : "SOMETHING IS READ WRONG, treat the rest as suspect");
        }
    }
    if (seen > g_faCellsPeak) g_faCellsPeak = seen;

    if (g_faSamples++ % 10u == 0u) {
        unsigned i, total = 0;
        for (i = 0; i < g_faCells; i++) total += g_fa[i].live;
        fixlog("fastarea: %u cells, %u live in total, maximum so far %u of %u",
               seen, total, g_faPeakAll, FA_USABLE);
    }
}

static void fa_tick(void)
{
    if (!g_faWanted) return;
    if (++g_faTick % 1000u) return;
    fa_sample();
}

static void fa_report(void)
{
    unsigned i, worst = 0;

    if (!g_faWanted) return;
    fixlog("");
    if (!g_faCells) {
        fixlog("fast-object references: no area cell was ever initialised");
        return;
    }
    fixlog("fast-object references (2045 usable per 1024x1024 area cell):");
    fixlog("   cell      live   peak   peak %% of 2045");
    for (i = 0; i < g_faCells; i++) {
        fixlog("   %08X  %5u  %5u   %u%%", (unsigned)g_fa[i].cell,
               g_fa[i].live, g_fa[i].peak,
               g_fa[i].peak * 100u / FA_USABLE);
        if (g_fa[i].peak > worst) worst = g_fa[i].peak;
    }
    fixlog("   %u cells allocated at once at most; the busiest peaked at %u"
           " of %u", g_faCellsPeak, worst, FA_USABLE);
    if (g_faDropped)
        fixlog("   %u cells did not fit the table, so the rows above are a"
               " subset. The maximum is still whole - it is taken per cell as"
               " the list is walked, not from this table", g_faDropped);
    if (!g_faChecked)
        fixlog("   the free-list check never ran - no cell held a live object,"
               " so the 2045 reading is unverified this session");
}




static void dump_entity(const char *what, unsigned id, uintptr_t ent)
{
    unsigned i;
    char line[256];
    int n = 0;

    {
        uintptr_t tbl = readable4(0x008515D0u) ? rd32(0x008515D0u) : 0;
        uintptr_t slot = tbl ? tbl + id * 0x0Cu : 0;
        if (slot && readable4(slot) && readable4(slot + 8))
            fixlog("    %s id %u: slot %08X = [%08X %08X %08X]", what, id,
                   (unsigned)slot, rd32(slot), rd32(slot + 4), rd32(slot + 8));
        else
            fixlog("    %s id %u: slot not readable", what, id);
    }

    if (!ent || !readable4(ent)) {
        fixlog("      no entity - a handle to something already gone");
        return;
    }
    fixlog("    %s id %u at %08X: vtable %08X", what, id, (unsigned)ent,
           rd32(ent));
    for (i = 0; i < 0x30; i += 4) {
        if (!readable4(ent + i)) break;
        n += wsprintfA(line + n, "%08X ", rd32(ent + i));
        if (i % 0x10 != 0x0C) continue;
        fixlog("      +%02X  %s", i - 0x0C, line);
        n = 0;
    }
}

void u9probe_dump_entity(const char *what, unsigned id, uintptr_t ent)
{
    dump_entity(what, id, ent);
}

void u9probe_actor_tick(void *self)
{
    probe_actor(self);
    census_tick();
    fa_tick();
    u9npc_tick();
    u9actor_tick();
    u9mesh_tick();
    u9meshstat_tick();
    u9bird_tick();
    u9portal_tick();
    u9light_tick();
    u9flyer_tick();
    u9tele_actor_tick(self);
}

void u9probe_init(void)
{
    g_probeIdx = u9cfg_int("diag_probe_index", -1);
    if (g_probeIdx >= 0)
        fixlog("probe: watching actor index %d - its countdown, the six lists"
               " at +0x240 and the rebuild flag at +0x2BC", g_probeIdx);

    if (g_probeIdx >= 0 && u9cfg_int("diag_probe_deep", 0)) {
        attack_probe_install();
        decide_probe_install();
        sibling_probe_install();
    }

    g_diagTele = u9cfg_int("diag_teleport", 0);
    if (g_diagTele || u9cfg_int("diag_tele_watch", 0)) u9tele_install(g_diagTele);
    if (u9cfg_int("diag_path_watch", 0)) u9path_install();
    if (u9cfg_int("diag_bag_watch", 0)) u9bag_install();
    if (u9cfg_int("diag_bird_watch", 0) || u9cfg_int("diag_sound_watch", 0))
        u9bird_install();
    if (u9cfg_int("diag_npc_watch", 0)) u9npc_install();
    u9light_install();
    if (u9cfg_int("diag_travel_watch", 0)) u9travel_install();
    if (u9cfg_int("diag_trigger_watch", 0)) u9trig_install(0);
    else if (u9cfg_int("diag_shrine_watch", 0)) u9trig_install(1);
    u9player_watch_install();
    u9mesh_install();
    u9meshstat_install();

    g_dumpId = u9cfg_int("diag_dump_entity", -1);
    if (g_dumpId >= 0)
        fixlog("diag_dump_entity: watching entity id %d - reported whenever its"
               " pointer or +0x14 changes, so a transition is visible WITHIN"
               " one session", g_dumpId);

    g_censusWanted = u9cfg_int("diag_census", 0);
    if (g_censusWanted)
        fixlog("census: the actor table is sampled every 1200 ticks and the"
               " last snapshot is printed at exit");

    g_faWanted = u9cfg_int("diag_fastarea", 0) || g_diagTele;
    if (g_faWanted)
        fixlog("fastarea: sampled every 1000 actor ticks. A new maximum is"
               " printed as it happens, so the figure survives a crash before"
               " the exit report; 2045 usable per 1024x1024 area cell");
}

void u9probe_report(void)
{
    if (g_decFromCount) {
        unsigned i;
        fixlog("  probe %d: who called Actor_CombatDecision", g_probeIdx);
        for (i = 0; i < g_decFromCount; i++)
            fixlog("      from %08X: %u calls, %u returned STRIKE",
                   (unsigned)g_decFrom[i].addr, g_decFrom[i].calls,
                   g_decFrom[i].yes);
    }
    if (g_decCalls)
        fixlog("  probe %d DECISION: %u calls, %u with the countdown expired,"
               " %u said STRIKE", g_probeIdx, g_decCalls, g_decExpired,
               g_decYes);
    if (g_atkCalls)
        fixlog("  probe %d: attack call made %u times, succeeded %u",
               g_probeIdx, g_atkCalls, g_atkOk);
    if (o_sibA || o_sibB)
        fixlog("  probe %d siblings: 004DE090 said yes %u times,"
               " 004DE0E0 said yes %u times", g_probeIdx, g_sibAYes, g_sibBYes);
    if (g_probeIdx >= 0 && g_probeSeen) {
        fixlog("  probe %d: final list state %u/6 non-empty, %u nodes",
               g_probeIdx, g_probeLists, g_probeNodes);
        fixlog("  probe %d: seen on %u ticks, +0x174 changed %u times%s",
               g_probeIdx, g_probeTicks, g_probeLogged,
               (g_probeTicks && !g_probeLogged)
               ? " - NEVER, so it is not being decremented" : "");
    }
    u9tele_report();
    u9path_report();
    u9bag_report();
    u9bird_report();
    u9light_report();
    u9npc_report();
    u9actor_report();
    u9travel_report();
    u9trig_report();
    u9player_watch_report();
    u9mesh_report();
    u9meshstat_report();
    census_report();
    fa_report();
}

void u9probe_remove(void)
{
    if (o_decide || o_sibA || o_sibB) u9inline_remove_all();
    if (o_attack) {
        u9patch_revert(&g_patAtkA, 1);
        u9patch_revert(&g_patAtkB, 1);
    }
}
