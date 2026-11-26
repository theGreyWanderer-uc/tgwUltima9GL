#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../injection/patch.h"
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../common/lifecycle.h"
#include "fixmem.h"
#include "fixes.h"
#include "spellguard.h"
#include "clockfix.h"
#include "flyerfix.h"
#include "doorfix.h"
#include "portalfix.h"
#include "shrinewatch.h"
#include "chasegrid.h"
#include "breathfix.h"
#include "diagmove.h"
#include "mousefix.h"
#include "lettbook.h"
#include "gold.h"
#include "../common/buildid.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

static void (*g_tickObserver)(void *self);

void u9fix_set_tick_observer(void (*fn)(void *self))
{
    g_tickObserver = fn;
}

static void (*g_entityDumper)(const char *what, unsigned id, uintptr_t ent);

void u9fix_set_entity_dumper(void (*fn)(const char *, unsigned, uintptr_t))
{
    g_entityDumper = fn;
}

typedef struct {
    const char *name;
    LONGLONG    ticks;
    unsigned    calls;
    unsigned    acted;
} FixCost;

static LARGE_INTEGER g_qpf;

static void cost_report(const FixCost *c)
{
    double ms, us;

    if (!c->calls) {
        u9log_line("  %s: never ran", c->name);
        return;
    }
    ms = (double)c->ticks * 1000.0 / (double)g_qpf.QuadPart;
    us = ms * 1000.0 / (double)c->calls;
    {
        char a[32], b[32];
        wsprintfA(a, "%d.%03d", (int)ms, (int)((ms - (int)ms) * 1000));
        wsprintfA(b, "%d.%03d", (int)us, (int)((us - (int)us) * 1000));
        u9log_line("  %s: %u calls, %u acted, %s ms total, %s us per call",
               c->name, c->calls, c->acted, a, b);
    }
}

#define F3_FIELD    0x284u

typedef void (U9CALL *TickFn)(void *self);
static TickFn  g_origA, g_origB;
static FixCost g_f3 = { "F-3 stale reference sanitiser", 0, 0, 0 };
static unsigned g_f3Reported, g_f3Guarded;

static int entry_survivable(uint32_t e)
{
    uint32_t inner;

    if (e == 0) return 1;
    if (!readable4((uintptr_t)e + 8)) return 0;
    inner = *(const uint32_t *)((uintptr_t)e + 8);
    return readable4((uintptr_t)inner);
}

static void sanitise(void *self)
{
    if (g_tickObserver) g_tickObserver(self);
    LARGE_INTEGER t0, t1;
    uint32_t *slot;
    unsigned i, fixed = 0;

    if (!self) return;
    if (*(const volatile unsigned char *)((uintptr_t)self + 0x2BC)) {
        g_f3Guarded++;
        return;
    }
    QueryPerformanceCounter(&t0);
    slot = (uint32_t *)((uintptr_t)self + F3_FIELD);
    for (i = 0; i < 6; i++) {
        if (entry_survivable(slot[i])) continue;
        if (g_f3Reported < 20) {
            g_f3Reported++;
            u9log_line("F-3 actor 0x%08X entry %u held 0x%08X, not a usable"
                   " reference, and +0x2BC is CLEAR so nothing was going to"
                   " zero it - cleared", (unsigned)(uintptr_t)self, i, slot[i]);
        }
        slot[i] = 0;
        fixed++;
    }
    QueryPerformanceCounter(&t1);
    g_f3.ticks += t1.QuadPart - t0.QuadPart;
    g_f3.calls++;
    if (fixed) g_f3.acted++;
}

static void U9CALL h_tickA(void *self) { sanitise(self); g_origA(self); }
static void U9CALL h_tickB(void *self) { sanitise(self); g_origB(self); }

static const uint8_t F3_EXPECT_BYTES[4] = { 0xF0, 0xD0, 0x4D, 0x00 };

static const U9PatchSpec F3_SPEC_A = {
    "F3_SANITISE_A", U9PATCH_VTABLE, U9SYM_ACTOR_TICK_PRUNE_SLOT_A,
    4, F3_EXPECT_BYTES, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec F3_SPEC_B = {
    "F3_SANITISE_B", U9PATCH_VTABLE, U9SYM_ACTOR_TICK_PRUNE_SLOT_B,
    4, F3_EXPECT_BYTES, 4, U9PF_EXCLUSIVE
};

static void f3_install(void)
{
    g_origA = (TickFn)u9patch_install(&F3_SPEC_A, (void *)h_tickA);
    g_origB = (TickFn)u9patch_install(&F3_SPEC_B, (void *)h_tickB);
}

#define ACTOR_SIZE        0x2C0u

typedef void *(__cdecl *NewFn)(unsigned size);
static NewFn   o_newBase, o_newDerived;
static unsigned g_initFrom, g_initTo;
static FixCost g_init = { "actor initialisation (zero on allocation)", 0, 0, 0 };
static unsigned g_initReported, g_initDirty, g_initBadTimer, g_initBadRefs;

static void *zero_actor(void *p, unsigned size)
{
    LARGE_INTEGER t0, t1;
    const uint32_t *w = (const uint32_t *)p;
    unsigned i, nonzero = 0;
    int32_t timer;
    int badRefs = 0;

    if (!p) return p;
    QueryPerformanceCounter(&t0);

    for (i = 0; i < size / 4; i++) if (w[i]) nonzero++;
    timer = (int32_t)w[0x174 / 4];
    for (i = 0; i < 6; i++) if (w[(0x284 / 4) + i]) badRefs = 1;

    if (nonzero) g_initDirty++;
    if (timer < 0 || timer > 36000) g_initBadTimer++;
    if (badRefs) g_initBadRefs++;

    {
        unsigned lo = g_initFrom, hi = g_initTo;
        if (hi > size) hi = size;
        if (lo < hi) memset((char *)p + lo, 0, hi - lo);
    }
    QueryPerformanceCounter(&t1);
    g_init.ticks += t1.QuadPart - t0.QuadPart;

    if (g_initReported < 20 && (badRefs || timer < 0 || timer > 36000)) {
        g_initReported++;
        u9log_line("actor block %08X arrived with %u of %u words set;"
               " +0x174 %d (%s), +0x284 %s; cleared +0x%X..+0x%X",
               (unsigned)(uintptr_t)p, nonzero, size / 4, timer,
               timer == 0 ? "fine" : timer < 0 ? "NEGATIVE, would never expire"
               : timer > 36000 ? "huge, months to expire" : "plausible",
               badRefs ? "held non-null references" : "clear",
               g_initFrom, g_initTo);
    }
    g_init.calls++;
    if (nonzero) g_init.acted++;
    return p;
}

static void *__cdecl h_new_base(unsigned size)
{
    return zero_actor(o_newBase(size), size);
}

static void *__cdecl h_new_derived(unsigned size)
{
    return zero_actor(o_newDerived(size), size);
}

static const uint8_t CALL_E8[1] = { 0xE8 };

static const U9PatchSpec INIT_SPEC_BASE = {
    "F4_ZERO_BASE", U9PATCH_CALLSITE, U9SYM_ACTOR_NEW_CALL_BASE,
    5, CALL_E8, 1, U9PF_EXCLUSIVE
};
static const U9PatchSpec INIT_SPEC_DERIVED = {
    "F4_ZERO_DERIV", U9PATCH_CALLSITE, U9SYM_ACTOR_NEW_CALL_DERIVED,
    5, CALL_E8, 1, U9PF_EXCLUSIVE
};

static void init_install(void)
{
    g_initFrom = (unsigned)u9cfg_int("fix_actor_init_from", 0x174);
    g_initTo   = (unsigned)u9cfg_int("fix_actor_init_to", 0x17C);
    if (g_initTo > ACTOR_SIZE) g_initTo = ACTOR_SIZE;
    u9log_line("actor initialisation: zeroing +0x%X..+0x%X of 0x%X",
           g_initFrom, g_initTo, ACTOR_SIZE);
    o_newBase = (NewFn)u9patch_install(&INIT_SPEC_BASE, (void *)h_new_base);
    o_newDerived = (NewFn)u9patch_install(&INIT_SPEC_DERIVED,
                                          (void *)h_new_derived);
}

typedef void (U9CALL *ActorCreateFn)(void *table, int index);
static ActorCreateFn o_actorCreate;
static unsigned g_acCalls, g_acCleared, g_acGarbage, g_acLogged, g_acSetupFixed, g_acSetupLogged;

static unsigned repair_setup(uintptr_t act, char *what, size_t n)
{
    unsigned fixed = 0;
    size_t len = 0;
    int32_t charmer = (int32_t)rd32(act + 0x140), wander = (int32_t)rd32(act + 0x168),
            reversing = (int32_t)rd32(act + 0x16C);
    uint8_t home = *(volatile uint8_t *)(act + 0x170), sight = *(volatile uint8_t *)(act + 0x171);
    what[0] = 0;
    if (charmer < -1 || charmer >= (int32_t)ACTOR_COUNT) {
        len += (size_t)snprintf(what + len, n - len, " charmer %d->-1", (int)charmer);
        *(volatile int32_t *)(act + 0x140) = -1; fixed++;
    }
    if (wander < 0 || wander > 10000) {
        len += (size_t)snprintf(what + len, n - len, " wander %d->0", (int)wander);
        *(volatile int32_t *)(act + 0x168) = 0; fixed++;
    }
    if (reversing != 0 && reversing != 1) {
        len += (size_t)snprintf(what + len, n - len, " reversing %d->0", (int)reversing);
        *(volatile int32_t *)(act + 0x16C) = 0; fixed++;
    }
    if (home > 1) {
        len += (size_t)snprintf(what + len, n - len, " go-home %u->1", (unsigned)home);
        *(volatile uint8_t *)(act + 0x170) = 1; fixed++;
    }
    if (sight > 1 && len + 24 < n) {
        snprintf(what + len, n - len, " line-of-sight %u->1", (unsigned)sight);
        *(volatile uint8_t *)(act + 0x171) = 1; fixed++;
    }
    return fixed;
}

static void U9CALL h_actor_create(void *table, int index)
{
    uintptr_t tbl, act;
    int32_t counter, reload;

    o_actorCreate(table, index);
    g_acCalls++;
    if (index < 0 || index >= (int)ACTOR_COUNT || !readable4(ACTOR_TABLE)) return;
    tbl = rd32(ACTOR_TABLE);
    if (!tbl || !readable4(tbl + 4u * (unsigned)index)) return;
    act = rd32(tbl + 4u * (unsigned)index);
    if (!act || !readable4(act + 0x178)) return;
    counter = (int32_t)rd32(act + 0x174);
    reload = (int32_t)rd32(act + 0x178);
    if (counter < 0 || counter > 36000 || reload < 0 || reload > 600) {
        g_acGarbage++;
        if (g_acLogged < 20) {
            g_acLogged++;
            u9log_line("F-4 (all classes): actor %d (class %08X) was created with sight counter %d and reload %d - %s;"
                       " cleared", index, (unsigned)rd32(act), (int)counter, (int)reload,
                       counter < 0 ? "negative, it would never look" : "it would not look for a long time");
        }
    }
    *(volatile uint32_t *)(act + 0x174) = 0;
    *(volatile uint32_t *)(act + 0x178) = 0;
    g_acCleared++;
    {
        char what[160];
        unsigned fixed = repair_setup(act, what, sizeof what);
        if (fixed) {
            g_acSetupFixed++;
            if (g_acSetupLogged < 20) {
                g_acSetupLogged++;
                u9log_line("F-4 (all classes): actor %d (class %08X) setup fields repaired:%s", index,
                           (unsigned)rd32(act), what);
            }
        }
    }
}

static const U9PatchSpec ACREATE_SPEC_SCHED = {
    "F4_CREATE_SCHEDULED", U9PATCH_CALLSITE, U9SYM_ACTOR_CREATE_CALL_SCHEDULED, 5, CALL_E8, 1, U9PF_EXCLUSIVE
};
static const U9PatchSpec ACREATE_SPEC_LOAD = {
    "F4_CREATE_LOAD", U9PATCH_CALLSITE, U9SYM_ACTOR_CREATE_CALL_LOAD, 5, CALL_E8, 1, U9PF_EXCLUSIVE
};
static const U9PatchSpec ACREATE_SPEC_PACKED = {
    "F4_CREATE_PACKED", U9PATCH_CALLSITE, U9SYM_ACTOR_CREATE_CALL_PACKED, 5, CALL_E8, 1, U9PF_EXCLUSIVE
};

static void actor_create_install(void)
{
    ActorCreateFn a = (ActorCreateFn)u9patch_install(&ACREATE_SPEC_SCHED, (void *)h_actor_create);
    ActorCreateFn b = (ActorCreateFn)u9patch_install(&ACREATE_SPEC_LOAD, (void *)h_actor_create);
    ActorCreateFn c = (ActorCreateFn)u9patch_install(&ACREATE_SPEC_PACKED, (void *)h_actor_create);
    o_actorCreate = a ? a : b ? b : c;
    u9log_line("F-4 (all classes): the sight counter and reload are cleared as every actor is created"
               " (call sites: scheduled %s, load %s, packed %s)", a ? "hooked" : "REFUSED",
               b ? "hooked" : "REFUSED", c ? "hooked" : "REFUSED");
}

static int      g_clampOn = -1;
static DWORD    g_clampLast;
static unsigned g_clampFixed, g_clampLogged;

void u9fix_los_clamp_tick(void)
{
    uintptr_t tbl;
    unsigned i;
    DWORD now;
    if (g_clampOn < 0) {
        g_clampOn = u9cfg_int("fix_actor_los_clamp", 0);
        if (g_clampOn) u9log_line("F-4 backstop: actors in combat with a sight counter or reload out of range are"
                                  " set to 0 (fix_actor_los_clamp)");
    }
    if (!g_clampOn) return;
    now = u9time_now();
    if (now - g_clampLast < 500u) return;
    g_clampLast = now;
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (!tbl || !readable4(tbl + 4u * (ACTOR_COUNT - 1))) return;
    for (i = 1; i < ACTOR_COUNT; i++) {
        uintptr_t act = rd32(tbl + 4u * i);
        uint32_t vt;
        int32_t counter, reload;
        if (!act || !readable4(act) || !readable4(act + 0x178)) continue;
        vt = rd32(act);
        if (vt < IMAGE_LO || vt >= IMAGE_HI) continue;
        if ((int32_t)rd32(act + 0x104) < 0) continue;
        counter = (int32_t)rd32(act + 0x174);
        reload = (int32_t)rd32(act + 0x178);
        if (counter >= 0 && counter <= 36000 && reload >= 0 && reload <= 600) continue;
        if (g_clampLogged < 20) {
            g_clampLogged++;
            u9log_line("F-4 backstop: actor %u (class %08X) fighting with sight counter %d, reload %d - set to 0",
                       i, (unsigned)vt, (int)counter, (int)reload);
        }
        if (counter < 0 || counter > 36000) *(volatile uint32_t *)(act + 0x174) = 0;
        if (reload < 0 || reload > 600) *(volatile uint32_t *)(act + 0x178) = 0;
        g_clampFixed++;
    }
}

#define ACTOR_REBUILD_FLAG  0x2BCu

typedef void (U9CALL *ActorCtorFn)(void *self, unsigned arg);
static ActorCtorFn o_ctorBase, o_ctorDerived;
static unsigned    g_rebuildSet;

static void set_rebuild_flag(void *self)
{
    if (!self) return;
    *(volatile unsigned char *)((uintptr_t)self + ACTOR_REBUILD_FLAG) = 1;
    g_rebuildSet++;
}

static void U9CALL h_ctor_base(void *self, unsigned arg)
{
    o_ctorBase(self, arg);
    set_rebuild_flag(self);
}

static void U9CALL h_ctor_derived(void *self, unsigned arg)
{
    o_ctorDerived(self, arg);
    set_rebuild_flag(self);
}

static const U9PatchSpec REBUILD_SPEC_BASE = {
    "F4_REBUILD_BASE", U9PATCH_CALLSITE, U9SYM_ACTOR_CTOR_CALL_BASE,
    5, CALL_E8, 1, U9PF_EXCLUSIVE
};
static const U9PatchSpec REBUILD_SPEC_DERIVED = {
    "F4_REBUILD_DERIV", U9PATCH_CALLSITE, U9SYM_ACTOR_CTOR_CALL_DERIVED,
    5, CALL_E8, 1, U9PF_EXCLUSIVE
};

static void rebuild_flag_install(void)
{
    o_ctorBase = (ActorCtorFn)u9patch_install(&REBUILD_SPEC_BASE,
                                              (void *)h_ctor_base);
    o_ctorDerived = (ActorCtorFn)u9patch_install(&REBUILD_SPEC_DERIVED,
                                                 (void *)h_ctor_derived);
    u9log_line("F-4 rebuild flag: +0x2BC will be set after every actor"
               " constructor, so casting no longer depends on how the game was"
               " started");
}

static const uint8_t RELOAD_EXPECT[4] = { 0x05, 0x00, 0x00, 0x00 };
static const uint8_t RELOAD_ZERO[4]   = { 0x00, 0x00, 0x00, 0x00 };

static const U9PatchSpec RELOAD_SPEC = {
    "F4_RELOAD_IMM", U9PATCH_STUB, U9SYM_RESET_TRANSIENT_RELOAD_IMM,
    4, RELOAD_EXPECT, 4, U9PF_EXCLUSIVE
};

static void reload_imm_install(void)
{
    if (u9patch_install_bytes(&RELOAD_SPEC, RELOAD_ZERO, 4))
        u9log_line("F-4: Actor_ResetTransientState now writes 0 to +0x178"
                   " instead of 5, so the fix survives a re-registration");
}


static const uint8_t SPELLKIND_EXPECT[4] = { 0x9A, 0x00, 0x00, 0x00 };
static const uint8_t SPELLKIND_97[4]     = { 0x97, 0x00, 0x00, 0x00 };

static const U9PatchSpec SPELLKIND_SPEC_1 = {
    "F19_SPELL_KIND_1", U9PATCH_STUB, U9SYM_SPELL_KIND_1,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_2 = {
    "F19_SPELL_KIND_2", U9PATCH_STUB, U9SYM_SPELL_KIND_2,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_3 = {
    "F19_SPELL_KIND_3", U9PATCH_STUB, U9SYM_SPELL_KIND_3,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_4 = {
    "F19_SPELL_KIND_4", U9PATCH_STUB, U9SYM_SPELL_KIND_4,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_5 = {
    "F19_SPELL_KIND_5", U9PATCH_STUB, U9SYM_SPELL_KIND_5,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_6 = {
    "F19_SPELL_KIND_6", U9PATCH_STUB, U9SYM_SPELL_KIND_6,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_7 = {
    "F19_SPELL_KIND_7", U9PATCH_STUB, U9SYM_SPELL_KIND_7,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_8 = {
    "F19_SPELL_KIND_8", U9PATCH_STUB, U9SYM_SPELL_KIND_8,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_9 = {
    "F19_SPELL_KIND_9", U9PATCH_STUB, U9SYM_SPELL_KIND_9,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_10 = {
    "F19_SPELL_KIND_10", U9PATCH_STUB, U9SYM_SPELL_KIND_10,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_11 = {
    "F19_SPELL_KIND_11", U9PATCH_STUB, U9SYM_SPELL_KIND_11,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPELLKIND_SPEC_12 = {
    "F19_SPELL_KIND_12", U9PATCH_STUB, U9SYM_SPELL_KIND_12,
    4, SPELLKIND_EXPECT, 4, U9PF_EXCLUSIVE
};

static void spell_kind_install(void)
{
    static const U9PatchSpec *const specs[12] = {
        &SPELLKIND_SPEC_1, &SPELLKIND_SPEC_2, &SPELLKIND_SPEC_3, &SPELLKIND_SPEC_4,
        &SPELLKIND_SPEC_5, &SPELLKIND_SPEC_6, &SPELLKIND_SPEC_7, &SPELLKIND_SPEC_8,
        &SPELLKIND_SPEC_9, &SPELLKIND_SPEC_10, &SPELLKIND_SPEC_11, &SPELLKIND_SPEC_12
    };
    int i, done = 0;
    for (i = 0; i < 12; i++)
        done += u9patch_install_bytes(specs[i], SPELLKIND_97, 4) ? 1 : 0;
    u9log_line("F-19: the spell-type test ends at 0x97 in %d of 12 places - the Ring of Fire's"
               " burning ring (0x9A) and the effects 0x98/0x99 are no longer taken for spells", done);
}


#define MODEL_MAX_LODS  16u
#define MODEL_MAX_LIMBS 255u

static const uint8_t MODELREC_EXPECT[8] = { 0x3B, 0xEE, 0x89, 0x6C, 0x24, 0x10, 0x75, 0x0E };
static const U9PatchSpec MODELREC_SPEC = {
    "F23_MODEL_RECORD", U9PATCH_STUB, U9SYM_MODEL_RECORD_TEST,
    8, MODELREC_EXPECT, 8, U9PF_EXCLUSIVE
};
static uint8_t          *g_modelStub;
static volatile LONG     g_modelRefused;
static volatile uint32_t g_modelLastLimbs, g_modelLastLods;

static void model_refused(uint32_t rec)
{
    const uint32_t *r = (const uint32_t *)(uintptr_t)rec;
    g_modelLastLimbs = r[0];
    g_modelLastLods  = r[1];
    InterlockedIncrement(&g_modelRefused);
}

static uint8_t *stub_alloc(void);
static int jump_to_stub(const U9PatchSpec *spec, uintptr_t site, uint8_t *st);

static void model_guard_install(void)
{
    uintptr_t site = u9sym(U9SYM_MODEL_RECORD_TEST);
    uintptr_t ok   = u9sym(U9SYM_MODEL_RECORD_OK);
    uintptr_t none = u9sym(U9SYM_MODEL_RECORD_NONE);
    uint8_t *st, *p, *jzNone, *jaBad1, *jaBad2, *bad, *fail;

    if (!site || !ok || !none || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x89; *p++ = 0x6C; *p++ = 0x24; *p++ = 0x10;
    *p++ = 0x85; *p++ = 0xED;
    *p++ = 0x74; jzNone = p++;
    *p++ = 0x83; *p++ = 0x7D; *p++ = 0x04; *p++ = (uint8_t)MODEL_MAX_LODS;
    *p++ = 0x77; jaBad1 = p++;
    *p++ = 0x81; *p++ = 0x7D; *p++ = 0x00;
    *(uint32_t *)p = MODEL_MAX_LIMBS; p += 4;
    *p++ = 0x77; jaBad2 = p++;
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(ok - ((uintptr_t)p + 4)); p += 4;
    bad = p;
    *p++ = 0x60;
    *p++ = 0x55;
    *p++ = 0xE8;
    *(int32_t *)p = (int32_t)((uintptr_t)&model_refused - ((uintptr_t)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;
    *p++ = 0x61;
    fail = p;
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(none - ((uintptr_t)p + 4)); p += 4;

    *jzNone = (uint8_t)(fail - (jzNone + 1));
    *jaBad1 = (uint8_t)(bad - (jaBad1 + 1));
    *jaBad2 = (uint8_t)(bad - (jaBad2 + 1));
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&MODELREC_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_modelStub = st;
    u9log_line("F-23: models the game cannot read (more than %u LODs or %u limbs) are refused"
               " instead of built", MODEL_MAX_LODS, MODEL_MAX_LIMBS);
}


static const uint8_t SOUNDSRC_EXPECT[5] = { 0xE8, 0xAC, 0x83, 0xF8, 0xFF };
static const U9PatchSpec SOUNDSRC_SPEC = {
    "F24_SOUNDSRC_RESOLVE", U9PATCH_STUB, U9SYM_SOUNDSRC_RESOLVE_CALL,
    5, SOUNDSRC_EXPECT, 5, U9PF_EXCLUSIVE
};
static uint8_t       *g_soundSrcStub;
static volatile LONG  g_soundSrcNoSlot;

static uint8_t *emit_rel32(uint8_t *p, uint8_t op, uintptr_t target)
{
    *p++ = op;
    *(int32_t *)p = (int32_t)(target - ((uintptr_t)p + 4));
    return p + 4;
}

static void sound_source_guard_install(void)
{
    uintptr_t site     = u9sym(U9SYM_SOUNDSRC_RESOLVE_CALL);
    uintptr_t resolved = u9sym(U9SYM_SOUNDSRC_RESOLVED);
    uintptr_t worldPos = u9sym(U9SYM_SOUNDSRC_WORLD_POS);
    uintptr_t slotFn   = u9sym(U9SYM_FASTREF_SLOT_OR_REACQUIRE);
    uintptr_t resolve  = u9sym(U9SYM_FASTREF_RESOLVE_RECORD);
    uint8_t *st, *p, *jz;

    if (!site || !resolved || !worldPos || !slotFn || !resolve || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x51;
    *p++ = 0x51;
    *p++ = 0xA1; *(uint32_t *)p = 0x00AD127Cu; p += 4;
    *p++ = 0x8B; *p++ = 0x48; *p++ = 0x64;
    *p++ = 0x8B; *p++ = 0x89;
    *(uint32_t *)p = 0xD0u; p += 4;
    p = emit_rel32(p, 0xE8, slotFn);
    *p++ = 0x59;
    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x74; jz = p++;
    p = emit_rel32(p, 0xE8, resolve);
    p = emit_rel32(p, 0xE9, resolved);
    *jz = (uint8_t)(p - (jz + 1));
    *p++ = 0xF0; *p++ = 0xFF; *p++ = 0x05;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_soundSrcNoSlot; p += 4;
    p = emit_rel32(p, 0xE9, worldPos);
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&SOUNDSRC_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_soundSrcStub = st;
    u9log_line("F-24: a sound source whose object has lost its fast-area slot is measured from"
               " the object's world position instead of crashing");
}


static const uint8_t LINEHIT_EXPECT[6] = { 0x8B, 0x44, 0x24, 0x74, 0xA8, 0x40 };
static const U9PatchSpec LINEHIT_SPEC = {
    "F25_LINEHIT_BEST", U9PATCH_STUB, U9SYM_LINEHIT_SEARCH_START,
    6, LINEHIT_EXPECT, 6, U9PF_EXCLUSIVE
};

static const uint8_t LINEOBJ_EXPECT[5] = { 0x8B, 0x68, 0x14, 0xEB, 0x02 };
static const U9PatchSpec LINEOBJ_SPEC = {
    "F25_LINEHIT_OBJECT", U9PATCH_STUB, U9SYM_LINEHIT_OBJECT_READ,
    5, LINEOBJ_EXPECT, 5, U9PF_EXCLUSIVE
};
static volatile LONG g_lineObjNone;
static unsigned      g_lineObjLogged;
#define LINEOBJ_LOG_MAX   24u
#define LINEOBJ_WALK_MAX  200000u

static int line_obj_ok(uintptr_t obj, uintptr_t *itemOut)
{
    uintptr_t item;
    *itemOut = 0;
    if (obj >= IMAGE_LO && obj < IMAGE_HI) return 0;
    if (!readable4(obj + 0x14)) return 0;
    item = rd32(obj + 0x14);
    *itemOut = item;
    if (item == 0) return 1;
    if (item == 0xFFFFFFFFu || !readable4(item + 0x14)) return 0;
    return 1;
}

static void line_obj_shape(const char *tag, uintptr_t sh)
{
    uintptr_t rec = readable4(sh) ? rd32(sh) : 0;
    unsigned type = (rec && rec != 0xFFFFFFFFu && readable4(rec + 0x08))
                    ? (unsigned)*(const volatile uint16_t *)(rec + 0x0A) : 0xFFFFu;
    if (!readable4(sh + 0x70)) { u9log_line("LINEHIT   %s shape %08X unreadable", tag, (unsigned)sh); return; }
    u9log_line("LINEHIT   %s shape %08X: record %08X type %u, flags+4C %08X, object+50 %08X, mesh+54 %08X,"
               " element+68 %08X, head+70 %08X, box x %d..%d y %d..%d", tag, (unsigned)sh, (unsigned)rec,
               type, (unsigned)rd32(sh + 0x4C), (unsigned)rd32(sh + 0x50), (unsigned)rd32(sh + 0x54),
               (unsigned)rd32(sh + 0x68), (unsigned)rd32(sh + 0x70),
               (int)rd32(sh + 0x04) >> 1, (int)rd32(sh + 0x24) >> 1,
               (int)rd32(sh + 0x14) >> 1, (int)rd32(sh + 0x34) >> 1);
}

static uintptr_t __cdecl line_obj_check(uintptr_t obj, uintptr_t probe)
{
    uintptr_t item, node, head, sh;
    unsigned n = 0, total = 0, found = 0;
    int ax, ay, az;

    if (line_obj_ok(obj, &item)) return item;
    InterlockedIncrement(&g_lineObjNone);
    if (g_lineObjLogged >= LINEOBJ_LOG_MAX) return 0;
    g_lineObjLogged++;

    if (!u9avatar_position(&ax, &ay, &az)) ax = ay = az = 0;
    u9log_line("LINEHIT stale #%u: hit object %08X (%s), item %08X; probe shape %08X; Avatar at %d,%d,%d",
               g_lineObjLogged, (unsigned)obj,
               (obj >= IMAGE_LO && obj < IMAGE_HI) ? "inside u9.exe's image" :
               readable4(obj) ? "readable" : "unreadable", (unsigned)item, (unsigned)probe, ax, ay, az);
    if (readable4(obj))
        u9log_line("LINEHIT   object's words +0 %08X +4 %08X +8 %08X +14 %08X", (unsigned)rd32(obj),
                   (unsigned)(readable4(obj + 4) ? rd32(obj + 4) : 0),
                   (unsigned)(readable4(obj + 8) ? rd32(obj + 8) : 0),
                   (unsigned)(readable4(obj + 0x14) ? rd32(obj + 0x14) : 0));
    if (!readable4(probe + 0x70)) return 0;
    line_obj_shape("probe", probe);

    head = probe + 0x04;
    while (n < LINEOBJ_WALK_MAX && readable4(head + 0x0C) && rd32(head + 0x0C)) { head = rd32(head + 0x0C); n++; }
    for (node = head, n = 0; node && n < LINEOBJ_WALK_MAX && readable4(node + 0x0C); node = rd32(node + 0x08), n++) {
        if (rd32(node) & 1u) continue;
        total++;
        sh = rd32(node + 0x04);
        if (!readable4(sh + 0x50)) {
            if (found < 8) {
                found++;
                u9log_line("LINEHIT   node %08X (value %d) names unreadable shape %08X",
                           (unsigned)node, (int)rd32(node) >> 1, (unsigned)sh);
            }
            continue;
        }
        if (rd32(sh + 0x50) == obj && found < 8) { found++; line_obj_shape("stale", sh); }
    }
    u9log_line("LINEHIT   X list: %u shapes walked from head %08X%s; %u reported", total, (unsigned)head,
               n >= LINEOBJ_WALK_MAX ? " (stopped at the cap)" : "", found);
    return 0;
}

static void line_object_install(void)
{
    uintptr_t site = u9sym(U9SYM_LINEHIT_OBJECT_READ);
    uintptr_t done = u9sym(U9SYM_LINEHIT_OBJECT_DONE);
    uint8_t *st, *p;

    if (!site || !done || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x9C;
    *p++ = 0x60;
    *p++ = 0x56;
    *p++ = 0x50;
    p = emit_rel32(p, 0xE8, (uintptr_t)&line_obj_check);
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08;
    *p++ = 0x89; *p++ = 0x44; *p++ = 0x24; *p++ = 0x08;
    *p++ = 0x61;
    *p++ = 0x9D;
    p = emit_rel32(p, 0xE9, done);
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&LINEOBJ_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    u9log_line("F-25: a line test's hit whose object is not a live-looking game object is taken as"
               " no object, and the shapes still naming it are logged (LINEHIT)");
}

static const uint8_t CAMOBJ_EXPECT[5] = { 0x8B, 0x40, 0x14, 0xEB, 0x02 };
static const U9PatchSpec CAMOBJ_SPEC = {
    "F25_CAMERA_HIT_OBJECT", U9PATCH_STUB, U9SYM_CAMHIT_OBJECT_READ,
    5, CAMOBJ_EXPECT, 5, U9PF_EXCLUSIVE
};
static volatile LONG g_camObjNone;
static unsigned      g_camObjLogged;

static uintptr_t __cdecl cam_obj_check(uintptr_t obj)
{
    uintptr_t item;
    int ax, ay, az;
    if (line_obj_ok(obj, &item)) return item;
    InterlockedIncrement(&g_camObjNone);
    if (g_camObjLogged < 8) {
        g_camObjLogged++;
        if (!u9avatar_position(&ax, &ay, &az)) ax = ay = az = 0;
        u9log_line("LINEHIT camera #%u: hit object %08X (%s), item %08X - taken as none; Avatar at %d,%d,%d",
                   g_camObjLogged, (unsigned)obj,
                   (obj >= IMAGE_LO && obj < IMAGE_HI) ? "inside u9.exe's image" :
                   readable4(obj) ? "readable" : "unreadable", (unsigned)item, ax, ay, az);
    }
    return 0;
}

static void cam_object_install(void)
{
    uintptr_t site = u9sym(U9SYM_CAMHIT_OBJECT_READ);
    uintptr_t done = u9sym(U9SYM_CAMHIT_OBJECT_DONE);
    uint8_t *st, *p;

    if (!site || !done || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x9C;
    *p++ = 0x60;
    *p++ = 0x50;
    p = emit_rel32(p, 0xE8, (uintptr_t)&cam_obj_check);
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;
    *p++ = 0x89; *p++ = 0x44; *p++ = 0x24; *p++ = 0x1C;
    *p++ = 0x61;
    *p++ = 0x9D;
    p = emit_rel32(p, 0xE9, done);
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&CAMOBJ_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    u9log_line("F-25: the camera's collision hit is checked the same way before its object is read");
}

static volatile uintptr_t g_lineProbe;
static unsigned g_scanFrames, g_scanBadLogged, g_scanRuns, g_scanBadTotal;
#define SCAN_EVERY      30u
#define SCAN_LOG_MAX    40u

static void scan_list(uintptr_t start, const char *axis, int ax, int ay, int az)
{
    uintptr_t node = start, sh, obj, item;
    unsigned n = 0, walked = 0, bad = 0;
    const char *why;

    while (n < LINEOBJ_WALK_MAX && readable4(node + 0x0C) && rd32(node + 0x0C)) { node = rd32(node + 0x0C); n++; }
    for (n = 0; node && n < LINEOBJ_WALK_MAX && readable4(node + 0x0C); node = rd32(node + 0x08), n++) {
        walked++;
        sh = rd32(node + 0x04);
        why = NULL;
        if (!readable4(sh + 0x70)) why = "shape unreadable";
        else if (node != sh + 0x04 && node != sh + 0x14 && node != sh + 0x24 && node != sh + 0x34)
            why = "shape does not own this node";
        else if ((obj = rd32(sh + 0x50)) != 0 && !line_obj_ok(obj, &item))
            why = "shape's object is dead";
        if (!why) continue;
        bad++;
        g_scanBadTotal++;
        if (g_scanBadLogged >= SCAN_LOG_MAX) continue;
        g_scanBadLogged++;
        u9log_line("LINEHIT scan %s: node %08X value %d%s - %s; Avatar at %d,%d,%d", axis, (unsigned)node,
                   (int)rd32(node) >> 1, (rd32(node) & 1u) ? " (max)" : " (min)", why, ax, ay, az);
        if (readable4(sh + 0x70)) line_obj_shape("bad", sh);
        else u9log_line("LINEHIT   bad shape %08X unreadable", (unsigned)sh);
    }
    if (bad && g_scanBadLogged <= SCAN_LOG_MAX)
        u9log_line("LINEHIT scan %s: %u bad of %u nodes walked", axis, bad, walked);
}

void u9linehit_tick(void)
{
    uintptr_t probe = g_lineProbe;
    int ax, ay, az;
    static int on = -1;
    if (on < 0) on = u9cfg_int("diag_linehit_scan", 0);
    if (!on || ++g_scanFrames < SCAN_EVERY) return;
    g_scanFrames = 0;
    if (!probe || !readable4(probe + 0x70) || g_scanBadLogged >= SCAN_LOG_MAX) return;
    g_scanRuns++;
    if (!u9avatar_position(&ax, &ay, &az)) ax = ay = az = 0;
    scan_list(probe + 0x04, "X", ax, ay, az);
    scan_list(probe + 0x14, "Y", ax, ay, az);
}

static const uint8_t LIMB_EXPECT[9] = { 0x8B, 0x80, 0xA8, 0x00, 0x00, 0x00, 0x83, 0xC0, 0x30 };
static const U9PatchSpec LIMB_SPEC = {
    "F26_LIMB_FIRST", U9PATCH_STUB, U9SYM_LIMB_FIRST_READ,
    9, LIMB_EXPECT, 9, U9PF_EXCLUSIVE
};
static volatile LONG g_limbNoElement, g_limbNoA8;
static volatile LONG g_limbType;

static void limb_first_install(void)
{
    uintptr_t site = u9sym(U9SYM_LIMB_FIRST_READ);
    uintptr_t done = u9sym(U9SYM_LIMB_FIRST_DONE);
    uintptr_t none = u9sym(U9SYM_LIMB_NO_FIRST);
    uint8_t *st, *p, *jzElem, *jzA8, *noA8, *common;

    if (!site || !done || !none || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x74; jzElem = p++;
    memcpy(p, LIMB_EXPECT, 6); p += 6;
    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x74; jzA8 = p++;
    memcpy(p, LIMB_EXPECT + 6, 3); p += 3;
    p = emit_rel32(p, 0xE9, done);
    *jzElem = (uint8_t)(p - (jzElem + 1));
    *p++ = 0xF0; *p++ = 0xFF; *p++ = 0x05;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_limbNoElement; p += 4;
    *p++ = 0xEB; common = p++;
    noA8 = p;
    *jzA8 = (uint8_t)(noA8 - (jzA8 + 1));
    *p++ = 0xF0; *p++ = 0xFF; *p++ = 0x05;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_limbNoA8; p += 4;
    *common = (uint8_t)(p - (common + 1));
    *p++ = 0x8B; *p++ = 0x03;
    *p++ = 0x0F; *p++ = 0xB7; *p++ = 0x40; *p++ = 0x0A;
    *p++ = 0xA3;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_limbType; p += 4;
    *p++ = 0xC7; *p++ = 0x43; *p++ = 0x68;
    *(uint32_t *)p = 0; p += 4;
    p = emit_rel32(p, 0xE9, none);
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&LIMB_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    u9log_line("F-26: a cylinder's limb build with no usable first limb builds none instead of crashing");
}

static void line_hit_install(void)
{
    uintptr_t site = u9sym(U9SYM_LINEHIT_SEARCH_START);
    uintptr_t jz   = u9sym(U9SYM_LINEHIT_SEARCH_JZ);
    uint8_t *st, *p;

    line_object_install();
    cam_object_install();
    limb_first_install();
    if (!site || !jz || !(st = stub_alloc())) return;
    p = st;
    *p++ = 0x89; *p++ = 0x35;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_lineProbe; p += 4;
    *p++ = 0xC7; *p++ = 0x44; *p++ = 0x24; *p++ = 0x18;
    *(uint32_t *)p = 0; p += 4;
    memcpy(p, LINEHIT_EXPECT, 6); p += 6;
    p = emit_rel32(p, 0xE9, jz);
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&LINEHIT_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    u9log_line("F-25: a collision line test that finds no nearer hit starts its best hit at"
               " nothing instead of a leftover float");
}


#define TYPE_TABLE_PTR    0x00AE7C7Cu
#define TYPE_TABLE_COUNT  0x00AE7C74u
#define HALBERD_TYPE      3463u
#define HALBERD_MASS      100u

static const U9PatchSpec TYPELOAD_SPEC = {
    "HALBERD_TYPE_LOAD", U9PATCH_CALLSITE, U9SYM_TYPE_TABLE_LOAD_CALL,
    5, CALL_E8, 1, U9PF_EXCLUSIVE
};
typedef void (*TypeLoadFn)(void);
static TypeLoadFn o_typeLoad;

static void halberd_apply(const char *when)
{
    uint8_t *table = (uint8_t *)(uintptr_t)rd32(TYPE_TABLE_PTR);
    uint32_t count = rd32(TYPE_TABLE_COUNT);
    uint8_t *mass;

    if (!table || count <= HALBERD_TYPE) return;
    mass = table + HALBERD_TYPE * 16u + 0x0A;
    if (*mass == 0xFF) {
        *mass = (uint8_t)HALBERD_MASS;
        u9log_line("HALBERD: type %u mass 255 -> %u (%s) - it can be picked up, carried and wielded",
                   HALBERD_TYPE, HALBERD_MASS, when);
    } else {
        u9log_line("HALBERD: type %u mass is %u, not 255 (%s) - left as it is",
                   HALBERD_TYPE, (unsigned)*mass, when);
    }
}

static void type_load_hook(void)
{
    o_typeLoad();
    halberd_apply("after types.dat loaded");
}

#define HALBERD_SOUND_TEMPLATE 178u

static const uint8_t SOUNDASSOC_EXPECT[6] = { 0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1 };
static const U9PatchSpec SOUNDASSOC_SPEC = {
    "HALBERD_SOUND", U9PATCH_INLINE, U9SYM_SOUND_ASSOC_LOOKUP,
    6, SOUNDASSOC_EXPECT, 6, U9PF_EXCLUSIVE
};
typedef int (U9CALL *SoundAssocFn)(void *self, uint32_t type, uint32_t *tmpl);
static SoundAssocFn o_soundAssoc;
static volatile LONG g_halberdSounds;

static int U9CALL h_soundAssoc(void *self, uint32_t type, uint32_t *tmpl)
{
    int r = o_soundAssoc(self, type, tmpl);
    if (r != 0 && (type & 0xFFFFu) == HALBERD_TYPE && tmpl) {
        *tmpl = HALBERD_SOUND_TEMPLATE;
        InterlockedIncrement(&g_halberdSounds);
        return 0;
    }
    return r;
}

#define HALBERD_ROW_SIZE 0x2Cu
static const uint8_t HALBERD_ROW_EXPECT[HALBERD_ROW_SIZE] = {
    0x87, 0x0D, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x90, 0xC1,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD8, 0x41,
};
static const U9PatchSpec HALBERD_OFFSET_SPEC = {
    "HALBERD_GRIP_OFFSET", U9PATCH_STUB, U9SYM_HALBERD_ATTACH_OFFSET,
    12, HALBERD_ROW_EXPECT + 8, 12, U9PF_EXCLUSIVE
};
static const U9PatchSpec HALBERD_ROTATION_SPEC = {
    "HALBERD_GRIP_ROTATION", U9PATCH_STUB, U9SYM_HALBERD_ATTACH_ROTATION,
    12, HALBERD_ROW_EXPECT + 32, 12, U9PF_EXCLUSIVE
};
static float *g_halberdBack;

static float    g_hand[9] = { 0, 0, 0, 1, 1, 1, 0, 0, 0 };
static int      g_handOn, g_handHooked;
static volatile LONG g_handApplied;
#define HALBERD_KEY_HAND 1

static const uint8_t ATTACHADJ_EXPECT[8] = { 0x56, 0x57, 0x8B, 0x3D, 0x2C, 0xEF, 0x79, 0x00 };
static const U9PatchSpec ATTACHADJ_SPEC = {
    "HALBERD_GRIP_HAND", U9PATCH_INLINE, U9SYM_ATTACH_ADJUST_APPLY,
    8, ATTACHADJ_EXPECT, 8, U9PF_EXCLUSIVE
};
typedef void (__attribute__((fastcall)) *AttachAdjFn)(int type, int key, float *off, float *scale, void *rot);
typedef void (U9CALL *RotFromEulerFn)(void *rot, float rx, float ry, float rz);
static AttachAdjFn o_attachAdj;
#define DEG_TO_RAD 0.017453292f

static void __attribute__((fastcall)) h_attachAdj(int type, int key, float *off, float *scale, void *rot)
{
    if (g_handOn && (type & 0xFFFF) == (int)HALBERD_TYPE && key == HALBERD_KEY_HAND && off && scale && rot) {
        RotFromEulerFn rf = (RotFromEulerFn)u9sym(U9SYM_ROT_FROM_EULER);
        off[0] = g_hand[0];   off[1] = g_hand[1];   off[2] = g_hand[2];
        scale[0] = g_hand[3]; scale[1] = g_hand[4]; scale[2] = g_hand[5];
        if (rf) rf(rot, g_hand[6] * DEG_TO_RAD, g_hand[7] * DEG_TO_RAD, g_hand[8] * DEG_TO_RAD);
        InterlockedIncrement(&g_handApplied);
        return;
    }
    o_attachAdj(type, key, off, scale, rot);
}

static void halberd_grip_log(const char *where, const float *f, const char *why)
{
    u9log_line("HALBERD %s (%s): offset %.1f,%.1f,%.1f rotation %.1f,%.1f,%.1f", where, why,
               (double)f[0], (double)f[1], (double)f[2], (double)f[6], (double)f[7], (double)f[8]);
}

static int halberd_grip_parse(const char *s, float *f)
{
    float v[6];
    int n = 0;
    char *end;
    while (s && *s && n < 6) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        v[n] = (float)strtod(s, &end);
        if (end == s) return 0;
        s = end;
        n++;
    }
    if (n != 3 && n != 6) return 0;
    f[0] = v[0]; f[1] = v[1]; f[2] = v[2];
    if (n == 6) { f[6] = v[3]; f[7] = v[4]; f[8] = v[5]; }
    return 1;
}

void u9halberd_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v;
    int back;
    while (arg && *arg == ' ') arg++;
    back = arg && !strncmp(arg, "back", 4);
    if (!arg || (strncmp(arg, "grip", 4) && !back)) {
        out("usage: halberd grip|back [x y z [rx ry rz] | reset]   grip: in the hand, back: carried");
        return;
    }
    v = arg + 4;
    while (*v == ' ') v++;
    if (back) {
        if (!g_halberdBack) { out("halberd back: its row is not claimed (see the manifest)"); return; }
        if (!strcmp(v, "reset")) {
            memcpy(g_halberdBack, HALBERD_ROW_EXPECT + 8, 12);
            memcpy(g_halberdBack + 6, HALBERD_ROW_EXPECT + 32, 12);
            halberd_grip_log("back", g_halberdBack, "console reset");
        } else if (*v) {
            if (!halberd_grip_parse(v, g_halberdBack)) { out("usage: halberd back x y z [rx ry rz] | reset"); return; }
            halberd_grip_log("back", g_halberdBack, "console");
        }
        out("halberd back: offset %.1f %.1f %.1f  rotation %.1f %.1f %.1f  (the game's: 3 -2 -18  0 0 27)",
            (double)g_halberdBack[0], (double)g_halberdBack[1], (double)g_halberdBack[2],
            (double)g_halberdBack[6], (double)g_halberdBack[7], (double)g_halberdBack[8]);
    } else {
        if (!g_handHooked) { out("halberd grip: the hand's hook is not installed (see the manifest)"); return; }
        if (!strcmp(v, "reset")) {
            g_handOn = 0;
            out("halberd grip: the game's - nothing applied in the hand");
            u9log_line("HALBERD grip (console reset): the game's, nothing applied in the hand");
        } else if (*v) {
            float f[9];
            memcpy(f, g_hand, sizeof f);
            if (!halberd_grip_parse(v, f)) { out("usage: halberd grip x y z [rx ry rz] | reset"); return; }
            memcpy(g_hand, f, sizeof f);
            g_handOn = 1;
            halberd_grip_log("grip", g_hand, "console");
        }
        if (g_handOn)
            out("halberd grip: offset %.1f %.1f %.1f  rotation %.1f %.1f %.1f  (applied %ld times)",
                (double)g_hand[0], (double)g_hand[1], (double)g_hand[2],
                (double)g_hand[6], (double)g_hand[7], (double)g_hand[8], (long)g_handApplied);
        else
            out("halberd grip: the game's - no adjustment in the hand; set one with halberd grip x y z");
    }
    out("a change shows the next time it is attached: put the halberd away and draw it again");
}

static void halberd_grip_install(void)
{
    const char *hand = u9cfg_int("fix_halberd_grip", 1) ? u9cfg_str("halberd_grip", "0,0,-15") : "";
    const char *back = u9cfg_str("halberd_back", "");
    uintptr_t row = u9sym(U9SYM_HALBERD_ATTACH_ROW);

    o_attachAdj = (AttachAdjFn)u9patch_install(&ATTACHADJ_SPEC, (void *)h_attachAdj);
    g_handHooked = o_attachAdj != NULL;
    if (!g_handHooked) {
        u9log_line("HALBERD grip: the hand's hook was REFUSED (see the manifest) - held as the game holds it");
    } else if (hand && *hand) {
        if (halberd_grip_parse(hand, g_hand)) { g_handOn = 1; halberd_grip_log("grip", g_hand, "halberd_grip"); }
        else u9log_line("HALBERD: halberd_grip = '%s' not understood - want x,y,z or x,y,z,rx,ry,rz", hand);
    } else {
        u9log_line("HALBERD grip: the game's - nothing applied in the hand (fix_halberd_grip off, or halberd_grip empty)");
    }

    if (!row || memcmp((const void *)row, HALBERD_ROW_EXPECT, 8) != 0) {
        u9log_line("HALBERD: the AttachAdjust row at 0x%08X is not the halberd's (type 3463, key 3) - back left alone",
                   (unsigned)row);
        return;
    }
    if (!u9patch_install_bytes(&HALBERD_OFFSET_SPEC, HALBERD_ROW_EXPECT + 8, 12) ||
        !u9patch_install_bytes(&HALBERD_ROTATION_SPEC, HALBERD_ROW_EXPECT + 32, 12)) {
        u9log_line("HALBERD: its AttachAdjust row was REFUSED (see the manifest) - carried as the game carries it");
        return;
    }
    g_halberdBack = (float *)(row + 8);
    if (back && *back && !halberd_grip_parse(back, g_halberdBack))
        u9log_line("HALBERD: halberd_back = '%s' not understood - want x,y,z or x,y,z,rx,ry,rz", back);
    halberd_grip_log("back", g_halberdBack, back && *back ? "halberd_back" : "the game's");
}

static void halberd_install(void)
{
    o_typeLoad = (TypeLoadFn)u9patch_install(&TYPELOAD_SPEC, (void *)type_load_hook);
    if (!o_typeLoad) { u9log_line("HALBERD: the type-table load call was refused - not restored"); return; }
    halberd_apply("at install");
    halberd_grip_install();

    o_soundAssoc = (SoundAssocFn)u9patch_install(&SOUNDASSOC_SPEC, (void *)h_soundAssoc);
    u9log_line("HALBERD: sounds %s - the battleaxe's (template %u)",
               o_soundAssoc ? "linked" : "REFUSED, it stays silent", HALBERD_SOUND_TEMPLATE);
}


static const uint8_t BLITCLAMP_EXPECT[2] = { 0x7D, 0x06 };
static const uint8_t BLITCLAMP_JAE[2]    = { 0x73, 0x06 };

static const U9PatchSpec BLITCLAMP_SPEC = {
    "LAA_BLIT_ROW_CLAMP", U9PATCH_STUB, U9SYM_BLIT_ROW_CLAMP_JCC,
    2, BLITCLAMP_EXPECT, 2, U9PF_EXCLUSIVE
};

static void blit_clamp_install(void)
{
    if (u9patch_install_bytes(&BLITCLAMP_SPEC, BLITCLAMP_JAE, 2))
        u9log_line("LAA: Bitmap_Blit's row clamp compares unsigned (JGE -> JAE"
                   " at 0x005E2292), correct above 2GB");
}


static const uint8_t COLREF_EXPECT[3] = { 0x47, 0x3B, 0xD6 };
static const uint8_t COLREF_NOP[3]    = { 0x90, 0x3B, 0xD6 };

static const U9PatchSpec COLREF_SPEC = {
    "I14_COLLIDER_EXTRA_REF", U9PATCH_STUB, U9SYM_COLLIDER_EXTRA_REF_INC,
    3, COLREF_EXPECT, 3, U9PF_EXCLUSIVE
};

static void collider_ref_install(void)
{
    if (u9patch_install_bytes(&COLREF_SPEC, COLREF_NOP, 3))
        u9log_line("I-14: a copied collision shape takes one reference on its mesh,"
                   " not two (INC -> NOP at 0x00564D1B)");
}


static void laa_report(void)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt =
        (const IMAGE_NT_HEADERS *)((const char *)dos + dos->e_lfanew);
    MEMORYSTATUSEX ms;
    int flag = (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
    char exe[MAX_PATH];
    const char *base;

    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) ms.ullTotalVirtual = 0;
    if (!GetModuleFileNameA(NULL, exe, sizeof(exe))) lstrcpyA(exe, "?");
    base = strrchr(exe, 92);
    u9log_line("LAA: %s is %slarge address aware; address space %u MB",
               base ? base + 1 : exe, flag ? "" : "NOT ",
               (unsigned)(ms.ullTotalVirtual >> 20));
}


static uintptr_t g_f5Bail;

static uint8_t  *g_f5Stub;
static int       g_f5Patched;
static volatile uint32_t g_f5Fired;

static unsigned g_f5Logged;


static void __attribute__((cdecl)) f5_report(void *spell)
{
    uintptr_t sp = (uintptr_t)spell;
    uintptr_t tbl, ent = 0;
    uint32_t id = 0xFFFFFFFFu, f14 = 0, top = 0xFFFFFFFFu;

    g_f5Fired++;
    if (g_f5Logged >= 20) return;
    g_f5Logged++;

    if (readable4(sp + 0xF0)) id = rd32(sp + 0xF0);
    tbl = readable4(0x008515D0u) ? rd32(0x008515D0u) : 0;
    if (tbl && id != 0xFFFFFFFFu && readable4(tbl + id * 0x0Cu + 8))
        ent = rd32(tbl + id * 0x0Cu + 8);
    if (ent && readable4(ent + 0x14)) {
        f14 = rd32(ent + 0x14);
        top = f14 >> 21;
    }

    u9log_line("F-5 guard: spell %08X vtable %08X (%s) [+0x18 %u, +0x20 %u],"
           " handle at +0xF0 = %u, entity %08X, +0x14 = %08X, top11 = %u  -> %s",
           (unsigned)sp,
           readable4(sp) ? rd32(sp) : 0,
           (readable4(sp) && rd32(sp) == 0x00756260u) ? "SpellTeleport"
                                                      : "another spell class",
           readable4(sp + 0x18) ? rd32(sp + 0x18) : 0,
           readable4(sp + 0x20) ? rd32(sp + 0x20) : 0,
           id, (unsigned)ent, f14, top,
           !ent ? "entity did not resolve (stale handle)"
           : (top == 0 || top == 0x7FEu || top == 0x7FFu)
             ? "the SENTINEL for no resource - the engine's own no-value, not"
               " missing data"
           : "resource lookup failed or its +0x0C was null");
    if (g_entityDumper) g_entityDumper("+0xF0 entity", id, ent);
}

static unsigned g_f5bLogged;
static volatile uint32_t g_f5bFired;

static void __attribute__((cdecl)) f5b_report(void *spell)
{
    uintptr_t sp = (uintptr_t)spell;
    uint32_t id = 0xFFFFFFFFu, vt = 0;

    g_f5bFired++;
    if (g_f5bLogged >= 20) return;
    g_f5bLogged++;
    if (readable4(sp)) vt = rd32(sp);
    if (readable4(sp + 0xF4)) id = rd32(sp + 0xF4);
    u9log_line("F-5b guard: spell %08X vtable %08X (%s), handle at +0xF4 = %u"
           " resolved to NULL", (unsigned)sp, vt,
           vt == 0x00756260u ? "SpellTeleport" : "another spell class", id);
    {
        uintptr_t tbl = readable4(0x008515D0u) ? rd32(0x008515D0u) : 0;
        uintptr_t ent = 0;
        if (tbl && id != 0xFFFFFFFFu && readable4(tbl + id * 0x0Cu + 8))
            ent = rd32(tbl + id * 0x0Cu + 8);
        if (g_entityDumper) g_entityDumper("+0xF4 entity", id, ent);
    }
}





static const uint8_t F5_EXPECT_A[6] = { 0x8B, 0x95, 0xE8, 0x00, 0x00, 0x00 };
static const U9PatchSpec F5_SPEC_A = {
    "F5_GUARD_A", U9PATCH_STUB, U9SYM_TELEPORT_NULL_A,
    6, F5_EXPECT_A, 6, U9PF_EXCLUSIVE
};

static void f5_install(void)
{
    static const uint8_t expect[6] = { 0x8B, 0x95, 0xE8, 0x00, 0x00, 0x00 };
    uintptr_t F5_SITE   = u9sym(U9SYM_TELEPORT_NULL_A);
    uintptr_t F5_RESUME = u9sym(U9SYM_TELEPORT_RESUME_A);
    uint8_t *st;
    unsigned i;

    if (!F5_SITE || !F5_RESUME) return;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("F-5: no memory for the stub"); return; }

    st[0]  = 0x85; st[1] = 0xED;
    st[2]  = 0x75; st[3] = 0x12;
    st[4]  = 0x60;
    st[5]  = 0x56;
    st[6]  = 0xE8;
    *(int32_t *)(st + 7) = (int32_t)((uintptr_t)&f5_report - (uintptr_t)(st + 11));
    st[11] = 0x83; st[12] = 0xC4; st[13] = 0x04;
    st[14] = 0x61;
    st[15] = 0xE9;
    *(int32_t *)(st + 16) = (int32_t)(g_f5Bail - (uintptr_t)(st + 20));
    st[20] = 0x90; st[21] = 0x90;
    for (i = 0; i < 6; i++) st[22 + i] = expect[i];
    st[28] = 0xE9;
    *(int32_t *)(st + 29) = (int32_t)(F5_RESUME - (uintptr_t)(st + 33));

    {
        uint8_t jmp[6];
        jmp[0] = 0xE9;
        *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (F5_SITE + 5));
        jmp[5] = 0x90;
        if (!u9patch_install_bytes(&F5_SPEC_A, jmp, 6)) {
            VirtualFree(st, 0, MEM_RELEASE);
            return;
        }
    }
    g_f5Stub = st;
    g_f5Patched = 1;
    u9log_line("F-5: guard A bails to 0x%08X (%s)", (unsigned)g_f5Bail,
               g_f5Bail == u9sym(U9SYM_TELEPORT_BAIL_END)
                 ? "slot 0x2C, ending the spell" : "the plain epilogue");
}

static uint8_t  *g_f5bStub;
static int       g_f5bPatched;

static const uint8_t F5_EXPECT_B[5] = { 0xE8, 0xE4, 0x4F, 0xE7, 0xFF };
static const U9PatchSpec F5_SPEC_B = {
    "F5_GUARD_B", U9PATCH_STUB, U9SYM_TELEPORT_NULL_B,
    5, F5_EXPECT_B, 5, U9PF_EXCLUSIVE
};

static void f5b_install(void)
{
    uintptr_t F5B_SITE   = u9sym(U9SYM_TELEPORT_NULL_B);
    uintptr_t F5B_TARGET = u9sym(U9SYM_TELEPORT_CALL_TARGET);
    uintptr_t F5B_RESUME = F5B_SITE + 5;
    uint8_t *st;

    if (!F5B_SITE || !F5B_TARGET) return;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("F-5b: no memory for the stub"); return; }

    st[0]  = 0x85; st[1] = 0xC9;
    st[2]  = 0x74; st[3] = 0x0B;
    st[4]  = 0xE8;
    *(int32_t *)(st + 5) = (int32_t)(F5B_TARGET - (uintptr_t)(st + 9));
    st[9]  = 0xE9;
    *(int32_t *)(st + 10) = (int32_t)(F5B_RESUME - (uintptr_t)(st + 14));
    st[14] = 0x90;
    st[15] = 0x60;
    st[16] = 0x56;
    st[17] = 0xE8;
    *(int32_t *)(st + 18) = (int32_t)((uintptr_t)&f5b_report - (uintptr_t)(st + 22));
    st[22] = 0x83; st[23] = 0xC4; st[24] = 0x04;
    st[25] = 0x61;
    st[26] = 0x83; st[27] = 0xC4; st[28] = 0x04;
    st[29] = 0xE9;
    *(int32_t *)(st + 30) = (int32_t)(g_f5Bail - (uintptr_t)(st + 34));

    {
        uint8_t jmp[5];
        jmp[0] = 0xE9;
        *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (F5B_SITE + 5));
        if (!u9patch_install_bytes(&F5_SPEC_B, jmp, 5)) {
            VirtualFree(st, 0, MEM_RELEASE);
            return;
        }
    }
    g_f5bStub = st;
    g_f5bPatched = 1;
}

static uint8_t          *g_recastStub;
static volatile uint32_t g_recastShort;
static int               g_recastMs = 250;
static unsigned          g_recastLogged;
static struct { uintptr_t actor; int x, y; DWORD at; } g_blinkMark[4];
static unsigned          g_blinkMarkNext;

static int ent_xy(uintptr_t ent, int *x, int *y)
{
    uintptr_t page;
    uint32_t xy;

    if (!ent || !readable4(ent + 0x14) || (rd32(ent + 0x14) & 0x200u)) return 0;
    page = ent & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(ent + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + 0x10);
    return 1;
}

static void blink_mark(uintptr_t ent, unsigned id)
{
    uintptr_t tbl, act;
    int x, y;

    if (id >= ACTOR_COUNT || !ent_xy(ent, &x, &y)) return;
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (!tbl || !readable4(tbl + id * 4u)) return;
    act = rd32(tbl + id * 4u);
    if (!act) return;
    g_blinkMark[g_blinkMarkNext].actor = act;
    g_blinkMark[g_blinkMarkNext].x = x;
    g_blinkMark[g_blinkMarkNext].y = y;
    g_blinkMark[g_blinkMarkNext].at = u9time_now();
    g_blinkMarkNext = (g_blinkMarkNext + 1) % 4;
}

#define MOVER_VTABLE 0x00749C00u

static uint8_t          *g_blinkStub, *g_queryStub, *g_levelStub;
static volatile uint32_t g_blinkLoosened, g_blinkKept, g_blinkSteep, g_blinkLevelled;
static volatile uint32_t g_blinkGrounded;
static unsigned          g_blinkLogged, g_steepLogged, g_levelLogged;
static float             g_blinkSteepRad = 0.3490659f;

static struct { uintptr_t mover, dest; uint32_t tol; } g_blinkArm;

static void (*g_queryObserver)(uint32_t *regs);

void u9fix_set_mover_query_observer(void (*fn)(uint32_t *regs))
{
    g_queryObserver = fn;
}

static float fbits(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }

static uint32_t __attribute__((cdecl)) blink_tolerance(uintptr_t mover,
                                                       uintptr_t spell,
                                                       uintptr_t sp)
{
    uintptr_t tbl, ent = 0;
    unsigned id = 0;
    float tol = 0.0f, step, scale;
    uint32_t bits, h;

    if (readable4(spell + 0xF0) && readable4(ENTITY_TABLE)) {
        h = rd32(spell + 0xF0);
        tbl = rd32(ENTITY_TABLE);
        if (tbl && readable4(tbl + h * 0x0Cu + 8)) ent = rd32(tbl + h * 0x0Cu + 8);
        if (ent && readable4(ent + 0x08)) id = rd32(ent + 0x08) >> 16;
    }
    if (ent && id != 0 && mover && readable4(mover) &&
        rd32(mover) == MOVER_VTABLE && readable4(mover + 0x638)) {
        step  = fbits(rd32(mover + 0x1F8));
        scale = fbits(rd32(mover + 0x638));
        tol = step * scale;
        if (!(tol > 0.0f && tol <= 200.0f)) tol = 0.0f;
    }
    memcpy(&bits, &tol, 4);
    if (tol > 0.0f) {
        g_blinkLoosened++;
        blink_mark(ent, id);
        g_blinkArm.mover = mover;
        g_blinkArm.dest  = sp + 12 + 0x1C;
        g_blinkArm.tol   = bits;
    }
    else g_blinkKept++;
    if (g_blinkLogged < 10) {
        g_blinkLogged++;
        u9log_line("I-2: blink by caster id %u, mover %08X: tolerance %d%s",
                   id, (unsigned)mover, (int)(tol + 0.5f),
                   tol > 0.0f ? " (its step height)"
                   : id == 0 ? " (the Avatar keeps the engine's zero)"
                             : " (not a mover this fix knows, left at zero)");
    }
    return bits;
}

static void __attribute__((cdecl)) mover_query(uint32_t *regs)
{
    uintptr_t base = regs[3];

    if (g_blinkArm.mover && regs[1] == g_blinkArm.mover &&
        readable4(base + 0x58) && rd32(base + 0x50) == g_blinkArm.dest &&
        rd32(base + 0x58) == g_blinkArm.tol) {
        float gz = fbits(rd32(base + 0x30)), pz = fbits(rd32(base + 0x14));
        float ang = atanf(fbits(rd32(base + 0x34)));

        g_blinkArm.mover = 0;
        if (gz > pz + 2.0f && ang > g_blinkSteepRad) {
            *(volatile uint32_t *)(base + 0x58) = 0;
            g_blinkSteep++;
            if (g_steepLogged < 10) {
                g_steepLogged++;
                u9log_line("I-2: blink refused a step %d up onto a %d degree"
                           " surface - the roof rule", (int)(gz - pz + 0.5f),
                           (int)(ang * 57.29578f + 0.5f));
            }
        }

        if (gz != -1.0f && gz != 3.402823466e38f && readable4(base + 0x20)) {
            float nz = fbits(rd32(base + 0x20));
            memcpy((void *)(base + 0x20), &gz, 4);
            if (nz - gz > 1.0f || gz - nz > 1.0f) g_blinkGrounded++;
        }
    }
    if (g_queryObserver) g_queryObserver(regs);
}

static void __attribute__((cdecl)) blink_level(uint32_t *regs)
{
    uintptr_t out = regs[1], base = regs[3];
    float ez, was;

    if (!readable4(base + 0x1C) || !readable4(out + 8)) return;
    ez  = fbits(rd32(base + 0x1C));
    was = fbits(rd32(out + 8));
    if (!(ez == ez) || ez > 1.0e6f || ez < -1.0e6f) return;
    memcpy((void *)(out + 8), &ez, 4);
    g_blinkLevelled++;
    if (g_levelLogged < 10) {
        g_levelLogged++;
        u9log_line("I-2: blink spot height %d -> %d, the enemy's feet (%d)",
                   (int)was, (int)ez, (int)(ez - was));
    }
}

static uint8_t *emit_observe(uint8_t *p, void (*fn)(uint32_t *))
{
    *p++ = 0x60;
    *p++ = 0x54;
    *p++ = 0xE8;
    *(int32_t *)p = (int32_t)((uintptr_t)fn - ((uintptr_t)p + 4));
    p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;
    *p++ = 0x61;
    return p;
}

static int jump_to_stub(const U9PatchSpec *spec, uintptr_t site, uint8_t *st)
{
    uint8_t jmp[16];
    unsigned i;

    if (spec->length < 5 || spec->length > sizeof jmp) return 0;
    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    for (i = 5; i < spec->length; i++) jmp[i] = 0x90;
    return u9patch_install_bytes(spec, jmp, spec->length);
}

static const uint8_t BLINK_EXPECT[8] = { 0x6A, 0x00, 0x8D, 0x44, 0x24, 0x60,
                                         0x6A, 0x00 };
static const U9PatchSpec BLINK_SPEC = {
    "I2_BLINK_TOLERANCE", U9PATCH_STUB, U9SYM_TELEPORT_TOLERANCE_PUSH,
    8, BLINK_EXPECT, 8, U9PF_EXCLUSIVE
};
static const uint8_t QUERY_EXPECT[6] = { 0x8B, 0x8E, 0xD8, 0x00, 0x00, 0x00 };
static const U9PatchSpec QUERY_SPEC = {
    "I2_MOVER_QUERY", U9PATCH_STUB, U9SYM_MOVER_QUERY_DONE,
    6, QUERY_EXPECT, 6, U9PF_EXCLUSIVE
};
static const uint8_t LEVEL_EXPECT[6] = { 0x8B, 0xC6, 0x5E, 0x83, 0xC4, 0x34 };
static const U9PatchSpec LEVEL_SPEC = {
    "I2_BLINK_LEVEL", U9PATCH_STUB, U9SYM_BLINK_SPOT_RETURN,
    6, LEVEL_EXPECT, 6, U9PF_EXCLUSIVE
};

static uint8_t *stub_alloc(void)
{
    uint8_t *st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_EXECUTE_READWRITE);
    if (!st) u9log_line("I-2: no memory for a stub");
    return st;
}

static void blink_install(void)
{
    uintptr_t site   = u9sym(U9SYM_TELEPORT_TOLERANCE_PUSH);
    uintptr_t resume = u9sym(U9SYM_TELEPORT_TOLERANCE_RESUME);
    uint8_t *st;
    unsigned i;

    if (!site || !resume || !(st = stub_alloc())) return;

    for (i = 0; i < 6; i++) st[i] = BLINK_EXPECT[i];
    st[6]  = 0x50;
    st[7]  = 0x52;
    st[8]  = 0x54;
    st[9]  = 0x56;
    st[10] = 0x52;
    st[11] = 0xE8;
    *(int32_t *)(st + 12) = (int32_t)((uintptr_t)&blink_tolerance - (uintptr_t)(st + 16));
    st[16] = 0x83; st[17] = 0xC4; st[18] = 0x0C;
    st[19] = 0x89; st[20] = 0xC1;
    st[21] = 0x5A;
    st[22] = 0x58;
    st[23] = 0x51;
    st[24] = 0xE9;
    *(int32_t *)(st + 25) = (int32_t)(resume - (uintptr_t)(st + 29));
    FlushInstructionCache(GetCurrentProcess(), st, 29);

    if (!jump_to_stub(&BLINK_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_blinkStub = st;
    u9log_line("I-2: NPC blinks get their step height as the landing tolerance");
}

static void query_install(void)
{
    uintptr_t site   = u9sym(U9SYM_MOVER_QUERY_DONE);
    uintptr_t resume = u9sym(U9SYM_MOVER_QUERY_RESUME);
    uint8_t *st, *p;
    unsigned i;

    if (!site || !resume || !(st = stub_alloc())) return;
    p = emit_observe(st, mover_query);
    for (i = 0; i < 6; i++) *p++ = QUERY_EXPECT[i];
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&QUERY_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_queryStub = st;
    if (g_blinkStub)
        u9log_line("I-2: a blink's step up onto ground steeper than %d degrees"
                   " keeps the engine's zero", (int)(g_blinkSteepRad * 57.29578f + 0.5f));
}

static void level_install(void)
{
    uintptr_t site = u9sym(U9SYM_BLINK_SPOT_RETURN);
    uint8_t *st, *p;
    unsigned i;

    if (!site || !(st = stub_alloc())) return;
    p = emit_observe(st, blink_level);
    for (i = 0; i < 6; i++) *p++ = LEVEL_EXPECT[i];
    *p++ = 0xC2; *p++ = 0x04; *p++ = 0x00;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&LEVEL_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_levelStub = st;
    u9log_line("I-2: blink spots are levelled with the enemy's feet");
}


typedef int  (U9CALL *PlaceAtFn)(void *self, float *dest, float facing,
                                 float tol, int testOnly);
typedef void (U9CALL *SetCylFn)(void *self, float *vec, int a, int b);

static uint8_t          *g_searchStub, *g_searchStubB;
static int               g_searchRings = 5;
static int               g_searchDropOn = 1;
static int               g_bandUnits = 50;
static volatile uint32_t g_bandRejected;
static volatile uint32_t g_searchPicked, g_searchMoved, g_searchNone, g_searchSkipped;
static unsigned          g_searchLogged;

static int in_fast_area(const float p[3])
{
    uintptr_t mgr, grid, cell, slot;
    int cx, cy;

    mgr = readable4(0x00AD127Cu) ? rd32(0x00AD127Cu) : 0;
    if (!mgr || !readable4(mgr + 0x64)) return 0;
    grid = rd32(mgr + 0x64);
    if (!grid || !readable4(grid + 0x30)) return 0;
    if (!(p[0] > -1.0e8f && p[0] < 1.0e8f && p[1] > -1.0e8f && p[1] < 1.0e8f))
        return 0;
    cx = ((int)p[0]) >> 10;
    cy = ((int)p[1]) >> 10;
    if (cx < 0 || cy < 0 || cx >= ((int32_t)rd32(grid + 0x30) >> 3) ||
        cy >= ((int32_t)rd32(grid + 0x2C) >> 3))
        return 0;
    cell = rd32(mgr + 0x44) +
           ((uintptr_t)rd32(mgr + 0x4C) * (unsigned)cy + (unsigned)cx) * 0x28u;
    if (!cell) return 0;
    slot = cell + rd32(mgr + 0x18) * 4u;
    return readable4(slot) && rd32(slot) != 0;
}

static int blink_test(uintptr_t mover, float cand[3], float tol,
                      const float cyl[3])
{
    uintptr_t vt = rd32(mover);
    PlaceAtFn place = (PlaceAtFn)rd32(vt + 0xE4);
    int ok;

    memcpy(&g_blinkArm.tol, &tol, 4);
    g_blinkArm.dest  = (uintptr_t)cand;
    g_blinkArm.mover = mover;
    ok = place((void *)mover, cand, 0.0f, tol, 1);
    g_blinkArm.mover = 0;

    if (memcmp((const void *)(mover + 0x160), cyl, 12) != 0) {
        float back[3];
        memcpy(back, cyl, 12);
        ((SetCylFn)rd32(vt + 0x12C))((void *)mover, back, 0, 0);
    }
    return ok;
}

static int               g_footUnits = 48;
static volatile uint32_t g_footRejected;
static unsigned          g_footLogged;

static int blink_footing(uintptr_t mover, const float spot[3], float tol,
                         const float cyl[3], int *tests)
{
    static const float dir[8][2] = {
        { 1.0f, 0.0f }, { 0.7071f, 0.7071f }, { 0.0f, 1.0f }, { -0.7071f, 0.7071f },
        { -1.0f, 0.0f }, { -0.7071f, -0.7071f }, { 0.0f, -1.0f }, { 0.7071f, -0.7071f }
    };
    float p[3];
    int i, ok = 0;

    for (i = 0; i < 8; i++) {
        p[0] = spot[0] + dir[i][0] * (float)g_footUnits;
        p[1] = spot[1] + dir[i][1] * (float)g_footUnits;
        p[2] = spot[2];
        if (!in_fast_area(p)) continue;
        (*tests)++;
        if (!blink_test(mover, p, tol, cyl)) continue;
        if (p[2] - spot[2] > tol || spot[2] - p[2] > tol) continue;
        ok++;
    }
    return ok;
}

static int blink_search_run(uint32_t *regs, int tested)
{
    uintptr_t actor = regs[1], spotp = regs[7], mover, ent = 0, tbl;
    float *spot = (float *)spotp;
    float orig[3], cand[3], cyl[3], tol, diam;
    int ring, x, y, tests = 0;
    uint32_t h;

    if (!readable4(actor + 0x14) || !readable4(spotp + 8)) return 0;
    mover = rd32(actor + 0x14);
    if (!mover || !readable4(mover) || rd32(mover) != MOVER_VTABLE ||
        !readable4(mover + 0x638)) {
        g_searchSkipped++;
        if (g_searchLogged < 20) {
            g_searchLogged++;
            u9log_line("I-2 search: actor %08X has no mover this fix knows"
                       " (%08X) - spot left as picked", (unsigned)actor,
                       (unsigned)mover);
        }
        return 0;
    }
    h = rd32(mover + 0xFC);
    tbl = readable4(ENTITY_TABLE) ? rd32(ENTITY_TABLE) : 0;
    if (tbl && readable4(tbl + h * 0x0Cu + 8)) ent = rd32(tbl + h * 0x0Cu + 8);
    if (!ent || !readable4(ent + 0x14) || !(rd32(ent + 0x14) & 0x2u) ||
        (rd32(mover + 0xD8) & 0x200u)) {
        g_searchSkipped++;
        return 0;
    }
    tol  = fbits(rd32(mover + 0x1F8)) * fbits(rd32(mover + 0x638));
    diam = fbits(rd32(mover + 0x198));
    if (!(tol > 0.0f && tol <= 200.0f) || !(diam >= 4.0f && diam <= 400.0f)) {
        g_searchSkipped++;
        return 0;
    }
    memcpy(orig, spot, 12);
    memcpy(cyl, (const void *)(mover + 0x160), 12);

    for (ring = 0; ring <= g_searchRings; ring++) {
        int n = ring ? 8 * ring : 1, k;
        for (k = 0; k < n; k++) {
            if (!ring) { x = 0; y = 0; }
            else if (k < 2 * (2 * ring + 1)) {
                x = -ring + k / 2;
                y = (k & 1) ? ring : -ring;
            } else {
                int j = k - 2 * (2 * ring + 1);
                y = -(ring - 1) + j / 2;
                x = (j & 1) ? ring : -ring;
            }
            cand[0] = orig[0] + (float)x * diam;
            cand[1] = orig[1] + (float)y * diam;
            cand[2] = orig[2];
            if (!in_fast_area(cand)) continue;
            tests++;
            if (!blink_test(mover, cand, tol, cyl)) continue;
            if (g_bandUnits > 0 &&
                (cand[2] - orig[2] > (float)g_bandUnits ||
                 orig[2] - cand[2] > (float)g_bandUnits)) {
                g_bandRejected++;
                continue;
            }
            if (g_footUnits > 0) {
                int sides = blink_footing(mover, cand, tol, cyl, &tests);
                if (sides < 6) {
                    g_footRejected++;
                    if (g_footLogged < 10) {
                        g_footLogged++;
                        u9log_line("I-2 search: a spot at ring %d fits but has ground"
                                   " on only %d of 8 sides within %d - a wall top or a"
                                   " ledge; looking on", ring, sides, g_footUnits);
                    }
                    continue;
                }
            }

            memcpy(spot, cand, 12);
            if (!ring) g_searchPicked++; else g_searchMoved++;
            if (g_searchLogged < 20) {
                g_searchLogged++;
                u9log_line("I-2 search: %s after %d test%s, ring %d of %d,"
                           " %d from the picked spot, height %d -> %d",
                           ring ? "moved the spot" : "the picked spot fits",
                           tests, tests == 1 ? "" : "s", ring, g_searchRings,
                           (int)(sqrtf((float)(x * x + y * y)) * diam + 0.5f),
                           (int)orig[2], (int)cand[2]);
            }
            return 0;
        }
    }
    memcpy(spot, orig, 12);
    g_searchNone++;
    if (g_searchLogged < 20) {
        g_searchLogged++;
        u9log_line("I-2 search: nothing fits within %d rings of %d (%d tests)"
                   " - %s", g_searchRings, (int)(diam + 0.5f), tests,
                   !tested ? "spot left as picked"
                   : g_searchDropOn ? "cast dropped, before the game's own"
                     " check could move her there untested"
                   : "spot left as picked (dropping is off)");
    }
    return 1;
}

static void __attribute__((cdecl)) blink_search_a(uint32_t *regs)
{
    blink_search_run(regs, 0);
}

static volatile uint32_t g_searchDropNow, g_searchDropped;

static void __attribute__((cdecl)) blink_search_b(uint32_t *regs)
{
    g_searchDropNow = blink_search_run(regs, 1) && g_searchDropOn;
    if (g_searchDropNow) g_searchDropped++;
}

static const uint8_t SEARCH_EXPECT_A[5] = { 0x50, 0x8D, 0x4C, 0x24, 0x24 };
static const uint8_t SEARCH_EXPECT_B[5] = { 0x50, 0x8D, 0x4C, 0x24, 0x20 };
static const U9PatchSpec SEARCH_SPEC_A = {
    "I2_BLINK_SEARCH_A", U9PATCH_STUB, U9SYM_BLINK_SPOT_PICKED,
    5, SEARCH_EXPECT_A, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec SEARCH_SPEC_B = {
    "I2_BLINK_SEARCH_B", U9PATCH_STUB, U9SYM_BLINK_SPOT_PICKED_B,
    5, SEARCH_EXPECT_B, 5, U9PF_EXCLUSIVE
};

static uint8_t *search_site(const U9PatchSpec *spec, U9Symbol resumeSym,
                            void (*fn)(uint32_t *), uintptr_t bail)
{
    uintptr_t site = u9sym(spec->target), resume = u9sym(resumeSym);
    uint8_t *st, *p;
    unsigned i;

    if (!site || !resume || !(st = stub_alloc())) return 0;
    p = emit_observe(st, fn);
    if (bail) {
        *p++ = 0x83; *p++ = 0x3D;
        *(uint32_t *)p = (uint32_t)(uintptr_t)&g_searchDropNow;
        p += 4;
        *p++ = 0x00;
        *p++ = 0x0F; *p++ = 0x85;
        *(int32_t *)p = (int32_t)(bail - ((uintptr_t)p + 4));
        p += 4;
    }
    for (i = 0; i < 5; i++) *p++ = spec->expected[i];
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));
    if (!jump_to_stub(spec, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return 0; }
    return st;
}

static void search_install(void)
{
    g_searchStub  = search_site(&SEARCH_SPEC_A, U9SYM_BLINK_SPOT_PICKED_RESUME,
                                blink_search_a, 0);
    g_searchStubB = search_site(&SEARCH_SPEC_B, U9SYM_BLINK_SPOT_PICKED_B_RESUME,
                                blink_search_b, u9sym(U9SYM_BLINK_PRECAST_BAIL));
    u9log_line("I-2: blink spots are tested before the cast, and moved up to %d"
               " rings if they do not fit (untested caster %s, tested caster %s)",
               g_searchRings, g_searchStub ? "hooked" : "REFUSED",
               g_searchStubB ? "hooked" : "REFUSED");
}


static uint8_t          *g_rangeStub;
static volatile uint32_t g_rangeStand, g_rangeStood, g_rangeWalked;
static unsigned          g_rangeLogged;
static int               g_rangePct = 125;

static int caster_can_bolt(uintptr_t act, unsigned mana)
{
    uintptr_t node;
    unsigned guard = 0;

    if (!readable4(act + 0x240)) return 0;
    for (node = rd32(act + 0x240); node && guard++ < 64; node = rd32(node + 4)) {
        uintptr_t spell;
        if (!readable4(node + 8)) return 0;
        spell = rd32(node + 8);
        if (spell && readable4(spell + 0x18) && (rd32(spell + 0x18) & 0xFFu) <= mana)
            return 1;
    }
    return 0;
}

#define HOLD_SLOTS 8
static struct {
    uintptr_t act;
    DWORD     seen, freeSince, hopAt, noHopUntil;
    int       hx, hy;
} g_hold[HOLD_SLOTS];
static int               g_holdMs = 3000, g_hopUnits = 200;
static volatile uint32_t g_hops, g_hopFailed;
static unsigned          g_hopLogged;

typedef void (U9CALL *ApproachAtFn)(void *self, float x, float y, float z,
                                    float dist, int speed, int wait, int moveIfFailed);

static int hold_slot(uintptr_t act, DWORD now, int make)
{
    unsigned i, lru = 0;

    for (i = 0; i < HOLD_SLOTS; i++)
        if (g_hold[i].act == act) { g_hold[i].seen = now; return (int)i; }
    if (!make) return -1;
    for (i = 0; i < HOLD_SLOTS; i++) {
        if (!g_hold[i].act) { lru = i; break; }
        if (now - g_hold[i].seen > now - g_hold[lru].seen) lru = i;
    }
    memset(&g_hold[lru], 0, sizeof g_hold[lru]);
    g_hold[lru].act = act;
    g_hold[lru].seen = now;
    return (int)lru;
}

static int ent_xyz(uintptr_t ent, int p[3])
{
    if (!ent_xy(ent, &p[0], &p[1]) || !readable4(ent + 8)) return 0;
    p[2] = (int16_t)(rd32(ent + 8) & 0xFFFFu);
    return 1;
}

static uintptr_t actor_ent(uintptr_t act)
{
    uintptr_t tbl;
    uint32_t h;

    if (!act || !readable4(act + 0x18) || !readable4(ENTITY_TABLE)) return 0;
    h = rd32(act + 0x18);
    tbl = rd32(ENTITY_TABLE);
    if (!tbl || !readable4(tbl + h * 0x0Cu + 8)) return 0;
    return rd32(tbl + h * 0x0Cu + 8);
}

static int caster_hop(uintptr_t act, int s, DWORD now, float dist)
{
    uintptr_t fn = u9sym(U9SYM_CASTER_APPROACH_AT), vt, tbl, eact;
    int me[3], en[3];
    int32_t idx;
    double dx, dy, d, t;
    float strike = 0.0f;

    if (!fn || !readable4(act) || !(vt = rd32(act)) || !readable4(vt + 0xD0) ||
        rd32(vt + 0xD0) != fn)
        return 0;
    if ((int32_t)(now - g_hold[s].noHopUntil) < 0) return 0;
    if (!ent_xyz(actor_ent(act), me)) return 0;
    idx = (int32_t)rd32(act + 0x104);
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (idx < 0 || (unsigned)idx >= ACTOR_COUNT || !tbl || !readable4(tbl + (unsigned)idx * 4u))
        return 0;
    eact = rd32(tbl + (unsigned)idx * 4u);
    if (!ent_xyz(actor_ent(eact), en)) return 0;
    if (readable4(act + 0x22C)) strike = fbits(rd32(act + 0x22C));
    if (!(strike >= 0.0f && strike < 1000.0f)) strike = 0.0f;
    dx = en[0] - me[0];
    dy = en[1] - me[1];
    d  = sqrt(dx * dx + dy * dy);
    if (d <= (double)g_hopUnits + strike + 40.0) return 0;

    if (g_hold[s].hopAt && now - g_hold[s].hopAt < 700u) {
        double mx = me[0] - g_hold[s].hx, my = me[1] - g_hold[s].hy;
        if (mx * mx + my * my < 20.0 * 20.0) {
            g_hopFailed++;
            g_hold[s].hopAt = 0;
            g_hold[s].noHopUntil = now + 3000u;
            u9log_line("I-2 range: a caster's stretch ended where it began; the stock"
                       " approach for 3 s");
            return 0;
        }
    }
    t = (double)g_hopUnits / d;
    g_hold[s].hopAt = now;
    g_hold[s].hx = me[0];
    g_hold[s].hy = me[1];
    g_hops++;
    if (g_hopLogged < 12) {
        g_hopLogged++;
        u9log_line("I-2 range: a caster cast nothing for %d ms after her pause; she"
                   " walks %d towards the enemy, %d away", g_holdMs, g_hopUnits, (int)dist);
    }
    ((ApproachAtFn)fn)((void *)act, (float)(me[0] + dx * t), (float)(me[1] + dy * t),
                       (float)(me[2] + (en[2] - me[2]) * t), -1.0f, 0, 0, 0);
    return 1;
}

static void __attribute__((cdecl)) caster_approach(uint32_t *regs)
{
    uintptr_t act = regs[1], npc;
    float dist;
    unsigned mana, radius;
    DWORD now;
    int s;

    g_rangeStand = 0;
    if (!readable4(regs[3] + 4) || !readable4(act + 0x0Cu)) return;
    npc = rd32(act + 0x0Cu);
    if (!npc || !readable4(npc + 0x54)) return;
    mana   = (rd32(npc + 0x38) >> 16) & 0xFFFFu;
    radius = rd32(npc + 0x54) & 0xFFFFu;
    dist   = fbits(rd32(regs[3] + 4));
    if (!radius || !(dist > 0.0f) || !caster_can_bolt(act, mana)) return;

    now = u9time_now();
    if (dist > (float)radius * (float)g_rangePct / 100.0f) {
        g_rangeWalked++;
        if ((s = hold_slot(act, now, 0)) >= 0) g_hold[s].freeSince = 0;
        return;
    }
    s = hold_slot(act, now, 1);
    if (g_holdMs > 0 && readable4(act + 0x2B8) && (int32_t)rd32(act + 0x2B8) <= 0) {
        if (!g_hold[s].freeSince) g_hold[s].freeSince = now;
        if (now - g_hold[s].freeSince >= (DWORD)g_holdMs) {
            g_rangeStand = (uint32_t)caster_hop(act, s, now, dist);
            if (!g_rangeStand) g_rangeWalked++;
            return;
        }
    }
    else g_hold[s].freeSince = 0;

    g_rangeStand = 1;
    g_rangeStood++;
    if (g_rangeLogged < 10) {
        g_rangeLogged++;
        u9log_line("I-2 range: a caster holds at %d (%d%% of her notice radius"
                   " %u) with mana %u, instead of walking to melee range",
                   (int)dist, (int)(dist * 100.0f / (float)radius), radius, mana);
    }
}

static const uint8_t RANGE_EXPECT[6] = { 0x8B, 0x86, 0x04, 0x01, 0x00, 0x00 };
static const U9PatchSpec RANGE_SPEC = {
    "I2_CASTER_RANGE", U9PATCH_STUB, U9SYM_CASTER_APPROACH,
    6, RANGE_EXPECT, 6, U9PF_EXCLUSIVE
};

static void range_install(void)
{
    uintptr_t site   = u9sym(U9SYM_CASTER_APPROACH);
    uintptr_t resume = u9sym(U9SYM_CASTER_APPROACH_RESUME);
    uintptr_t skip   = u9sym(U9SYM_CASTER_APPROACH_SKIP);
    uint8_t *st, *p;
    unsigned i;

    if (!site || !resume || !skip || !(st = stub_alloc())) return;
    p = emit_observe(st, caster_approach);
    *p++ = 0x83; *p++ = 0x3D;
    *(uint32_t *)p = (uint32_t)(uintptr_t)&g_rangeStand;
    p += 4;
    *p++ = 0x00;
    *p++ = 0x0F; *p++ = 0x85;
    *(int32_t *)p = (int32_t)(skip - ((uintptr_t)p + 4));
    p += 4;
    for (i = 0; i < 6; i++) *p++ = RANGE_EXPECT[i];
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&RANGE_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_rangeStub = st;
    u9log_line("I-2: a caster with an affordable offensive spell holds her ground"
               " inside %d%% of her notice radius instead of walking to melee;"
               " after %d ms with nothing cast she walks towards the enemy %d at a"
               " time, deciding again after each", g_rangePct, g_holdMs, g_hopUnits);
}


static const uint8_t BLINKDIST_EXPECT[10] = { 0xBA, 0x50, 0x00, 0x00, 0x00,
                                              0xB9, 0x42, 0x00, 0x00, 0x00 };
static const U9PatchSpec BLINKDIST_SPEC = {
    "I2_BLINK_DISTANCE", U9PATCH_STUB, U9SYM_BLINK_DISTANCE,
    10, BLINKDIST_EXPECT, 10, U9PF_EXCLUSIVE
};

static void blinkdist_install(int pct, int cap)
{
    uint8_t b[10];
    uint32_t hi = 80u * (uint32_t)pct / 100u, lo = 66u * (uint32_t)pct / 100u;

    if (cap > 0 && hi > (uint32_t)cap) hi = (uint32_t)cap;
    if (lo + 10u > hi) lo = hi > 10u ? hi - 10u : hi;
    if (hi == 80u && lo == 66u) return;
    memcpy(b, BLINKDIST_EXPECT, sizeof b);
    memcpy(b + 1, &hi, 4);
    memcpy(b + 6, &lo, 4);
    if (u9patch_install_bytes(&BLINKDIST_SPEC, b, sizeof b))
        u9log_line("I-2: blinks land %u-%u%% of the caster's notice radius from"
                   " the enemy (stock 66-80)", (unsigned)lo, (unsigned)hi);
}


static void __attribute__((cdecl)) blink_recast(uint32_t *regs)
{
    uintptr_t act = regs[1], ent = 0, tbl;
    DWORD now = u9time_now();
    unsigned i;
    int x, y;
    double dx, dy;

    for (i = 0; i < 4; i++) {
        if (g_blinkMark[i].actor != act) continue;
        g_blinkMark[i].actor = 0;
        if (now - g_blinkMark[i].at > 10000u) return;
        if (readable4(act + 0x18) && readable4(ENTITY_TABLE)) {
            uint32_t h = rd32(act + 0x18);
            tbl = rd32(ENTITY_TABLE);
            if (tbl && readable4(tbl + h * 0x0Cu + 8)) ent = rd32(tbl + h * 0x0Cu + 8);
        }
        if (!ent_xy(ent, &x, &y)) return;
        dx = (double)x - g_blinkMark[i].x;
        dy = (double)y - g_blinkMark[i].y;
        if (dx * dx + dy * dy < 100.0 * 100.0) return;
        g_recastShort++;
        if (g_recastLogged < 10) {
            g_recastLogged++;
            u9log_line("I-2 recast: a caster landed %d away from where she blinked;"
                       " her pause is %d ms instead of %u", (int)sqrt(dx * dx + dy * dy),
                       g_recastMs, (unsigned)regs[7]);
        }
        regs[7] = (uint32_t)g_recastMs;
        return;
    }
}

static const uint8_t RECAST_EXPECT[6] = { 0x89, 0x86, 0xB8, 0x02, 0x00, 0x00 };
static const U9PatchSpec RECAST_SPEC = {
    "I2_BLINK_RECAST", U9PATCH_STUB, U9SYM_CASTER_PAUSE_SET,
    6, RECAST_EXPECT, 6, U9PF_EXCLUSIVE
};

static void recast_install(void)
{
    uintptr_t site   = u9sym(U9SYM_CASTER_PAUSE_SET);
    uintptr_t resume = u9sym(U9SYM_CASTER_PAUSE_RESUME);
    uint8_t *st, *p;
    unsigned i;

    if (!site || !resume || !(st = stub_alloc())) return;
    p = emit_observe(st, blink_recast);
    for (i = 0; i < 6; i++) *p++ = RECAST_EXPECT[i];
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    if (!jump_to_stub(&RECAST_SPEC, site, st)) { VirtualFree(st, 0, MEM_RELEASE); return; }
    g_recastStub = st;
    u9log_line("I-2: after a blink that moved her, a caster's pause is %d ms"
               " (stock 2000-4000)", g_recastMs);
}


#define PF_LEGGOAL_TO_GOAL  0x0Cu

typedef void *(U9CALL *VecAssignFn)(void *dst, const void *src);
static VecAssignFn o_chaseAssign;
static unsigned    g_chaseRefreshes;

static void *U9CALL h_chase_leggoal(void *dst, const void *src)
{
    void *r = o_chaseAssign(dst, src);
    o_chaseAssign((void *)((uintptr_t)dst - PF_LEGGOAL_TO_GOAL), src);
    g_chaseRefreshes++;
    return r;
}

static const uint8_t CHASE_CALL[5] = { 0xE8, 0x88, 0xFB, 0x0A, 0x00 };

static const U9PatchSpec CHASE_SPEC = {
    "I10_CHASE_GOAL", U9PATCH_CALLSITE, U9SYM_CHASE_LEGGOAL_CALL,
    5, CHASE_CALL, 5, U9PF_EXCLUSIVE
};

static void chase_goal_install(void)
{
    o_chaseAssign = (VecAssignFn)u9patch_install(&CHASE_SPEC,
                                                 (void *)h_chase_leggoal);
    u9log_line("I-10 chase goal: %s - a chasing NPC's detour now leads to"
               " where its target is, not where it was",
               o_chaseAssign ? "installed" : "REFUSED");
}

#define ACT_NPC_HANDLE  0xFCu

typedef void (U9CALL *PathToPointFn)(void *act, int x, int y, float tol, int flags);
static PathToPointFn o_readbookPath;
static int           g_readbookStandoff;
static unsigned      g_readbookMoved, g_readbookStayed, g_readbookLogged;

static int readbook_obj_xy(uintptr_t obj, int *x, int *y)
{
    uintptr_t page = obj & ~(uintptr_t)0xFFFu;
    uint32_t xy;
    if (!obj || !readable4(obj + 4) || !readable4(page + 0x10)) return 0;
    xy = rd32(obj + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + 0x10);
    return 1;
}

static void U9CALL h_readbook_path(void *act, int x, int y, float tol, int flags)
{
    uintptr_t a = (uintptr_t)act, et, obj = 0;
    int nx, ny;
    if (readable4(a + ACT_NPC_HANDLE) && readable4(ENTITY_TABLE) && (et = rd32(ENTITY_TABLE))) {
        uint32_t h = rd32(a + ACT_NPC_HANDLE);
        if (h && readable4(et + h * 0x0Cu + 8)) obj = rd32(et + h * 0x0Cu + 8);
    }
    if (obj && readbook_obj_xy(obj, &nx, &ny)) {
        double dx = nx - x, dy = ny - y, d = sqrt(dx * dx + dy * dy);
        int bx = x, by = y;
        if (d > g_readbookStandoff) {
            x = bx + (int)(dx * g_readbookStandoff / d);
            y = by + (int)(dy * g_readbookStandoff / d);
            g_readbookMoved++;
        } else {
            x = nx; y = ny;
            g_readbookStayed++;
        }
        if (g_readbookLogged < 20) {
            g_readbookLogged++;
            u9log_line("READBOOK npc %d: book at %d,%d, NPC at %d,%d (%.0f away) -> walks to %d,%d",
                       readable4(a + 0xD8) ? (int)rd32(a + 0xD8) : -1, bx, by, nx, ny, d, x, y);
        }
    }
    o_readbookPath(act, x, y, tol, flags);
}

static const uint8_t READBOOK_CALL[5] = { 0xE8, 0x01, 0x88, 0xFF, 0xFF };

static const U9PatchSpec READBOOK_SPEC = {
    "READBOOK_STANDOFF", U9PATCH_CALLSITE, U9SYM_READBOOK_PATH_CALL,
    5, READBOOK_CALL, 5, U9PF_EXCLUSIVE
};

static void readbook_install(int standoff)
{
    g_readbookStandoff = standoff;
    o_readbookPath = (PathToPointFn)u9patch_install(&READBOOK_SPEC, (void *)h_readbook_path);
    u9log_line("readbook standoff: %s - a shopkeeper reading a book stops %d from it,"
               " on its own side (pick-up reach 100)",
               o_readbookPath ? "installed" : "REFUSED", standoff);
}

#define AVATAR_ACTOR    0
#define GAIT_WALK       0
#define GAIT_RUN        1
#define ACTOR_MOVER     0x14u
#define MOVER_PROCTYPE  0x18u
#define MOVER_RUN_FWD   0x278u
#define NO_ANIM         0xFFFFFFFFu

typedef uint32_t (U9CALL *PathToActorFn)(void *self, int enemy, uint32_t dist,
                                         int speed, int wait, int moveIfFailed);
static PathToActorFn o_pathToActor;
static unsigned      g_runSwitched, g_runLogged, g_runKept;
static uint32_t      g_keptTypes[16];
static unsigned      g_keptTypeCount;

static int has_run_anim(void *self, uint32_t *procType)
{
    uintptr_t mover;

    *procType = 0;
    if (!readable4((uintptr_t)self + ACTOR_MOVER)) return 0;
    mover = rd32((uintptr_t)self + ACTOR_MOVER);
    if (!mover || !readable4(mover + MOVER_RUN_FWD)) return 0;
    *procType = rd32(mover + MOVER_PROCTYPE);
    return rd32(mover + MOVER_RUN_FWD) != NO_ANIM;
}

static uint32_t U9CALL h_approach_path(void *self, int enemy, uint32_t dist,
                                       int speed, int wait, int moveIfFailed)
{
    if (speed == GAIT_WALK && enemy == AVATAR_ACTOR) {
        uint32_t procType;
        if (!has_run_anim(self, &procType)) {
            unsigned i;
            g_runKept++;
            for (i = 0; i < g_keptTypeCount; i++) if (g_keptTypes[i] == procType) break;
            if (i == g_keptTypeCount && g_keptTypeCount < 16) {
                g_keptTypes[g_keptTypeCount++] = procType;
                u9log_line("COMBAT RUN: actor %08X (mover procType 0x%X) has no run"
                           " animation - left walking", (unsigned)(uintptr_t)self,
                           procType);
            }
            return o_pathToActor(self, enemy, dist, speed, wait, moveIfFailed);
        }
        speed = GAIT_RUN;
        g_runSwitched++;
        if (g_runLogged < 12) {
            g_runLogged++;
            u9log_line("COMBAT RUN: actor %08X approaches the Avatar running,"
                       " not walking (%u so far)", (unsigned)(uintptr_t)self,
                       g_runSwitched);
        }
    }
    return o_pathToActor(self, enemy, dist, speed, wait, moveIfFailed);
}

static const uint8_t APPROACH_CALL[5] = { 0xE8, 0x61, 0x1B, 0xF8, 0xFF };

static const U9PatchSpec APPROACH_SPEC = {
    "COMBAT_RUN", U9PATCH_CALLSITE, U9SYM_APPROACH_PATH_CALL,
    5, APPROACH_CALL, 5, U9PF_EXCLUSIVE
};

static void combat_run_install(void)
{
    o_pathToActor = (PathToActorFn)u9patch_install(&APPROACH_SPEC,
                                                   (void *)h_approach_path);
    u9log_line("COMBAT RUN: %s - an enemy that would walk at the Avatar runs, if it"
               " has a run animation",
               o_pathToActor ? "installed" : "REFUSED");
}

typedef void *(U9CALL *SpaceAtFn)(void *tree, float *point);
static SpaceAtFn o_spaceAt;
static unsigned  g_rainStopped;

static void *U9CALL h_rain_space_at(void *tree, float *point)
{
    void *space = o_spaceAt(tree, point);
    if (space && readable4((uintptr_t)(point + 8)) && point[8] < 0.5235988f) {
        point[8] = 1.5707964f;
        if (g_rainStopped++ < 4)
            u9log_line("I-19 RAIN: a drop fell into a space at %.0f,%.0f,%.0f - removed without"
                       " its splash at %.0f (%u so far)", (double)point[0], (double)point[1],
                       (double)point[2], (double)point[6], g_rainStopped);
    }
    return space;
}

static const uint8_t RAIN_SPACE_CALL[5] = { 0xE8, 0xDC, 0x14, 0xF8, 0xFF };

static const U9PatchSpec RAIN_SPACE_SPEC = {
    "RAIN_SPLASH", U9PATCH_CALLSITE, U9SYM_RAIN_SPACE_CALL,
    5, RAIN_SPACE_CALL, 5, U9PF_EXCLUSIVE
};

static void rain_splash_install(void)
{
    o_spaceAt = (SpaceAtFn)u9patch_install(&RAIN_SPACE_SPEC, (void *)h_rain_space_at);
    u9log_line("I-19 RAIN: %s - a rain drop that falls into a room is removed without"
               " a splash", o_spaceAt ? "installed" : "REFUSED");
}

#define SPACES_MGR      0x00AD1340u
#define SM_CAM_SPACE    0x10020u
#define SM_TREE         0x10008u
#define SM_FLAGS        0x1000Cu
#define SPACE_FLAGS     0x36u
#define CAMERA_OF_GAME  0xBCu
#define CAMERA_POS      0xC4u
#define CAMERA_FOCUS    0xE0u
#define SKY_STORM       0x6Cu

typedef void *(U9CALL *SpaceAtCallFn)(void *tree, const float *point);
static SpaceAtCallFn f_spaceAt;
static int           g_spaceAtChecked;

static int space_at_ready(void)
{
    static const uint8_t expSpaceAt[6] = { 0x53, 0x33, 0xDB, 0x56, 0x8B, 0xF1 };
    if (!g_spaceAtChecked) {
        uintptr_t at = u9sym(U9SYM_SPACE_AT);
        g_spaceAtChecked = 1;
        if (at && !memcmp((const void *)at, expSpaceAt, sizeof expSpaceAt)) f_spaceAt = (SpaceAtCallFn)at;
        else u9log_line("WEATHER: SpaceTree_SpaceAt is not where expected - rooms judged by the camera alone");
    }
    return f_spaceAt != NULL;
}

static uintptr_t space_at(const float *point)
{
    uintptr_t mgr = readable4(SPACES_MGR) ? rd32(SPACES_MGR) : 0, tree;
    if (!space_at_ready() || !mgr || !readable4(mgr + SM_TREE)) return 0;
    tree = rd32(mgr + SM_TREE);
    return tree ? (uintptr_t)f_spaceAt((void *)tree, point) : 0;
}

static int camera_points(float pos[3], float focus[3])
{
    uintptr_t game = readable4(0x00850228u) ? rd32(0x00850228u) : 0, cam;
    if (!game || !readable4(game + CAMERA_OF_GAME)) return 0;
    cam = rd32(game + CAMERA_OF_GAME);
    if (!cam || !readable4(cam + CAMERA_POS + 8) || !readable4(cam + CAMERA_FOCUS + 8)) return 0;
    memcpy(pos, (const void *)(cam + CAMERA_POS), 12);
    memcpy(focus, (const void *)(cam + CAMERA_FOCUS), 12);
    return 1;
}

static int view_indoors(void)
{
    uintptr_t mgr = readable4(SPACES_MGR) ? rd32(SPACES_MGR) : 0;
    float pos[3], focus[3];
    if (mgr && readable4(mgr + SM_CAM_SPACE) && (int)rd32(mgr + SM_CAM_SPACE) >= 0) return 1;
    return camera_points(pos, focus) && space_at(focus) != 0;
}

typedef void (__attribute__((fastcall)) *SkyFogFn)(uintptr_t sky);
static SkyFogFn o_skyFog;
static int      g_fogIndoors = -1;

static void __attribute__((fastcall)) h_sky_fog(uintptr_t sky)
{
    int indoors = readable4(sky + SKY_STORM) && view_indoors();
    if (indoors != g_fogIndoors) {
        g_fogIndoors = indoors;
        u9log_line("I-19 FOG: view %s - %s", indoors ? "indoors" : "outside",
                   indoors ? "fog as in clear weather" : "the storm's fog");
    }
    if (indoors) {
        float storm = *(float *)(sky + SKY_STORM);
        *(float *)(sky + SKY_STORM) = 0.0f;
        o_skyFog(sky);
        *(float *)(sky + SKY_STORM) = storm;
    } else {
        o_skyFog(sky);
    }
}

static const uint8_t SKY_FOG_CALL[5] = { 0xE8, 0x00, 0x05, 0x00, 0x00 };

static const U9PatchSpec SKY_FOG_SPEC = {
    "INDOOR_FOG", U9PATCH_CALLSITE, U9SYM_SKY_FOG_CALL,
    5, SKY_FOG_CALL, 5, U9PF_EXCLUSIVE
};

typedef void *(U9CALL *LightCtorFn)(void *self, int r, int g, int b, float px, float py, float pz,
                                    float spread, int range, int flags);
static LightCtorFn   o_lightCtor;
static float         g_hallHeight = 700.0f;
static unsigned      g_hallStrikes;

static int tall_at(const float *p)
{
    float up[3];
    up[0] = p[0]; up[1] = p[1]; up[2] = p[2] + g_hallHeight;
    return space_at(p) && space_at(up);
}

static int camera_in_hall(void)
{
    uintptr_t mgr = readable4(SPACES_MGR) ? rd32(SPACES_MGR) : 0, sp;
    float pos[3], focus[3];
    if (mgr && readable4(mgr + SM_FLAGS) && (rd32(mgr + SM_FLAGS) & 0x10u) &&
        readable4(mgr + SM_CAM_SPACE) && (int)rd32(mgr + SM_CAM_SPACE) >= 0) return 1;
    if (!camera_points(pos, focus)) return 0;
    sp = space_at(focus);
    if (sp && readable4(sp + SPACE_FLAGS) && (*(const uint8_t *)(sp + SPACE_FLAGS) & 1u)) return 1;
    return tall_at(focus) || tall_at(pos);
}

static void *U9CALL h_lightning_light(void *self, int r, int g, int b, float px, float py, float pz,
                                      float spread, int range, int flags)
{
    if (camera_in_hall()) {
        if (g_hallStrikes++ < 8)
            u9log_line("LIGHTNING: a strike of %d with the camera in a hall - its light built black"
                       " (%u so far)", r, g_hallStrikes);
        r = g = b = 0;
    }
    return o_lightCtor(self, r, g, b, px, py, pz, spread, range, flags);
}

typedef void (U9CALL *LightColourFn)(void *light, int r, int g, int b);
static LightColourFn o_lightColour;

static void U9CALL h_lightning_colour(void *light, int r, int g, int b)
{
    if (camera_in_hall()) r = g = b = 0;
    o_lightColour(light, r, g, b);
}

typedef void *(__attribute__((fastcall)) *PoofEnvFn)(int effect, int flags,
    uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
    uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
    uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15, uint32_t a16, uint32_t a17);
static PoofEnvFn o_boltFx;
static unsigned  g_boltsSkipped, g_boltsMade;

static void *__attribute__((fastcall)) h_lightning_bolt(int effect, int flags,
    uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
    uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
    uint32_t a12, uint32_t a13, uint32_t a14, uint32_t a15, uint32_t a16, uint32_t a17)
{
    if (camera_in_hall()) {
        if (g_boltsSkipped++ < 8)
            u9log_line("LIGHTNING: the bolt (and its light) not made with the camera sheltered"
                       " (%u so far)", g_boltsSkipped);
        return NULL;
    }
    if (g_boltsMade++ < 8) {
        uint32_t raw[6];
        float s[3], d[3];
        raw[0] = a0; raw[1] = a1; raw[2] = a2; raw[3] = a3; raw[4] = a4; raw[5] = a5;
        memcpy(s, raw, sizeof s);
        memcpy(d, raw + 3, sizeof d);
        u9log_line("LIGHTNING: a bolt made, camera not sheltered - from %.0f,%.0f,%.0f to %.0f,%.0f,%.0f"
                   " (%u so far)", (double)s[0], (double)s[1], (double)s[2], (double)d[0], (double)d[1],
                   (double)d[2], g_boltsMade);
    }
    return o_boltFx(effect, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,
                    a12, a13, a14, a15, a16, a17);
}

typedef void (U9CALL *LightListFn)(void *lightSys, void *light, int flag);
typedef void (U9CALL *LightDtorFn)(void *light);
typedef void (*FreeFn)(void *p);
static LightListFn o_lightAdd, o_lightRemove;
static LightDtorFn f_lightDtor;
static FreeFn      f_free;
static void       *g_keptOut;
static unsigned    g_keptOutCount;

static void U9CALL h_lightning_add(void *lightSys, void *light, int flag)
{
    if (light && f_lightDtor && f_free && !g_keptOut && camera_in_hall()) {
        g_keptOut = light;
        if (g_keptOutCount++ < 8)
            u9log_line("LIGHTNING: the strike's light kept out of the light system with the camera"
                       " sheltered (%u so far)", g_keptOutCount);
        return;
    }
    o_lightAdd(lightSys, light, flag);
}

static void U9CALL h_lightning_remove(void *lightSys, void *light, int flag)
{
    if (light && light == g_keptOut) {
        g_keptOut = NULL;
        f_lightDtor(light);
        f_free(light);
        return;
    }
    o_lightRemove(lightSys, light, flag);
}

static const uint8_t LIGHTNING_ADD_CALL[5] = { 0xE8, 0x0A, 0x96, 0xF1, 0xFF };
static const uint8_t LIGHTNING_REMOVE_CALL[5] = { 0xE8, 0x7E, 0x96, 0xF1, 0xFF };

static const U9PatchSpec LIGHTNING_ADD_SPEC = {
    "LIGHTNING_HALL_ADD", U9PATCH_CALLSITE, U9SYM_LIGHTNING_ADD_CALL,
    5, LIGHTNING_ADD_CALL, 5, U9PF_EXCLUSIVE
};

static const U9PatchSpec LIGHTNING_REMOVE_SPEC = {
    "LIGHTNING_HALL_REMOVE", U9PATCH_CALLSITE, U9SYM_LIGHTNING_REMOVE_CALL,
    5, LIGHTNING_REMOVE_CALL, 5, U9PF_EXCLUSIVE
};

static const uint8_t LIGHTNING_CTOR_CALL[5] = { 0xE8, 0x54, 0x89, 0xF1, 0xFF };
static const uint8_t LIGHTNING_COLOUR_CALL[5] = { 0xE8, 0xEC, 0x85, 0xF1, 0xFF };
static const uint8_t LIGHTNING_BOLT_CALL[5] = { 0xE8, 0x71, 0xF2, 0x04, 0x00 };

static const U9PatchSpec LIGHTNING_BOLT_SPEC = {
    "LIGHTNING_HALL_BOLT", U9PATCH_CALLSITE, U9SYM_LIGHTNING_BOLT_CALL,
    5, LIGHTNING_BOLT_CALL, 5, U9PF_EXCLUSIVE
};

static const uint8_t LIGHTNING_SCRIPT_CALL[5] = { 0xE8, 0x1F, 0x73, 0x12, 0x00 };
static const uint8_t LIGHTNING_STORMOBJ_CALL[5] = { 0xE8, 0xC8, 0xCD, 0xFD, 0xFF };

static const U9PatchSpec LIGHTNING_SCRIPT_SPEC = {
    "LIGHTNING_HALL_SCRIPT", U9PATCH_CALLSITE, U9SYM_LIGHTNING_SCRIPT_CALL,
    5, LIGHTNING_SCRIPT_CALL, 5, U9PF_EXCLUSIVE
};

static const U9PatchSpec LIGHTNING_STORMOBJ_SPEC = {
    "LIGHTNING_HALL_STORMOBJ", U9PATCH_CALLSITE, U9SYM_LIGHTNING_STORMOBJ_CALL,
    5, LIGHTNING_STORMOBJ_CALL, 5, U9PF_EXCLUSIVE
};

static const U9PatchSpec LIGHTNING_SPEC = {
    "LIGHTNING_HALL", U9PATCH_CALLSITE, U9SYM_LIGHTNING_LIGHT_CALL,
    5, LIGHTNING_CTOR_CALL, 5, U9PF_EXCLUSIVE
};

static const U9PatchSpec LIGHTNING_COLOUR_SPEC = {
    "LIGHTNING_HALL_FADE", U9PATCH_CALLSITE, U9SYM_LIGHTNING_COLOUR_CALL,
    5, LIGHTNING_COLOUR_CALL, 5, U9PF_EXCLUSIVE
};

static void lightning_hall_install(int height)
{
    if (!space_at_ready()) {
        u9log_line("LIGHTNING: SpaceTree_SpaceAt is not where expected - not installed");
        return;
    }
    g_hallHeight = (float)height;
    o_lightCtor = (LightCtorFn)u9patch_install(&LIGHTNING_SPEC, (void *)h_lightning_light);
    o_lightColour = (LightColourFn)u9patch_install(&LIGHTNING_COLOUR_SPEC, (void *)h_lightning_colour);
    o_boltFx = (PoofEnvFn)u9patch_install(&LIGHTNING_BOLT_SPEC, (void *)h_lightning_bolt);
    {
        static const uint8_t expDtor[4] = { 0x8B, 0x41, 0x04, 0x56 };
        static const uint8_t expFree[4] = { 0x8B, 0x44, 0x24, 0x04 };
        uintptr_t dtor = u9sym(U9SYM_LIGHT_DTOR), fr = u9sym(U9SYM_ENGINE_FREE);
        if (dtor && fr && !memcmp((const void *)dtor, expDtor, 4) && !memcmp((const void *)fr, expFree, 4)) {
            f_lightDtor = (LightDtorFn)dtor;
            f_free = (FreeFn)fr;
            o_lightRemove = (LightListFn)u9patch_install(&LIGHTNING_REMOVE_SPEC, (void *)h_lightning_remove);
            if (o_lightRemove)
                o_lightAdd = (LightListFn)u9patch_install(&LIGHTNING_ADD_SPEC, (void *)h_lightning_add);
        }
        u9log_line("LIGHTNING: keep-out %s", o_lightAdd ? "installed - the strike's light is not added"
                   " while the camera is sheltered" : "NOT installed - the light is only built black");
    }
    {
        void *a = u9patch_install(&LIGHTNING_SCRIPT_SPEC, (void *)h_lightning_bolt);
        void *b = u9patch_install(&LIGHTNING_STORMOBJ_SPEC, (void *)h_lightning_bolt);
        if (!o_boltFx) o_boltFx = (PoofEnvFn)(a ? a : b);
        u9log_line("LIGHTNING: %s, fade %s, bolt %s, script bolt %s, storm bolt %s - no flash while"
                   " the camera is in a space that hides the outside, or %d high above it",
                   o_lightCtor ? "installed" : "REFUSED", o_lightColour ? "installed" : "REFUSED",
                   o_boltFx ? "installed" : "REFUSED", a ? "installed" : "REFUSED",
                   b ? "installed" : "REFUSED", height);
    }
}

static void indoor_fog_install(void)
{
    o_skyFog = (SkyFogFn)u9patch_install(&SKY_FOG_SPEC, (void *)h_sky_fog);
    u9log_line("I-19 FOG: %s - in a room the fog is as in clear weather",
               o_skyFog ? "installed" : "REFUSED");
}

typedef uintptr_t (__attribute__((fastcall)) *NpcDataFn)(uintptr_t obj);
static NpcDataFn o_npcData;
static uint8_t   g_noCasterRecord[0x13C];
static unsigned  g_noCasterHits;

static uintptr_t __attribute__((fastcall)) h_spell_npcdata(uintptr_t obj)
{
    if (!obj || !readable4(obj + 0x14)) {
        if (g_noCasterHits++ < 8)
            u9log_line("F-11: a spell's damage roll found its caster gone - rolled"
                       " without the caster's bonus instead of faulting (%u so far)",
                       g_noCasterHits);
        return (uintptr_t)g_noCasterRecord;
    }
    return o_npcData(obj);
}

static const uint8_t SPELLDMG_CALL[5] = { 0xE8, 0xE0, 0xEA, 0xEC, 0xFF };

static const U9PatchSpec SPELLDMG_SPEC = {
    "F11_SPELL_CASTER", U9PATCH_CALLSITE, U9SYM_SPELLDAMAGE_NPCDATA_CALL,
    5, SPELLDMG_CALL, 5, U9PF_EXCLUSIVE
};

static void spell_caster_install(void)
{
    o_npcData = (NpcDataFn)u9patch_install(&SPELLDMG_SPEC, (void *)h_spell_npcdata);
    u9log_line("F-11 spell caster guard: %s - a spell's damage roll survives its"
               " caster being unloaded", o_npcData ? "installed" : "REFUSED");
}

#define DEG2RAD(d)   ((d) * 0.017453292519943295)
#define RAD2DEG(r)   ((r) * 57.29577951308232)
#define CAM_VIEWDIST_X  0x08u
#define CAM_VIEWDIST_Y  0x0Cu
#define CAM_HALF_W      0xA4u
#define CAM_HALF_H      0xA8u
#define CAM_HFOV        0x15Cu
#define CAM_VFOV        0x160u

typedef void (U9CALL *CamPlanesFn)(void *camera);
typedef void (U9CALL *CamSetFovFn)(void *camera, float hfov);
static CamPlanesFn f_camPlanes;
static CamSetFovFn o_bookSetFov;
static unsigned    g_wideLogged;

static int wide_apply(uintptr_t cam)
{
    float halfW = *(volatile float *)(cam + CAM_HALF_W);
    float halfH = *(volatile float *)(cam + CAM_HALF_H);
    float h     = *(volatile float *)(cam + CAM_HFOV);
    double aspect, hEff, dist;

    if (!(halfW > 0.0f) || !(halfH > 0.0f) || !(h > 0.0f) || !(h < 180.0f)) return 0;
    aspect = (double)halfW / (double)halfH;
    if (aspect <= 4.0 / 3.0 + 1e-3) return 0;
    hEff = 2.0 * atan(tan(DEG2RAD(h) / 2.0) * aspect * 0.75);
    if (hEff > DEG2RAD(170.0)) hEff = DEG2RAD(170.0);
    dist = halfW / tan(hEff / 2.0);
    *(volatile float *)(cam + CAM_VIEWDIST_X) = (float)dist;
    *(volatile float *)(cam + CAM_VIEWDIST_Y) = (float)dist;
    *(volatile float *)(cam + CAM_VFOV) = (float)RAD2DEG(2.0 * atan(halfH / dist));
    if (g_wideLogged < 8) {
        g_wideLogged++;
        u9log_line("WIDE VIEW: %.0fx%.0f, angle %.1f kept, drawn at %.1f wide,"
                   " %.1f high (4:3 height)", halfW * 2.0f, halfH * 2.0f, h,
                   RAD2DEG(hEff), RAD2DEG(2.0 * atan(halfH / dist)));
    }
    return 1;
}

static void wide_apply_x(uintptr_t cam)
{
    float halfW = *(volatile float *)(cam + CAM_HALF_W);
    float halfH = *(volatile float *)(cam + CAM_HALF_H);
    float h     = *(volatile float *)(cam + CAM_HFOV);
    double aspect, hEff;

    if (!(halfW > 0.0f) || !(halfH > 0.0f) || !(h > 0.0f) || !(h < 180.0f)) return;
    aspect = (double)halfW / (double)halfH;
    if (aspect <= 4.0 / 3.0 + 1e-3) return;
    hEff = 2.0 * atan(tan(DEG2RAD(h) / 2.0) * aspect * 0.75);
    if (hEff > DEG2RAD(170.0)) hEff = DEG2RAD(170.0);
    *(volatile float *)(cam + CAM_VIEWDIST_X) = (float)(halfW / tan(hEff / 2.0));
}

static void U9CALL h_cam_planes_x(void *camera)
{
    wide_apply_x((uintptr_t)camera);
    f_camPlanes(camera);
}

static void U9CALL h_cam_planes(void *camera)
{
    wide_apply((uintptr_t)camera);
    f_camPlanes(camera);
}

static void U9CALL h_book_setfov(void *camera, float hfov)
{
    o_bookSetFov(camera, hfov);
    if (wide_apply((uintptr_t)camera)) f_camPlanes(camera);
}

static const uint8_t CAM_PLANES_CALL[5] = { 0xE8, 0xE7, 0x01, 0x00, 0x00 };
static const uint8_t BOOK_FOV_CALL[5]   = { 0xE8, 0xC2, 0xC2, 0x13, 0x00 };
static const uint8_t SIZE_PLANES_CALL[5] = { 0xE8, 0x3F, 0x03, 0x00, 0x00 };
static const uint8_t LOAD_PLANES_CALL[5] = { 0xE8, 0x00, 0xB8, 0xFF, 0xFF };
static const uint8_t FOV2_PLANES_CALL[5] = { 0xE8, 0x08, 0x01, 0x00, 0x00 };

static const U9PatchSpec WIDE_ALL_SPEC = {
    "WIDE_VIEW_ALL", U9PATCH_CALLSITE, U9SYM_CAMERA_PLANES_CALL,
    5, CAM_PLANES_CALL, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec WIDE_SIZE_SPEC = {
    "WIDE_VIEW_SIZE", U9PATCH_CALLSITE, U9SYM_CAMERA_SIZE_PLANES_CALL,
    5, SIZE_PLANES_CALL, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec WIDE_LOAD_SPEC = {
    "WIDE_VIEW_LOAD", U9PATCH_CALLSITE, U9SYM_CAMERA_LOAD_PLANES_CALL,
    5, LOAD_PLANES_CALL, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec WIDE_FOV2_SPEC = {
    "WIDE_VIEW_FOV2", U9PATCH_CALLSITE, U9SYM_CAMERA_FOV2_PLANES_CALL,
    5, FOV2_PLANES_CALL, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec WIDE_BOOK_SPEC = {
    "WIDE_VIEW_BOOKS", U9PATCH_CALLSITE, U9SYM_BOOK_SETFOV_CALL,
    5, BOOK_FOV_CALL, 5, U9PF_EXCLUSIVE
};

static void wide_view_install(int mode)
{
    static const uint8_t expPlanes[5] = { 0x56, 0x8B, 0xF1, 0x57, 0xBF };
    uintptr_t planes = u9sym(U9SYM_CAMERA_FIXPLANES);

    if (!planes || memcmp((const void *)planes, expPlanes, 5)) {
        u9log_line("WIDE VIEW: the plane rebuild is not where expected - not installed");
        return;
    }
    f_camPlanes = (CamPlanesFn)planes;
    if (mode >= 2) {
        int n = 0;
        n += u9patch_install(&WIDE_ALL_SPEC,  (void *)h_cam_planes)   != 0;
        n += u9patch_install(&WIDE_SIZE_SPEC, (void *)h_cam_planes)   != 0;
        n += u9patch_install(&WIDE_LOAD_SPEC, (void *)h_cam_planes)   != 0;
        n += u9patch_install(&WIDE_FOV2_SPEC, (void *)h_cam_planes_x) != 0;
        u9log_line("WIDE VIEW: %d of 4 sites installed - the whole game keeps the"
                   " 4:3 view height on a wide screen%s", n,
                   n == 4 ? "" : " (INCOMPLETE - see the manifest)");
    } else {
        o_bookSetFov = (CamSetFovFn)u9patch_install(&WIDE_BOOK_SPEC, (void *)h_book_setfov);
        u9log_line("WIDE VIEW: %s - books keep the 4:3 view height on a wide screen",
                   o_bookSetFov ? "installed" : "REFUSED");
    }
}

static void f5_free_stubs(void)
{
    if (g_f5Stub)  { VirtualFree(g_f5Stub, 0, MEM_RELEASE);  g_f5Stub = 0; }
    if (g_f5bStub) { VirtualFree(g_f5bStub, 0, MEM_RELEASE); g_f5bStub = 0; }
    if (g_blinkStub) { VirtualFree(g_blinkStub, 0, MEM_RELEASE); g_blinkStub = 0; }
    if (g_queryStub) { VirtualFree(g_queryStub, 0, MEM_RELEASE); g_queryStub = 0; }
    if (g_levelStub) { VirtualFree(g_levelStub, 0, MEM_RELEASE); g_levelStub = 0; }
    if (g_searchStub) { VirtualFree(g_searchStub, 0, MEM_RELEASE); g_searchStub = 0; }
    if (g_searchStubB) { VirtualFree(g_searchStubB, 0, MEM_RELEASE); g_searchStubB = 0; }
    if (g_rangeStub) { VirtualFree(g_rangeStub, 0, MEM_RELEASE); g_rangeStub = 0; }
    if (g_recastStub) { VirtualFree(g_recastStub, 0, MEM_RELEASE); g_recastStub = 0; }
    if (g_modelStub) { VirtualFree(g_modelStub, 0, MEM_RELEASE); g_modelStub = 0; }
    if (g_soundSrcStub) { VirtualFree(g_soundSrcStub, 0, MEM_RELEASE); g_soundSrcStub = 0; }
}

int      g_u9ReadCache = 8;
unsigned g_u9ReadCalls, g_u9ReadAsked;

void u9fix_install(void)
{
    QueryPerformanceFrequency(&g_qpf);
    g_u9ReadCache = u9cfg_int("fix_read_cache", 8);
    if (g_u9ReadCache < 1) g_u9ReadCache = 1;
    if (g_u9ReadCache > U9READ_SLOTS) g_u9ReadCache = U9READ_SLOTS;

    if (!u9build_is_exact()) {
        u9log_line("BUILD not recognised (%s) - no fix will be installed",
                   u9build_mismatch() ? u9build_mismatch() : "?");
        return;
    }

    u9life_push("f5 stubs", f5_free_stubs);

    u9clock_install();

    if (u9cfg_int("fix_actor_init", 1)) { init_install(); actor_create_install(); }
    else u9log_line("F-4 transient clear disabled by config");

    if (u9cfg_int("fix_f5", 1)) {
        g_f5Bail = u9cfg_int("fix_f5_bail", 1)
                     ? u9sym(U9SYM_TELEPORT_BAIL_END)
                     : u9sym(U9SYM_TELEPORT_BAIL_RETURN);
        f5_install();
        f5b_install();
    }
    else u9log_line("F-5 null guards disabled by config");

    if (u9cfg_int("fix_blink_tolerance", 1)) {
        g_blinkSteepRad = (float)u9cfg_int("fix_blink_step_slope", 20) * 0.01745329f;
        blink_install();
    }
    else u9log_line("I-2 blink tolerance disabled by config - NPC blinks land"
                    " only on ground level with or just below their point");
    if (g_blinkStub || g_queryObserver) query_install();
    if (g_blinkStub && !g_queryStub)
        u9log_line("I-2: the query site was refused, so the roof rule is OFF -"
                   " a blink can step onto a pitched roof");
    if (u9cfg_int("fix_blink_level", 1)) level_install();
    else u9log_line("I-2 blink levelling disabled by config");
    if (g_blinkStub && u9cfg_int("fix_blink_search", 1)) {
        g_searchRings = u9cfg_int("fix_blink_search_rings", 5);
        g_searchDropOn = u9cfg_int("fix_blink_search_drop", 1);
        g_bandUnits = u9cfg_int("fix_blink_height_band", 50);
        if (g_bandUnits < 0) g_bandUnits = 0;
        g_footUnits = u9cfg_int("fix_blink_footing", 48);
        if (g_footUnits < 0) g_footUnits = 0;
        if (g_footUnits > 200) g_footUnits = 200;
        if (g_searchRings < 0) g_searchRings = 0;
        if (g_searchRings > 8) g_searchRings = 8;
        search_install();
    }
    else u9log_line("I-2 blink spot search off (disabled, or the tolerance fix"
                    " is not in)");
    {
        int pct = u9cfg_int("fix_blink_distance_pct", 150);
        int cap = u9cfg_int("fix_blink_max_pct", 90);
        if (pct < 50) pct = 50;
        if (pct > 250) pct = 250;
        if (cap < 0) cap = 0;
        if (cap && cap < 50) cap = 50;
        blinkdist_install(pct, cap);
    }
    if (g_blinkStub && u9cfg_int("fix_blink_recast", 1)) {
        g_recastMs = u9cfg_int("fix_blink_recast_ms", 250);
        if (g_recastMs < 0) g_recastMs = 0;
        if (g_recastMs > 4000) g_recastMs = 4000;
        recast_install();
    }
    if (u9cfg_int("fix_caster_range", 1)) {
        g_rangePct = u9cfg_int("fix_caster_range_pct", 125);
        if (g_rangePct < 20) g_rangePct = 20;
        if (g_rangePct > 250) g_rangePct = 250;
        g_holdMs = u9cfg_int("fix_caster_hold_ms", 3000);
        if (g_holdMs < 0) g_holdMs = 0;
        if (g_holdMs > 60000) g_holdMs = 60000;
        g_hopUnits = u9cfg_int("fix_caster_hop", 200);
        if (g_hopUnits < 50) g_hopUnits = 50;
        if (g_hopUnits > 1000) g_hopUnits = 1000;
        range_install();
    }
    else u9log_line("I-2 caster range off by config - casters walk to melee"
                    " range between spells, as stock");

    if (u9cfg_int("fix_spell_orphans", 1)) u9spellguard_install();
    else u9log_line("orphan guards disabled by config");

    if (u9cfg_int("fix_absent_target", 1)) u9absent_target_install();
    else u9log_line("F-15/F-16 absent-target guards disabled by config");

    if (u9cfg_int("fix_spell_caster", 1)) spell_caster_install();
    else u9log_line("F-11 spell caster guard disabled by config");

    if (u9cfg_int("fix_combat_run", 1)) combat_run_install();

    if (u9cfg_int("fix_deep_breath", 1)) u9breathfix_install();
    else u9log_line("I-20 deep breath fix off by config - deep water drains a breath a frame, as stock");

    if (u9cfg_int("fix_breath_zones", 1)) u9breathzones_install();
    else u9log_line("I-20 breath zones fix off by config - the last zone crossed decides, as stock");

    if (u9cfg_int("diagonal_move", 0)) u9diagmove_install();

    u9mouse_install();

    if (u9cfg_int("fix_lett_book", 1)) u9lettbook_install();
    else u9log_line("LETT BOOK fix off by config - the book teaches only when read on the ground, as stock");

    if (u9cfg_int("fix_rain_splash", 1)) rain_splash_install();
    else u9log_line("I-19 rain splash fix off by config - rain that falls into a room still"
                    " splashes on the ground under it, as stock");

    if (u9cfg_int("fix_indoor_fog", 1)) indoor_fog_install();
    else u9log_line("I-19 indoor fog fix off by config - rooms get the storm's fog, as stock");

    if (u9cfg_int("fix_lightning_hall", 1)) {
        int height = u9cfg_int("fix_lightning_hall_height", 700);
        if (height < 100) height = 100;
        if (height > 5000) height = 5000;
        lightning_hall_install(height);
    }
    else u9log_line("lightning hall fix off by config - a strike lights every room, as stock");

    {
        int wide = u9cfg_int("wide_view", 0);
        if (wide > 0) wide_view_install(wide);
    }

    if (u9cfg_int("fix_chase_goal", 1)) chase_goal_install();
    else u9log_line("I-10 chase goal off by config - a blocked chase detours"
                    " to where its target was when the chase began, as stock");

    {
        int standoff = u9cfg_int("fix_readbook_standoff", 80);
        if (standoff > 0 && standoff < 100) readbook_install(standoff);
        else if (standoff) u9log_line("readbook standoff %d ignored: it must be 1..99, inside the"
                                      " 100 a shopkeeper needs to pick the book up", standoff);
    }

    if (u9cfg_int("fix_rebuild_flag", 1)) rebuild_flag_install();
    else u9log_line("F-4 rebuild flag disabled by config - casting will depend"
                    " on uninitialised memory again");

    if (u9cfg_int("fix_reload_imm", 1)) reload_imm_install();
    else u9log_line("F-4 reload constant disabled by config");

    if (u9cfg_int("fix_spell_kind", 1)) spell_kind_install();
    else u9log_line("F-19 spell-type bound off by config - effects 0x98..0x9A pass as spells, as stock");

    if (u9cfg_int("fix_bad_models", 1)) model_guard_install();
    else u9log_line("F-23 model guard off by config - a model the game cannot read is built anyway, as stock");

    if (u9cfg_int("fix_sound_slot", 1)) sound_source_guard_install();
    else u9log_line("F-24 sound source guard off by config - a source with no fast-area slot crashes, as stock");

    if (u9cfg_int("fix_line_hit", 1)) line_hit_install();
    else u9log_line("F-25 line-test fix off by config - a test with no hit can read a float as a pointer, as stock");

    if (u9cfg_int("restore_halberd", 1)) halberd_install();

    laa_report();
    if (u9cfg_int("fix_blit_unsigned", 1)) blit_clamp_install();
    else u9log_line("LAA blit clamp off by config - signed, as stock");

    if (u9cfg_int("fix_collider_ref", 1)) collider_ref_install();
    else u9log_line("I-14 collider reference fix disabled by config - copies keep"
                    " their mesh loaded, as stock");

    u9flyer_install();

    u9door_install();

    u9portal_install();

    u9shrine_install();

    u9chase_install();

    if (u9cfg_int("fix_f3", 1)) f3_install();
    else u9log_line("F-3 sanitiser disabled by config");
}

void u9fix_report(void)
{
    u9log_line("");
    u9log_line("cost:");
    u9flyer_report();
    u9door_report();
    u9portal_report();
    u9shrine_report();
    u9chase_report();
    u9breathzones_report();
    u9diagmove_report();
    u9mouse_report();
    u9lettbook_report();
    cost_report(&g_init);
    cost_report(&g_f3);

    if (g_rebuildSet)
        u9log_line("  rebuild flag set on %u actors after construction",
                   g_rebuildSet);
    if (o_chaseAssign)
        u9log_line("  I-10: a chase's goal refreshed on %u follow ticks",
                   g_chaseRefreshes);
    if (o_readbookPath)
        u9log_line("  readbook standoff: %u walks pulled back from the book, %u already close"
                   " enough to stay put", g_readbookMoved, g_readbookStayed);
    if (g_init.calls)
        u9log_line("  actor blocks: %u allocated, %u arrived dirty,"
                   " %u with an unusable timer, %u with stale references",
                   g_init.calls, g_initDirty, g_initBadTimer, g_initBadRefs);

    if (g_f3.calls || g_f3Guarded) {
        u9log_line("  ticks entered with a rebuild already pending: %u -"
                   " left alone, slot 0x1D4 zeroes +0x284 for those itself",
                   g_f3Guarded);
        if (g_f3.acted)
            u9log_line("  NOTE: the sanitiser still acted %u times - the fix"
                       " above it did not cover everything", g_f3.acted);
        else
            u9log_line("  the sanitiser never acted, which is what a working"
                       " fix above it looks like");
    }
    if (g_modelRefused)
        u9log_line("  F-23: %u model builds refused - records the game cannot read (the last:"
                   " limbs %u, LODs %u); each object was left without a model instead of crashing",
                   (unsigned)g_modelRefused, (unsigned)g_modelLastLimbs, (unsigned)g_modelLastLods);
    else if (g_modelStub)
        u9log_line("  F-23 model guard never fired - no unreadable model was asked for");
    if (o_soundAssoc)
        u9log_line("  HALBERD: given the battleaxe's sounds %u times", (unsigned)g_halberdSounds);
    if (g_soundSrcNoSlot)
        u9log_line("  F-24: %u sound-source measurements found the object without a fast-area slot"
                   " and used its world position instead of crashing", (unsigned)g_soundSrcNoSlot);
    else if (g_soundSrcStub)
        u9log_line("  F-24 sound source guard never fired - every source's object resolved");
    if (g_acCalls)
        u9log_line("  F-4 (all classes): %u actors created, %u cleared, %u of them arrived with a sight counter or"
                   " reload out of range; %u had other setup fields repaired (charmer, flags, counts)",
                   g_acCalls, g_acCleared, g_acGarbage, g_acSetupFixed);
    if (g_clampFixed)
        u9log_line("  F-4 backstop: %u actors in combat had a sight counter or reload out of range, set to 0",
                   g_clampFixed);
    if (g_lineObjNone)
        u9log_line("  F-25: %u line-test hits named a dead object and were taken as no object instead of"
                   " crashing (details: LINEHIT lines, the first %u)", (unsigned)g_lineObjNone, LINEOBJ_LOG_MAX);
    if (g_limbNoElement || g_limbNoA8)
        u9log_line("  F-26: %u limb builds found no first limb and %u found one without +0xA8; both built no"
                   " limbs instead of crashing (last object type %u)",
                   (unsigned)g_limbNoElement, (unsigned)g_limbNoA8, (unsigned)g_limbType);
    if (g_camObjNone)
        u9log_line("  F-25: %u camera hits named a dead object and were taken as none", (unsigned)g_camObjNone);
    if (g_scanRuns)
        u9log_line("  F-25: broad-phase scan ran %u times, %u bad nodes seen (LINEHIT scan lines)",
                   g_scanRuns, g_scanBadTotal);
    if (g_f5Fired)
        u9log_line("  F-5 guard fired %u times - each one is a spell tick"
                   " abandoned, not a spell that worked", (unsigned)g_f5Fired);
    else if (g_f5Patched)
        u9log_line("  F-5 guard never fired - no null was hit this session");
    if (g_f5bFired)
        u9log_line("  F-5b guard fired %u times - the second handle, at +0xF4",
                   (unsigned)g_f5bFired);
    if (g_blinkStub)
        u9log_line("  I-2: %u blinks given a step-height tolerance, %u left at"
                   " zero, %u refused a steep step up", (unsigned)g_blinkLoosened,
                   (unsigned)g_blinkKept, (unsigned)g_blinkSteep);
    if (g_levelStub)
        u9log_line("  I-2: %u blink spots levelled with the enemy",
                   (unsigned)g_blinkLevelled);
    if (g_queryStub && g_blinkStub)
        u9log_line("  I-2: %u blink landings (tests included) put on the ground"
                   " the query found, more than a unit from their point",
                   (unsigned)g_blinkGrounded);
    if (g_recastStub)
        u9log_line("  I-2 recast: %u post-blink pauses shortened",
                   (unsigned)g_recastShort);
    if (g_rangeStub)
        u9log_line("  I-2 range: casters held their ground %u times, walked on %u;"
                   " %u stretches walked after a hold ran out (%u ended at once)",
                   (unsigned)g_rangeStood, (unsigned)g_rangeWalked,
                   (unsigned)g_hops, (unsigned)g_hopFailed);
    if (g_searchStub || g_searchStubB)
        u9log_line("  I-2 search: picked spot fitted %u, moved %u, nothing fitted"
                   " %u (casts dropped %u), skipped %u", (unsigned)g_searchPicked,
                   (unsigned)g_searchMoved, (unsigned)g_searchNone,
                   (unsigned)g_searchDropped, (unsigned)g_searchSkipped);
    if (g_searchStub || g_searchStubB)
        u9log_line("  I-2 search: %u fitting spots refused for landing more than %d"
                   " from the enemy's height", (unsigned)g_bandRejected, g_bandUnits);
    if ((g_searchStub || g_searchStubB) && g_footUnits > 0)
        u9log_line("  I-2 search: %u fitting spots refused for ground on fewer than 6"
                   " of 8 sides within %d", (unsigned)g_footRejected, g_footUnits);
}
