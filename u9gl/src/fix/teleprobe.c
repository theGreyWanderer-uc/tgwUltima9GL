#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "teleprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define FA_MGR          0x00AD127Cu
#define MARKER_ID       0x6F6u

#define SP_FLAGS        0x1Cu
#define SP_HOLD         0xE4u
#define SP_STATE        0xE8u
#define SP_CASTER       0xF0u
#define SP_TARGET       0xF4u
#define SP_EFFECT       0xF8u

#define OBJ_XY          0x04u
#define OBJ_ZID         0x08u
#define OBJ_FLAGS       0x14u
#define PAGE_BASE_X     0x0Cu
#define PAGE_BASE_Y     0x10u

#define ACT_NPCDATA     0x0Cu
#define ACT_SELF        0x18u
#define ACT_ENEMY       0x104u

static unsigned g_lineCap = 900u;
static unsigned g_keyCap  = 200u;
static unsigned g_lines;

static void tlog(const char *fmt, ...)
{
    char line[1024];
    va_list ap;

    if (g_lines >= g_lineCap) {
        if (g_lines == g_lineCap) {
            g_lines++;
            u9log_line("TELE line cap reached - counting continues and the"
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

static unsigned g_keyLines;

static void tlog_key(const char *fmt, ...)
{
    char line[1024];
    va_list ap;

    if (g_keyLines >= g_keyCap) return;
    g_keyLines++;
    va_start(ap, fmt);
    wvsprintfA(line, fmt, ap);
    va_end(ap);
    u9log_line("%s", line);
}

typedef struct {
    uint32_t  h;
    uintptr_t ent;
    uint32_t  flags;
    unsigned  id;
    int       pos[3];
    int       posOk;
} Ent;

static uintptr_t handle_ent(uint32_t h)
{
    uintptr_t tbl, slot;

    if (!h || !readable4(ENTITY_TABLE)) return 0;
    tbl = rd32(ENTITY_TABLE);
    if (!tbl) return 0;
    slot = tbl + h * 0x0Cu;
    if (!readable4(slot + 8)) return 0;
    return rd32(slot + 8);
}

static void ent_fill(uintptr_t ent, Ent *e)
{
    uintptr_t page;
    uint32_t xy, zid;

    e->ent = ent;
    if (!ent || !readable4(ent + OBJ_FLAGS)) { e->ent = 0; return; }
    e->flags = rd32(ent + OBJ_FLAGS);
    xy  = rd32(ent + OBJ_XY);
    zid = rd32(ent + OBJ_ZID);
    e->id = zid >> 16;
    if (e->flags & 0x200u) return;
    page = ent & ~(uintptr_t)0xFFFu;
    if (!readable4(page + PAGE_BASE_Y)) return;
    e->pos[0] = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + PAGE_BASE_X);
    e->pos[1] = (int16_t)(xy >> 16)     + (int32_t)rd32(page + PAGE_BASE_Y);
    e->pos[2] = (int16_t)(zid & 0xFFFFu);
    e->posOk = 1;
}

static void ent_read(uint32_t h, Ent *e)
{
    memset(e, 0, sizeof *e);
    e->h = h;
    ent_fill(handle_ent(h), e);
}

static int ref_none(uint32_t flags)
{
    unsigned top = flags >> 21;
    return top == 0 || top == 0x7FEu || top == 0x7FFu;
}

static uintptr_t fast_slot(const Ent *e, const char **why)
{
    uintptr_t mgr, grid, cell, base;
    int cx, cy;

    if (ref_none(e->flags)) { *why = "no fast ref"; return 0; }
    if (!e->posOk)          { *why = "contained";   return 0; }
    mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0;
    if (!mgr || !readable4(mgr + 0x64)) { *why = "no area manager"; return 0; }
    grid = rd32(mgr + 0x64);
    if (!grid || !readable4(grid + 0x30)) { *why = "no area grid"; return 0; }
    cx = e->pos[0] >> 10;
    cy = e->pos[1] >> 10;
    if (cx < 0 || cy < 0 || cx >= ((int32_t)rd32(grid + 0x30) >> 3) ||
        cy >= ((int32_t)rd32(grid + 0x2C) >> 3)) {
        *why = "off the area grid";
        return 0;
    }
    cell = rd32(mgr + 0x44) +
           ((uintptr_t)rd32(mgr + 0x4C) * (unsigned)cy + (unsigned)cx) * 0x28u;
    if (!readable4(cell + 0x10)) { *why = "cell unreadable"; return 0; }
    base = rd32(cell + 0x10);
    if (!base) { *why = "cell not set up"; return 0; }
    *why = "ok";
    return base + (e->flags >> 21) * 0x10u;
}

static uintptr_t resource_of(const Ent *e, const char **why)
{
    uintptr_t slot = fast_slot(e, why), res;

    if (!slot) return 0;
    if (!readable4(slot + 0x0C)) { *why = "slot unreadable"; return 0; }
    res = rd32(slot + 0x0C);
    if (!res) *why = "slot holds none";
    return res;
}

static char *ent_str(const Ent *e, char *buf)
{
    char ref[16];
    int n;

    if (!e->h) { lstrcpyA(buf, "no handle"); return buf; }
    if (!e->ent) {
        wsprintfA(buf, "h%u -> NOTHING, the table slot is empty", e->h);
        return buf;
    }
    if (ref_none(e->flags)) wsprintfA(ref, "none(%X)", e->flags >> 21);
    else                    wsprintfA(ref, "%u", e->flags >> 21);
    n = wsprintfA(buf, "h%u -> %08X id %u flags %08X (%s%s%s%s) ref %s",
                  e->h, (unsigned)e->ent, e->id, e->flags,
                  (e->flags & 1u)     ? "fixed" : "nonfixed",
                  (e->flags & 0x4u)   ? ",detached" : "",
                  (e->flags & 0x200u) ? ",contained" : "",
                  (e->flags & 0x400u) ? ",disposable" : "", ref);
    if (e->posOk)
        wsprintfA(buf + n, " at %d,%d,%d", e->pos[0], e->pos[1], e->pos[2]);
    return buf;
}

static unsigned dist_xy(const int a[3], const int b[3])
{
    double dx = (double)a[0] - b[0], dy = (double)a[1] - b[1];
    return (unsigned)(sqrt(dx * dx + dy * dy) + 0.5);
}

#define FA_RADIUS       2896
#define EDGE_WARN       512

typedef struct {
    int      ok, inArea;
    unsigned cx, cy;
    int      margin;
} Edge;

static int focus_xy(double *fx, double *fy)
{
    uintptr_t mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0, cam;
    float x, y;
    uint32_t vx, vy;
    if (!mgr || !readable4(mgr + 0x60)) return 0;
    cam = rd32(mgr + 0x60);
    if (!cam || !readable4(cam + 0xE4)) return 0;
    vx = rd32(cam + 0xE0); vy = rd32(cam + 0xE4);
    memcpy(&x, &vx, 4); memcpy(&y, &vy, 4);
    *fx = x; *fy = y;
    return 1;
}

static void edge_of(uintptr_t obj, Edge *g)
{
    uintptr_t mgr, cells;
    Ent e;
    double fx, fy, dx, dy;

    memset(g, 0, sizeof *g);
    memset(&e, 0, sizeof e);
    if (!obj) return;
    ent_fill(obj, &e);
    if (!e.posOk || e.pos[0] < 0 || e.pos[1] < 0) return;
    g->cx = (unsigned)e.pos[0] >> 10;
    g->cy = (unsigned)e.pos[1] >> 10;
    mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0;
    if (mgr && readable4(mgr + 0x50) && (cells = rd32(mgr + 0x44)) != 0 &&
        g->cx < rd32(mgr + 0x4C) && g->cy < rd32(mgr + 0x50)) {
        uintptr_t cell = cells + (rd32(mgr + 0x4C) * g->cy + g->cx) * 0x28u;
        if (readable4(cell + 0x1C)) g->inArea = (rd32(cell + 0x1C) & 2u) != 0;
    }
    if (!focus_xy(&fx, &fy)) return;
    dx = fx - (g->cx * 1024.0 + 512.0);
    dy = fy - (g->cy * 1024.0 + 512.0);
    g->margin = FA_RADIUS - (int)(sqrt(dx * dx + dy * dy) + 0.5);
    g->ok = 1;
}

static char *edge_str(const Edge *g, char *buf)
{
    if (!g->ok) { lstrcpyA(buf, "unknown"); return buf; }
    wsprintfA(buf, "cell %u,%u %s, margin %d", g->cx, g->cy,
              g->inArea ? "in" : "NOT in", g->margin);
    return buf;
}

static int player_xy(int p[3])
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t act = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    Ent pl;
    if (!act || !readable4(act + ACT_SELF)) return 0;
    ent_read(rd32(act + ACT_SELF), &pl);
    if (!pl.posOk) return 0;
    memcpy(p, pl.pos, sizeof pl.pos);
    return 1;
}

#define TELE_MAX 16

typedef struct {
    uintptr_t spell;
    unsigned  seq;
    int       live;
    uint32_t  state, hold;
    unsigned  ticks, inState;
    char      path[20];
    unsigned  pathLen;
    uint32_t  hCaster, hTarget;
    uintptr_t caster, target;
    int       acted5;
    int       moveCalled;
    uint32_t  moveRet;
    int       before[3], beforeOk;
    int       want[3];
    int       movedBy;
    unsigned  releases, frees;
    int       relOpen;
    DWORD     relAt;
    unsigned  relAiTicks;
    unsigned  aiTicks;
    int       minEdgeC, minEdgeT, warnedC, warnedT;
    int       pl[3], plOk;
    DWORD     plAt;
} TeleRec;

static TeleRec  g_rec[TELE_MAX];
static unsigned g_seq, g_overwritten;

static unsigned g_moveCalls, g_moveZero, g_moved, g_notMoved;
static unsigned g_null5Res, g_null5Target, g_acted5;
static unsigned g_relCaster, g_relTarget, g_freeCaster, g_freeTarget;
static unsigned g_markerRel, g_markerFree, g_finished;
static unsigned g_inMove;
static int      g_okMove;
static float    g_queryGround;
static int      g_queryGroundOk;
static unsigned g_landedHigh;
static unsigned g_relBack, g_relNever, g_relBackMaxMs, g_relBackSumMs;
static unsigned g_edgeWarnC, g_edgeWarnT, g_camChanges;
static uintptr_t g_cam;

static TeleRec *rec_find(uintptr_t sp)
{
    unsigned i;
    for (i = 0; i < TELE_MAX; i++)
        if (g_rec[i].live && g_rec[i].spell == sp) return &g_rec[i];
    return 0;
}

static TeleRec *rec_new(uintptr_t sp)
{
    TeleRec *best = 0;
    unsigned i;

    for (i = 0; i < TELE_MAX; i++)
        if (!g_rec[i].live && (!best || g_rec[i].seq < best->seq))
            best = &g_rec[i];
    if (!best) {
        for (i = 0; i < TELE_MAX; i++)
            if (!best || g_rec[i].seq < best->seq) best = &g_rec[i];
        g_overwritten++;
    }
    memset(best, 0, sizeof *best);
    best->spell = sp;
    best->seq = ++g_seq;
    best->live = 1;
    best->state = 0xFFFFFFFFu;
    best->movedBy = -1;
    best->minEdgeC = best->minEdgeT = 0x7FFFFFFF;
    return best;
}

static void path_add(TeleRec *r, uint32_t state)
{
    static const char HEX[] = "0123456789ABCDEF";
    if (r->pathLen >= sizeof r->path - 1) return;
    r->path[r->pathLen++] = state < 16 ? HEX[state] : '?';
    r->path[r->pathLen] = 0;
}

static int effect_playing(uint32_t h) { return h && handle_ent(h) != 0; }

static void actor_lines(const TeleRec *r, const Ent *c, const Ent *t)
{
    uintptr_t tbl, act, npc, enemyAct;
    unsigned radius = 0;
    int enemy;
    Ent en;
    char es[256];

    if (!c->ent || c->id >= ACTOR_COUNT) return;
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (!tbl || !readable4(tbl + c->id * 4u)) return;
    act = rd32(tbl + c->id * 4u);
    if (!act || !readable4(act) || !is_actor_vtable(rd32(act))) {
        tlog("TELE #%u     caster id %u is not an actor-class object (%08X)",
             r->seq, c->id, (unsigned)act);
        return;
    }
    enemy = (int32_t)rd32(act + ACT_ENEMY);
    npc = rd32(act + ACT_NPCDATA);
    if (npc && readable4(npc + 0x54)) radius = rd32(npc + 0x54) & 0xFFFFu;

    memset(&en, 0, sizeof en);
    if (enemy >= 0 && (unsigned)enemy < ACTOR_COUNT &&
        readable4(tbl + (unsigned)enemy * 4u)) {
        enemyAct = rd32(tbl + (unsigned)enemy * 4u);
        if (enemyAct && readable4(enemyAct + ACT_SELF))
            ent_read(rd32(enemyAct + ACT_SELF), &en);
    }
    tlog("TELE #%u     caster actor %08X vtable %08X, notice radius %u;"
         " enemy index %d: %s", r->seq, (unsigned)act, rd32(act), radius,
         enemy, ent_str(&en, es));
    if (c->posOk && t->posOk && en.posOk)
        tlog("TELE #%u     the spot is %u from the caster and %u from the enemy"
             " across the ground, %d above the enemy's feet and %d above the"
             " caster's", r->seq, dist_xy(c->pos, t->pos),
             dist_xy(en.pos, t->pos), t->pos[2] - en.pos[2],
             t->pos[2] - c->pos[2]);
}

static void edge_line(TeleRec *r, const Ent *c, const Ent *t);

static void first_sight(TeleRec *r, uint32_t state, uint32_t hold)
{
    Ent c, t;
    char cs[256], ts[256];
    const char *why = "?";
    uintptr_t res;

    ent_read(r->hCaster, &c);
    ent_read(r->hTarget, &t);
    r->state = state;
    r->hold = hold;
    path_add(r, state);

    tlog("");
    tlog("TELE #%u spell %08X first seen in state %u, +0xE4 %u", r->seq,
         (unsigned)r->spell, state, hold);
    tlog("TELE #%u     caster %s", r->seq, ent_str(&c, cs));
    tlog("TELE #%u     target %s", r->seq, ent_str(&t, ts));

    res = c.ent ? resource_of(&c, &why) : 0;
    if (res) {
        uintptr_t mover = readable4(res + 0xE8) ? rd32(res + 0xE8) : 0;
        uintptr_t vt = (mover && readable4(mover)) ? rd32(mover) : 0;
        uintptr_t fn = (vt && readable4(vt + 0xE4)) ? rd32(vt + 0xE4) : 0;
        tlog("TELE #%u     caster resource %08X, its +0xE8 %08X vtable %08X,"
             " whose +0xE4 (the move) is %08X", r->seq, (unsigned)res,
             (unsigned)mover, (unsigned)vt, (unsigned)fn);
    } else {
        tlog("TELE #%u     caster resource NONE (%s) - if that holds until"
             " state 5, it is F-5's first null", r->seq,
             c.ent ? why : "no caster object");
    }
    actor_lines(r, &c, &t);
    edge_line(r, &c, &t);
}

static void state5_check(TeleRec *r)
{
    Ent c, t;
    char ts[256];
    const char *why = "no caster object";
    uintptr_t res;

    r->acted5 = 1;
    g_acted5++;
    ent_read(r->hCaster, &c);
    ent_read(r->hTarget, &t);
    res = c.ent ? resource_of(&c, &why) : 0;
    if (!res) g_null5Res++;
    if (!t.ent) g_null5Target++;
    ((!res || !t.ent) ? tlog_key : tlog)
        ("TELE #%u state 5 acts now (tick %u): caster resource %08X (%s),"
         " target %s%s", r->seq, r->ticks, (unsigned)res, res ? "ok" : why,
         ent_str(&t, ts),
         (!res || !t.ent) ? "  <- an F-5 null; the guard will end the spell"
                          : "");
}

static void edge_line(TeleRec *r, const Ent *c, const Ent *t)
{
    Edge ec, et;
    char a[96], b[96], pl[160];
    int p[3];
    DWORD now = u9time_now();

    edge_of(c->ent, &ec);
    edge_of(t->ent, &et);
    if (player_xy(p)) {
        int n = wsprintfA(pl, "player at %d,%d", p[0], p[1]);
        if (c->posOk) n += wsprintfA(pl + n, ", %u from the caster", dist_xy(p, c->pos));
        if (t->posOk) n += wsprintfA(pl + n, ", %u from the marker", dist_xy(p, t->pos));
        if (r->plOk && (LONG)(now - r->plAt) > 0)
            wsprintfA(pl + n, ", moving %u/s", (unsigned)(dist_xy(p, r->pl) * 1000u / (now - r->plAt)));
        memcpy(r->pl, p, sizeof r->pl);
        r->plOk = 1;
        r->plAt = now;
    } else {
        lstrcpyA(pl, "player not found");
    }
    tlog("TELE #%u     edge: caster %s; marker %s; %s", r->seq, edge_str(&ec, a),
         edge_str(&et, b), pl);
}

static void transition(TeleRec *r, uint32_t after, uint32_t eff)
{
    Ent c, t;
    char cs[256], ts[256];

    path_add(r, after);
    ent_read(r->hCaster, &c);
    ent_read(r->hTarget, &t);
    tlog("TELE #%u tick %u: state %u -> %u after %u ticks in it; effect h%u %s%s",
         r->seq, r->ticks, r->state, after, r->inState, eff,
         !eff ? "(none)" : effect_playing(eff) ? "(playing)" : "(gone)",
         (after != 2 && after != 3 && after != 5 && after != 7)
           ? "  <- not a handled state: the switch default returns, so this"
             " spell cannot advance" : "");
    tlog("TELE #%u     caster %s", r->seq, ent_str(&c, cs));
    tlog("TELE #%u     target %s", r->seq, ent_str(&t, ts));
    edge_line(r, &c, &t);
    r->state = after;
    r->inState = 1;
}

static void watch_tick(TeleRec *r)
{
    uintptr_t mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0;
    uintptr_t cam = (mgr && readable4(mgr + 0x60)) ? rd32(mgr + 0x60) : 0;
    Edge ec, et;
    char a[96];

    if (cam && cam != g_cam) {
        if (g_cam) {
            g_camChanges++;
            tlog_key("TELE #%u the fast area's camera object changed %08X -> %08X in state"
                     " %u at tick %u - every cell is dropped", r->seq, (unsigned)g_cam,
                     (unsigned)cam, r->state, r->ticks);
        }
        g_cam = cam;
    }

    edge_of(r->caster, &ec);
    edge_of(r->target, &et);
    if (ec.ok && ec.margin < r->minEdgeC) r->minEdgeC = ec.margin;
    if (et.ok && et.margin < r->minEdgeT) r->minEdgeT = et.margin;
    if (ec.ok && ec.margin < EDGE_WARN && !r->warnedC) {
        r->warnedC = 1; g_edgeWarnC++;
        tlog_key("TELE #%u the caster's %s - near the fast area's edge, state %u tick %u",
                 r->seq, edge_str(&ec, a), r->state, r->ticks);
    }
    if (et.ok && et.margin < EDGE_WARN && !r->warnedT) {
        r->warnedT = 1; g_edgeWarnT++;
        tlog_key("TELE #%u the marker's %s - near the fast area's edge, state %u tick %u"
                 " (it is disposable: the cell leaving destroys it)", r->seq,
                 edge_str(&et, a), r->state, r->ticks);
    }

    if (r->relOpen) {
        uintptr_t c = handle_ent(r->hCaster);
        if (c && readable4(c + OBJ_FLAGS) && !ref_none(rd32(c + OBJ_FLAGS))) {
            unsigned ms = (unsigned)(u9time_now() - r->relAt);
            r->relOpen = 0;
            g_relBack++;
            g_relBackSumMs += ms;
            if (ms > g_relBackMaxMs) g_relBackMaxMs = ms;
            tlog_key("TELE #%u the caster's slot is BACK after %u ms (state %u, tick %u);"
                     " her AI ticked %u times while it was out", r->seq, ms, r->state,
                     r->ticks, r->relAiTicks);
        }
    }
}

static void finish(TeleRec *r)
{
    const char *how;

    r->live = 0;
    g_finished++;
    if (!r->acted5)          how = "state 5 never acted";
    else if (!r->moveCalled) how = "state 5 acted but the move was not called";
    else if (r->movedBy > 0) how = "the caster moved";
    else if (r->movedBy == 0) how = "the move was called and the caster DID NOT MOVE";
    else                     how = "the move was called, position unknown";
    tlog("TELE #%u finished after %u ticks: path %s, %s", r->seq, r->ticks,
         r->path, how);
    if (r->relOpen) {
        r->relOpen = 0;
        g_relNever++;
        tlog_key("TELE #%u ended %u ms after its caster's slot was released, which never"
                 " came back; her AI ticked %u times meanwhile", r->seq,
                 (unsigned)(u9time_now() - r->relAt), r->relAiTicks);
    }
    tlog("TELE #%u     her AI ticked %u times during it; closest to the fast area's"
         " edge: caster %d, marker %d", r->seq, r->aiTicks,
         r->minEdgeC == 0x7FFFFFFF ? 0 : r->minEdgeC,
         r->minEdgeT == 0x7FFFFFFF ? 0 : r->minEdgeT);
}

typedef void (U9CALL *TickFn)(void *self);
static TickFn o_tick;

static void U9CALL h_tick(void *self)
{
    uintptr_t sp = (uintptr_t)self;
    TeleRec *r;
    uint32_t before, hold, hTarget, after, eff;

    if (!readable4(sp + SP_EFFECT)) { o_tick(self); return; }
    before  = rd32(sp + SP_STATE);
    hold    = rd32(sp + SP_HOLD);
    hTarget = rd32(sp + SP_TARGET);

    r = rec_find(sp);
    if (r && r->ticks && r->hTarget != hTarget) { r->live = 0; r = 0; }
    if (!r) {
        if (rd32(sp + SP_FLAGS) & 1u) { o_tick(self); return; }
        r = rec_new(sp);
    }
    r->hCaster = rd32(sp + SP_CASTER);
    r->hTarget = hTarget;
    r->caster  = handle_ent(r->hCaster);
    r->target  = handle_ent(r->hTarget);

    if (!r->ticks) first_sight(r, before, hold);
    else if (hold != r->hold) {
        tlog("TELE #%u tick %u: +0xE4 %u -> %u%s", r->seq, r->ticks, r->hold,
             hold, hold ? " - the tick now returns without doing anything" : "");
        r->hold = hold;
    }
    r->ticks++;
    if (before == 5 && !hold && !r->acted5 && !effect_playing(rd32(sp + SP_EFFECT)))
        state5_check(r);

    o_tick(self);

    after = readable4(sp + SP_STATE) ? rd32(sp + SP_STATE) : before;
    eff   = rd32(sp + SP_EFFECT);
    if (after != r->state) transition(r, after, eff);
    else r->inState++;
    r->caster = handle_ent(r->hCaster);
    watch_tick(r);
    if (rd32(sp + SP_FLAGS) & 1u) finish(r);
}

static void move_pre(uint32_t *regs)
{
    uintptr_t sp = regs[1];
    const float *loc = (const float *)(uintptr_t)regs[8];
    float facing;
    TeleRec *r = rec_find(sp);
    Ent c;
    char cs[256];
    unsigned seq = r ? r->seq : 0;

    g_moveCalls++;
    g_inMove = seq ? seq : 0xFFFFu;
    memcpy(&facing, &regs[9], sizeof facing);
    ent_read(r ? r->hCaster : rd32(sp + SP_CASTER), &c);
    if (r) {
        r->moveCalled = 1;
        r->want[0] = (int)loc[0];
        r->want[1] = (int)loc[1];
        r->want[2] = (int)loc[2];
        r->beforeOk = c.posOk;
        memcpy(r->before, c.pos, sizeof r->before);
    }
    tlog("TELE #%u MOVE: %08X->%08X to %d,%d,%d facing %d deg, then %08X, %u",
         seq, regs[6], readable4(regs[4] + 0xE4) ? rd32(regs[4] + 0xE4) : 0,
         (int)loc[0], (int)loc[1], (int)loc[2], (int)(facing * 57.29578f),
         regs[10], regs[11]);
    tlog("TELE #%u     caster before %s", seq, ent_str(&c, cs));
}

static void move_post(uint32_t *regs)
{
    uintptr_t sp = regs[1];
    uint32_t ret = regs[7];
    TeleRec *r = rec_find(sp);
    Ent c;
    char cs[256];
    int moved = -1;
    unsigned seq = r ? r->seq : 0;

    g_inMove = 0;
    ent_read(r ? r->hCaster : rd32(sp + SP_CASTER), &c);
    if (r && r->beforeOk && c.posOk)
        moved = (int)dist_xy(r->before, c.pos) + abs(c.pos[2] - r->before[2]);
    if ((ret & 0xFFu) == 0) g_moveZero++;
    if (moved > 0) g_moved++;
    else if (moved == 0) g_notMoved++;
    if (r) { r->moveRet = ret; r->movedBy = moved; }

    tlog("TELE #%u MOVE returned %08X (AL %u); caster now %s", seq, ret,
         ret & 0xFFu, ent_str(&c, cs));
    if (r && c.posOk)
        tlog("TELE #%u     %s; %u from where it was sent across the ground,"
             " %d above it", seq,
             moved < 0 ? "no position before the call"
             : moved ? "MOVED" : "DID NOT MOVE",
             dist_xy(r->want, c.pos), c.pos[2] - r->want[2]);
    if (moved > 0 && c.posOk && g_queryGroundOk) {
        int up = c.pos[2] - (int)(g_queryGround + (g_queryGround < 0 ? -0.5f : 0.5f));
        if (up > 4) g_landedHigh++;
        tlog("TELE #%u     landed %d above the ground the query found%s", seq, up,
             up > 4 ? "  <- IN THE AIR" : up < -6 ? "  <- in the ground" : "");
    }
    g_queryGroundOk = 0;
}

static unsigned g_why[6];
static const char *const WHY[6] = {
    "FALL - the ground is further below the point than the fall limit",
    "SLIDE - the slope there is steeper than the limit",
    "TOO HIGH - the ground is above the point by more than the tolerance",
    "NO FLOOR - the query found nothing to stand on",
    "ON A CREATURE",
    "passes every test"
};

static float fget(uintptr_t p) { float f; uint32_t v = rd32(p); memcpy(&f, &v, 4); return f; }

static char *hund(char *b, float v)
{
    int n;
    if (v > 1.0e7f || v < -1.0e7f) { lstrcpyA(b, v > 0 ? "+huge" : "-huge"); return b; }
    n = (int)(v * 100.0f + (v < 0 ? -0.5f : 0.5f));
    wsprintfA(b, "%s%d.%02d", n < 0 ? "-" : "", abs(n) / 100, abs(n) % 100);
    return b;
}

static void query_hook(uint32_t *regs)
{
    uintptr_t base = regs[3], mv = regs[1];
    float dz, gz, slope, tol, fall, slide, step, sc, ang;
    uint32_t flags;
    unsigned onNpc, seq = g_inMove, why;
    char a[24], b[24], c[24], d[24], e[24], f[24];

    if (!g_inMove || !readable4(base + 0x5C) || !readable4(mv + 0x638)) return;
    dz    = fget(base + 0x14);
    gz    = fget(base + 0x30);
    g_queryGround = gz;
    g_queryGroundOk = (gz != -1.0f && gz < 1.0e7f);
    slope = fget(base + 0x34);
    onNpc = rd32(base + 0x48) & 0xFFFFu;
    tol   = fget(base + 0x58);
    flags = rd32(mv + 0xD8);
    sc    = fget(mv + 0x638);
    fall  = fget(mv + 0x220) * sc;
    slide = fget(mv + 0x200);
    step  = fget(mv + 0x1F8) * sc;
    ang   = atanf(slope);

    if (!(flags & 0x100u) && gz < dz && (dz - gz) > fall + tol) why = 0;
    else if (!(flags & 0x100u) && ang > slide)                   why = 1;
    else if (gz > dz && gz > dz + tol)                           why = 2;
    else if (gz == -1.0f || gz == 3.402823466e38f)               why = 3;
    else if (onNpc)                                              why = 4;
    else                                                         why = 5;
    g_why[why]++;

    tlog("TELE #%u QUERY: ground %s, point %s, ground minus point %s;"
         " tolerance %s, fall limit %s, step height %s", seq, hund(a, gz),
         hund(b, dz), hund(c, gz - dz), hund(d, tol), hund(e, fall),
         hund(f, step));
    tlog("TELE #%u     slope %d deg (limit %d), on a creature %u, mover +0xD8"
         " %08X%s -> %s", seq, (int)(ang * 57.29578f),
         (int)(slide * 57.29578f), onNpc, flags,
         (flags & 0x100u) ? " (fall and slope not tested)" : "", WHY[why]);
}

static uintptr_t g_codeLo, g_codeHi;
static unsigned  g_baseRel, g_baseFree;

static void code_range(void)
{
    const uint8_t *base = (const uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *s;
    unsigned i;

    if (g_codeHi || !base) return;
    nt = (IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    s  = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        uintptr_t lo, hi;
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        lo = (uintptr_t)base + s->VirtualAddress;
        hi = lo + s->Misc.VirtualSize;
        if (!g_codeLo || lo < g_codeLo) g_codeLo = lo;
        if (hi > g_codeHi) g_codeHi = hi;
    }
}

static int after_call(uintptr_t a, uintptr_t *target)
{
    const uint8_t *p = (const uint8_t *)a;
    uint8_t m;

    *target = 0;
    if (a < g_codeLo + 7 || a > g_codeHi) return 0;
    if (p[-5] == 0xE8) {
        uintptr_t t = a + *(const int32_t *)(p - 4);
        if (t >= g_codeLo && t < g_codeHi) { *target = t; return 1; }
    }
    m = p[-5];
    if (p[-6] == 0xFF && (m == 0x15 || (m >= 0x90 && m <= 0x97 && m != 0x94))) return 1;
    if (p[-7] == 0xFF && p[-6] == 0x94) return 1;
    m = p[-2];
    if (p[-3] == 0xFF && m >= 0x50 && m <= 0x57 && m != 0x54) return 1;
    if (p[-4] == 0xFF && p[-3] == 0x54) return 1;
    m = p[-1];
    if (p[-2] == 0xFF && ((m >= 0xD0 && m <= 0xD7) ||
                          (m >= 0x10 && m <= 0x17 && m != 0x14 && m != 0x15)))
        return 1;
    return 0;
}

static void call_chain(uintptr_t sp, char *out, unsigned outLen)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    uintptr_t top = (uintptr_t)tib->StackBase, p, t;
    unsigned n = 0, len = 0;

    code_range();
    if (!g_codeHi || sp < (uintptr_t)tib->StackLimit || sp >= top) {
        lstrcpyA(out, "(the stack is not where it should be)");
        return;
    }
    if (top > sp + 0x1000u) top = sp + 0x1000u;
    out[0] = 0;
    for (p = sp; p + 4 <= top && n < 10 && len + 24 < outLen; p += 4) {
        uintptr_t v = *(const uint32_t *)p;
        if (!after_call(v, &t)) continue;
        len += t ? (unsigned)wsprintfA(out + len, "%s%08X>%08X", n ? " " : "",
                                       (unsigned)v, (unsigned)t)
                 : (unsigned)wsprintfA(out + len, "%s%08X", n ? " " : "",
                                       (unsigned)v);
        n++;
    }
    if (!n) lstrcpyA(out, "none found");
}

static void obj_where(uintptr_t obj, char *out)
{
    uintptr_t mgr, cam, cells, tbl, act;
    unsigned cx, cy;
    Ent e, pl;
    int n;

    memset(&e, 0, sizeof e);
    ent_fill(obj, &e);
    if (!e.posOk) { lstrcpyA(out, "its position is not readable (contained?)"); return; }
    cx = (unsigned)e.pos[0] >> 10;
    cy = (unsigned)e.pos[1] >> 10;
    n = wsprintfA(out, "it stands at %d,%d,%d, cell %u,%u", e.pos[0], e.pos[1],
                  e.pos[2], cx, cy);
    mgr = readable4(FA_MGR) ? rd32(FA_MGR) : 0;
    if (mgr && readable4(mgr + 0x60) && readable4(mgr + 0x50)) {
        cells = rd32(mgr + 0x44);
        if (cells && e.pos[0] >= 0 && e.pos[1] >= 0 &&
            cx < rd32(mgr + 0x4C) && cy < rd32(mgr + 0x50)) {
            uintptr_t cell = cells + (rd32(mgr + 0x4C) * cy + cx) * 0x28u;
            if (readable4(cell + 0x1C)) {
                uint32_t f = rd32(cell + 0x1C);
                n += wsprintfA(out + n, " (flags %X: %s the fast area)", f,
                               (f & 2u) ? "in" : "NOT in");
            }
        }
        cam = rd32(mgr + 0x60);
        if (cam && readable4(cam + 0xE4)) {
            float fx = fget(cam + 0xE0), fy = fget(cam + 0xE4);
            double dx = fx - e.pos[0], dy = fy - e.pos[1];
            n += wsprintfA(out + n, "; the fast area's centre %d,%d is %u away",
                           (int)fx, (int)fy, (unsigned)(sqrt(dx * dx + dy * dy) + 0.5));
        }
    }
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    act = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    if (act && readable4(act + ACT_SELF)) {
        ent_read(rd32(act + ACT_SELF), &pl);
        if (pl.posOk)
            wsprintfA(out + n, "; the player is %u away", dist_xy(e.pos, pl.pos));
    }
}

static void obj_detail(unsigned seq, uintptr_t obj, uint32_t *regs, int key)
{
    char chain[320], where[320];

    call_chain((uintptr_t)(regs + 9), chain, sizeof chain);
    obj_where(obj, where);
    (key ? tlog_key : tlog)("TELE #%u     callers above it: %s", seq, chain);
    (key ? tlog_key : tlog)("TELE #%u     %s", seq, where);
}

static TeleRec *rec_holding(uintptr_t obj, const char **which)
{
    unsigned i;
    for (i = 0; i < TELE_MAX; i++) {
        TeleRec *r = &g_rec[i];
        if (!r->live) continue;
        if (obj == r->caster) { *which = "caster"; return r; }
        if (obj == r->target) { *which = "target"; return r; }
    }
    return 0;
}

static unsigned obj_id(uintptr_t obj)
{
    return readable4(obj + OBJ_ZID) ? rd32(obj + OBJ_ZID) >> 16 : 0xFFFFu;
}

static void rel_hook(uint32_t *regs)
{
    uintptr_t obj = regs[9], from = regs[8];
    const char *which = "";
    TeleRec *r = rec_holding(obj, &which);
    uint32_t flags;
    int key;

    if (!r) {
        if (obj_id(obj) != MARKER_ID) return;
        if (++g_markerRel <= 20)
            tlog("TELE marker %08X loses its fast ref (flags %08X), from %08X,"
                 " held by no watched spell", (unsigned)obj,
                 rd32(obj + OBJ_FLAGS), (unsigned)from);
        return;
    }
    flags = rd32(obj + OBJ_FLAGS);
    r->releases++;
    if (which[0] == 'c') g_relCaster++; else g_relTarget++;
    key = !g_inMove && (which[0] == 'c' || r->state != 7);
    (key ? tlog_key : tlog)
        ("TELE #%u RELEASE: its %s %08X loses fast ref %u (flags %08X), from"
         " %08X, in state %u at tick %u", r->seq, which, (unsigned)obj,
         flags >> 21, flags, (unsigned)from, r->state, r->ticks);
    if (key || (!g_inMove && g_baseRel++ < 2)) obj_detail(r->seq, obj, regs, key);
    if (which[0] == 'c' && !g_inMove && !r->relOpen) {
        r->relOpen = 1;
        r->relAt = u9time_now();
        r->relAiTicks = 0;
    }
}

static void free_hook(uint32_t *regs)
{
    uintptr_t obj = regs[9], from = regs[8];
    const char *which = "";
    TeleRec *r = rec_holding(obj, &which);
    int key;

    if (!r) {
        if (obj_id(obj) != MARKER_ID) return;
        if (++g_markerFree <= 20)
            tlog("TELE marker %08X freed, from %08X, held by no watched spell",
                 (unsigned)obj, (unsigned)from);
        return;
    }
    r->frees++;
    if (which[0] == 'c') g_freeCaster++; else g_freeTarget++;
    key = !g_inMove && (which[0] == 'c' || r->state != 7);
    (key ? tlog_key : tlog)
        ("TELE #%u FREE: its %s record %08X, from %08X, in state %u at tick %u",
         r->seq, which, (unsigned)obj, (unsigned)from, r->state, r->ticks);
    if (key || (!g_inMove && g_baseFree++ < 2)) obj_detail(r->seq, obj, regs, key);
    if (which[0] == 'c') r->caster = 0; else r->target = 0;
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

static int stub_install(const U9PatchSpec *spec, U9Symbol resumeSym,
                        void (*pre)(uint32_t *), void (*post)(uint32_t *))
{
    uintptr_t site = u9sym(spec->target), resume = u9sym(resumeSym);
    uint8_t *st, *p, jmp[8];
    unsigned i;

    if (!site || !resume || spec->length < 5 || spec->length > sizeof jmp)
        return 0;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
    if (!st) return 0;
    p = st;
    if (pre) p = emit_observe(p, pre);
    for (i = 0; i < spec->expectedLen; i++) *p++ = spec->expected[i];
    if (post) p = emit_observe(p, post);
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    for (i = 5; i < spec->length; i++) jmp[i] = 0x90;
    if (!u9patch_install_bytes(spec, jmp, spec->length)) {
        VirtualFree(st, 0, MEM_RELEASE);
        return 0;
    }
    return 1;
}

typedef uint32_t (U9CALL *CastFn)(void *self, void *link);
static CastFn   o_castChecked, o_castPlain;
static unsigned g_castLines, g_castSeen, g_castStarted, g_castByCat[6];
static struct { uintptr_t actor; DWORD at; } g_castLast[8];
static unsigned g_castLastNext;

static DWORD cast_since(uintptr_t actor, DWORD now)
{
    unsigned i;
    DWORD d;

    for (i = 0; i < 8; i++)
        if (g_castLast[i].actor == actor) {
            d = now - g_castLast[i].at;
            g_castLast[i].at = now;
            return d;
        }
    g_castLast[g_castLastNext].actor = actor;
    g_castLast[g_castLastNext].at = now;
    g_castLastNext = (g_castLastNext + 1) % 8;
    return 0xFFFFFFFFu;
}

static uint32_t cast_common(CastFn orig, int checked, void *self, void *link)
{
    uintptr_t act = (uintptr_t)self, lk = (uintptr_t)link, sd = 0;
    uintptr_t tbl, enemyAct, npc;
    unsigned num = 0, cat = 99, kind = 0, radius = 0, flat = 0, started;
    unsigned mana = 0, manaMax = 0, cost = 0, manaOk = 0;
    int delay = 0;
    int enemy = -1, up = 0, have = 0;
    Ent me, en;
    DWORD now = u9time_now(), since;
    uint32_t r;
    char when[32];

    memset(&me, 0, sizeof me);
    memset(&en, 0, sizeof en);
    if (lk && readable4(lk + 8)) sd = rd32(lk + 8);
    if (sd && readable4(sd + 8)) {
        num  = rd32(sd);
        cat  = rd32(sd + 4);
        kind = rd32(sd + 8);
    }
    if (act && readable4(act + ACT_ENEMY) && readable4(act + ACT_SELF)) {
        ent_read(rd32(act + ACT_SELF), &me);
        enemy = (int32_t)rd32(act + ACT_ENEMY);
        npc = rd32(act + ACT_NPCDATA);
        if (npc && readable4(npc + 0x54)) radius = rd32(npc + 0x54) & 0xFFFFu;
        if (npc && readable4(npc + 0x3C)) {
            mana    = (rd32(npc + 0x38) >> 16) & 0xFFFFu;
            manaMax = (rd32(npc + 0x3C) >> 16) & 0xFFFFu;
            manaOk  = 1;
        }
        if (sd && readable4(sd + 0x18)) cost = rd32(sd + 0x18) & 0xFFu;
        if (readable4(act + 0x2B8)) delay = (int32_t)rd32(act + 0x2B8);
        tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
        if (tbl && enemy >= 0 && (unsigned)enemy < ACTOR_COUNT &&
            readable4(tbl + (unsigned)enemy * 4u)) {
            enemyAct = rd32(tbl + (unsigned)enemy * 4u);
            if (enemyAct && readable4(enemyAct + ACT_SELF))
                ent_read(rd32(enemyAct + ACT_SELF), &en);
        }
        if (me.posOk && en.posOk) {
            flat = dist_xy(me.pos, en.pos);
            up = en.pos[2] - me.pos[2];
            have = 1;
        }
    }

    r = orig(self, link);

    started = act && readable4(act + 0x2B4) && rd32(act + 0x2B4) != 0;
    g_castSeen++;
    if (started) g_castStarted++;
    g_castByCat[cat < 5 ? cat : 5]++;
    since = cast_since(act, now);
    if (since == 0xFFFFFFFFu) lstrcpyA(when, "her first cast seen");
    else wsprintfA(when, "%u.%u s since her last", (unsigned)(since / 1000),
                   (unsigned)(since % 1000) / 100);

    if (g_castLines < 400) {
        g_castLines++;
        if (have)
            u9log_line("TELE CAST: id %u spell %u, category %u%s, kind %02X - %s;"
                       " enemy %d is %u away across the ground (%u%% of her notice"
                       " radius %u), %d above her; %s; mana %u of %u, cost %u,"
                       " +0x2B8 %d%s", me.id, num, cat,
                       cat == 3 ? " (the blink)" : cat == 0 ? " (offensive?)" : "",
                       kind, started ? "started" : "NO SPELL STARTED", enemy, flat,
                       radius ? flat * 100u / radius : 0u, radius, up, when,
                       mana, manaMax, cost, delay,
                       !manaOk ? " (mana unreadable)" : checked ? "" : " [sibling class]");
        else
            u9log_line("TELE CAST: actor %08X spell %u, category %u, kind %02X -"
                       " %s; enemy %d, positions unreadable; %s%s",
                       (unsigned)act, num, cat, kind,
                       started ? "started" : "NO SPELL STARTED", enemy, when,
                       checked ? "" : " [sibling class]");
    }
    return r;
}

static uint32_t U9CALL h_castChecked(void *self, void *link)
{
    return cast_common(o_castChecked, 1, self, link);
}

static uint32_t U9CALL h_castPlain(void *self, void *link)
{
    return cast_common(o_castPlain, 0, self, link);
}

static volatile uint32_t g_precastOk, g_precastDropped;

static void precast_hook(uint32_t *regs)
{
    uintptr_t actor = regs[1], base = regs[3];
    uint32_t ok = regs[7];
    float sp[3];
    Ent me;
    char ms[256];

    if (ok) g_precastOk++; else g_precastDropped++;
    if (!readable4(base + 0x24)) return;
    memcpy(sp, (const void *)(base + 0x1C), 12);
    memset(&me, 0, sizeof me);
    if (readable4(actor + ACT_SELF)) ent_read(rd32(actor + ACT_SELF), &me);
    tlog("TELE precast: actor %08X, the game's nearby-spot test %s, spot %d,%d,%d;"
         " caster %s", (unsigned)actor,
         ok ? "passed - the cast goes ahead"
            : "FAILED - the cast is dropped and no spell starts",
         (int)sp[0], (int)sp[1], (int)sp[2], ent_str(&me, ms));
}

static int precast_install(void)
{
    static const uint8_t exp[8] = { 0x85, 0xC0, 0x0F, 0x84, 0x8E, 0x01, 0x00, 0x00 };
    static const U9PatchSpec spec = {
        "TELE_PRECAST", U9PATCH_STUB, U9SYM_BLINK_PRECAST_TEST,
        8, exp, 8, U9PF_EXCLUSIVE
    };
    uintptr_t site   = u9sym(U9SYM_BLINK_PRECAST_TEST);
    uintptr_t resume = u9sym(U9SYM_BLINK_PRECAST_RESUME);
    uintptr_t bail   = u9sym(U9SYM_BLINK_PRECAST_BAIL);
    uint8_t *st, *p, jmp[8];

    if (!site || !resume || !bail) return 0;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                 PAGE_EXECUTE_READWRITE);
    if (!st) return 0;
    p = emit_observe(st, precast_hook);
    *p++ = 0x85; *p++ = 0xC0;
    *p++ = 0x0F; *p++ = 0x84;
    *(int32_t *)p = (int32_t)(bail - ((uintptr_t)p + 4));
    p += 4;
    *p++ = 0xE9;
    *(int32_t *)p = (int32_t)(resume - ((uintptr_t)p + 4));
    p += 4;
    FlushInstructionCache(GetCurrentProcess(), st, (SIZE_T)(p - st));

    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    jmp[5] = 0x90; jmp[6] = 0x90; jmp[7] = 0x90;
    if (!u9patch_install_bytes(&spec, jmp, 8)) {
        VirtualFree(st, 0, MEM_RELEASE);
        return 0;
    }
    return 1;
}

void u9tele_actor_tick(void *actor)
{
    uintptr_t a = (uintptr_t)actor;
    uint32_t self;
    unsigned i;

    if (!o_tick || !g_seq || !readable4(a + ACT_SELF)) return;
    self = rd32(a + ACT_SELF);
    if (!self) return;
    for (i = 0; i < TELE_MAX; i++) {
        TeleRec *r = &g_rec[i];
        if (!r->live || r->hCaster != self) continue;
        r->aiTicks++;
        if (r->relOpen) r->relAiTicks++;
    }
}

void u9tele_install(int everything)
{
    static const uint8_t expTick[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t expMove[6] = { 0xFF, 0x93, 0xE4, 0x00, 0x00, 0x00 };
    static const uint8_t expRel[6]  = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10 };
    static const uint8_t expFree[5] = { 0x8B, 0x54, 0x24, 0x04, 0x56 };
    static const U9PatchSpec specTick = {
        "TELE_TICK", U9PATCH_INLINE, U9SYM_TELEPORT_TICK,
        6, expTick, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specMove = {
        "TELE_MOVE", U9PATCH_STUB, U9SYM_TELEPORT_MOVE_CALL,
        6, expMove, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specRel = {
        "TELE_FASTREF_RELEASE", U9PATCH_STUB, U9SYM_FASTREF_RELEASE,
        6, expRel, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specFree = {
        "TELE_PAGE_FREECELL", U9PATCH_STUB, U9SYM_PAGE_FREECELL,
        5, expFree, 5, U9PF_EXCLUSIVE
    };
    int okRel, okFree, okPre;

    if (everything) {
        g_lineCap = 6000u;
        g_keyCap  = 2000u;
        tlog("teleport watch: diag_teleport - long-session caps (%u lines, %u key"
             " lines), fast-area sampling on", g_lineCap, g_keyCap);
    }
    o_tick = (TickFn)u9patch_install(&specTick, (void *)h_tick);
    g_okMove = stub_install(&specMove, U9SYM_TELEPORT_MOVE_RESUME, move_pre, move_post);
    okRel  = stub_install(&specRel,  U9SYM_FASTREF_RELEASE_RESUME, rel_hook, 0);
    okFree = stub_install(&specFree, U9SYM_PAGE_FREECELL_RESUME, free_hook, 0);
    okPre  = precast_install();
    {
        static const uint8_t expA[7] = { 0x6A, 0xFF, 0x68, 0x98, 0xCF, 0x72, 0x00 };
        static const uint8_t expB[7] = { 0x6A, 0xFF, 0x68, 0xC0, 0xB8, 0x72, 0x00 };
        static const U9PatchSpec specA = {
            "TELE_CAST_CHECKED", U9PATCH_INLINE, U9SYM_CAST_CHECKED,
            7, expA, 7, U9PF_EXCLUSIVE
        };
        static const U9PatchSpec specB = {
            "TELE_CAST_PLAIN", U9PATCH_INLINE, U9SYM_CAST_PLAIN,
            7, expB, 7, U9PF_EXCLUSIVE
        };
        o_castChecked = (CastFn)u9patch_install(&specA, (void *)h_castChecked);
        o_castPlain   = (CastFn)u9patch_install(&specB, (void *)h_castPlain);
        tlog("teleport watch: every cast logged (TELE CAST) - tested class %s,"
             " sibling class %s", o_castChecked ? "hooked" : "REFUSED",
             o_castPlain ? "hooked" : "REFUSED");
    }
    tlog("teleport watch: tick %s, move %s, pre-cast test %s, fast-ref release %s,"
         " record free %s; the ground query is observed through the I-2 fix's"
         " site - every line starts TELE", o_tick ? "hooked" : "REFUSED",
         g_okMove ? "hooked" : "REFUSED", okPre ? "hooked" : "REFUSED",
         okRel ? "hooked" : "REFUSED", okFree ? "hooked" : "REFUSED");
}

U9TeleQueryFn u9tele_query_observer(void)
{
    return g_okMove ? query_hook : 0;
}

void u9tele_report(void)
{
    unsigned i, n = 0;

    if (!o_tick && !g_seq) return;
    u9log_line("");
    u9log_line("teleport spells: %u seen, %u finished; state 5 acted %u times"
               " (caster resource null %u, target gone %u)", g_seq, g_finished,
               g_acted5, g_null5Res, g_null5Target);
    u9log_line("  move called %u times: returned AL=0 %u, caster moved %u,"
               " did not move %u", g_moveCalls, g_moveZero, g_moved, g_notMoved);
    u9log_line("  moves that left the caster more than 4 above the ground: %u",
               g_landedHigh);
    u9log_line("  casts: %u, a spell started for %u; by category 0:%u 1:%u 2:%u"
               " 3 (blink):%u 4:%u other:%u", g_castSeen, g_castStarted,
               g_castByCat[0], g_castByCat[1], g_castByCat[2], g_castByCat[3],
               g_castByCat[4], g_castByCat[5]);
    u9log_line("  pre-cast test (tested caster class): passed %u, dropped the cast"
               " %u", (unsigned)g_precastOk, (unsigned)g_precastDropped);
    u9log_line("  ground query said: fall %u, slide %u, too high %u, no floor %u,"
               " on a creature %u, passes %u", g_why[0], g_why[1], g_why[2],
               g_why[3], g_why[4], g_why[5]);
    u9log_line("  fast refs released: caster %u, target %u; records freed:"
               " caster %u, target %u; unwatched markers released %u, freed %u",
               g_relCaster, g_relTarget, g_freeCaster, g_freeTarget,
               g_markerRel, g_markerFree);
    u9log_line("  caster slot released outside a move: came back %u times (longest %u ms,"
               " average %u ms), never came back %u times", g_relBack, g_relBackMaxMs,
               g_relBack ? g_relBackSumMs / g_relBack : 0, g_relNever);
    u9log_line("  near the fast area's edge (margin < %d): casters %u, markers %u;"
               " camera object changes with a teleport in flight %u", EDGE_WARN,
               g_edgeWarnC, g_edgeWarnT, g_camChanges);
    if (g_overwritten)
        u9log_line("  %u live records were overwritten - spells ended without"
                   " their finish being seen", g_overwritten);
    for (i = 0; i < TELE_MAX; i++) {
        const TeleRec *r = &g_rec[i];
        if (!r->seq) continue;
        n++;
        u9log_line("  #%-4u path %-8s %5u ticks  %s  move %s ret %08X moved %d"
                   "  releases %u frees %u", r->seq, r->path, r->ticks,
                   r->live ? "LIVE    " : "finished",
                   r->moveCalled ? "yes" : "no ", r->moveRet, r->movedBy,
                   r->releases, r->frees);
    }
    if (n)
        u9log_line("  states are hex. 2357 is the whole walk; a path that stops"
                   " short of 7 ended through a teardown that leaves the state"
                   " as it was");
}
