#include <windows.h>
#include <stdint.h>
#include "log.h"
#include <string.h>
#include "diag.h"
#include "../injection/patch.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"

#ifndef U9CALL
#define U9CALL __attribute__((thiscall))
#endif

int u9cfg_int(const char *key, int def);

#define U9WATCH_MAX 4

typedef void (U9CALL *ThisFn)(void *self);

typedef struct {
    const char *name;
    U9Symbol    sym;
    uintptr_t   slotAddr;
    uintptr_t   expectFn;
    unsigned    offset;
    unsigned    count;
    unsigned    trace;
    unsigned    armHere;
    ThisFn      orig;
    U9PatchSpec spec;
    uint8_t     expectBytes[4];
    unsigned    calls;
    unsigned    bad;
    unsigned    unguarded;
    unsigned    reported;
    uintptr_t   lastActor;
    uintptr_t   dumped;
} SlotWatch;

static int suspect_dword(uint32_t v)
{
    if (v == 0) return 0;
    if (v < 0x10000u) return 1;
    if (!u9diag_u32(v)) return 1;
    return 0;
}

static void fmt_float(char *out, uint32_t v)
{
    uint32_t exp = (v >> 23) & 0xFFu;
    int neg = (int)(v >> 31);
    float f;
    int whole;
    unsigned frac;

    if (exp == 0xFFu) { lstrcpyA(out, "nan/inf"); return; }
    if (v == 0 || v == 0x80000000u) { lstrcpyA(out, "0"); return; }
    if (exp == 0) { lstrcpyA(out, "denormal"); return; }
    memcpy(&f, &v, 4);
    if (f < 0) f = -f;
    if (f < 1e-4f) { lstrcpyA(out, "~0"); return; }
    if (f >= 1e9f) { lstrcpyA(out, "huge"); return; }
    whole = (int)f;
    frac  = (unsigned)((f - (float)whole) * 10000.0f);
    if (frac > 9999u) frac = 9999u;
    wsprintfA(out, "%s%d.%04u", neg ? "-" : "", whole, frac);
}

static SlotWatch g_watch[U9WATCH_MAX] = {
    {
        .name = "Actor_Tick_PruneRefs@0x00749058",
        .sym  = U9SYM_ACTOR_TICK_PRUNE_SLOT_A,
        .slotAddr = 0x00749058u, .expectFn = 0x004DD0F0u,
        .offset = 0x284u, .count = 6u
    },
    {
        .name = "Actor_Tick_PruneRefs@0x00747F78",
        .sym  = U9SYM_ACTOR_TICK_PRUNE_SLOT_B,
        .slotAddr = 0x00747F78u, .expectFn = 0x004DD0F0u,
        .offset = 0x284u, .count = 6u
    },
    {
        .name = "Actor_ScheduleRebuild@0x00748FEC",
        .sym  = U9SYM_ACTOR_SCHEDULE_REBUILD_SLOT,
        .slotAddr = 0x00748FECu, .expectFn = 0x004DCFC0u,
        .offset = 0x284u, .count = 6u, .trace = 1
    },
    {
        .name = "Actor_RebuildStimulusTables@0x007490DC",
        .sym  = U9SYM_ACTOR_REBUILD_STIM_SLOT,
        .slotAddr = 0x007490DCu, .expectFn = 0x004DD000u,
        .offset = 0x284u, .count = 6u, .trace = 1, .armHere = 1
    },
};
static unsigned g_watchCount = 4;
#define U9ENQ_SEEN 512
static unsigned char g_enqIndex[U9ENQ_SEEN];
static unsigned char g_actorSeen[U9ENQ_SEEN];
static uint32_t      g_actorAt[U9ENQ_SEEN];
static unsigned char g_schedIndex[U9ENQ_SEEN];
static uint32_t      g_enqFlags[U9ENQ_SEEN];

#define U9_DRAIN_FLAG 0x008515ECu
static unsigned char g_enqDuringDrain[U9ENQ_SEEN];
static unsigned      g_enqThread[U9ENQ_SEEN];
static unsigned      g_schedThread[U9ENQ_SEEN];
static uint32_t      g_enqMgr[U9ENQ_SEEN];
static unsigned      g_enqFrame[U9ENQ_SEEN];

static int      g_skipBad;
static int      g_hwWanted;

#define U9WATCH_VTABLES 16
static struct { uint32_t vt; unsigned good, bad; } g_vt[U9WATCH_VTABLES];
static unsigned g_vtCount;

#define U9WATCH_OBJECTS 32
static struct {
    uintptr_t addr;
    uint32_t  vt;
    unsigned  good, bad;
    int       lastState;
    unsigned  transitions;
    uint32_t  x, y;
    unsigned  firstFrame, lastFrame;
    unsigned  g1Pass, g1Fail, g2NonZero, g2Zero;
    unsigned  stimuli;
} g_obj[U9WATCH_OBJECTS];
static unsigned g_objCount;
static unsigned g_objOverflow;

static uint32_t actor_type(uintptr_t obj)
{
    return u9diag_u32(obj + 0x18);
}

static void dump_descriptor(uintptr_t obj)
{
    uint32_t base = u9diag_u32(0x008515D0u);
    uint32_t idx  = u9diag_u32(obj + 0x18);
    uint32_t entry;
    unsigned i;

    if (!base || idx > 0xFFFFu) return;
    entry = u9diag_u32(base + 8 + idx * 0x0Cu);
    u9diag_line("  descriptor: table 0x%08X  index %u  ->  entry 0x%08X",
                base, idx, entry);
    for (i = 0; i < 3; i++)
        u9diag_line("     slot +0x%02X  %08X", i * 4,
                    u9diag_u32(base + 8 + idx * 0x0Cu + i * 4));
    if (!entry) return;
    for (i = 0; i < 12; i++) {
        uint32_t v = u9diag_u32(entry + i * 4);
        u9diag_line("     entry +0x%02X  %08X  %s", i * 4, v,
                    v == 0 ? "empty"
                    : v < 0x2000u ? "small - could be a type id"
                    : u9diag_u32(v) ? "-> readable" : "not readable");
    }
}

#define U9_NPC_TABLE 0x0098F31Cu
#define U9_NPC_STRIDE 0x13Cu

#define U9_ACTOR_TABLE 0x0098F324u
#define U9_ACTOR_SLOTS 0x200u

static int actor_npc_index(uintptr_t actor)
{
    uint32_t tbl = u9diag_u32(U9_ACTOR_TABLE);
    unsigned i;

    if (!tbl) return -2;
    for (i = 0; i < U9_ACTOR_SLOTS; i++)
        if (u9diag_u32(tbl + i * 4) == (uint32_t)actor)
            return (int)i;
    return -1;
}

static void log_npc_record(uintptr_t obj)
{
    uint32_t base = u9diag_u32(U9_NPC_TABLE);
    int      sidx = actor_npc_index(obj);
    uint32_t idx, rec, gate, flags;

    if (!base) { u9diag_line("  npc record: table not built yet"); return; }
    if (sidx == -2) {
        u9diag_line("  actor table global 0x%08X reads 0 - not built yet,"
                    " so nothing can be concluded about this actor",
                    U9_ACTOR_TABLE);
        return;
    }
    if (sidx < 0) {
        u9diag_line("  actor 0x%08X is NOT in the engine's actor table"
                    " (0x%08X) - FUN_004f7260 returns NULL for it, so the"
                    " scheduler at 0x004F2D84 is never reached",
                    (unsigned)obj, u9diag_u32(U9_ACTOR_TABLE));
        return;
    }
    idx   = (uint32_t)sidx;
    rec   = base + idx * U9_NPC_STRIDE;
    gate  = u9diag_u32(rec + 0xC4);
    flags = u9diag_u32(rec + 0x48);
    u9diag_line("  npc index %-4u record 0x%08X  +0xC4 %08X  +0x48 %08X"
                "  -> %s",
                idx, rec, gate, flags,
                (gate & 1u) ? "bit0 SET: FUN_004f29d0 skips its whole block,"
                              " so the scheduler is never reached"
                            : "bit0 clear: nothing here should stop scheduling");
}

static uint32_t actor_flags(uintptr_t obj)
{
    uint32_t base = u9diag_u32(0x008515D0u);
    uint32_t idx  = u9diag_u32(obj + 0x18);
    uint32_t entry;

    if (!base || idx > 0xFFFFu) return 0;
    entry = u9diag_u32(base + 8 + idx * 0x0Cu);
    if (!entry) return 0;
    return u9diag_u32(entry + 0x14);
}

static void note_object(uintptr_t addr, uint32_t vt, int bad)
{
    unsigned i;
    for (i = 0; i < g_objCount; i++) {
        if (g_obj[i].addr != addr) continue;
        if (bad) g_obj[i].bad++; else g_obj[i].good++;
        if (g_obj[i].lastState >= 0 && g_obj[i].lastState != bad) {
            g_obj[i].transitions++;
            if (g_obj[i].transitions <= 8)
                u9diag_line("TRANSITION obj 0x%08X went %s at frame %u"
                            "  (good %u, bad %u so far)",
                            (unsigned)addr, bad ? "GOOD -> BAD" : "BAD -> GOOD",
                            (unsigned)u9log_frame(), g_obj[i].good, g_obj[i].bad);
        }
        g_obj[i].lastState = bad;
        g_obj[i].lastFrame = (unsigned)u9log_frame();
        return;
    }
    if (g_objCount >= U9WATCH_OBJECTS) { g_objOverflow++; return; }
    {
        uint32_t x = u9diag_u32(addr + 0x48), y = u9diag_u32(addr + 0x4C);
        u9diag_line("OBJECT APPEARED 0x%08X  type %-5u flags 0x%08X"
                    "  first tick is %s  pos %u,%u  frame %u",
                    (unsigned)addr, actor_type(addr), actor_flags(addr),
                    bad ? "BAD" : "good", x, y, (unsigned)u9log_frame());
        g_obj[g_objCount].x = x;
        g_obj[g_objCount].y = y;
        log_npc_record(addr);
    }
    g_obj[g_objCount].addr = addr;
    g_obj[g_objCount].vt = vt;
    if (bad) g_obj[g_objCount].bad = 1; else g_obj[g_objCount].good = 1;
    g_obj[g_objCount].lastState = bad;
    g_obj[g_objCount].firstFrame = g_obj[g_objCount].lastFrame =
        (unsigned)u9log_frame();
    g_objCount++;
}

static void note_vtable(uint32_t vt, int bad)
{
    unsigned i;
    for (i = 0; i < g_vtCount; i++)
        if (g_vt[i].vt == vt) {
            if (bad) g_vt[i].bad++; else g_vt[i].good++;
            return;
        }
    if (g_vtCount < U9WATCH_VTABLES) {
        g_vt[g_vtCount].vt = vt;
        if (bad) g_vt[g_vtCount].bad = 1; else g_vt[g_vtCount].good = 1;
        g_vtCount++;
    }
}

static void log_chain_positions(uintptr_t start)
{
    uintptr_t p = start;
    unsigned i;
    uint32_t startVt = u9diag_vtable(start);

    u9diag_line("POS frame %u:", (unsigned)u9log_frame());
    for (i = 0; i < 12 && p; i++) {
        uint32_t x = u9diag_u32(p + 0x48);
        uint32_t y = u9diag_u32(p + 0x4C);
        uint32_t next = u9diag_u32(p + 0x04);
        u9diag_line("   [%u] obj 0x%08X  +0x48=%-8u +0x4C=%-8u%s",
                    i, (unsigned)p, x, y, p == start ? "   <- the watched one" : "");
        if (next == (uint32_t)start) break;
        if (u9diag_vtable(next) != startVt) break;
        p = next;
    }
}

static void report(SlotWatch *w, void *self)
{
    uintptr_t p = (uintptr_t)self;
    unsigned i;

    u9diag_line("");
    u9diag_line("WATCH %s: bad state at +0x%X", w->name, w->offset);
    u9diag_line("  this=0x%08X vtable=0x%08X  index %u  flags 0x%08X",
                (unsigned)p, u9diag_vtable(p), actor_type(p), actor_flags(p));
    log_npc_record(p);
    dump_descriptor(p);
    u9diag_line("  +0x2BC re-init pending = %u  %s",
                u9diag_u32(p + 0x2BC) & 0xFFu,
                (u9diag_u32(p + 0x2BC) & 0xFFu)
                    ? "the engine will zero these before the prune loop"
                    : "NOTHING WILL ZERO THESE");
    u9diag_line("  its own position: %u,%u",
                u9diag_u32(p + 0x48), u9diag_u32(p + 0x4C));
    {
        static const struct { unsigned off, n; const char *what; } part[] = {
            { 0x23Cu, 18u, "listheads" },
            { 0x284u,  6u, "CURRENT  " },
            { 0x29Cu,  6u, "previous " },
        };
        unsigned k;
        for (k = 0; k < 3; k++) {
            for (i = 0; i < part[k].n; i++) {
                uint32_t off = part[k].off + i * 4;
                uint32_t v = u9diag_u32(p + off);
                char fs[32];
                fmt_float(fs, v);
                u9diag_line("    +0x%03X %s [%u] %08X  as float %-14s %s",
                            off, part[k].what, i, v, fs,
                            v == 0 ? "empty"
                            : suspect_dword(v) ? "NOT A POINTER" : "-> readable");
            }
        }
    }
    u9diag_line("  %u ticks so far, %u of them bad", w->calls, w->bad);

    if (w->dumped != p) {
        w->dumped = p;
        u9diag_dump_object("  object", p, 48);
    }
}

static void watch_hit(unsigned idx, void *self, uintptr_t caller)
{
    SlotWatch *w = &g_watch[idx];
    unsigned i;
    int bad = 0;
    unsigned initPending = 0;

    w->calls++;
    if (!self) return;
    if (w->trace && w->calls <= 60)
        u9diag_line("%s on 0x%08X  index %-5u flags 0x%08X  frame %u"
                    "  (+0x284 %s)  called from 0x%08X",
                    w->name, (unsigned)(uintptr_t)self,
                    actor_type((uintptr_t)self), actor_flags((uintptr_t)self),
                    (unsigned)u9log_frame(),
                    suspect_dword(u9diag_u32((uintptr_t)self + 0x284))
                        ? "DIRTY" : "clean", (unsigned)caller);
    if (w->armHere && g_hwWanted && u9cfg_int("hw_arm_rebuild", 0)) {
        if (!u9hw_is_armed()) u9hw_arm((uintptr_t)self);
        else u9hw_follow((uintptr_t)self);
    }
    initPending = u9diag_u32((uintptr_t)self + 0x2BC) & 0xFFu;
    for (i = 0; i < w->count; i++)
        if (suspect_dword(u9diag_u32((uintptr_t)self + w->offset + i * 4))) {
            bad = 1;
            break;
        }
    {
        uint32_t vt = u9diag_vtable((uintptr_t)self);
        note_vtable(vt, bad);
        note_object((uintptr_t)self, vt, bad);
        if (g_hwWanted && u9hw_is_armed())
            u9hw_follow((uintptr_t)self);
    }
    if (!bad) {
        if ((uintptr_t)self == w->lastActor) w->lastActor = 0;
        return;
    }

    w->bad++;
    if (!initPending) {
        w->unguarded++;
        if (w->unguarded <= 20)
            u9diag_line("UNGUARDED obj 0x%08X frame %u: +0x284 is dirty and"
                        " +0x2BC is CLEAR - no re-init is pending, so nothing"
                        " will zero it before the prune loop runs",
                        (unsigned)(uintptr_t)self, (unsigned)u9log_frame());
    }
    if ((w->bad % 120) == 0) log_chain_positions((uintptr_t)self);

    if ((uintptr_t)self != w->lastActor || (w->bad % 600) == 0) {
        w->lastActor = (uintptr_t)self;
        if (w->reported < 40 || (w->bad % 600) == 0) {
            w->reported++;
            report(w, self);
        }
    }
}

#define WATCH_THUNK(i)                                                       \
    static void U9CALL h_watch##i(void *self)                                \
    {                                                                        \
        watch_hit(i, self, (uintptr_t)__builtin_return_address(0));          \
        if (g_skipBad && g_watch[i].lastActor == (uintptr_t)self) return;    \
        g_watch[i].orig(self);                                               \
    }
WATCH_THUNK(0)
WATCH_THUNK(1)
WATCH_THUNK(2)
WATCH_THUNK(3)
static const ThisFn g_thunks[U9WATCH_MAX] = { h_watch0, h_watch1, h_watch2, h_watch3 };

#define U9_POOL_FIRST 0x161u
#define U9_POOL_LAST  0x1FFu

static void log_pool_census(const char *when)
{
    uint32_t tbl = u9diag_u32(U9_ACTOR_TABLE);
    uint32_t npc = u9diag_u32(U9_NPC_TABLE);
    unsigned used = 0, i;

    if (!tbl) return;
    u9diag_line("");
    u9diag_line("pool census (%s): slots 0x%03X..0x%03X", when,
                U9_POOL_FIRST, U9_POOL_LAST);
    for (i = U9_POOL_FIRST; i <= U9_POOL_LAST; i++) {
        uint32_t actor = u9diag_u32(tbl + i * 4);
        uint32_t flags;
        if (!actor) continue;
        used++;
        flags = npc ? u9diag_u32(npc + i * U9_NPC_STRIDE + 0x48) : 0;
        if (used <= 24)
            u9diag_line("   slot %u  actor 0x%08X  record +0x48 %08X  pos %u,%u",
                        i, actor, flags,
                        u9diag_u32(actor + 0x48), u9diag_u32(actor + 0x4C));
    }
    u9diag_line("   %u of %u pool slots in use", used,
                U9_POOL_LAST - U9_POOL_FIRST + 1);
}

typedef void (U9CALL *SchedFn)(void *mgr, void *obj);
static SchedFn  o_sched;
static unsigned g_schedSeen;

static void U9CALL h_sched(void *mgr, void *obj)
{
    if (obj && g_schedSeen < 64) {
        uint32_t idx   = u9diag_u32((uintptr_t)obj + 0x08) >> 16;
        uint32_t f14   = u9diag_u32((uintptr_t)obj + 0x14);
        uint32_t base  = u9diag_u32(U9_NPC_TABLE);
        uint32_t gate  = base ? u9diag_u32(base + idx * U9_NPC_STRIDE + 0xC4) : 0;
        uint32_t flags = base ? u9diag_u32(base + idx * U9_NPC_STRIDE + 0x48) : 0;

        if (idx < U9ENQ_SEEN) {
            g_schedIndex[idx] = 1;
            g_schedThread[idx] = GetCurrentThreadId();
        }
        g_schedSeen++;
        u9diag_line("NPC PASS obj 0x%08X  index %-4u  +0x14 %08X (0x10 %s)"
                    "  record +0xC4 %08X (bit0 %s)  +0x48 %08X  frame %u",
                    (unsigned)(uintptr_t)obj, idx, f14,
                    (f14 & 0x10u) ? "set" : "CLEAR",
                    gate, (gate & 1u) ? "SET" : "clear", flags,
                    (unsigned)u9log_frame());
        if (!(f14 & 0x10u) || (gate & 1u))
            u9diag_line("   -> this one will NOT be scheduled");
    }
    o_sched(mgr, obj);
    {
        static unsigned lastCensus = 0xFFFFFFFFu;
        static unsigned bursts;
        unsigned now = (unsigned)u9log_frame();
        if (now != lastCensus) {
            char label[80];
            lastCensus = now;
            bursts++;
            wsprintfA(label, "scheduling burst %u, frame %u", bursts, now);
            log_pool_census(label);
        }
    }
}

typedef int (U9CALL *Gate1Fn)(void *self);
typedef int (U9CALL *Gate2Fn)(void *self, int a, int b);
static Gate1Fn o_gate1;
static Gate2Fn o_gate2;
static U9Patch g_gate1Patch, g_gate2Patch;

static void note_gate(uintptr_t addr, int which, int result)
{
    unsigned i;
    for (i = 0; i < g_objCount; i++) {
        if (g_obj[i].addr != addr) continue;
        if (which == 0) {
            if (result) g_obj[i].g1Pass++; else g_obj[i].g1Fail++;
        } else {
            if (result) g_obj[i].g2NonZero++; else g_obj[i].g2Zero++;
        }
        return;
    }
}

static int U9CALL h_gate1(void *self)
{
    int r = o_gate1(self);
    if (self) note_gate((uintptr_t)self, 0, r & 0xFF);
    return r;
}

static int U9CALL h_gate2(void *self, int a, int b)
{
    int r = o_gate2(self, a, b);
    if (self) note_gate((uintptr_t)self, 1, r);
    return r;
}

static void install_gates(void)
{
    if (u9patch_begin(&g_gate1Patch, (void *)0x00749024u, 1)) {
        o_gate1 = (Gate1Fn)u9patch_slot(&g_gate1Patch, 0, (void *)h_gate1);
        u9patch_end(&g_gate1Patch, 1);
        u9log_line("WATCH gate +0x11C installed (original 0x%08X)",
                   (unsigned)(uintptr_t)o_gate1);
    }
    if (u9patch_begin(&g_gate2Patch, (void *)0x00749030u, 1)) {
        o_gate2 = (Gate2Fn)u9patch_slot(&g_gate2Patch, 0, (void *)h_gate2);
        u9patch_end(&g_gate2Patch, 1);
        u9log_line("WATCH gate +0x128 installed (original 0x%08X)",
                   (unsigned)(uintptr_t)o_gate2);
    }
}

typedef void (U9CALL *RecFn)(void *self, void *obj);
static RecFn   o_record;
static U9Patch g_recPatch;
static unsigned g_recSeen;

static RecFn   o_record2;
static U9Patch g_rec2Patch;
static unsigned g_rec2Seen;

static void U9CALL h_record2(void *self, void *obj)
{
    if (self && g_rec2Seen < 40) {
        g_rec2Seen++;
        u9diag_line("STIMULUS(sibling) on 0x%08X  vtable 0x%08X  npc idx %u"
                    "  event 0x%08X  frame %u",
                    (unsigned)(uintptr_t)self, u9diag_vtable((uintptr_t)self),
                    u9diag_u32((uintptr_t)self + 0x08) >> 16,
                    (unsigned)(uintptr_t)obj, (unsigned)u9log_frame());
    }
    o_record2(self, obj);
}

static void U9CALL h_record(void *self, void *obj)
{
    if (self) {
        unsigned i;
        for (i = 0; i < g_objCount; i++)
            if (g_obj[i].addr == (uintptr_t)self) { g_obj[i].stimuli++; break; }
        if (g_recSeen < 40) {
            g_recSeen++;
            u9diag_line("STIMULUS to actor 0x%08X at %u,%u  event 0x%08X"
                        "  frame %u",
                        (unsigned)(uintptr_t)self, u9diag_u32((uintptr_t)self + 0x48),
                        u9diag_u32((uintptr_t)self + 0x4C),
                        (unsigned)(uintptr_t)obj, (unsigned)u9log_frame());
        }
    }
    o_record(self, obj);
}

#define U9TW_MAX 64
static struct { uintptr_t addr; uint32_t timer; unsigned frame; int garbage; }
    g_tw[U9TW_MAX];
static unsigned g_twCount, g_twWrites;

static int timer_implausible(uint32_t v)
{
    return v > 36000u;
}

static void track_timer(uintptr_t addr, uint32_t now, unsigned frame)
{
    unsigned i;
    for (i = 0; i < g_twCount; i++) {
        if (g_tw[i].addr != addr) continue;
        {
            uint32_t was = g_tw[i].timer;
            unsigned dt = frame - g_tw[i].frame;
            if (now != was) {
                int expected = (was > now) && ((was - now) <= dt + 2);
                if (!expected && g_twWrites < 40) {
                    g_twWrites++;
                    u9diag_line("TIMER WRITE obj 0x%08X  +0x174 %08X -> %08X"
                                "  over %u frames  (%s)",
                                (unsigned)addr, was, now, dt,
                                g_tw[i].garbage ? "was GARBAGE - a writer exists"
                                                : "was sane");
                }
            }
            g_tw[i].timer = now;
            g_tw[i].frame = frame;
        }
        return;
    }
    if (g_twCount >= U9TW_MAX) return;
    g_tw[g_twCount].addr = addr;
    g_tw[g_twCount].timer = now;
    g_tw[g_twCount].frame = frame;
    g_tw[g_twCount].garbage = timer_implausible(now);
    if (g_tw[g_twCount].garbage)
        u9diag_line("GARBAGE TIMER obj 0x%08X  +0x174 %08X (%u frames,"
                    " %u days) at frame %u", (unsigned)addr, now, now,
                    now / (60u * 60u * 24u), frame);
    g_twCount++;
}

void u9watch_stream(int frame)
{
    int every = u9cfg_int("stream_actors", 0);
    uint32_t tbl;
    unsigned i;

    if (every <= 0 || frame <= 0 || (frame % every) != 0) return;
    tbl = u9diag_u32(U9_ACTOR_TABLE);
    if (!tbl) return;

    for (i = 0; i < U9_ACTOR_SLOTS; i++) {
        uint32_t a = u9diag_u32(tbl + i * 4);
        uint32_t e[6];
        unsigned k, filled = 0;

        if (!a || !u9diag_vtable(a)) continue;
        for (k = 0; k < 6; k++) {
            e[k] = u9diag_u32(a + 0x284 + k * 4);
            if (e[k]) filled++;
        }
        if (i < U9ENQ_SEEN) {
            g_actorSeen[i] = 1;
            g_actorAt[i] = u9diag_u32(a + 0x48);
        }
        {
            uint32_t t = u9diag_u32(a + 0x174);
            track_timer(a, t, (unsigned)frame);
            if (g_hwWanted && !u9hw_is_armed() && timer_implausible(t)) {
                static const unsigned offs[4] = { 0x174u, 0x178u, 0x170u, 0x284u };
                u9hw_arm_at(a, offs, 4);
            }
        }
        u9log_line("ATBL f%-6d slot %-4u obj %08X vt %08X pos %u,%u flag %u"
                   " events %u/6 behav %08X detect %08X"
                   " init[%08X %08X %08X %08X]"
                   "  state %u mode %u t1 %d t2 %d"
                   "  %08X %08X %08X %08X %08X %08X",
                   frame, i, a, u9diag_vtable(a),
                   u9diag_u32(a + 0x48), u9diag_u32(a + 0x4C),
                   u9diag_u32(a + 0x2BC) & 0xFFu, filled,
                   u9diag_u32(a + 0x21C), u9diag_u32(a + 0x104),
                   u9diag_u32(a + 0x140), u9diag_u32(a + 0x170),
                   u9diag_u32(a + 0x174), u9diag_u32(a + 0x178),
                   u9diag_u32(a + 0x220), u9diag_u32(a + 0x224),
                   (int)u9diag_u32(a + 0x228), (int)u9diag_u32(a + 0x2B8),
                   e[0], e[1], e[2], e[3], e[4], e[5]);
    }
}

void u9watch_dump_all(void)
{
    uint32_t tbl = u9diag_u32(U9_ACTOR_TABLE);
    unsigned i, dumped = 0;
    int words = u9cfg_int("dump_words", 0x2C0 / 4);

    if (!tbl) {
        u9log_line("ODUMP: actor table not built yet");
        return;
    }
    u9log_line("ODUMP begin frame %u  %d dwords per actor",
               (unsigned)u9log_frame(), words);
    for (i = 0; i < U9_ACTOR_SLOTS; i++) {
        uint32_t a = u9diag_u32(tbl + i * 4);
        int k;

        if (!a || !u9diag_vtable(a)) continue;
        dumped++;
        u9log_line("ODUMP actor slot %u obj %08X vtable %08X pos %u,%u",
                   i, a, u9diag_vtable(a),
                   u9diag_u32(a + 0x48), u9diag_u32(a + 0x4C));
        for (k = 0; k < words; k += 8) {
            int n;
            char line[160];
            int at = wsprintfA(line, "ODUMP %08X +%03X ", a, k * 4);
            for (n = 0; n < 8 && k + n < words; n++)
                at += wsprintfA(line + at, "%08X ",
                                u9diag_u32(a + (uint32_t)(k + n) * 4));
            u9log_line("%s", line);
        }
    }
    u9log_line("ODUMP end, %u actors", dumped);
    u9diag_line("ODUMP: %u actors dumped to the render log at frame %u",
                dumped, (unsigned)u9log_frame());
}

void u9watch_revive(void)
{
    uint32_t tbl = u9diag_u32(U9_ACTOR_TABLE);
    unsigned i, seen = 0, dirty = 0;

    if (!tbl) {
        u9log_line("REVIVE: actor table not built yet");
        return;
    }
    u9diag_line("");
    u9diag_line("REVIVE requested at frame %u", (unsigned)u9log_frame());
    for (i = 0; i < U9_ACTOR_SLOTS; i++) {
        uint32_t a = u9diag_u32(tbl + i * 4);
        unsigned k;
        int bad = 0;

        unsigned emptyLists = 0;

        if (!a || !u9diag_vtable(a)) continue;
        seen++;
        for (k = 0; k < 6; k++)
            if (suspect_dword(u9diag_u32(a + 0x284 + k * 4))) { bad = 1; break; }

        for (k = 0; k < 6; k++) {
            uint32_t h = a + 0x23C + k * 12;
            if (!u9diag_u32(h + 4) && !u9diag_u32(h + 8)) emptyLists++;
        }

        if (!bad) {
            unsigned filled = 0;
            for (k = 0; k < 6; k++)
                if (u9diag_u32(a + 0x284 + k * 4)) filled++;
            u9diag_line("   slot %-4u actor 0x%08X at %u,%u  ok"
                        "  events recorded %u of 6  (%u of 6 behaviour lists"
                        " empty)%s",
                        i, a, u9diag_u32(a + 0x48), u9diag_u32(a + 0x4C),
                        filled, emptyLists,
                        filled ? "" : "   <- has perceived NOTHING");
            continue;
        }

        dirty++;
        *(volatile unsigned char *)(uintptr_t)(a + 0x2BC) = 1;
        u9diag_line("   slot %-4u actor 0x%08X at %u,%u  %s"
                    " (%u of 6 behaviour lists empty) - +0x2BC set",
                    i, a, u9diag_u32(a + 0x48), u9diag_u32(a + 0x4C),
                    "dirty tables", emptyLists);
    }
    u9diag_line("   %u actors in the table, %u were dirty and flagged",
                seen, dirty);
    u9diag_line("   NOTE: a flag is only consumed by an actor that TICKS."
                " An actor listed dirty on every press is one the engine is"
                " not ticking, so nothing reads +0x2BC for it.");
    u9log_line("REVIVE: %u actors, %u dirty and flagged for rebuild", seen, dirty);
}

#define U9_UNSCHED_SITE 0x004CC5F1u
#define U9_MODE_GLOBAL  0x00AD126Cu

typedef void (U9CALL *RegFn)(void *mgr, void *obj);
static RegFn    o_regUnsched;
static unsigned g_unschedSeen, g_unschedTotal;

static uint32_t mode_flag(void)
{
    uint32_t obj = u9diag_u32(U9_MODE_GLOBAL);
    return obj ? u9diag_u32(obj) : 0xFFFFFFFFu;
}

static void U9CALL h_reg_unsched(void *mgr, void *obj)
{
    uint32_t idx = obj ? (u9diag_u32((uintptr_t)obj + 0x08) >> 16) : 0xFFFF;

    g_unschedTotal++;
    if (g_unschedSeen < 40) {
        g_unschedSeen++;
        u9diag_line("UNSCHEDULED index %-4u at frame %-6u  mode flag %08X"
                    "  - registered WITHOUT initialisation",
                    idx, (unsigned)u9log_frame(), mode_flag());
    }
    o_regUnsched(mgr, obj);
}

typedef void (U9CALL *DrainFn)(void *mgr);
static DrainFn  o_drainA, o_drainB, o_drainC;
static unsigned g_drainRuns, g_drainConsumed, g_drainLogged;

static void drain_note(void *mgr, unsigned before, unsigned after, const char *site)
{
    g_drainRuns++;
    g_drainConsumed += before;
    if (g_drainLogged < 40) {
        g_drainLogged++;
        u9diag_line("DRAIN  from %s at frame %-6u  manager 0x%08X"
                    "  list %u -> %u", site, (unsigned)u9log_frame(),
                    (unsigned)(uintptr_t)mgr, before, after);
    }
}

static void drain_preview(void *mgr)
{
    unsigned count, i;

    if (!mgr) return;
    count = u9diag_u32((uintptr_t)mgr + 0x85C);
    if (!count || count > 512 || g_drainLogged >= 40) return;
    for (i = 0; i < count && i < 32; i++) {
        uint32_t e = u9diag_u32((uintptr_t)mgr + 0x5C + i * 4);
        uint32_t f14 = e ? u9diag_u32(e + 0x14) : 0;
        uint32_t idx = e ? (u9diag_u32(e + 0x08) >> 16) : 0xFFFF;
        u9diag_line("   entry %-2u index %-4u  +0x14 %08X  0x10 %s  ->  %s",
                    i, idx, f14, (f14 & 0x10u) ? "set" : "CLEAR",
                    (e && (f14 & 0x10u)) ? "will be scheduled"
                                         : "WILL BE SKIPPED by the drain");
    }
}

#define DRAIN_THUNK(letter, site)                                                static void U9CALL h_drain##letter(void *mgr)                                {                                                                                unsigned before = mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0;              if (before) drain_preview(mgr);              o_drain##letter(mgr);                                                        drain_note(mgr, before,                                                                 mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0, site);          }
DRAIN_THUNK(A, "0x004CBD75")
DRAIN_THUNK(B, "0x00573436")
DRAIN_THUNK(C, "0x00574F60")

#define U9_ENQUEUE_A 0x004CA369u
#define U9_ENQUEUE_B 0x004CBD53u

typedef void (U9CALL *EnqFn)(void *mgr, void *obj);
static EnqFn    o_enqA, o_enqB;
static unsigned g_enqSeen, g_enqAdded, g_enqBlocked;

static void enq_note(void *mgr, void *obj, unsigned before, unsigned after)
{
    uint32_t f14;

    if (!obj) return;
    f14 = u9diag_u32((uintptr_t)obj + 0x14);
    if (after > before) {
        g_enqAdded++;
        if (mgr) {
            uint32_t desc = u9diag_u32((uintptr_t)mgr + 0x5C + (after - 1) * 4);
            uint32_t idx  = desc ? (u9diag_u32(desc + 0x08) >> 16) : 0xFFFF;
            if (idx < U9ENQ_SEEN) {
                g_enqIndex[idx] = 1;
                g_enqFlags[idx] = u9diag_u32(desc + 0x14);
                g_enqDuringDrain[idx] =
                    (unsigned char)(u9diag_u32(U9_DRAIN_FLAG) & 0xFFu);
                g_enqThread[idx] = GetCurrentThreadId();
                g_enqMgr[idx] = (uint32_t)(uintptr_t)mgr;
                g_enqFrame[idx] = (unsigned)u9log_frame();
            }
            if (g_enqSeen < 60)
                u9diag_line("ENQUEUE index %u queued (descriptor 0x%08X,"
                            " +0x14 %08X)", idx, desc, f14);
        }
    } else g_enqBlocked++;
    if (g_enqSeen < 60) {
        g_enqSeen++;
        u9diag_line("ENQUEUE obj 0x%08X  +0x14 %08X  bit 0x40000 %s  ->  %s"
                    "  (list %u -> %u)",
                    (unsigned)(uintptr_t)obj, f14,
                    (f14 & 0x40000u) ? "SET" : "clear",
                    (after > before) ? "queued" : "not queued (usually: not an NPC)",
                    before, after);
    }
}

static void U9CALL h_enqA(void *mgr, void *obj)
{
    unsigned before = mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0;
    o_enqA(mgr, obj);
    enq_note(mgr, obj, before, mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0);
}

static void U9CALL h_enqB(void *mgr, void *obj)
{
    unsigned before = mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0;
    o_enqB(mgr, obj);
    enq_note(mgr, obj, before, mgr ? u9diag_u32((uintptr_t)mgr + 0x85C) : 0);
}

#define U9FREE_LOG 256
static struct { uintptr_t addr; unsigned elem, count, frame; } g_freed[U9FREE_LOG];
static unsigned g_freedAt, g_freedTotal;

typedef void (__cdecl *FreeFn)(void *p);
static FreeFn  o_free60a, o_free60b, o_free18a, o_free18b;

static void note_free(void *p, unsigned elem)
{
    unsigned count;
    if (!p) return;
    count = u9diag_u32((uintptr_t)p);
    g_freed[g_freedAt].addr  = (uintptr_t)p;
    g_freed[g_freedAt].elem  = elem;
    g_freed[g_freedAt].count = count;
    g_freed[g_freedAt].frame = (unsigned)u9log_frame();
    g_freedAt = (g_freedAt + 1) % U9FREE_LOG;
    g_freedTotal++;
    if (g_freedTotal <= 40)
        u9log_line("MFREE block %08X  %u elements of 0x%X  = %u bytes  frame %u",
                   (unsigned)(uintptr_t)p, count, elem, 4 + count * elem,
                   (unsigned)u9log_frame());
}

static void __cdecl h_free60a(void *p) { note_free(p, 0x60); o_free60a(p); }
static void __cdecl h_free60b(void *p) { note_free(p, 0x60); o_free60b(p); }
static void __cdecl h_free18a(void *p) { note_free(p, 0x18); o_free18a(p); }
static void __cdecl h_free18b(void *p) { note_free(p, 0x18); o_free18b(p); }

static int freed_as(uintptr_t addr, unsigned *elem, unsigned *count, unsigned *frame)
{
    unsigned i;
    for (i = 0; i < U9FREE_LOG; i++)
        if (g_freed[i].addr == addr) {
            *elem = g_freed[i].elem; *count = g_freed[i].count;
            *frame = g_freed[i].frame;
            return 1;
        }
    return 0;
}

#define U9_OP_DELETE 0x007108E0u

typedef void (__cdecl *DelFn)(void *p);
static DelFn    o_delete;
static unsigned g_delSeen;

#define U9FREE_BITS 17
#define U9FREE_SLOTS (1u << U9FREE_BITS)
static struct { uintptr_t addr, caller; unsigned frame; } g_anyFree[U9FREE_SLOTS];

static unsigned free_slot(uintptr_t addr)
{
    uintptr_t h = addr >> 3;
    h ^= h >> U9FREE_BITS;
    return (unsigned)(h & (U9FREE_SLOTS - 1));
}

static void __cdecl h_delete(void *p)
{
    if (p) {
        unsigned k = free_slot((uintptr_t)p);
        g_anyFree[k].addr   = (uintptr_t)p;
        g_anyFree[k].caller = (uintptr_t)__builtin_return_address(0);
        g_anyFree[k].frame  = (unsigned)u9log_frame();
        g_delSeen++;
    }
    o_delete(p);
}

static int freed_by(uintptr_t addr, uintptr_t *caller, unsigned *frame)
{
    unsigned k = free_slot(addr);
    if (g_anyFree[k].addr != addr) return 0;
    *caller = g_anyFree[k].caller;
    *frame  = g_anyFree[k].frame;
    return 1;
}

#define U9_ACTOR_NEW_SITE 0x004F6250u

typedef void *(__cdecl *NewFn)(unsigned size);
static NewFn    o_actorNew;
static unsigned g_newSeen;

static void *__cdecl h_actor_new(unsigned size)
{
    void *p = o_actorNew(size);
    int cap = u9cfg_int("dump_new_blocks", 0);

    if (p && (int)g_newSeen < cap) {
        const uint32_t *w = (const uint32_t *)p;
        unsigned i, nonzero = 0, words = size / 4;
        int32_t timer;

        unsigned el, ct, fr;
        g_newSeen++;
        for (i = 0; i < words; i++) if (w[i]) nonzero++;
        timer = (int32_t)w[0x174 / 4];
        {
            uintptr_t who; unsigned wf;
            if (freed_by((uintptr_t)p, &who, &wf))
                u9log_line("OWNER  block %08X last freed at frame %u by the"
                           " caller at 0x%08X", (unsigned)(uintptr_t)p, wf,
                           (unsigned)who);
            else
                u9log_line("OWNER  block %08X has no recorded free - fresh"
                           " memory", (unsigned)(uintptr_t)p);
        }
        if (freed_as((uintptr_t)p, &el, &ct, &fr))
            u9log_line("ATTRIB block %08X was freed at frame %u as %u elements"
                       " of 0x%X (%u bytes)%s",
                       (unsigned)(uintptr_t)p, fr, ct, el, 4 + ct * el,
                       el == 0x18 ? "  - A MATERIAL ARRAY" : "");
        u9log_line("ANEW #%u block %08X size %u  %u of %u words non-zero"
                   "  +0x174 would be %d (%s)",
                   g_newSeen, (unsigned)(uintptr_t)p, size, nonzero, words,
                   timer,
                   timer == 0 ? "clean"
                   : timer < 0 ? "NEGATIVE - would never count down"
                   : timer > 36000 ? "huge - months to expire" : "plausible");
        for (i = 0; i < words; i += 8) {
            char line[160];
            unsigned n, at = wsprintfA(line, "ANEW %08X +%03X ",
                                       (unsigned)(uintptr_t)p, i * 4);
            for (n = 0; n < 8 && i + n < words; n++)
                at += wsprintfA(line + at, "%08X ", w[i + n]);
            u9log_line("%s", line);
        }
    }
    return p;
}

void u9watch_install(void)
{
    unsigned i;

    if (!u9cfg_int("watch_slots", 1)) {
        u9log_line("WATCH disabled by config");
        return;
    }
    g_skipBad = u9cfg_int("watch_skip_bad", 0);
    g_hwWanted = u9cfg_int("watch_hw", 0);

    if (u9cfg_int("watch_gates", 1)) install_gates();

    if (u9cfg_int("watch_unsched", 1)) {
        o_regUnsched = (RegFn)u9call_patch(U9_UNSCHED_SITE,
                                           (void *)h_reg_unsched);
        u9log_line("WATCH unscheduled register path: %s  (mode flag now %08X)",
                   o_regUnsched ? "hooked" : "FAILED", mode_flag());
    }

    if (u9cfg_int("watch_drain", 1)) {
        o_drainA = (DrainFn)u9call_patch(0x004CBD75u, (void *)h_drainA);
        o_drainB = (DrainFn)u9call_patch(0x00573436u, (void *)h_drainB);
        o_drainC = (DrainFn)u9call_patch(0x00574F60u, (void *)h_drainC);
        u9log_line("WATCH drain sites: %s / %s / %s",
                   o_drainA ? "hooked" : "FAILED",
                   o_drainB ? "hooked" : "FAILED",
                   o_drainC ? "hooked" : "FAILED");
    }

    if (u9cfg_int("watch_enqueue", 1)) {
        o_enqA = (EnqFn)u9call_patch(U9_ENQUEUE_A, (void *)h_enqA);
        o_enqB = (EnqFn)u9call_patch(U9_ENQUEUE_B, (void *)h_enqB);
        u9log_line("WATCH enqueue sites: %s / %s",
                   o_enqA ? "hooked" : "FAILED", o_enqB ? "hooked" : "FAILED");
    }

    if (u9cfg_int("watch_delete", 1)) {
        static const unsigned char expect[5] = { 0x8B, 0x44, 0x24, 0x04, 0x50 };
        o_delete = (DelFn)u9inline_hook(U9_OP_DELETE, (void *)h_delete, 5, expect);
        if (!o_delete) u9log_line("WATCH operator delete not hooked");
    }

    if (u9cfg_int("watch_free", 1)) {
        struct { uintptr_t site; FreeFn *orig; void *thunk; } f[] = {
            { 0x00576062u, &o_free60a, (void *)h_free60a },
            { 0x00576086u, &o_free60b, (void *)h_free60b },
            { 0x005760AAu, &o_free18a, (void *)h_free18a },
            { 0x005760CEu, &o_free18b, (void *)h_free18b },
        };
        unsigned k;
        for (k = 0; k < 4; k++)
            *f[k].orig = (FreeFn)u9call_patch(f[k].site, f[k].thunk);
    }

    if (u9cfg_int("watch_alloc", 1)) {
        o_actorNew = (NewFn)u9call_patch(U9_ACTOR_NEW_SITE, (void *)h_actor_new);
        if (!o_actorNew)
            u9log_line("WATCH could not redirect the actor allocation site");
    }

    if (u9cfg_int("watch_stimulus", 1) &&
        u9patch_begin(&g_recPatch, (void *)0x007490D8u, 1)) {
        o_record = (RecFn)u9patch_slot(&g_recPatch, 0, (void *)h_record);
        u9patch_end(&g_recPatch, 1);
        u9log_line("WATCH stimulus recorder installed (original 0x%08X)",
                   (unsigned)(uintptr_t)o_record);
    }

    if (u9cfg_int("watch_stimulus", 1) &&
        u9patch_begin(&g_rec2Patch, (void *)0x00747FF8u, 1)) {
        o_record2 = (RecFn)u9patch_slot(&g_rec2Patch, 0, (void *)h_record2);
        u9patch_end(&g_rec2Patch, 1);
        u9log_line("WATCH sibling recorder installed (original 0x%08X)",
                   (unsigned)(uintptr_t)o_record2);
    }

    if (u9cfg_int("watch_sched", 1)) {
        o_sched = (SchedFn)u9call_patch(0x004CC5E0u, (void *)h_sched);
        if (!o_sched)
            u9log_line("WATCH could not redirect the scheduling call site");
    }

    for (i = 0; i < g_watchCount && i < U9WATCH_MAX; i++) {
        SlotWatch *w = &g_watch[i];
        w->expectBytes[0] = (uint8_t)(w->expectFn      );
        w->expectBytes[1] = (uint8_t)(w->expectFn >>  8);
        w->expectBytes[2] = (uint8_t)(w->expectFn >> 16);
        w->expectBytes[3] = (uint8_t)(w->expectFn >> 24);

        w->spec.name        = w->name;
        w->spec.mechanism   = U9PATCH_VTABLE;
        w->spec.target      = w->sym;
        w->spec.length      = 4;
        w->spec.expected    = w->expectBytes;
        w->spec.expectedLen = 4;
        w->spec.flags       = U9PF_EXCLUSIVE;

        w->orig = (ThisFn)u9patch_install(&w->spec, (void *)g_thunks[i]);
        if (!w->orig) continue;

        u9log_line("WATCH %s installed (original 0x%08X)%s", w->name,
                   (unsigned)(uintptr_t)w->orig,
                   g_skipBad ? "  SKIPPING bad ticks" : "");
        u9diag_line("watch installed: %s%s", w->name,
                    g_skipBad ? "  (will skip bad ticks)" : "  (observe only)");
    }
}

void u9watch_report(void)
{
    unsigned i;

    log_pool_census("at exit");

    if (g_vtCount) {
        u9diag_line("");
        u9diag_line("watch: ticks by vtable (good = the six dwords all look like"
                    " pointers or zero)");
        for (i = 0; i < g_vtCount; i++)
            u9diag_line("  vtable 0x%08X  good %-8u bad %u",
                        g_vt[i].vt, g_vt[i].good, g_vt[i].bad);
    }

    if (g_twCount) {
        unsigned i, garbage = 0;
        for (i = 0; i < g_twCount; i++) if (g_tw[i].garbage) garbage++;
        u9diag_line("");
        u9diag_line("timer watch: %u actors tracked, %u had a garbage +0x174,"
                    " %u unexplained writes seen",
                    g_twCount, garbage, g_twWrites);
        if (garbage && !g_twWrites)
            u9diag_line("   nothing ever wrote a garbage timer: consistent with"
                        " no writer existing, the field simply never being"
                        " initialised");
    }

    if (g_objCount) {
        unsigned mixed = 0;
        u9diag_line("");
        u9diag_line("watch: ticks by OBJECT - an object with BOTH good and bad"
                    " ticks means the field changes state");
        for (i = 0; i < g_objCount; i++) {
            int both = (g_obj[i].good && g_obj[i].bad);
            if (both) mixed++;
            u9diag_line("  obj 0x%08X pos %u,%u  good %-7u bad %-7u"
                        " transitions %u  frames %u-%u%s",
                        (unsigned)g_obj[i].addr, g_obj[i].x, g_obj[i].y,
                        g_obj[i].good, g_obj[i].bad, g_obj[i].transitions,
                        g_obj[i].firstFrame, g_obj[i].lastFrame,
                        both ? "   <- BOTH" : "");
            u9diag_line("      stimuli recorded on it: %u", g_obj[i].stimuli);
            u9diag_line("      gate +0x11C pass %-6u fail %-6u |"
                        " gate +0x128 nonzero %-6u zero %-6u  %s",
                        g_obj[i].g1Pass, g_obj[i].g1Fail,
                        g_obj[i].g2NonZero, g_obj[i].g2Zero,
                        g_obj[i].g1Fail && !g_obj[i].g1Pass
                            ? "<- ALWAYS gated out at the first call"
                            : (g_obj[i].g2Zero && !g_obj[i].g2NonZero)
                              ? "<- never reaches the prune loop" : "");
        }

        for (i = 0; i < g_objCount; i++) {
            unsigned j, inc = 0, gi = 0, bi = 0;
            for (j = 0; j < g_objCount; j++) {
                if (g_obj[j].x != g_obj[i].x || g_obj[j].y != g_obj[i].y)
                    continue;
                if (j < i) break;
                inc++;
                if (g_obj[j].bad) bi++; else gi++;
            }
            if (j < i || inc < 2) continue;
            u9diag_line("  pos %u,%u appeared as %u separate objects:"
                        " %u bad, %u good%s", g_obj[i].x, g_obj[i].y, inc,
                        bi, gi,
                        (bi && gi) ? "   <- SAME spot, BOTH outcomes"
                                   : "   (consistent across incarnations)");
        }
        if (mixed)
            u9diag_line("  %u of %u objects were seen BOTH good and bad"
                        "  => the field CHANGES state", mixed, g_objCount);
        else if (g_objCount >= 4)
            u9diag_line("  0 of %u objects were mixed - each is uniformly one"
                        " or the other, which points at layout", g_objCount);
        else
            u9diag_line("  0 of %u objects were mixed - NO VERDICT, too few"
                        " objects. Need 4 or more, or one mixed object.",
                        g_objCount);
        if (g_objOverflow)
            u9diag_line("  %u ticks on objects past the %u-object table",
                        g_objOverflow, (unsigned)U9WATCH_OBJECTS);
    }
    if (g_enqAdded || g_enqBlocked) {
        uint32_t tbl = u9diag_u32(U9_ACTOR_TABLE);
        unsigned i, live = 0, missed = 0;
        u9diag_line("");
        u9diag_line("enqueue: %u objects queued, %u not (most are not NPCs)",
                    g_enqAdded, g_enqBlocked);
        (void)tbl;
        for (i = 0; i < U9ENQ_SEEN; i++) {
            if (!g_actorSeen[i]) continue;
            live++;
            if (!g_enqIndex[i]) {
                missed++;
                u9diag_line("   actor slot %-4u was seen (x=%u) but its index"
                            " was NEVER queued", i, g_actorAt[i]);
            }
        }
        u9diag_line("   %u actors seen, %u of them never queued", live, missed);

        {
            unsigned q = 0, dropped = 0;
            for (i = 0; i < U9ENQ_SEEN; i++) {
                if (!g_enqIndex[i]) continue;
                q++;
                if (!g_schedIndex[i]) {
                    dropped++;
                    u9diag_line("   index %-4u queued at frame %-6u"
                                " (manager 0x%08X, drain %s)  NEVER scheduled",
                                i, g_enqFrame[i], g_enqMgr[i],
                                g_enqDuringDrain[i] ? "running" : "idle");
                }
            }
            u9diag_line("   %u indices queued, %u never reached the scheduler",
                        q, dropped);
            {
                unsigned dq = 0, sq = 0, threads = 0, first = 0;
                for (i = 0; i < U9ENQ_SEEN; i++) {
                    if (!g_enqIndex[i]) continue;
                    if (g_enqDuringDrain[i]) dq++;
                    if (g_schedIndex[i]) sq++;
                    if (!first) first = g_enqThread[i];
                    else if (g_enqThread[i] != first) threads++;
                }
                u9diag_line("   %u of them were queued while the drain was"
                            " running; %u reached the scheduler", dq, sq);
                {
                    uint32_t mOK = 0, mBad = 0;
                    int mixed = 0;
                    for (i = 0; i < U9ENQ_SEEN; i++) {
                        if (!g_enqIndex[i]) continue;
                        if (g_schedIndex[i]) {
                            if (!mOK) mOK = g_enqMgr[i];
                            else if (mOK != g_enqMgr[i]) mixed = 1;
                        } else {
                            if (!mBad) mBad = g_enqMgr[i];
                            else if (mBad != g_enqMgr[i]) mixed = 1;
                        }
                    }
                    u9diag_line("   manager for scheduled indices 0x%08X,"
                                " for dropped indices 0x%08X  -  %s",
                                mOK, mBad,
                                mixed ? "MIXED, more than one each"
                                : (mOK && mBad && mOK != mBad)
                                    ? "DIFFERENT MANAGERS: the drain never sees"
                                      " the dropped ones"
                                    : "the same manager");
                }
                u9diag_line("   drains: %u runs, %u entries consumed",
                            g_drainRuns, g_drainConsumed);
                u9diag_line("   %u actors took the UNSCHEDULED path"
                            " - registered without a rebuild, which the load"
                            " path declines on purpose; see Actor_NeedsRebuild",
                            g_unschedTotal);
                u9diag_line("   enqueue threads: %s",
                            threads ? "MORE THAN ONE - the drain is not"
                                      " single threaded"
                                    : "all the same thread");
            }
        }
        if (live && !missed)
            u9diag_line("   every actor seen was queued at some point - the"
                        " omission is AFTER the queue, in the drain");
    }

    if (g_delSeen)
        u9diag_line("operator delete saw %u frees this session", g_delSeen);

    for (i = 0; i < g_watchCount; i++)
        if (g_watch[i].orig && g_watch[i].calls)
            u9diag_line("watch %s: %u ticks, %u bad, %u of those UNGUARDED"
                        " (dirty with no re-init pending)", g_watch[i].name,
                        g_watch[i].calls, g_watch[i].bad, g_watch[i].unguarded);
}

void u9watch_remove(void)
{
    unsigned i;
    u9hw_disarm();
    u9call_revert_all();
    u9inline_remove_all();
    if (o_record) u9patch_revert(&g_recPatch, 1);
    if (o_record2) u9patch_revert(&g_rec2Patch, 1);
    if (o_gate1) u9patch_revert(&g_gate1Patch, 1);
    if (o_gate2) u9patch_revert(&g_gate2Patch, 1);
    (void)i;
}
