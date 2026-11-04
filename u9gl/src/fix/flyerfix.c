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
#include "flyerfix.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define ACT_MOVER       0x14u
#define ACT_SELF        0x18u
#define FLY_STATE       0x214u
#define FLY_FLAGS       0x228u
#define FLY_MOVER       0x238u

#define MOVER_FLAGS     0xD8u
#define MOVER_YAW       0x1A8u
#define VT_GETYAW       0xCCu
#define VT_PLACENEAR    0xE8u

#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define REC_NAME        0x04u

#define STREAKS_MAX     64
#define LINE_CAP        2000
#define STILL_MS        10000u
#define ATTEMPTS_MAX    3
#define RETRY_MS        1000u

typedef int (U9CALL *PlaceNearFn)(void *mover, float *dest, float facing, int testOnly);

typedef struct {
    uintptr_t act;
    DWORD     first, last, lastStill, nextTry;
    int       x, y, z;
    unsigned  reports, flagged, attempts, placed;
} Streak;

static int      g_fix, g_watch;
static DWORD    g_stuckMs = 2500;
static unsigned g_stuckReports = 4;
static int      g_stuckDist = 16;
static DWORD    g_t0;
static unsigned g_lines, g_calls, g_found, g_freed, g_tries, g_placed, g_refused, g_gaveUp;
static unsigned g_dropped, g_gone;
static Streak   g_streak[STREAKS_MAX];
static unsigned g_streakCount;

static void slog(const char *fmt, ...)
{
    char line[480];
    va_list ap;

    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) { g_lines++; u9log_line("STUCK line cap reached - counting continues"); }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    u9log_line("STUCK %7lu ms %s", (unsigned long)(u9time_now() - g_t0), line);
}

static uintptr_t actor_object(uintptr_t act)
{
    uintptr_t tab = readable4(ENTITY_TABLE) ? rd32(ENTITY_TABLE) : 0;
    uint32_t h;
    if (!act || !tab || !readable4(act + ACT_SELF)) return 0;
    h = rd32(act + ACT_SELF);
    if (!h || !readable4(tab + h * 12u + 8u)) return 0;
    return rd32(tab + h * 12u + 8u);
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

static int actor_pos(uintptr_t act, int *x, int *y, int *z)
{
    return obj_pos(actor_object(act), x, y, z);
}

static int dist_to_avatar(int x, int y)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t av = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    int ax, ay, az;
    double dx, dy;
    if (!actor_pos(av, &ax, &ay, &az)) return -1;
    dx = x - ax; dy = y - ay;
    return (int)sqrt(dx * dx + dy * dy);
}

static void npc_name(uintptr_t act, int *type, char out[33])
{
    uintptr_t obj = actor_object(act);
    uintptr_t base = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0, r;

    out[0] = 0;
    *type = (obj && readable4(obj + OBJ_TYPE)) ? *(const volatile uint16_t *)(obj + OBJ_TYPE) : -1;
    if (*type < 0 || (unsigned)*type >= ACTOR_COUNT || !base) return;
    r = base + (unsigned)*type * RECORD_STRIDE;
    if (!readable4(r + REC_NAME) || !readable4(r + REC_NAME + 28)) return;
    memcpy(out, (const void *)(r + REC_NAME), 32);
    out[32] = 0;
}

static void streak_line(const char *what, const Streak *k, uintptr_t act, DWORD now)
{
    int type, x = 0, y = 0, z = 0;
    char name[33];
    DWORD span = now - k->first;

    npc_name(act, &type, name);
    actor_pos(act, &x, &y, &z);
    slog("%-7s act %08X vt %08X npc %d '%s' state %d flags %04X: %u refusals in %lu ms (%.1f/s),"
         " at %d,%d,%d (began %d,%d,%d), Avatar %d away", what, (unsigned)act,
         readable4(act) ? (unsigned)rd32(act) : 0u, type, name,
         readable4(act + FLY_STATE) ? (int)rd32(act + FLY_STATE) : -1,
         readable4(act + FLY_FLAGS) ? (unsigned)rd32(act + FLY_FLAGS) : 0u,
         k->reports, (unsigned long)span, span ? k->reports * 1000.0 / span : 0.0,
         x, y, z, k->x, k->y, k->z, dist_to_avatar(x, y));
}

static int actor_alive(uintptr_t act)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    unsigned i;
    if (!act || !tbl || !readable4(tbl) || !readable4(tbl + (ACTOR_COUNT - 1) * 4u)) return 0;
    for (i = 0; i < ACTOR_COUNT; i++) if (rd32(tbl + i * 4u) == act) return 1;
    return 0;
}

static Streak *streak_of(uintptr_t act, DWORD now)
{
    unsigned i, oldest = 0;
    for (i = 0; i < g_streakCount; i++) if (g_streak[i].act == act) return &g_streak[i];
    if (g_streakCount < STREAKS_MAX) i = g_streakCount++;
    else {
        for (i = 1; i < g_streakCount; i++)
            if (now - g_streak[i].last > now - g_streak[oldest].last) oldest = i;
        if (now - g_streak[oldest].last < 60000u) { g_dropped++; return 0; }
        i = oldest;
    }
    memset(&g_streak[i], 0, sizeof g_streak[i]);
    g_streak[i].act = act;
    return &g_streak[i];
}

static void check_stuck(Streak *k, DWORD now)
{
    uint32_t flags;
    int state;

    if (k->flagged || !k->reports || k->reports < g_stuckReports) return;
    if (now - k->first < g_stuckMs || now - k->last > 1000u) return;
    flags = readable4(k->act + FLY_FLAGS) ? rd32(k->act + FLY_FLAGS) : 0;
    state = readable4(k->act + FLY_STATE) ? (int)rd32(k->act + FLY_STATE) : -1;
    if ((flags & 0x200u) || state == 4) return;
    k->flagged = 1;
    k->lastStill = k->nextTry = now;
    g_found++;
    streak_line("STUCK", k, k->act, now);
}

typedef void (U9CALL *BlockedFn)(void *self, uint32_t a, uint32_t b);
static BlockedFn o_blocked;

static void U9CALL h_blocked(void *self, uint32_t a, uint32_t b)
{
    uintptr_t act = (uintptr_t)self;
    DWORD now = u9time_now();
    Streak *k;
    int x, y, z;

    o_blocked(self, a, b);
    g_calls++;
    if (!(k = streak_of(act, now))) return;
    if (!actor_pos(act, &x, &y, &z)) return;

    if (k->reports && (abs(x - k->x) > g_stuckDist || abs(y - k->y) > g_stuckDist ||
                       abs(z - k->z) > g_stuckDist || now - k->last > 5000u)) {
        if (k->reports >= 3) {
            g_freed++;
            streak_line(k->placed ? "PLACED+" : k->flagged ? "UNSTUCK" : "FREED", k, act, k->last);
        }
        k->reports = 0;
    }
    if (!k->reports) {
        k->first = k->lastStill = now;
        k->x = x; k->y = y; k->z = z;
        k->flagged = k->attempts = k->placed = 0;
    }
    k->reports++;
    k->last = now;

    if (!k->flagged) check_stuck(k, now);
    else if (now - k->lastStill >= STILL_MS) {
        k->lastStill = now;
        streak_line("still", k, act, now);
    }
}

static uintptr_t placeable_mover(uintptr_t act, const char **which)
{
    static const unsigned offs[2] = { FLY_MOVER, ACT_MOVER };
    unsigned i;
    for (i = 0; i < 2; i++) {
        uintptr_t m = readable4(act + offs[i]) ? rd32(act + offs[i]) : 0, vt;
        if (!m || !readable4(m) || !(vt = rd32(m)) || !readable4(vt + VT_PLACENEAR)) continue;
        if (rd32(vt + VT_PLACENEAR) != u9sym(U9SYM_MOVER_PLACENEAR)) continue;
        *which = i == 0 ? "+0x238" : "+0x14";
        return m;
    }
    return 0;
}

static float mover_yaw(uintptr_t m)
{
    uintptr_t vt = rd32(m);
    float yaw = 0.0f;
    if (readable4(vt + VT_GETYAW) && rd32(vt + VT_GETYAW) == u9sym(U9SYM_MOVER_GETYAW)) {
        uint32_t v = rd32(m + MOVER_YAW);
        memcpy(&yaw, &v, 4);
        if (!(yaw > -100.0f && yaw < 100.0f)) yaw = 0.0f;
    }
    return yaw;
}

static int far_enough(const float *p, int x, int y, int z)
{
    return fabsf(p[0] - x) > 32.0f || fabsf(p[1] - y) > 32.0f || fabsf(p[2] - z) > 32.0f;
}

static void try_place(Streak *k, DWORD now)
{
    static const int cand[][3] = {
        { 0, 0, 300 }, { 0, 0, 600 },
        { 400, 0, 0 }, { -400, 0, 0 }, { 0, 400, 0 }, { 0, -400, 0 },
        { 283, 283, 0 }, { -283, 283, 0 }, { 283, -283, 0 }, { -283, -283, 0 },
    };
    PlaceNearFn place = (PlaceNearFn)u9sym(U9SYM_MOVER_PLACENEAR);
    const char *which = "?";
    uintptr_t m = placeable_mover(k->act, &which);
    int x, y, z, nx, ny, nz, ok;
    float dest[3], yaw;
    unsigned i;
    const char *how = 0;

    k->attempts++;
    k->nextTry = now + RETRY_MS;
    g_tries++;
    if (!m || !place || !actor_pos(k->act, &x, &y, &z)) {
        g_refused++;
        slog("REFUSED act %08X: no mover with Mover_PlaceNear at +0x238 or +0x14", (unsigned)k->act);
        return;
    }
    yaw = mover_yaw(m);

    dest[0] = (float)x; dest[1] = (float)y; dest[2] = (float)z;
    ok = place((void *)m, dest, yaw, 1);
    if (ok && far_enough(dest, x, y, z)) how = "nearest free spot";
    else {
        for (i = 0; i < sizeof cand / sizeof cand[0]; i++) {
            dest[0] = (float)(x + cand[i][0]);
            dest[1] = (float)(y + cand[i][1]);
            dest[2] = (float)(z + cand[i][2]);
            ok = place((void *)m, dest, yaw, 1);
            if (ok && far_enough(dest, x, y, z)) { how = i < 2 ? "above" : "beside"; break; }
        }
    }
    if (!how) {
        g_refused++;
        slog("REFUSED act %08X try %u: no free spot passed the test around %d,%d,%d (mover %s,"
             " +0xD8 %08X)", (unsigned)k->act, k->attempts, x, y, z, which,
             readable4(m + MOVER_FLAGS) ? (unsigned)rd32(m + MOVER_FLAGS) : 0u);
    } else {
        ok = place((void *)m, dest, yaw, 0);
        actor_pos(k->act, &nx, &ny, &nz);
        if (abs(nx - x) > g_stuckDist || abs(ny - y) > g_stuckDist || abs(nz - z) > g_stuckDist) {
            k->placed = 1;
            g_placed++;
        }
        slog("PLACE   act %08X try %u after %lu ms stuck: %s %.0f,%.0f,%.0f -> returned %d, now"
             " at %d,%d,%d%s (mover %s, +0xD8 %08X)", (unsigned)k->act, k->attempts,
             (unsigned long)(now - k->first), how, dest[0], dest[1], dest[2], ok, nx, ny, nz,
             k->placed ? "" : " - DID NOT MOVE", which,
             readable4(m + MOVER_FLAGS) ? (unsigned)rd32(m + MOVER_FLAGS) : 0u);
    }
    if (!k->placed && k->attempts >= ATTEMPTS_MAX) {
        g_gaveUp++;
        slog("GAVE UP act %08X after %u attempts; left as it is", (unsigned)k->act, k->attempts);
    }
}

void u9flyer_install(void)
{
    static const uint8_t expBlocked[6] = { 0x8B, 0x81, 0x28, 0x02, 0x00, 0x00 };
    static const U9PatchSpec specBlocked = {
        "FLYER_BLOCKED", U9PATCH_INLINE, U9SYM_FLYER_REFUSED, 6, expBlocked, 6, U9PF_EXCLUSIVE
    };

    g_fix = u9cfg_int("fix_flyer_unstick", 1);
    g_watch = u9cfg_int("diag_stuck_watch", 0);
    if (!g_fix && !g_watch) return;
    g_stuckMs = (DWORD)u9cfg_int("fix_flyer_stuck_ms", 2500);
    g_stuckReports = (unsigned)u9cfg_int("fix_flyer_stuck_reports", 4);
    g_stuckDist = u9cfg_int("fix_flyer_stuck_dist", 16);
    g_t0 = u9time_now();

    o_blocked = (BlockedFn)u9patch_install(&specBlocked, (void *)h_blocked);
    u9log_line("STUCK flyers: blocked report %s; stuck after %lu ms with %u refusals within %d"
               " units, not perched or dying; %s", o_blocked ? "hooked" : "REFUSED",
               (unsigned long)g_stuckMs, g_stuckReports, g_stuckDist,
               g_fix ? "fix_flyer_unstick ON - stuck flyers are placed nearby"
                     : "watch only (diag_stuck_watch), nothing is moved");
}

void u9flyer_tick(void)
{
    DWORD now;
    unsigned i;

    if (!o_blocked) return;
    now = u9time_now();
    for (i = 0; i < g_streakCount; i++) {
        Streak *k = &g_streak[i];
        if (!actor_alive(k->act)) {
            g_gone++;
            g_streak[i--] = g_streak[--g_streakCount];
            continue;
        }
        check_stuck(k, now);
        if (!g_fix || !k->flagged || k->placed || k->attempts >= ATTEMPTS_MAX) continue;
        if ((LONG)(now - k->nextTry) < 0) continue;
        if (readable4(k->act + FLY_FLAGS) && (rd32(k->act + FLY_FLAGS) & 0x200u)) continue;
        if (readable4(k->act + FLY_STATE) && rd32(k->act + FLY_STATE) == 4) continue;
        if (now - k->last > 5000u) continue;
        try_place(k, now);
    }
}

void u9flyer_report(void)
{
    if (!o_blocked) return;
    u9log_line("STUCK totals: %u refusals, %u streaks stuck, %u ended free after 3+ refusals;"
               " %u placement tries, %u moved the creature, %u refused, %u gave up; %u creatures"
               " over the table's %u, %u forgotten when unloaded, %u lines", g_calls, g_found,
               g_freed, g_tries, g_placed, g_refused, g_gaveUp, g_dropped, STREAKS_MAX, g_gone,
               g_lines);
}
