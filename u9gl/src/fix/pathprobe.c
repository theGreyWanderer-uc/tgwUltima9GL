#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "pathprobe.h"

void u9log_line(const char *fmt, ...);

#define FASTREF_PTR     0x0098F320u

#define PF_WALKER       0xD8u
#define PF_TARGET       0xDCu
#define PF_WOBJ         0xE8u
#define PF_GOAL         0x104u
#define PF_LEGGOAL      0x110u
#define PF_STATE        0x170u
#define PF_PREVSTATE    0x174u
#define PF_LASTSEEN     0x18Cu
#define PF_GRID         0x19Cu

#define WOBJ_MOVER      0x50u
#define MOVER_POS       0xBCu

#define REC_MAX         64u
#define LINE_CAP        4000u
#define GRID_EVERY      60u

typedef struct {
    uintptr_t pf;
    unsigned  seq;
    int       walker, target;
    int       state;
    unsigned  ticks;
    unsigned  lastSeen;
    unsigned  gridSince;
    int       atBlock;
    int       gridMax;
    float     lastLeg[3];
} PathRec;

#define GRID_CELL       0x64u
#define GRID_COLS       0x6Cu
#define GRID_ROWS       0x70u

typedef float *(U9CALL *AimFn)(void *pf, float *out, uintptr_t target);

static int      g_ready;
static AimFn    g_aim;
static int      g_walkerOnly = -1;
static PathRec  g_rec[REC_MAX];
static unsigned g_tick, g_seq, g_lines;

static unsigned g_chases, g_blocks, g_grids, g_gridTicks, g_state7Ticks;
static unsigned g_noAim;
static int      g_worstBlock, g_worstGrid;
static unsigned g_blockHist[5];

static void plog(const char *fmt, ...)
{
    char line[512];
    va_list ap;

    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) {
            g_lines++;
            u9log_line("PATH line cap reached - counting continues and the"
                       " exit recap still prints");
        }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    wvsprintfA(line, fmt, ap);
    va_end(ap);
    u9log_line("%s", line);
}

static const char *state_name(int s)
{
    switch (s) {
    case 0:  return "done";
    case 1:  return "walk";
    case 5:  return "blocked";
    case 6:  return "paused";
    case 7:  return "follow";
    case 9:  return "grid-search";
    case 10: return "grid-follow";
    case 11: return "failed";
    default: return "?";
    }
}

static const char *v3(char *buf, const float *v)
{
    wsprintfA(buf, "%d,%d,%d", (int)v[0], (int)v[1], (int)v[2]);
    return buf;
}

static int dist3(const float *a, const float *b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return (int)(sqrtf(dx * dx + dy * dy + dz * dz) + 0.5f);
}

static void rd_vec(uintptr_t p, float *v)
{
    v[0] = *(const volatile float *)(p + 0);
    v[1] = *(const volatile float *)(p + 4);
    v[2] = *(const volatile float *)(p + 8);
}

static PathRec *rec_get(uintptr_t pf, int walker, int target, int *isNew)
{
    unsigned i, oldest = 0;

    *isNew = 0;
    for (i = 0; i < REC_MAX; i++)
        if (g_rec[i].pf == pf && g_rec[i].walker == walker
            && g_rec[i].target == target)
            return &g_rec[i];
    for (i = 0; i < REC_MAX; i++) {
        if (!g_rec[i].pf) { oldest = i; break; }
        if (g_rec[i].lastSeen < g_rec[oldest].lastSeen) oldest = i;
    }
    for (i = 0; i < REC_MAX; i++)
        if (g_rec[i].pf == pf) g_rec[i].pf = 0;
    ZeroMemory(&g_rec[oldest], sizeof(g_rec[oldest]));
    g_rec[oldest].pf     = pf;
    g_rec[oldest].seq    = ++g_seq;
    g_rec[oldest].walker = walker;
    g_rec[oldest].target = target;
    g_rec[oldest].state  = -1;
    *isNew = 1;
    return &g_rec[oldest];
}

static int live_aim(uintptr_t pf, int target, float *out)
{
    uintptr_t tbl, tobj, wobj, mover;

    tbl = rd32(FASTREF_PTR);
    if (!tbl || !readable4(tbl + 4 + (unsigned)target * 4)) return 0;
    tobj = rd32(tbl + 4 + (unsigned)target * 4);
    wobj = rd32(pf + PF_WOBJ);
    if (!tobj || !readable4(tobj + 0x14) || !wobj || !readable4(wobj + WOBJ_MOVER))
        return 0;
    mover = rd32(wobj + WOBJ_MOVER);
    if (!mover || !readable4(mover + MOVER_POS + 8)) return 0;
    g_aim((void *)pf, out, tobj);
    return 1;
}

static int walker_pos(uintptr_t pf, float *out)
{
    uintptr_t wobj = rd32(pf + PF_WOBJ), mover;

    if (!wobj || !readable4(wobj + WOBJ_MOVER)) return 0;
    mover = rd32(wobj + WOBJ_MOVER);
    if (!mover || !readable4(mover + MOVER_POS + 8)) return 0;
    rd_vec(mover + MOVER_POS, out);
    return 1;
}

static void sample(uintptr_t pf, int walker, int target)
{
    PathRec *r;
    int isNew, state, prevField, dGoal = -1, dLeg = -1, dWalk = -1;
    float aim[3], goal[3], leg[3], wpos[3];
    int hasAim, hasWalk;

    state     = (int)rd32(pf + PF_STATE);
    prevField = (int)rd32(pf + PF_PREVSTATE);
    r = rec_get(pf, walker, target, &isNew);
    r->ticks++;
    r->lastSeen = g_tick;

    rd_vec(pf + PF_GOAL, goal);
    rd_vec(pf + PF_LEGGOAL, leg);
    hasAim  = live_aim(pf, target, aim);
    hasWalk = walker_pos(pf, wpos);
    if (hasAim) {
        dGoal = dist3(aim, goal);
        dLeg  = dist3(aim, leg);
        if (hasWalk) dWalk = dist3(aim, wpos);
    } else {
        g_noAim++;
    }

    if (state == 7) g_state7Ticks++;

    {
        char bw[40] = "?", ba[40] = "?", bl[40] = "?";
        if (hasWalk) v3(bw, wpos);
        if (hasAim) v3(ba, aim);
        v3(bl, leg);
        if (isNew) {
            g_chases++;
            plog("PATH #%u new: walker %d chasing actor %d, state %d %s; goal is %d"
                 " from the target now, walker %d from it | walker %s target %s", r->seq,
                 walker, target, state, state_name(state), dGoal, dWalk, bw, ba);
        }
        if (state != r->state && !isNew)
            plog("PATH #%u tick %u at: walker %s target %s legGoal %s", r->seq, r->ticks,
                 bw, ba, bl);
        if (state == 10 && dist3(leg, r->lastLeg) > 8) {
            plog("PATH #%u tick %u waypoint %s (walker %s, %d from the target)", r->seq,
                 r->ticks, bl, bw, dWalk);
            r->lastLeg[0] = leg[0]; r->lastLeg[1] = leg[1]; r->lastLeg[2] = leg[2];
        }
    }

    if (state != r->state && !isNew) {
        plog("PATH #%u tick %u: %s -> %s (prev field %d); goal %d from the"
             " target, legGoal %d, walker %d from it", r->seq, r->ticks,
             state_name(r->state), state_name(state), prevField, dGoal, dLeg,
             dWalk);
        if (r->state == 9 || r->state == 10) {
            if (state != 9 && state != 10)
                plog("PATH #%u left grid pathing after %u ticks; the path's"
                     " end was up to %d from the target while it ran",
                     r->seq, r->ticks - r->gridSince, r->gridMax);
        }
        if ((state == 9 || state == 10) && r->state != 9 && r->state != 10) {
            g_grids++;
            r->gridSince = r->ticks;
            r->gridMax   = dGoal;
            plog("PATH #%u grid path built toward goal - %d from the target at"
                 " the block, %d now; the chase is %u ticks old", r->seq,
                 r->atBlock, dGoal, r->ticks);
            {
                uintptr_t g = rd32(pf + PF_GRID);
                if (g && readable4(g + GRID_ROWS)) {
                    float cell = *(const volatile float *)(g + GRID_CELL);
                    plog("PATH #%u grid %d x %d cells of %d", r->seq,
                         (int)rd32(g + GRID_COLS), (int)rd32(g + GRID_ROWS), (int)cell);
                }
            }
        }
    }

    if (state == 5 && r->state != 5) {
        unsigned b;
        g_blocks++;
        r->atBlock = dGoal;
        if (dGoal > g_worstBlock) g_worstBlock = dGoal;
        b = dGoal < 64 ? 0 : dGoal < 256 ? 1 : dGoal < 1024 ? 2
          : dGoal < 4096 ? 3 : 4;
        if (dGoal >= 0) g_blockHist[b]++;
    }

    if (state == 9 || state == 10) {
        g_gridTicks++;
        if (dGoal > r->gridMax) r->gridMax = dGoal;
        if (dGoal > g_worstGrid) g_worstGrid = dGoal;
        if (((r->ticks - r->gridSince) % GRID_EVERY) == 0 && r->ticks != r->gridSince)
            plog("PATH #%u tick %u: still %s, path end %d from the target,"
                 " walker %d from it, grid %s", r->seq, r->ticks,
                 state_name(state), dGoal, dWalk,
                 rd32(pf + PF_GRID) ? "present" : "gone");
    }

    r->state = state;
}

void u9path_observe(void *self)
{
    uintptr_t pf = (uintptr_t)self;
    int walker, target;

    if (!g_ready) return;
    g_tick++;
    if (!readable4(pf + PF_GRID)) return;
    walker = (int)rd32(pf + PF_WALKER);
    target = (int)rd32(pf + PF_TARGET);
    if (target >= 0 && target < 0x200
        && (g_walkerOnly < 0 || walker == g_walkerOnly))
        sample(pf, walker, target);
}

int u9path_ready(void)
{
    return g_ready;
}

void u9path_install(void)
{
    static const uint8_t expAim[3] = { 0x83, 0xEC, 0x78 };
    uintptr_t aim = u9sym(U9SYM_PATHFINDER_AIM);

    g_walkerOnly = u9cfg_int("diag_path_walker", -1);
    if (!aim || memcmp((const void *)aim, expAim, sizeof(expAim)) != 0) {
        u9log_line("PATH chase watch: Pathfinder_AimAtTarget not where expected"
                   " - not installed");
        return;
    }
    g_aim   = (AimFn)aim;
    g_ready = 1;
    u9log_line("PATH chase watch: ready, observing through the CHASE tick hook;"
               " actor-target pathfinders only, walker %d (-1 = all)", g_walkerOnly);
}

void u9path_report(void)
{
    unsigned i, live = 0;

    if (!g_ready || !g_tick) return;
    u9log_line("");
    u9log_line("PATH chases seen %u; blocked %u times, grid paths %u, %u ticks"
               " grid-walking, %u ticks following", g_chases, g_blocks, g_grids,
               g_gridTicks, g_state7Ticks);
    u9log_line("PATH goal drift at the block: <64 %u, <256 %u, <1024 %u,"
               " <4096 %u, more %u; worst %d at a block, %d during a grid walk",
               g_blockHist[0], g_blockHist[1], g_blockHist[2], g_blockHist[3],
               g_blockHist[4], g_worstBlock, g_worstGrid);
    if (g_noAim)
        u9log_line("PATH %u samples had no readable target or walker", g_noAim);
    for (i = 0; i < REC_MAX; i++) {
        if (!g_rec[i].pf) continue;
        if (live++ >= 16) break;
        u9log_line("PATH #%u walker %d -> actor %d: %u ticks, last %s",
                   g_rec[i].seq, g_rec[i].walker, g_rec[i].target,
                   g_rec[i].ticks, state_name(g_rec[i].state));
    }
}
