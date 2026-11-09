#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../common/u9time.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "playerwatch.h"

void u9log_line(const char *fmt, ...);

#define DATUM_STORE     0x01333E64u
#define DATUM_COUNT     0x400u
#define NARRATION_VTBL  0x007580A8u
#define CONVO_ENTRY     0x218u
#define LETT_ENTRY      188u
#define LINE_CAP        6000u
#define RELOAD_FIELDS   12u
#define COALESCE_MS     1000u

enum { K_U8, K_U16, K_S16, K_S32, K_U32, K_HEX, K_TEXT, K_BYTES, K_STATE, K_WEAPON, K_SKILL, K_SPELLS };
#define F_QUIET 1u
#define F_COAL  2u

typedef struct { uint16_t off, size; uint8_t kind, flags; const char *name; } Field;

static const Field FIELDS[] = {
    { 0x00, 4, K_HEX, 0, "pool handle" },
    { 0x04, 32, K_TEXT, 0, "name" },
    { 0x24, 1, K_U8, 0, "gender" },
    { 0x25, 1, K_U8, 0, "magic tier" },
    { 0x26, 1, K_U8, 0, "armour" },
    { 0x27, 1, K_U8, 0, "armour bonus" },
    { 0x28, 4, K_S32, 0, "strength" },
    { 0x2C, 4, K_S32, 0, "dexterity" },
    { 0x30, 4, K_S32, 0, "intelligence" },
    { 0x34, 2, K_U16, F_COAL, "hit points" },
    { 0x36, 2, K_U16, 0, "hit points max" },
    { 0x38, 2, K_U16, 0, "hit points base" },
    { 0x3A, 2, K_U16, F_COAL, "mana" },
    { 0x3C, 2, K_U16, 0, "mana max" },
    { 0x3E, 2, K_U16, 0, "mana base" },
    { 0x40, 4, K_BYTES, 0, "reserved 0x40" },
    { 0x44, 4, K_S32, 0, "combat behaviour" },
    { 0x48, 4, K_STATE, 0, "state" },
    { 0x4C, 2, K_U16, F_QUIET, "routine" },
    { 0x4E, 2, K_U16, F_QUIET, "fallback routine" },
    { 0x50, 2, K_U16, F_QUIET, "fallback routine argument" },
    { 0x52, 1, K_U8, 0, "magic resistance bonus" },
    { 0x53, 1, K_U8, F_QUIET, "route search workers" },
    { 0x54, 2, K_U16, 0, "awareness radius" },
    { 0x56, 1, K_U8, 0, "awareness arc" },
    { 0x57, 1, K_U8, 0, "awareness inner percent" },
    { 0x58, 4, K_S32, 0, "region" },
    { 0x5C, 4, K_S32, F_QUIET, "x" },
    { 0x60, 4, K_S32, F_QUIET, "y" },
    { 0x64, 2, K_S16, F_QUIET, "z" },
    { 0x66, 2, K_BYTES, F_QUIET, "position tail" },
    { 0x68, 1, K_U8, F_QUIET, "routine depth" },
    { 0x69, 1, K_U8, F_QUIET, "routine step" },
    { 0x6A, 2, K_U16, F_QUIET, "routine argument" },
    { 0x6C, 3, K_BYTES, 0, "scale" },
    { 0x6F, 1, K_HEX, 0, "reserved 0x6f" },
    { 0x70, 4, K_HEX, 0, "equipped slot 0" },
    { 0x74, 4, K_HEX, 0, "equipped slot 1" },
    { 0x78, 4, K_HEX, 0, "equipped slot 2" },
    { 0x7C, 4, K_HEX, 0, "equipped slot 3" },
    { 0x80, 4, K_HEX, 0, "equipped slot 4" },
    { 0x84, 4, K_HEX, 0, "equipped slot 5 (arrows)" },
    { 0x88, 4, K_HEX, 0, "equipped slot 6" },
    { 0x8C, 4, K_S32, 0, "attachment 0" },
    { 0x90, 4, K_S32, 0, "attachment 1" },
    { 0x94, 4, K_S32, 0, "attachment 2" },
    { 0x98, 4, K_S32, 0, "attachment 3" },
    { 0x9C, 4, K_S32, 0, "attachment 4" },
    { 0xA0, 4, K_S32, 0, "attachment 5" },
    { 0xA4, 4, K_S32, 0, "attachment 6" },
    { 0xA8, 4, K_WEAPON, 0, "weapon in hand" },
    { 0xAC, 4, K_U32, 0, "invulnerability" },
    { 0xB0, 4, K_S32, 0, "movement behaviour" },
    { 0xB4, 2, K_U16, F_COAL, "breath" },
    { 0xB6, 2, K_U16, 0, "breath max" },
    { 0xB8, 2, K_U16, 0, "breath base" },
    { 0xBA, 10, K_BYTES, 0, "reserved 0xba" },
    { 0xC4, 4, K_HEX, 0, "traits" },
    { 0xC8, 4, K_S32, 0, "impact material" },
    { 0xCC, 20, K_BYTES, 0, "reserved 0xcc" },
    { 0xE0, 2, K_U16, 0, "reserved 0xe0" },
    { 0xE2, 2, K_U16, F_QUIET, "proximity enter" },
    { 0xE4, 2, K_U16, F_QUIET, "proximity exit" },
    { 0xE6, 2, K_U16, F_QUIET, "queued routine argument" },
    { 0xE8, 4, K_S32, F_QUIET, "route search counter" },
    { 0xEC, 2, K_U16, F_QUIET, "routine end" },
    { 0xEE, 2, K_U16, F_QUIET, "routine start" },
    { 0xF0, 4, K_S32, F_QUIET, "routine duration" },
    { 0xF4, 4, K_HEX, F_QUIET, "queued routine" },
    { 0xF8, 4, K_S32, F_QUIET, "secondary routine duration" },
    { 0xFC, 20, K_BYTES, 0, "reserved 0xfc" },
    { 0x110, 8, K_BYTES, F_QUIET, "routine stack" },
    { 0x118, 12, K_SPELLS, 0, "spellbook" },
    { 0x124, 4, K_SKILL, 0, "fists" },
    { 0x128, 4, K_SKILL, 0, "1-handed" },
    { 0x12C, 4, K_SKILL, 0, "2-handed" },
    { 0x130, 4, K_SKILL, 0, "staff" },
    { 0x134, 4, K_SKILL, 0, "ranged" },
    { 0x138, 4, K_BYTES, 0, "reserved 0x138" },
};
#define NFIELDS (sizeof FIELDS / sizeof FIELDS[0])

static const char *const SKILL_WORD[4] = { "Novice", "Apprentice", "Journeyman", "Master" };
static const char *const SKILL_NAME[5] = { "fists", "1-handed", "2-handed", "staff", "ranged" };
static const char *const WEAPON_NAME[5] = { "fists", "1-handed", "2-handed", "staff", "ranged" };
static const char *const CLASS_NAME[8] = { "mage", "bard", "fighter", "druid", "tinker", "paladin", "ranger", "shepherd" };
static const char *const VIRTUE[8] = { "Honesty", "Compassion", "Valor", "Justice", "Sacrifice", "Honor", "Spirituality", "Humility" };
static const uint16_t SHRINE_DATUM[8] = { 49, 2, 123, 249, 236, 117, 222, 164 };
static const char *const STATE_BIT[32] = {
    "cannot die", "poisoned", "diseased", "hexed", "ward", "life depleted", "sleeping", "power boosted",
    "spawn seated", "hidden", "immobilized", "cloned archetype", "beguiled", "reflecting damage",
    "reflecting all damage", "fleeing", "spellcasting disabled", "magic resistant", "name revealed",
    "player recognized", "position locked", "fear immune", "has live actor", "arrival trigger armed",
    "travelling", "route search failed", "hostile mode", "primary hostile mode", "dialogue active",
    "departure trigger armed", "routine active", "route search blocked",
};
static const char *const SPELL[48] = {
    "(bit 0)", "Stone", "Gust", "Ignite", "Douse", "Create Reagents", "Telekinesis", "Light", "Light Heal",
    "Crystal Barrier", "Lightning Bolt", "Infernal Armor", "Cure", "Meteorite", "Wizard Eye", "Fireball", "Fog",
    "Charm", "Ethereal Sight", "Day", "Freeze", "Summon Undead", "Levitate", "Bolt of Flame", "Full Heal",
    "Death", "Invisibility", "Ring of Fire", "Mana Breath", "Time Stop", "Teleport", "Summon Daemon",
    "Frost Storm", "Earthquake", "Lightning Storm", "Inferno", "Searing Rain", "Sanctify", "Bind",
    "Restoration", "Return", "Spirit Speak", "Reunite Body and Spirit", "Summon Pyros", "Barrier of Life",
    "Armageddon", "(failure)", "(big stone)",
};

typedef void (U9CALL *RaiseFn)(void *convo);
typedef void (U9CALL *LearnFn)(void *record, int spell);
typedef void (U9CALL *RunFn)(void *convo);

static RaiseFn o_raise[5];
static LearnFn o_learn;
static RunFn   o_narr;

static int      g_level;
static unsigned g_pollMs = 100;
static DWORD    g_t0, g_next;
static unsigned g_lines, g_polls, g_changes, g_reloads, g_raises, g_learns, g_entries;
static int      g_have;
static uint8_t  g_prev[RECORD_STRIDE];
static int      g_datumsHave;
static uint8_t  g_class, g_reading, g_shrines;
static DWORD    g_lastLog[NFIELDS];
static int      g_pending[NFIELDS];
static uint32_t g_pendFrom[NFIELDS], g_pendTo[NFIELDS], g_pendCount[NFIELDS];
#define NARR_SLOTS 8
static uintptr_t g_narrConvo[NARR_SLOTS];
static uint32_t  g_narrEntry[NARR_SLOTS];

static void plog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) { g_lines++; u9log_line("PLAYER line cap reached - counting continues"); }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("PLAYER %7lu ms %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static uintptr_t record0(void)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    return (rec && readable4(rec) && readable4(rec + RECORD_STRIDE - 4)) ? rec : 0;
}

static uint32_t field_value(const uint8_t *r, const Field *f)
{
    switch (f->kind) {
    case K_U8: case K_HEX:
        if (f->size == 1) return r[f->off];
        /* fall through */
    default: {
        uint32_t v = 0;
        memcpy(&v, r + f->off, f->size < 4 ? f->size : 4);
        if (f->kind == K_S16) return (uint32_t)(int32_t)(int16_t)v;
        return v;
    }
    }
}

static void value_text(const Field *f, const uint8_t *r, char *out, size_t cap)
{
    uint32_t v = field_value(r, f);
    int s = (int32_t)v;
    unsigned i;
    switch (f->kind) {
    case K_S16: case K_S32: snprintf(out, cap, "%d", s); break;
    case K_HEX: snprintf(out, cap, "0x%X", (unsigned)v); break;
    case K_TEXT: {
        char t[33];
        memcpy(t, r + f->off, 32); t[32] = 0;
        for (i = 0; t[i]; i++) if ((unsigned char)t[i] < 32 || (unsigned char)t[i] > 126) t[i] = '.';
        snprintf(out, cap, "\"%s\"", t);
        break;
    }
    case K_BYTES: case K_SPELLS: {
        size_t n = 0;
        for (i = 0; i < f->size && n + 3 < cap; i++) n += (size_t)snprintf(out + n, cap - n, "%02X", r[f->off + i]);
        break;
    }
    case K_STATE: snprintf(out, cap, "0x%08X", (unsigned)v); break;
    case K_WEAPON: snprintf(out, cap, "%s", s >= 0 && s < 5 ? WEAPON_NAME[s] : s == -1 ? "none" : "?"); break;
    case K_SKILL: snprintf(out, cap, "%d (%s)", s, s >= 0 && s < 4 ? SKILL_WORD[s] : "?"); break;
    default: snprintf(out, cap, "%u", (unsigned)v); break;
    }
}

static void snapshot(const uint8_t *r, const char *why)
{
    char spells[16];
    unsigned n = 0, i;
    for (i = 1; i < 48; i++) if (r[0x118 + i / 8] & (1u << (i % 8))) n++;
    snprintf(spells, sizeof spells, "%u", n);
    plog("%s: str %d dex %d int %d - hp %u/%u/%u mana %u/%u/%u breath %u/%u/%u - armour %u+%u, magic resist +%u"
         " - weapon %d - skills fists %d 1h %d 2h %d staff %d ranged %d - %s spells - state 0x%08X - region %d",
         why, (int)field_value(r, &FIELDS[6]), (int)field_value(r, &FIELDS[7]), (int)field_value(r, &FIELDS[8]),
         *(const uint16_t *)(r + 0x34), *(const uint16_t *)(r + 0x36), *(const uint16_t *)(r + 0x38),
         *(const uint16_t *)(r + 0x3A), *(const uint16_t *)(r + 0x3C), *(const uint16_t *)(r + 0x3E),
         *(const uint16_t *)(r + 0xB4), *(const uint16_t *)(r + 0xB6), *(const uint16_t *)(r + 0xB8),
         r[0x26], r[0x27], r[0x52], *(const int32_t *)(r + 0xA8),
         *(const int32_t *)(r + 0x124), *(const int32_t *)(r + 0x128), *(const int32_t *)(r + 0x12C),
         *(const int32_t *)(r + 0x130), *(const int32_t *)(r + 0x134), spells,
         *(const uint32_t *)(r + 0x48), *(const int32_t *)(r + 0x58));
}

static void log_change(unsigned idx, const uint8_t *old, const uint8_t *cur, DWORD now)
{
    const Field *f = &FIELDS[idx];
    char a[96], b[96];
    unsigned i;
    if (f->kind == K_STATE) {
        uint32_t o = field_value(old, f), c = field_value(cur, f), d = o ^ c;
        for (i = 0; i < 32; i++)
            if (d & (1u << i)) plog("state %s %s (0x%X)", STATE_BIT[i], (c >> i) & 1u ? "ON" : "off", 1u << i);
        return;
    }
    if (f->kind == K_SPELLS) {
        for (i = 0; i < 96; i++) {
            unsigned ob = (old[f->off + i / 8] >> (i % 8)) & 1u, cb = (cur[f->off + i / 8] >> (i % 8)) & 1u;
            if (ob != cb) plog("spell %u %s %s", i, i < 48 ? SPELL[i] : "(beyond 47)", cb ? "LEARNED" : "forgotten");
        }
        return;
    }
    if (f->flags & F_COAL) {
        uint32_t o = field_value(old, f), c = field_value(cur, f);
        if (g_lastLog[idx] && now - g_lastLog[idx] < COALESCE_MS) {
            if (!g_pending[idx]) { g_pending[idx] = 1; g_pendFrom[idx] = o; g_pendCount[idx] = 0; }
            g_pendTo[idx] = c;
            g_pendCount[idx]++;
            return;
        }
        g_lastLog[idx] = now;
        plog("%s %u -> %u", f->name, (unsigned)o, (unsigned)c);
        return;
    }
    value_text(f, old, a, sizeof a);
    value_text(f, cur, b, sizeof b);
    plog("%s %s -> %s", f->name, a, b);
}

static void flush_pending(DWORD now, int all)
{
    unsigned i;
    for (i = 0; i < NFIELDS; i++)
        if (g_pending[i] && (all || now - g_lastLog[i] >= COALESCE_MS)) {
            plog("%s %u -> %u (%u changes)", FIELDS[i].name, (unsigned)g_pendFrom[i], (unsigned)g_pendTo[i],
                 (unsigned)g_pendCount[i]);
            g_pending[i] = 0;
            g_lastLog[i] = now;
        }
}

static void poll_datums(void)
{
    uintptr_t store = readable4(DATUM_STORE) ? rd32(DATUM_STORE) : 0;
    uint8_t cls, reading, shrines = 0;
    unsigned i;
    if (!store || !readable4(store) || !readable4(store + DATUM_COUNT - 4)) return;
    cls = *(const volatile uint8_t *)(store + 480);
    reading = *(const volatile uint8_t *)(store + 69);
    for (i = 0; i < 8; i++) if (*(const volatile uint8_t *)(store + SHRINE_DATUM[i])) shrines |= (uint8_t)(1u << i);
    if (!g_datumsHave) {
        g_datumsHave = 1;
        plog("datums: class %u (%s), reading %s, shrines 0x%02X", cls, cls < 8 ? CLASS_NAME[cls] : "?",
             reading ? "done" : "not done", shrines);
    } else {
        if (cls != g_class) plog("class %u (%s) -> %u (%s) - datum 480", g_class, g_class < 8 ? CLASS_NAME[g_class] : "?",
                                 cls, cls < 8 ? CLASS_NAME[cls] : "?");
        if (reading != g_reading) plog("gypsy's reading %u -> %u - datum 69", g_reading, reading);
        for (i = 0; i < 8; i++)
            if (((shrines ^ g_shrines) >> i) & 1u)
                plog("shrine of %s %s - datum %u", VIRTUE[i], (shrines >> i) & 1u ? "CLEANSED" : "uncleansed", SHRINE_DATUM[i]);
    }
    g_class = cls; g_reading = reading; g_shrines = shrines;
}

#define AVATAR_ACTOR_VTBL 0x00741CA8u
#define CAMERA_MGR        0x00850228u
static int      g_deepHave, g_deepOn;
static unsigned g_swimState = 0xFFFFu;
static DWORD    g_deepLast;
static unsigned g_deepTicks;

static float f32at(uintptr_t p) { float f; uint32_t v = rd32(p); memcpy(&f, &v, 4); return f; }

static void poll_actor(DWORD now)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t act = (tbl && readable4(tbl)) ? rd32(tbl) : 0, cm, cmc;
    int map = -1, bit, on;
    unsigned swim;
    float top, ref, factor;
    if (!act || !readable4(act) || rd32(act) != AVATAR_ACTOR_VTBL || !readable4(act + 0x6B4)) return;
    cm = readable4(CAMERA_MGR) ? rd32(CAMERA_MGR) : 0;
    cmc = (cm && readable4(cm + 0xCC)) ? rd32(cm + 0xCC) : 0;
    if (cmc && readable4(cmc + 0x20)) map = (int)rd32(cmc + 0x20);
    bit = (rd32(act + 0xDC) >> 15) & 1;
    on = bit && map == 9;
    top = f32at(act + 0xF4);
    ref = f32at(act + 0xE0);
    factor = (top - ref - 1400.0f) * 0.01f;
    if (factor < 0.1f) factor = 0.1f;
    swim = *(const volatile uint16_t *)(act + 0x144);
    if (swim != g_swimState) {
        if (g_swimState != 0xFFFFu) plog("actor state +0x144 %u -> %u", g_swimState, swim);
        g_swimState = swim;
    }
    if (!g_deepHave || on != g_deepOn) {
        plog("deep water %s - flag 0x8000 %s, map %d; +0xF4 %.1f, +0xE0 %.1f, difference %.1f, factor %.2f",
             on ? "ON" : "off", bit ? "set" : "clear", map, top, ref, top - ref, on ? factor : 1.0f);
        g_deepHave = 1;
        g_deepOn = on;
        g_deepLast = now;
        if (on) g_deepTicks++;
    } else if (on && now - g_deepLast >= COALESCE_MS) {
        plog("deep water: +0xF4 %.1f, +0xE0 %.1f, difference %.1f, factor %.2f; breath countdown %d, drown countdown %d",
             top, ref, top - ref, factor, (int)rd32(act + 0x680), (int)rd32(act + 0x684));
        g_deepLast = now;
    }
}

void u9player_watch_tick(void)
{
    uint8_t cur[RECORD_STRIDE];
    unsigned i, changed = 0;
    uintptr_t rec;
    DWORD now;

    if (!g_t0) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_pollMs;
    g_polls++;
    poll_datums();
    poll_actor(now);
    flush_pending(now, 0);
    if (!(rec = record0())) return;
    memcpy(cur, (const void *)rec, RECORD_STRIDE);
    if (!g_have) {
        g_have = 1;
        memcpy(g_prev, cur, sizeof cur);
        snapshot(cur, "baseline");
        return;
    }
    if (!memcmp(cur, g_prev, sizeof cur)) return;
    for (i = 0; i < NFIELDS; i++)
        if (!(FIELDS[i].flags & F_QUIET) && memcmp(cur + FIELDS[i].off, g_prev + FIELDS[i].off, FIELDS[i].size)) changed++;
    if (changed > RELOAD_FIELDS) {
        g_reloads++;
        flush_pending(now, 1);
        snapshot(cur, "record reloaded");
    } else {
        for (i = 0; i < NFIELDS; i++) {
            if ((FIELDS[i].flags & F_QUIET) && g_level < 2) continue;
            if (!memcmp(cur + FIELDS[i].off, g_prev + FIELDS[i].off, FIELDS[i].size)) continue;
            g_changes++;
            log_change(i, g_prev, cur, now);
        }
    }
    memcpy(g_prev, cur, sizeof cur);
}

static void convo_text(void *convo, char *out, size_t cap)
{
    uintptr_t c = (uintptr_t)convo;
    if (!c || !readable4(c) || !readable4(c + CONVO_ENTRY)) { snprintf(out, cap, "no conversation (%08X)", (unsigned)c); return; }
    if (rd32(c) == NARRATION_VTBL) snprintf(out, cap, "narration entry %u", (unsigned)rd32(c + CONVO_ENTRY));
    else snprintf(out, cap, "conversation %08X (vtable %08X) entry %u", (unsigned)c, (unsigned)rd32(c), (unsigned)rd32(c + CONVO_ENTRY));
}

static int skill_level(unsigned which)
{
    uintptr_t rec = record0();
    return rec ? (int)rd32(rec + 0x124 + 4 * which) : -99;
}

static void raise_hook(unsigned which, void *convo, void *from)
{
    char who[96];
    int before = skill_level(which), after;
    convo_text(convo, who, sizeof who);
    o_raise[which](convo);
    after = skill_level(which);
    g_raises++;
    plog("RAISE %s %d (%s) -> %d (%s) by %08X, %s%s", SKILL_NAME[which],
         before, before >= 0 && before < 4 ? SKILL_WORD[before] : "?",
         after, after >= 0 && after < 4 ? SKILL_WORD[after] : "?",
         (unsigned)(uintptr_t)from, who, before == after ? " - no change (already Master)" : "");
}

#define RAISE_HOOK(i) \
    static void U9CALL h_raise##i(void *convo) { raise_hook(i, convo, __builtin_return_address(0)); }
RAISE_HOOK(0)
RAISE_HOOK(1)
RAISE_HOOK(2)
RAISE_HOOK(3)
RAISE_HOOK(4)

static void U9CALL h_learn(void *record, int spell)
{
    uintptr_t r = (uintptr_t)record;
    int had = -1;
    if (spell >= 0 && spell < 96 && readable4((r + 0x118 + (unsigned)spell / 8) & ~(uintptr_t)3u))
        had = (*(const volatile uint8_t *)(r + 0x118 + (unsigned)spell / 8) >> (spell % 8)) & 1;
    o_learn(record, spell);
    g_learns++;
    if (r == record0())
        plog("LEARN spell %d %s by %08X%s", spell, spell >= 0 && spell < 48 ? SPELL[spell] : "?",
             (unsigned)(uintptr_t)__builtin_return_address(0), had == 1 ? " - already known" : "");
}

static void U9CALL h_narr(void *convo)
{
    uintptr_t c = (uintptr_t)convo;
    uint32_t entry = readable4(c + CONVO_ENTRY) ? rd32(c + CONVO_ENTRY) : 0xFFFFFFFFu;
    unsigned i, slot = NARR_SLOTS;
    for (i = 0; i < NARR_SLOTS; i++) if (g_narrConvo[i] == c) { slot = i; break; }
    if (slot == NARR_SLOTS)
        for (i = 0; i < NARR_SLOTS; i++) if (!g_narrConvo[i]) { slot = i; g_narrConvo[i] = c; g_narrEntry[i] = 0xFFFFFFFEu; break; }
    if (slot == NARR_SLOTS) { slot = 0; g_narrConvo[0] = c; g_narrEntry[0] = 0xFFFFFFFEu; }
    if (g_narrEntry[slot] != entry) {
        g_narrEntry[slot] = entry;
        g_entries++;
        if (entry == LETT_ENTRY) {
            uintptr_t rec = record0();
            int dex = rec ? (int)rd32(rec + 0x2C) : -99, two = rec ? (int)rd32(rec + 0x12C) : -99;
            plog("NARRATION %08X entry %u - the Lett book's lesson: dexterity %d, 2-handed %d -> %s", (unsigned)c,
                 (unsigned)entry, dex, two,
                 (dex >= 3 && two == 2) ? "should raise 2-handed to Master" :
                 two >= 3 ? "nothing (already Master)" :
                 dex < 3 ? "nothing, silently (dexterity below Adept)" : "nothing, silently (2-handed not Journeyman)");
        } else {
            plog("NARRATION %08X entry %u", (unsigned)c, (unsigned)entry);
        }
    }
    o_narr(convo);
}

#define FRAME_DELTA 0x0083D3A0u
typedef void (U9CALL *BreathFn)(void *actor);
static BreathFn o_breath;
static unsigned g_breathCalls, g_breathCallsSec, g_breathLogged;
static DWORD    g_breathSecT;

static void U9CALL h_breath(void *actor)
{
    uintptr_t a = (uintptr_t)actor, rec = record0();
    int ok = a && readable4(a + 0x6B4) && rec;
    unsigned b0 = 0, b1;
    int c0 = 0, d0 = 0, delta = 0;
    DWORD now = u9time_now();
    g_breathCalls++;
    g_breathCallsSec++;
    if (now - g_breathSecT >= 1000u) {
        if (g_breathSecT && ok && *(const volatile uint16_t *)(rec + 0xB4) != *(const volatile uint16_t *)(rec + 0xB8))
            plog("BREATH %u calls in the last second", g_breathCallsSec);
        g_breathSecT = now;
        g_breathCallsSec = 0;
    }
    if (ok) {
        b0 = *(const volatile uint16_t *)(rec + 0xB4);
        c0 = (int)rd32(a + 0x680);
        d0 = (int)rd32(a + 0x684);
        delta = readable4(FRAME_DELTA) ? (int)rd32(FRAME_DELTA) : -1;
    }
    o_breath(actor);
    if (!ok) return;
    b1 = *(const volatile uint16_t *)(rec + 0xB4);
    if (b1 != b0 && b1 < b0 && g_breathLogged < 3000u) {
        g_breathLogged++;
        plog("BREATH %u -> %u: frame %d ms, countdown %d -> %d, drown %d -> %d, state +0x144 %u,"
             " +0x68C %u +0x68D %u +0x6B4 %d, flags +0xDC 0x%08X",
             b0, b1, delta, c0, (int)rd32(a + 0x680), d0, (int)rd32(a + 0x684),
             (unsigned)*(const volatile uint16_t *)(a + 0x144), (unsigned)*(const volatile uint8_t *)(a + 0x68C),
             (unsigned)*(const volatile uint8_t *)(a + 0x68D), (int)rd32(a + 0x6B4), (unsigned)rd32(a + 0xDC));
    }
}

void u9player_watch_install(void)
{
    static const uint8_t expRaise[5] = { 0xA1, 0x1C, 0xF3, 0x98, 0x00 };
    static const uint8_t expLearn[8] = { 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x4C, 0x24, 0x0C };
    static const uint8_t expSlot[4]  = { 0x40, 0x4D, 0x68, 0x00 };
    static const U9PatchSpec specRaise[5] = {
        { "PLAYER_RAISE_UNARMED", U9PATCH_INLINE, U9SYM_SKILL_RAISE_UNARMED, 5, expRaise, 5, U9PF_EXCLUSIVE },
        { "PLAYER_RAISE_1H", U9PATCH_INLINE, U9SYM_SKILL_RAISE_1H, 5, expRaise, 5, U9PF_EXCLUSIVE },
        { "PLAYER_RAISE_2H", U9PATCH_INLINE, U9SYM_SKILL_RAISE_2H, 5, expRaise, 5, U9PF_EXCLUSIVE },
        { "PLAYER_RAISE_BLUNT", U9PATCH_INLINE, U9SYM_SKILL_RAISE_BLUNT, 5, expRaise, 5, U9PF_EXCLUSIVE },
        { "PLAYER_RAISE_RANGED", U9PATCH_INLINE, U9SYM_SKILL_RAISE_RANGED, 5, expRaise, 5, U9PF_EXCLUSIVE },
    };
    static const U9PatchSpec specLearn = {
        "PLAYER_LEARN_SPELL", U9PATCH_INLINE, U9SYM_NPCREC_LEARN_SPELL, 8, expLearn, 8, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specNarr = {
        "PLAYER_NARRATION_RUN", U9PATCH_VTABLE, U9SYM_NARRATION_SLOT, 4, expSlot, 4, U9PF_EXCLUSIVE
    };
    static const uint8_t expBreath[4] = { 0x80, 0x8B, 0x41, 0x00 };
    static const U9PatchSpec specBreath = {
        "PLAYER_BREATH_UPDATE", U9PATCH_VTABLE, U9SYM_AVATAR_BREATH_SLOT, 4, expBreath, 4, U9PF_EXCLUSIVE
    };
    void *const raiseHooks[5] = { (void *)h_raise0, (void *)h_raise1, (void *)h_raise2, (void *)h_raise3, (void *)h_raise4 };
    unsigned i, raised = 0;

    g_level = u9cfg_int("diag_player_watch", 0);
    if (g_level <= 0) return;
    g_pollMs = (unsigned)u9cfg_int("diag_player_watch_ms", 100);
    if (g_pollMs < 16) g_pollMs = 16;
    g_t0 = u9time_now();
    if (!g_t0) g_t0 = 1;
    g_next = g_t0;
    for (i = 0; i < 5; i++)
        if ((o_raise[i] = (RaiseFn)u9patch_install(&specRaise[i], raiseHooks[i])) != NULL) raised++;
    o_learn = (LearnFn)u9patch_install(&specLearn, (void *)h_learn);
    o_narr = (RunFn)u9patch_install(&specNarr, (void *)h_narr);
    o_breath = (BreathFn)u9patch_install(&specBreath, (void *)h_breath);
    plog("player watch: record 0 every %u ms%s; skill raises %u of 5 hooked, spell learning %s, narration entries %s,"
         " breath update %s",
         g_pollMs, g_level >= 2 ? " (with position and routines)" : "", raised,
         o_learn ? "hooked" : "REFUSED (see the manifest)", o_narr ? "hooked" : "REFUSED (see the manifest)",
         o_breath ? "hooked" : "REFUSED (see the manifest)");
}

void u9player_watch_report(void)
{
    if (!g_t0) return;
    flush_pending(u9time_now(), 1);
    u9log_line("PLAYER watch: %u polls, %u field changes, %u reloads, %u skill raises, %u spells learned,"
               " %u narration entries, %u times into deep water, %u breath updates, %u lines", g_polls, g_changes, g_reloads,
               g_raises, g_learns, g_entries, g_deepTicks, g_breathCalls, g_lines);
}
