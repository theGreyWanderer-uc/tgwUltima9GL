#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "pathprobe.h"
#include "chasegrid.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define PF_WALKER       0xD8u
#define PF_TARGET       0xDCu
#define PF_WOBJ         0xE8u
#define PF_LEGGOAL      0x110u
#define PF_STATE        0x170u
#define PF_CELL         0x17Cu
#define FASTREF_PTR     0x0098F320u

#define WOBJ_MOVER      0x50u
#define MOVER_POS       0xBCu
#define MOVER_SIZE      0xCCu

#define MARKS_MAX       128
#define TRACK_MAX       64
#define LINE_CAP        1500u

typedef struct { float x, y, z, r; DWORD until; unsigned vetoes; } Mark;
typedef struct { uintptr_t pf; int state; DWORD seen; } Track;

typedef void  (U9CALL *TickFn)(void *self);
typedef float (U9CALL *FloorFn)(void *obj, float *slope, float *vec, int flag);

static TickFn   o_tick;
static FloorFn  o_floor;
static int      g_fix, g_watch;
static DWORD    g_keepMs = 20000;
static int      g_pct = 75;
static Mark     g_mark[MARKS_MAX];
static Track    g_track[TRACK_MAX];
static unsigned g_lines, g_marks, g_skippedNearTarget, g_vetoes, g_queries;

static void chaselog(const char *fmt, ...)
{
    char line[400];
    va_list ap;
    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) { g_lines++; u9log_line("CHASE line cap reached - counting continues"); }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    wvsprintfA(line, fmt, ap);
    va_end(ap);
    u9log_line("CHASE %s", line);
}

static int rd_vec(uintptr_t p, float *v)
{
    if (!p || !readable4(p + 8)) return 0;
    v[0] = *(const volatile float *)(p + 0);
    v[1] = *(const volatile float *)(p + 4);
    v[2] = *(const volatile float *)(p + 8);
    return 1;
}

static uintptr_t mover_of(uintptr_t wobj)
{
    return (wobj && readable4(wobj + WOBJ_MOVER)) ? rd32(wobj + WOBJ_MOVER) : 0;
}

static float spacing_of(uintptr_t pf, uintptr_t mover)
{
    float cell = *(const volatile float *)(pf + PF_CELL);
    float sp;
    if (cell == 0.0f && mover && readable4(mover + MOVER_SIZE))
        cell = *(const volatile float *)(mover + MOVER_SIZE);
    if (!(cell > 1.0f && cell < 1000.0f)) cell = 40.0f;
    sp = 1.41421356f * cell;
    return sp > 64.0f ? 64.0f : sp;
}

static void add_mark(float x, float y, float z, float r, DWORD now, int walker, int ahead)
{
    unsigned i, slot = 0;
    LONG left, slotLeft = 0x7FFFFFFF;

    for (i = 0; i < MARKS_MAX; i++) {
        Mark *m = &g_mark[i];
        left = (LONG)(m->until - now);
        if (left > 0 && fabsf(m->x - x) < r * 0.5f && fabsf(m->y - y) < r * 0.5f) {
            m->until = now + g_keepMs;
            return;
        }
        if (left <= 0 || !m->until) left = -0x7FFFFFFF;
        if (left < slotLeft) { slotLeft = left; slot = i; }
    }
    g_mark[slot].x = x; g_mark[slot].y = y; g_mark[slot].z = z; g_mark[slot].r = r;
    g_mark[slot].until = now + g_keepMs;
    g_mark[slot].vetoes = 0;
    g_marks++;
    chaselog("mark %u at %d,%d,%d radius %d, %d ahead of walker %d; kept %lu s", g_marks,
         (int)x, (int)y, (int)z, (int)r, ahead, walker, (unsigned long)(g_keepMs / 1000));
}

static void on_block(uintptr_t pf, DWORD now)
{
    uintptr_t wobj = rd32(pf + PF_WOBJ), mover = mover_of(wobj), tbl, tobj;
    int walker = (int)rd32(pf + PF_WALKER), target = (int)rd32(pf + PF_TARGET);
    float w[3], leg[3], t[3], dx, dy, len, sp, step, mx, my;

    if (!rd_vec(mover ? mover + MOVER_POS : 0, w) || !rd_vec(pf + PF_LEGGOAL, leg)) return;
    dx = leg[0] - w[0]; dy = leg[1] - w[1];
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    sp = spacing_of(pf, mover);
    step = len < sp ? len : sp;
    mx = w[0] + dx / len * step;
    my = w[1] + dy / len * step;

    tbl = readable4(FASTREF_PTR) ? rd32(FASTREF_PTR) : 0;
    if (target >= 0 && target < 0x200 && tbl && readable4(tbl + 4 + (unsigned)target * 4) &&
        (tobj = rd32(tbl + 4 + (unsigned)target * 4)) && rd_vec(mover_of(tobj) ? mover_of(tobj) + MOVER_POS : 0, t)) {
        float ex = t[0] - mx, ey = t[1] - my;
        if (ex * ex + ey * ey < (2.0f * sp) * (2.0f * sp)) { g_skippedNearTarget++; return; }
    }
    add_mark(mx, my, w[2], sp * (float)g_pct / 100.0f, now, walker, (int)step);
}

static void U9CALL h_tick(void *self)
{
    uintptr_t pf = (uintptr_t)self;

    if (g_watch) u9path_observe(self);
    if (g_fix && readable4(pf + PF_STATE)) {
        int target = (int)rd32(pf + PF_TARGET), state = (int)rd32(pf + PF_STATE);
        if (target >= 0 && target < 0x200) {
            DWORD now = u9time_now(), age, slotAge = 0;
            unsigned i, slot = 0;
            Track *k = 0;
            for (i = 0; i < TRACK_MAX; i++) {
                if (g_track[i].pf == pf) { k = &g_track[i]; break; }
                age = g_track[i].pf ? now - g_track[i].seen : 0xFFFFFFFFu;
                if (age > slotAge) { slotAge = age; slot = i; }
            }
            if (!k) {
                k = &g_track[slot];
                k->pf = pf;
                k->state = -1;
            }
            if (state == 5 && k->state != 5) on_block(pf, now);
            k->state = state;
            k->seen = now;
        }
    }
    o_tick(self);
}

static float U9CALL h_floor(void *obj, float *slope, float *vec, int flag)
{
    float h = o_floor(obj, slope, vec, flag), p[3];
    uintptr_t mover;
    DWORD now;
    unsigned i;

    g_queries++;
    mover = mover_of((uintptr_t)obj);
    if (!slope || !rd_vec(mover ? mover + MOVER_POS : 0, p)) return h;
    now = u9time_now();
    for (i = 0; i < MARKS_MAX; i++) {
        Mark *m = &g_mark[i];
        float dx, dy;
        if ((LONG)(m->until - now) <= 0) continue;
        dx = p[0] - m->x; dy = p[1] - m->y;
        if (dx * dx + dy * dy > m->r * m->r || fabsf(p[2] - m->z) > 200.0f) continue;
        *slope = 1.0e6f;
        m->vetoes++;
        g_vetoes++;
        break;
    }
    return h;
}

void u9chase_install(void)
{
    static const uint8_t expTick[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const U9PatchSpec specTick = {
        "CHASE_TICK", U9PATCH_INLINE, U9SYM_PATHFINDER_TICK, 6, expTick, 6, U9PF_EXCLUSIVE
    };
    static const uint8_t expFloor[5] = { 0xE8, 0xF2, 0xA4, 0x06, 0x00 };
    static const U9PatchSpec specFloor = {
        "CHASE_GRID_FLOOR", U9PATCH_CALLSITE, U9SYM_GRID_FLOOR_CALL, 5, expFloor, 5, U9PF_EXCLUSIVE
    };

    g_fix = u9cfg_int("fix_chase_obstacles", 1);
    g_watch = u9cfg_int("diag_path_watch", 0) && u9path_ready();
    if (!g_fix && !g_watch) return;
    g_keepMs = (DWORD)u9cfg_int("fix_chase_obstacle_ms", 20000);
    g_pct = u9cfg_int("fix_chase_obstacle_pct", 75);
    if (g_pct < 10) g_pct = 10;
    if (g_pct > 200) g_pct = 200;

    o_tick = (TickFn)u9patch_install(&specTick, (void *)h_tick);
    if (g_fix && o_tick)
        o_floor = (FloorFn)u9patch_install(&specFloor, (void *)h_floor);
    if (g_fix && !o_floor) g_fix = 0;
    u9log_line("CHASE Pathfinder_Tick %s%s; fix_chase_obstacles %s", o_tick ? "hooked" : "REFUSED",
               g_watch ? " (the chase watch observes through it)" : "",
               g_fix ? "ON - blocked spots are marked for the grid planner" : "off");
    if (g_fix)
        u9log_line("CHASE marks kept %lu ms, radius %d%% of a node spacing; grid floor query %s",
                   (unsigned long)g_keepMs, g_pct, o_floor ? "hooked" : "REFUSED");
}

void u9chase_report(void)
{
    if (!o_tick || !g_fix) return;
    u9log_line("CHASE totals: %u marks, %u blocks not marked (next to the target),"
               " %u grid nodes refused of %u floor queries, %u lines", g_marks,
               g_skippedNearTarget, g_vetoes, g_queries, g_lines);
}
