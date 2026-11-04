#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/lifecycle.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "spellguard.h"

void u9log_line(const char *fmt, ...);

#define SPELL_CASTER    0xF0u
#define SPELL_CALLBACK  0x104u
#define PROC_TEARDOWN   0x2Cu

static uintptr_t sg_obj(uint32_t h)
{
    uintptr_t tab = rd32(ENTITY_TABLE);
    if (!h || !tab || !readable4(tab + h * 12u + 8u)) return 0;
    return rd32(tab + h * 12u + 8u);
}

typedef struct { uint32_t vt, tick; const char *name; } SpellClass;
static const SpellClass k_spells[] = {
    { 0x007558F0u, 0x0063E130u, "SpellDeath" },
    { 0x00755958u, 0x0063EB10u, "ItemInvis" },
    { 0x00755A18u, 0x0063FB00u, "SpellRingOfFire" },
    { 0x00755A80u, 0x0063FF50u, "SpellManaBreath" },
    { 0x00755AE8u, 0x00640B10u, "spell type 0x73" },
    { 0x00755B70u, 0x00641180u, "SpellLightningBolt" },
    { 0x00755C30u, 0x00642530u, "ItemInfernalArmor" },
    { 0x00755C98u, 0x00642D30u, "SpellCure" },
    { 0x00755D00u, 0x00643550u, "SpellStone" },
    { 0x00755DD8u, 0x00644680u, "SpellGust" },
    { 0x00755E40u, 0x00644C60u, "SpellIgnite" },
    { 0x00755EA8u, 0x006453A0u, "SpellDouse" },
    { 0x00755F10u, 0x00645950u, "SpellBigStone" },
    { 0x00755F78u, 0x00645FE0u, "SpellArmageddon" },
    { 0x00755FE0u, 0x00646440u, "SpellEarthquake" },
    { 0x00756048u, 0x00646E60u, "SpellLightningStorm" },
    { 0x007560C0u, 0x006476B0u, "SpellInferno" },
    { 0x00756128u, 0x00648070u, "SpellAcidRain" },
    { 0x00756190u, 0x006486F0u, "SpellFailure" },
    { 0x007561F8u, 0x00648C40u, "SpellTimeStop" },
    { 0x00756260u, 0x00649350u, "SpellTeleport" },
    { 0x007562D0u, 0x00649DC0u, "SpellSummonDeamon" },
    { 0x00756338u, 0x0064A390u, "SpellFrostStorm" },
    { 0x007563A0u, 0x0064ABF0u, "SpellSummonUndead" },
    { 0x00756408u, 0x0064B3C0u, "SpellLevitate" },
    { 0x00756470u, 0x0064B6E0u, "SpellFlameThrower" },
    { 0x00756530u, 0x0064BEB0u, "SpellFullHeal" },
    { 0x00756598u, 0x0064C990u, "SpellCharm" },
    { 0x00756600u, 0x0064CFC0u, "SpellEtherealSight" },
    { 0x00756668u, 0x0064D860u, "SpellDay" },
    { 0x007566D0u, 0x0064DD60u, "SpellFreeze" },
    { 0x00756738u, 0x0064E4F0u, "SpellMeteorite" },
    { 0x007567A8u, 0x0064EF50u, "SpellWizardEye" },
    { 0x00756810u, 0x0064F840u, "SpellFireball" },
    { 0x00756878u, 0x0064FE90u, "SpellFog" },
    { 0x007568E0u, 0x00650BD0u, "SpellCreateReagents" },
    { 0x00756960u, 0x00651180u, "SpellTelekinesis" },
    { 0x007569C8u, 0x00651A20u, "spell type 0x71" },
    { 0x00756A30u, 0x006522E0u, "SpellLightHeal" },
};
#define SPELL_COUNT ((int)(sizeof(k_spells) / sizeof(k_spells[0])))

typedef void (U9CALL *TickFn)(void *self);
typedef void (U9CALL *TeardownFn)(void *self);
static TickFn      o_tick[SPELL_COUNT];
static U9PatchSpec g_tickSpec[SPELL_COUNT];
static uint8_t     g_tickExpect[SPELL_COUNT][4];
static unsigned    g_orphans, g_orphanLogged;

static void U9CALL h_spell_tick(void *self)
{
    uintptr_t s = (uintptr_t)self;
    uint32_t vt = rd32(s), h;
    int i;

    for (i = 0; i < SPELL_COUNT; i++) if (k_spells[i].vt == vt) break;
    if (i == SPELL_COUNT || !o_tick[i]) {
        static int warned;
        if (!warned) { warned = 1; u9log_line("SPELL ORPHAN: a tick with vtable"
                       " %08X reached the guard unexpectedly", (unsigned)vt); }
        return;
    }
    h = rd32(s + SPELL_CASTER);
    if (h && !sg_obj(h)) {
        g_orphans++;
        if (g_orphanLogged < 12) {
            g_orphanLogged++;
            u9log_line("SPELL ORPHAN: %s (spell %08X) lost its caster (handle %u) -"
                       " fizzled instead of running on (%u so far)",
                       k_spells[i].name, (unsigned)s, h, g_orphans);
        }
        *(volatile uint32_t *)(s + SPELL_CALLBACK) = 0;
        ((TeardownFn)rd32(vt + PROC_TEARDOWN))(self);
        return;
    }
    o_tick[i](self);
}

typedef void (__attribute__((stdcall)) *HandleRelFn)(uint32_t h);
static unsigned g_cbSkips, g_cbLogged;

static uint32_t cb_skip(int entry, uint32_t h)
{
    ((HandleRelFn)u9sym(U9SYM_HANDLE_RELEASE))(h);
    g_cbSkips++;
    if (g_cbLogged < 12) {
        g_cbLogged++;
        u9log_line("COMBATANT CALLBACK %d: its creature is gone (handle %u) -"
                   " skipped instead of reading nothing (%u so far)", entry, h,
                   g_cbSkips);
    }
    return 0;
}

#define FC __attribute__((fastcall))
typedef uint32_t (FC *Cb1)(uint32_t, uint32_t, uint32_t);
typedef uint32_t (FC *Cb2)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (FC *Cb3)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t (FC *Cb4)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#define CB1(k) static Cb1 o_cb##k; \
    static uint32_t FC h_cb##k(uint32_t c, uint32_t d, uint32_t h) \
    { return sg_obj(h) ? o_cb##k(c, d, h) : cb_skip(k, h); }
#define CB2(k) static Cb2 o_cb##k; \
    static uint32_t FC h_cb##k(uint32_t c, uint32_t d, uint32_t h, uint32_t a) \
    { return sg_obj(h) ? o_cb##k(c, d, h, a) : cb_skip(k, h); }
#define CB3(k) static Cb3 o_cb##k; \
    static uint32_t FC h_cb##k(uint32_t c, uint32_t d, uint32_t h, uint32_t a, uint32_t b) \
    { return sg_obj(h) ? o_cb##k(c, d, h, a, b) : cb_skip(k, h); }
#define CB4(k) static Cb4 o_cb##k; \
    static uint32_t FC h_cb##k(uint32_t c, uint32_t d, uint32_t h, uint32_t a, uint32_t b, uint32_t e) \
    { return sg_obj(h) ? o_cb##k(c, d, h, a, b, e) : cb_skip(k, h); }

CB4(10) CB1(11) CB1(12) CB3(13) CB3(14) CB2(15) CB1(30)

static const uint8_t CB_FS[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t CB_12[7] = { 0x6A, 0xFF, 0x68, 0x78, 0x9F, 0x72, 0x00 };

#define CBSPEC(k, len, exp) static const U9PatchSpec CBSPEC_##k = { \
    "ORPHAN_CALLBACK_" #k, U9PATCH_INLINE, U9SYM_COMBATANT_CALLBACK_##k, \
    len, exp, len, U9PF_EXCLUSIVE };
CBSPEC(10, 6, CB_FS) CBSPEC(11, 6, CB_FS) CBSPEC(12, 7, CB_12) CBSPEC(13, 6, CB_FS)
CBSPEC(14, 6, CB_FS) CBSPEC(15, 6, CB_FS) CBSPEC(30, 6, CB_FS)

#define CBINSTALL(k, T) ((o_cb##k = (T)u9patch_install(&CBSPEC_##k, (void *)h_cb##k)) != 0)

#define ACTOR_INDEX     0x104u

static const uint8_t NT_DRAGON[5] = { 0xA1, 0xC8, 0x62, 0x78, 0x00 };
static const uint8_t NT_EB[5]     = { 0xA1, 0x08, 0xFA, 0x78, 0x00 };
static const U9PatchSpec NTSPEC_DRAGON = { "F15_NOTARGET_DRAGON", U9PATCH_STUB,
    U9SYM_ATTACK_NOTARGET_DRAGON, 5, NT_DRAGON, 5, U9PF_EXCLUSIVE };
static const U9PatchSpec NTSPEC_EB = { "F15_NOTARGET_EB", U9PATCH_STUB,
    U9SYM_ATTACK_NOTARGET_EB, 5, NT_EB, 5, U9PF_EXCLUSIVE };

static volatile uint32_t g_noTarget[2];
static uint8_t *g_ntStub[2];

static int nt_install(int k, const U9PatchSpec *spec, const uint8_t *displaced, U9Symbol exitSym)
{
    uintptr_t site = u9sym(spec->target), exit_ = u9sym(exitSym);
    uint8_t *st, jmp[5];
    if (!site || !exit_) return 0;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) return 0;
    st[0] = 0x8B; st[1] = 0x86; *(uint32_t *)(st + 2) = ACTOR_INDEX;
    st[6] = 0x3D; *(uint32_t *)(st + 7) = ACTOR_COUNT - 1;
    st[11] = 0x77; st[12] = 35 - 13;
    st[13] = 0x8B; st[14] = 0x15; *(uint32_t *)(st + 15) = ACTOR_TABLE;
    st[19] = 0x83; st[20] = 0x3C; st[21] = 0x82; st[22] = 0x00;
    st[23] = 0x74; st[24] = 35 - 25;
    memcpy(st + 25, displaced, 5);
    st[30] = 0xE9; *(int32_t *)(st + 31) = (int32_t)(site + 5 - (uintptr_t)(st + 35));
    st[35] = 0xFF; st[36] = 0x05;
    *(uint32_t *)(st + 37) = (uint32_t)(uintptr_t)&g_noTarget[k];
    st[41] = 0xE9; *(int32_t *)(st + 42) = (int32_t)(exit_ - (uintptr_t)(st + 46));
    jmp[0] = 0xE9; *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    if (!u9patch_install_bytes(spec, jmp, 5)) { VirtualFree(st, 0, MEM_RELEASE); return 0; }
    g_ntStub[k] = st;
    return 1;
}

static void nt_report(void)
{
    if (g_noTarget[0] || g_noTarget[1])
        u9log_line("NO-TARGET ATTACKS skipped (F-15): dragon breath %u, event 0xEB %u",
                   (unsigned)g_noTarget[0], (unsigned)g_noTarget[1]);
}

#define ACTOR_OBJECT    0x14u
#define FAR_AWAY        1.0e9f

typedef void    (U9CALL *FaceFn)(void *, int);
typedef uint8_t (U9CALL *FacingFn)(void *, int, float);
typedef float   (U9CALL *AngleFn)(void *, int);
typedef float   (U9CALL *DistFn)(void *, int, uint32_t, uint32_t);
typedef uint8_t (U9CALL *SeeFn)(void *, int);

static FaceFn   o_face;
static FacingFn o_facing, o_facingMe;
static AngleFn  o_angleTo, o_angleFrom;
static DistFn   o_dist;
static SeeFn    o_see;
static volatile uint32_t g_absent[7];
static volatile uint32_t g_tsSkips, g_arSkips;
static unsigned g_absentLogged;

static int actor_present(int npc)
{
    uintptr_t tbl, a;
    if ((unsigned)npc >= ACTOR_COUNT || !readable4(ACTOR_TABLE)) return 0;
    tbl = rd32(ACTOR_TABLE);
    if (!tbl || !readable4(tbl + (unsigned)npc * 4)) return 0;
    a = rd32(tbl + (unsigned)npc * 4);
    return a && readable4(a + ACTOR_OBJECT) && rd32(a + ACTOR_OBJECT);
}

static int absent(int which, void *self, int npc)
{
    if (actor_present(npc)) return 0;
    g_absent[which]++;
    if (g_absentLogged < 12) {
        g_absentLogged++;
        u9log_line("ABSENT ACTOR (F-16): a creature (%08X, vtable %08X) asked about NPC %d,"
                   " who is not in the actor table - answered harmlessly",
                   (unsigned)(uintptr_t)self,
                   readable4((uintptr_t)self) ? rd32((uintptr_t)self) : 0, npc);
    }
    return 1;
}

static void    U9CALL h_face(void *s, int n)                { if (!absent(0, s, n)) o_face(s, n); }
static uint8_t U9CALL h_facing(void *s, int n, float t)     { return absent(1, s, n) ? 0 : o_facing(s, n, t); }
static uint8_t U9CALL h_facingMe(void *s, int n, float t)   { return absent(2, s, n) ? 0 : o_facingMe(s, n, t); }
static float   U9CALL h_angleTo(void *s, int n)             { return absent(3, s, n) ? 0.0f : o_angleTo(s, n); }
static float   U9CALL h_angleFrom(void *s, int n)           { return absent(4, s, n) ? 0.0f : o_angleFrom(s, n); }
static float   U9CALL h_dist(void *s, int n, uint32_t a, uint32_t b) { return absent(5, s, n) ? FAR_AWAY : o_dist(s, n, a, b); }
static uint8_t U9CALL h_see(void *s, int n)                 { return absent(6, s, n) ? 0 : o_see(s, n); }

static const uint8_t AA_FACE[7]     = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x0C };
static const uint8_t AA_FACING[8]   = { 0x51, 0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x0C };
static const uint8_t AA_FACINGME[8] = { 0x51, 0x8B, 0x01, 0x56, 0x8B, 0x74, 0x24, 0x0C };
static const uint8_t AA_ANGLE[7]    = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x30 };
static const uint8_t AA_DIST[7]     = { 0x83, 0xEC, 0x38, 0x8B, 0x44, 0x24, 0x3C };
static const uint8_t AA_SEE[5]      = { 0x83, 0xEC, 0x2C, 0x53, 0x55 };

#define AASPEC(id, sym, exp) static const U9PatchSpec AASPEC_##id = { \
    "F16_ABSENT_" #id, U9PATCH_INLINE, sym, sizeof exp, exp, sizeof exp, U9PF_EXCLUSIVE };
AASPEC(FACE,      U9SYM_COMBATANT_FACE_ACTOR,         AA_FACE)
AASPEC(FACING,    U9SYM_COMBATANT_IS_FACING_ACTOR,    AA_FACING)
AASPEC(FACINGME,  U9SYM_COMBATANT_IS_ACTOR_FACING_ME, AA_FACINGME)
AASPEC(ANGLETO,   U9SYM_COMBATANT_ANGLE_TO_ACTOR,     AA_ANGLE)
AASPEC(ANGLEFROM, U9SYM_COMBATANT_ANGLE_FROM_ACTOR,   AA_ANGLE)
AASPEC(DIST,      U9SYM_COMBATANT_DISTANCE_TO_ACTOR,  AA_DIST)
AASPEC(SEE,       U9SYM_COMBATANT_CAN_SEE_ACTOR,      AA_SEE)

static void aa_report(void)
{
    unsigned i, n = 0;
    for (i = 0; i < 7; i++) n += g_absent[i];
    if (n)
        u9log_line("ABSENT ACTOR answers (F-16): face %u, facing %u, facing-me %u, angle-to %u,"
                   " angle-from %u, distance %u, can-see %u",
                   (unsigned)g_absent[0], (unsigned)g_absent[1], (unsigned)g_absent[2],
                   (unsigned)g_absent[3], (unsigned)g_absent[4], (unsigned)g_absent[5],
                   (unsigned)g_absent[6]);
    if (g_tsSkips || g_arSkips)
        u9log_line("ABSENT ACTOR (F-16): %u target notices skipped, %u Avatar radius reads skipped",
                   (unsigned)g_tsSkips, (unsigned)g_arSkips);
}

static const uint8_t TS_EXPECT[5] = { 0x8B, 0x0C, 0x81, 0x8B, 0x11 };
static const uint8_t AR_EXPECT[5] = { 0x8B, 0x08, 0xD8, 0x41, 0x3C };
static U9PatchSpec g_tsSpec[8];
static const U9PatchSpec AR_SPEC = { "F16_AVATAR_RADIUS", U9PATCH_STUB,
    U9SYM_AVATAR_RADIUS_ADD, 5, AR_EXPECT, 5, U9PF_EXCLUSIVE };
static const char *const TS_NAMES[8] = {
    "F16_TARGET_SLOT_0", "F16_TARGET_SLOT_1", "F16_TARGET_SLOT_2", "F16_TARGET_SLOT_3",
    "F16_TARGET_SLOT_4", "F16_TARGET_SLOT_5", "F16_TARGET_SLOT_6", "F16_TARGET_SLOT_7" };

static uint8_t *exec_alloc(void)
{
    return (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

static int jump_to(const U9PatchSpec *spec, uintptr_t site, uint8_t *st)
{
    uint8_t jmp[5];
    jmp[0] = 0xE9; *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    if (u9patch_install_bytes(spec, jmp, 5)) return 1;
    VirtualFree(st, 0, MEM_RELEASE);
    return 0;
}

static int ts_install(int k)
{
    uintptr_t site;
    uint8_t *st;
    g_tsSpec[k].name = TS_NAMES[k];
    g_tsSpec[k].mechanism = U9PATCH_STUB;
    g_tsSpec[k].target = (U9Symbol)(U9SYM_TARGET_SLOT_FIRST + k);
    g_tsSpec[k].length = 5;
    g_tsSpec[k].expected = TS_EXPECT;
    g_tsSpec[k].expectedLen = 5;
    g_tsSpec[k].flags = U9PF_EXCLUSIVE;
    site = u9sym(g_tsSpec[k].target);
    if (!site || !(st = exec_alloc())) return 0;
    st[0] = 0x8B; st[1] = 0x0C; st[2] = 0x81;
    st[3] = 0x85; st[4] = 0xC9;
    st[5] = 0x74; st[6] = 14 - 7;
    st[7] = 0x8B; st[8] = 0x11;
    st[9] = 0xE9; *(int32_t *)(st + 10) = (int32_t)(site + 5 - (uintptr_t)(st + 14));
    st[14] = 0x83; st[15] = 0xC4; st[16] = 0x04;
    st[17] = 0xFF; st[18] = 0x05; *(uint32_t *)(st + 19) = (uint32_t)(uintptr_t)&g_tsSkips;
    st[23] = 0xE9; *(int32_t *)(st + 24) = (int32_t)(site + 8 - (uintptr_t)(st + 28));
    return jump_to(&g_tsSpec[k], site, st);
}

static int ar_install(void)
{
    uintptr_t site = u9sym(U9SYM_AVATAR_RADIUS_ADD);
    uint8_t *st;
    if (!site || !(st = exec_alloc())) return 0;
    st[0] = 0x8B; st[1] = 0x08;
    st[2] = 0x85; st[3] = 0xC9;
    st[4] = 0x74; st[5] = 14 - 6;
    st[6] = 0xD8; st[7] = 0x41; st[8] = 0x3C;
    st[9] = 0xE9; *(int32_t *)(st + 10) = (int32_t)(site + 5 - (uintptr_t)(st + 14));
    st[14] = 0xFF; st[15] = 0x05; *(uint32_t *)(st + 16) = (uint32_t)(uintptr_t)&g_arSkips;
    st[20] = 0xE9; *(int32_t *)(st + 21) = (int32_t)(site + 5 - (uintptr_t)(st + 25));
    return jump_to(&AR_SPEC, site, st);
}

static int aa_install(void)
{
    int n = 0;
    n += (o_face      = (FaceFn)  u9patch_install(&AASPEC_FACE,      (void *)h_face))      != 0;
    n += (o_facing    = (FacingFn)u9patch_install(&AASPEC_FACING,    (void *)h_facing))    != 0;
    n += (o_facingMe  = (FacingFn)u9patch_install(&AASPEC_FACINGME,  (void *)h_facingMe))  != 0;
    n += (o_angleTo   = (AngleFn) u9patch_install(&AASPEC_ANGLETO,   (void *)h_angleTo))   != 0;
    n += (o_angleFrom = (AngleFn) u9patch_install(&AASPEC_ANGLEFROM, (void *)h_angleFrom)) != 0;
    n += (o_dist      = (DistFn)  u9patch_install(&AASPEC_DIST,      (void *)h_dist))      != 0;
    n += (o_see       = (SeeFn)   u9patch_install(&AASPEC_SEE,       (void *)h_see))       != 0;
    for (int k = 0; k < 8; k++) n += ts_install(k);
    n += ar_install();
    if (n) u9life_push_at_exit("absent-actor report", aa_report);
    return n;
}

void u9spellguard_install(void)
{
    int i, spells = 0, cbs = 0;

    for (i = 0; i < SPELL_COUNT; i++) {
        memcpy(g_tickExpect[i], &k_spells[i].tick, 4);
        g_tickSpec[i].name        = k_spells[i].name;
        g_tickSpec[i].mechanism   = U9PATCH_VTABLE;
        g_tickSpec[i].target      = (U9Symbol)(U9SYM_SPELL_TICK_SLOT_FIRST + i);
        g_tickSpec[i].length      = 4;
        g_tickSpec[i].expected    = g_tickExpect[i];
        g_tickSpec[i].expectedLen = 4;
        g_tickSpec[i].flags       = U9PF_EXCLUSIVE;
        o_tick[i] = (TickFn)u9patch_install(&g_tickSpec[i], (void *)h_spell_tick);
        if (o_tick[i]) spells++;
    }
    cbs += CBINSTALL(10, Cb4); cbs += CBINSTALL(11, Cb1); cbs += CBINSTALL(12, Cb1);
    cbs += CBINSTALL(13, Cb3); cbs += CBINSTALL(14, Cb3); cbs += CBINSTALL(15, Cb2);
    cbs += CBINSTALL(30, Cb1);
    u9log_line("ORPHAN GUARDS: %d of %d spell classes fizzle when their caster is"
               " gone; %d of 7 combatant callbacks skip a creature that is gone",
               spells, SPELL_COUNT, cbs);
}

void u9absent_target_install(void)
{
    int nts = 0, aas;
    nts += nt_install(0, &NTSPEC_DRAGON, NT_DRAGON, U9SYM_ATTACK_NOTARGET_DRAGON_EXIT);
    nts += nt_install(1, &NTSPEC_EB, NT_EB, U9SYM_ATTACK_NOTARGET_EB_EXIT);
    if (nts) u9life_push_at_exit("no-target attack report", nt_report);
    aas = aa_install();
    u9log_line("ABSENT TARGET GUARDS: %d of 2 attack events skip a target that is gone"
               " (F-15); %d of 16 absent-actor guards (F-16)", nts, aas);
}
