#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "birdprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define ACT_SELF        0x18u
#define FLY_STATE       0x214u
#define FLY_FLAGS       0x228u
#define FLY_NEXT_SING   0x230u
#define FLY_TIMESCALE   0x288u
#define FLY_STARTLE     0x24Cu
#define FLY_FEAR        0x29Cu
#define VT_CALLBACK     0x28u
#define VT_CHANGESTATE  0x198u
#define SONGBIRD_CHANGESTATE 0x00532550u
#define FLYING_CALLBACK      0x004936E0u

#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u

#define REC_NAME        0x04u

#define COMBATANT_SOUND_RET  0x0043FD58u

#define BIRD_CAP        3000
#define SND_CAP         4000
#define BIRDS_MAX       96
#define SOURCES_MAX     128
#define STILL_REPORT_MS 30000u
#define SUMMARY_MS      30000u

static DWORD g_t0;

DWORD u9snd_t0(void) { return g_t0; }

static void plog(unsigned *lines, unsigned cap, const char *tag, const char *fmt, ...)
{
    char line[480];
    va_list ap;

    if (*lines >= cap) {
        if (*lines == cap) {
            (*lines)++;
            u9log_line("%s line cap reached - counting continues", tag);
        }
        return;
    }
    (*lines)++;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    u9log_line("%s %7lu ms %s", tag, (unsigned long)(u9time_now() - g_t0), line);
}

static uintptr_t obj_of(uint32_t h)
{
    uintptr_t tab = readable4(ENTITY_TABLE) ? rd32(ENTITY_TABLE) : 0;
    if (!h || !tab || !readable4(tab + h * 12u + 8u)) return 0;
    return rd32(tab + h * 12u + 8u);
}

static uintptr_t actor_object(uintptr_t act)
{
    if (!act || !readable4(act + ACT_SELF)) return 0;
    return obj_of(rd32(act + ACT_SELF));
}

static int obj_type(uintptr_t o)
{
    return (o && readable4(o + OBJ_TYPE)) ? *(const volatile uint16_t *)(o + OBJ_TYPE) : -1;
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

static int dist_to_avatar(int x, int y)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t av = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    int ax, ay, az;
    double dx, dy;

    if (!obj_pos(actor_object(av), &ax, &ay, &az)) return -1;
    dx = x - ax; dy = y - ay;
    return (int)sqrt(dx * dx + dy * dy);
}

static void npc_name(int type, char out[33])
{
    uintptr_t base = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    uintptr_t r;

    out[0] = 0;
    if (type < 0 || (unsigned)type >= ACTOR_COUNT || !base) return;
    r = base + (unsigned)type * RECORD_STRIDE;
    if (!readable4(r + REC_NAME) || !readable4(r + REC_NAME + 28)) return;
    memcpy(out, (const void *)(r + REC_NAME), 32);
    out[32] = 0;
}

static float as_float(uint32_t v)
{
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int      g_birdMode;
static DWORD    g_birdEvery = 1000, g_birdNext;
static unsigned g_birdLines, g_birdSamples, g_birdSeen, g_birdStates, g_birdMoves;

typedef struct {
    uintptr_t act;
    unsigned  idx;
    int       state, x, y, z, havePos, live;
    DWORD     stillSince, lastReport;
} Bird;
static Bird     g_birds[BIRDS_MAX];
static unsigned g_birdCount;

static const char *bird_class(uintptr_t vt)
{
    uint32_t cs = readable4(vt + VT_CHANGESTATE) ? rd32(vt + VT_CHANGESTATE) : 0;
    uint32_t cb = readable4(vt + VT_CALLBACK) ? rd32(vt + VT_CALLBACK) : 0;
    if (cs == SONGBIRD_CHANGESTATE) return "songbird";
    if (cb == FLYING_CALLBACK && g_birdMode >= 2) return "flying";
    return 0;
}

static Bird *bird_slot(uintptr_t act)
{
    unsigned i;
    for (i = 0; i < g_birdCount; i++) if (g_birds[i].act == act) return &g_birds[i];
    for (i = 0; i < g_birdCount; i++) if (!g_birds[i].live) break;
    if (i == g_birdCount) {
        if (g_birdCount >= BIRDS_MAX) return 0;
        g_birdCount++;
    }
    memset(&g_birds[i], 0, sizeof g_birds[i]);
    g_birds[i].act = act;
    g_birds[i].state = -1;
    return &g_birds[i];
}

static void bird_line(const char *what, const Bird *b, uintptr_t act, const char *cls,
                      int state, DWORD now)
{
    uint32_t flags = readable4(act + FLY_FLAGS) ? rd32(act + FLY_FLAGS) : 0;
    uint32_t sing  = readable4(act + FLY_NEXT_SING) ? rd32(act + FLY_NEXT_SING) : 0;
    uint32_t scale = readable4(act + FLY_TIMESCALE) ? rd32(act + FLY_TIMESCALE) : 0;
    int type = obj_type(actor_object(act));
    char name[33];
    char singText[32];

    npc_name(type, name);
    if (strcmp(cls, "songbird") == 0)
        snprintf(singText, sizeof singText, "%+ld ms", (long)(int32_t)(sing - u9time_engine()));
    else
        snprintf(singText, sizeof singText, "-");
    plog(&g_birdLines, BIRD_CAP, "BIRD",
         "%-6s slot %-3u act %08X %s vt %08X npc %d '%s' state %d flags %04X scale %.1f"
         " sing %s at %d,%d,%d still %lu s, Avatar %d away",
         what, b->idx, (unsigned)act, cls, readable4(act) ? (unsigned)rd32(act) : 0u,
         type, name, state, (unsigned)flags,
         (double)as_float(scale), singText, b->x, b->y, b->z,
         (unsigned long)((now - b->stillSince) / 1000), dist_to_avatar(b->x, b->y));
}

static void bird_tick(void)
{
    uintptr_t tbl;
    DWORD now;
    unsigned i;

    if (!g_birdMode) return;
    now = u9time_now();
    if ((LONG)(now - g_birdNext) < 0) return;
    g_birdNext = now + g_birdEvery;
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (!tbl) return;
    g_birdSamples++;

    for (i = 0; i < g_birdCount; i++) g_birds[i].live = g_birds[i].live ? 2 : 0;

    for (i = 0; i < ACTOR_COUNT; i++) {
        uintptr_t act, vt;
        const char *cls;
        Bird *b;
        int state, x, y, z, moved;

        if (!readable4(tbl + i * 4)) continue;
        act = rd32(tbl + i * 4);
        if (!act || !readable4(act) || !readable4(act + FLY_TIMESCALE)) continue;
        vt = rd32(act);
        if (vt < IMAGE_LO || vt >= IMAGE_HI || !(cls = bird_class(vt))) continue;
        if (!(b = bird_slot(act))) continue;
        state = (int)rd32(act + FLY_STATE);
        if (!obj_pos(actor_object(act), &x, &y, &z)) continue;

        if (!b->live) {
            b->live = 1;
            b->idx = i;
            b->state = state;
            b->x = x; b->y = y; b->z = z;
            b->stillSince = b->lastReport = now;
            g_birdSeen++;
            bird_line("new", b, act, cls, state, now);
            continue;
        }
        b->live = 1;
        b->idx = i;
        moved = abs(x - b->x) > 16 || abs(y - b->y) > 16 || abs(z - b->z) > 16;
        if (state != b->state) {
            g_birdStates++;
            b->x = x; b->y = y; b->z = z;
            bird_line(moved ? "state+" : "state", b, act, cls, state, now);
            b->state = state;
            b->lastReport = now;
            if (moved) b->stillSince = now;
        } else if (moved) {
            g_birdMoves++;
            b->x = x; b->y = y; b->z = z;
            b->stillSince = now;
            if (now - b->lastReport >= 5000u) {
                bird_line("moved", b, act, cls, state, now);
                b->lastReport = now;
            }
        } else if (now - b->lastReport >= STILL_REPORT_MS) {
            bird_line("still", b, act, cls, state, now);
            b->lastReport = now;
        }
    }

    for (i = 0; i < g_birdCount; i++)
        if (g_birds[i].live == 2) {
            plog(&g_birdLines, BIRD_CAP, "BIRD", "gone   slot %-3u act %08X after %lu s"
                 " at %d,%d,%d", g_birds[i].idx, (unsigned)g_birds[i].act,
                 (unsigned long)((now - g_birds[i].stillSince) / 1000),
                 g_birds[i].x, g_birds[i].y, g_birds[i].z);
            g_birds[i].live = 0;
        }
}

static int      g_sndOn, g_sndRadius;
static unsigned g_sndLines, g_sndPlays, g_sndCombat, g_sndShown, g_sndFailed;
static DWORD    g_sndNextSummary;
static uintptr_t g_curActor;
static int      g_curCategory = -1;

typedef struct { uintptr_t obj; int type; unsigned window, total; } Source;
static Source   g_src[SOURCES_MAX];
static unsigned g_srcCount, g_srcDropped;

static void count_source(uintptr_t obj, int type)
{
    unsigned i;
    for (i = 0; i < g_srcCount; i++)
        if (g_src[i].obj == obj && g_src[i].type == type) break;
    if (i == g_srcCount) {
        if (g_srcCount >= SOURCES_MAX) { g_srcDropped++; return; }
        g_src[i].obj = obj;
        g_src[i].type = type;
        g_srcCount++;
    }
    g_src[i].window++;
    g_src[i].total++;
}

static void summary(const char *when, int session)
{
    int best[6] = { -1, -1, -1, -1, -1, -1 };
    char out[400];
    size_t m = 0;
    unsigned i, k;

    for (i = 0; i < g_srcCount; i++) {
        unsigned key = session ? g_src[i].total : g_src[i].window;
        if (!key) continue;
        for (k = 0; k < 6; k++) {
            if (best[k] < 0 || key > (session ? g_src[best[k]].total : g_src[best[k]].window)) {
                unsigned j;
                for (j = 5; j > k; j--) best[j] = best[j - 1];
                best[k] = (int)i;
                break;
            }
        }
    }
    out[0] = 0;
    for (k = 0; k < 6 && best[k] >= 0; k++) {
        const Source *s = &g_src[best[k]];
        char name[33];
        npc_name(s->type, name);
        m += (size_t)snprintf(out + m, sizeof out - m, " obj %08X type %d%s%s%s x%u",
                              (unsigned)s->obj, s->type, name[0] ? " '" : "", name,
                              name[0] ? "'" : "", session ? s->total : s->window);
        if (m >= sizeof out) break;
    }
    u9log_line("SND %7lu ms busiest %s:%s", (unsigned long)(u9time_now() - g_t0), when,
               out[0] ? out : " none");
    if (!session) for (i = 0; i < g_srcCount; i++) g_src[i].window = 0;
}

typedef int (U9CALL *PlayFn)(void *sound, uint32_t h, uint32_t category, uint32_t zero);
static PlayFn o_play;

static int U9CALL h_play(void *sound, uint32_t h, uint32_t category, uint32_t zero)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    uintptr_t obj = obj_of(h), act = 0;
    int type = obj_type(obj), x = 0, y = 0, z = 0, d = -1, havePos, combat, r;
    int cat = g_curCategory;

    combat = ret == COMBATANT_SOUND_RET && g_curActor;
    if (combat) act = g_curActor;
    havePos = obj_pos(obj, &x, &y, &z);
    if (havePos) d = dist_to_avatar(x, y);

    r = o_play(sound, h, category, zero);

    g_sndPlays++;
    if (combat) g_sndCombat++;
    if (r != 0) g_sndFailed++;
    if (g_sndRadius > 0 && (d < 0 || d > g_sndRadius)) return r;
    g_sndShown++;
    count_source(obj, type);
    {
        char name[33], who[200];
        npc_name(type, name);
        if (combat)
            snprintf(who, sizeof who, "creature sound %d, act %08X vt %08X state %d"
                     " flags %04X startle %d fear %.0f", cat,
                     (unsigned)act, readable4(act) ? (unsigned)rd32(act) : 0u,
                     readable4(act + FLY_STATE) ? (int)rd32(act + FLY_STATE) : -1,
                     readable4(act + FLY_FLAGS) ? (unsigned)rd32(act + FLY_FLAGS) : 0u,
                     readable4(act + FLY_STARTLE) ? (int)rd32(act + FLY_STARTLE) : -1,
                     readable4(act + FLY_FEAR) ? (double)as_float(rd32(act + FLY_FEAR)) : -1.0);
        else
            snprintf(who, sizeof who, "from %08X", (unsigned)ret);
        plog(&g_sndLines, SND_CAP, "SND ", "obj %08X type %d%s%s%s cat %u, %s, at %d,%d,%d"
             " Avatar %d away -> %d", (unsigned)obj, type, name[0] ? " '" : "", name,
             name[0] ? "'" : "", (unsigned)(category & 0xFFu), who, x, y, z, d, r);
    }
    return r;
}

typedef int (U9CALL *CombatSoundFn)(void *self, int category);
static CombatSoundFn o_combat;

static int U9CALL h_combat(void *self, int category)
{
    uintptr_t prevActor = g_curActor;
    int prevCat = g_curCategory, r;

    g_curActor = (uintptr_t)self;
    g_curCategory = category;
    r = o_combat(self, category);
    g_curActor = prevActor;
    g_curCategory = prevCat;
    return r;
}

static void sound_tick(void)
{
    DWORD now;
    if (!g_sndOn) return;
    now = u9time_now();
    if ((LONG)(now - g_sndNextSummary) < 0) return;
    g_sndNextSummary = now + SUMMARY_MS;
    if (g_sndShown) summary("last 30 s", 0);
}

void u9bird_install(void)
{
    static const uint8_t expPlay[7]   = { 0x6A, 0xFF, 0x68, 0x98, 0xFE, 0x72, 0x00 };
    static const uint8_t expCombat[6] = { 0x8B, 0x44, 0x24, 0x04, 0x6A, 0x00 };
    static const U9PatchSpec specPlay = {
        "SND_PLAY_FOR_OBJECT", U9PATCH_INLINE, U9SYM_SOUND_PLAY_FOR_OBJECT, 7, expPlay, 7,
        U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specCombat = {
        "SND_COMBATANT_PLAY", U9PATCH_INLINE, U9SYM_CREATURE_SOUND, 6, expCombat, 6,
        U9PF_EXCLUSIVE
    };

    g_t0 = u9time_now();
    g_birdMode = u9cfg_int("diag_bird_watch", 0);
    g_birdEvery = (DWORD)u9cfg_int("diag_bird_watch_ms", 1000);
    if (g_birdEvery < 100) g_birdEvery = 100;
    g_sndOn = u9cfg_int("diag_sound_watch", 0);
    g_sndRadius = u9cfg_int("diag_sound_watch_radius", 0);

    if (g_birdMode) {
        g_birdNext = g_t0;
        u9log_line("BIRD watch on: %s, sampled every %lu ms on the actor tick; state,"
                   " flags, sing deadline and x,y,z on a state change, a move, and every"
                   " 30 s while still", g_birdMode >= 2 ? "every flying creature" : "songbirds",
                   (unsigned long)g_birdEvery);
    }
    if (g_sndOn) {
        o_play = (PlayFn)u9patch_install(&specPlay, (void *)h_play);
        o_combat = (CombatSoundFn)u9patch_install(&specCombat, (void *)h_combat);
        g_sndNextSummary = g_t0 + SUMMARY_MS;
        u9log_line("SND  sound watch: play-for-object %s, creature sounds %s; %s; the"
                   " busiest sources every 30 s", o_play ? "hooked" : "REFUSED",
                   o_combat ? "hooked" : "REFUSED",
                   g_sndRadius > 0 ? "only within diag_sound_watch_radius of the Avatar"
                                   : "every sound, anywhere");
        if (!o_play) g_sndOn = 0;
    }
}

void u9bird_tick(void)
{
    bird_tick();
    sound_tick();
}

void u9bird_report(void)
{
    if (g_birdMode)
        u9log_line("BIRD totals: %u samples, %u birds seen, %u state changes, %u moves,"
                   " %u lines", g_birdSamples, g_birdSeen, g_birdStates, g_birdMoves,
                   g_birdLines);
    if (g_sndOn) {
        summary("whole session", 1);
        u9log_line("SND  totals: %u sounds played for objects, %u of them creature sounds,"
                   " %u not played (sound off or refused), %u logged; %u sources over the"
                   " table's %u", g_sndPlays, g_sndCombat, g_sndFailed, g_sndShown,
                   g_srcDropped, SOURCES_MAX);
    }
}
