#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../common/u9time.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "portalfix.h"

void u9log_line(const char *fmt, ...);

#define SPACES_MGR      0x00AD1340u
#define SM_SPACES       0x08u
#define SM_FLAGS        0x1000Cu
#define SM_CAM_SPACE    0x10020u
#define SPACE_SLOTS     0x2000u
#define GAME_OBJ        0x00850228u
#define GAME_MODE       0x00850220u
#define OBJTYPES_ADDR      0x00AE7C7Cu
#define OBJFLAG_DOOR       0x10u

#define SP_MAP          0x20u
#define SP_PLANES       0x24u
#define SP_NUMBER       0x34u
#define SP_FLAGS        0x36u
#define PL_NORMAL       0x10u
#define PL_PORTALS      0x20u
#define PO_CORNERS      0x04u
#define PO_TARGET       0x34u
#define PO_FLAGS        0x40u

#define SCENE_SPACE     0x1Cu
#define SCENE_FLAGS     0x170u
#define OBJ_FLAGS       0x14u
#define OBJ_TYPE        0x0Au
#define DOOR_OPEN_BIT   0x20000u

#define OBJECT_GET_PROPERTY   0x004BF010u
#define OBJECT_WORLD_POSITION 0x004BE730u

#define MAX_PORTALS     1024
#define MAX_LINES       8000u
#define MAX_ASSIGN      2000u
#define MAX_DOOR_NOTES  1000u
#define MAX_FIX_LINES   200u
#define MAX_DOORS       1024
#define MAP_ITEMS_CUR   0x008515E8u
#define ITEMS_BASE      0x34u
#define ARENA_SIZE      0x00C00000u
#define OBJ_PROPS       0x1Cu

typedef void (U9CALL *AssignFn)(void *spaces, uintptr_t scene, uintptr_t obj);
typedef void (U9CALL *ResetFn)(void *spaces);
typedef uint32_t (U9CALL *ObjPropFn)(uintptr_t obj, int property);
typedef void *(U9CALL *WorldPosFn)(uintptr_t obj, void *out);

typedef struct {
    uintptr_t p;
    uint32_t  flags;
    int       space, plane, idx, target;
    float     c[3], n[3];
    float     w, h;
} Portal;

static int      g_on;
static int      g_fix;
static DWORD    g_every = 1000, g_next, g_t0;
static AssignFn o_assign;
static ResetFn  o_reset;
static uintptr_t g_doors[MAX_DOORS];
static int      g_nDoors;
static uintptr_t g_doorsBase;
static unsigned g_doorsFull, g_resets, g_reblocked, g_fixLines;
static int      g_lastDisagree = -1;
static unsigned g_checks, g_checkMax;
static unsigned g_readChecks, g_readMismatch;

typedef struct {
    uintptr_t portal;
    int       pos[3];
    unsigned  gen;
    int       linked;
    uint32_t  link;
    int       map;
} Claim;
#define MAX_PORTAL_CLAIMS 1024
static Claim    g_pc[MAX_PORTAL_CLAIMS];
static int      g_nPc;
static unsigned g_gen = 1;
static unsigned g_staleSeen, g_staleCleared, g_pcFull, g_pcDead, g_pcGone;
static Portal   g_po[MAX_PORTALS];
static int      g_count, g_map = -1, g_camSpace = -2, g_overflow;
static unsigned g_lines, g_assignLines[4], g_doorNotes;
static unsigned g_assigns, g_doorAssigns, g_doorLinked, g_doorCleared, g_flagChanges,
                g_camChanges, g_sweeps, g_maps;

static int log_ok(void) { return g_lines < MAX_LINES; }

static uintptr_t spaces_mgr(void)
{
    return readable4(SPACES_MGR) ? rd32(SPACES_MGR) : 0;
}

static int current_map(void)
{
    uintptr_t g = readable4(GAME_OBJ) ? rd32(GAME_OBJ) : 0, m;
    if (!g || !readable4(g + 0xCC)) return -1;
    m = rd32(g + 0xCC);
    return (m && readable4(m + 0x20)) ? (int)rd32(m + 0x20) : -1;
}

static uintptr_t space_at(uintptr_t mgr, int s)
{
    uintptr_t a = mgr + SM_SPACES + (uintptr_t)s * 4u;
    if (s < 0 || s >= (int)SPACE_SLOTS || !readable4(a)) return 0;
    a = rd32(a);
    return (a && readable4(a + SP_FLAGS)) ? a : 0;
}

static const char *space_name(uintptr_t sp, char *buf, size_t n)
{
    size_t i;
    if (!sp) { snprintf(buf, n, "-"); return buf; }
    for (i = 0; i + 1 < n && i < 32; i++) {
        char ch = *(const char *)(sp + i);
        if (!ch) break;
        buf[i] = (ch >= 32 && ch < 127) ? ch : '?';
    }
    buf[i] = 0;
    return buf;
}

static int is_door(uintptr_t obj)
{
    uintptr_t t = readable4(OBJTYPES_ADDR) ? rd32(OBJTYPES_ADDR) : 0;
    unsigned type;
    if (!t || !obj || !readable4(obj + OBJ_FLAGS)) return 0;
    type = *(const uint16_t *)(obj + OBJ_TYPE);
    return readable4(t + type * 0x10u + 8) && (*(const uint8_t *)(t + type * 0x10u + 8) & OBJFLAG_DOOR);
}

static void portal_fill(Portal *q, uintptr_t po, uintptr_t pl, int space, int plane, int idx)
{
    const float *c = (const float *)(po + PO_CORNERS), *nn = (const float *)(pl + PL_NORMAL);
    float x0 = c[0], x1 = c[0], y0 = c[1], y1 = c[1], z0 = c[2], z1 = c[2];
    int k;
    q->p = po; q->space = space; q->plane = plane; q->idx = idx;
    q->target = (int)rd32(po + PO_TARGET);
    q->flags = rd32(po + PO_FLAGS);
    q->c[0] = q->c[1] = q->c[2] = 0.0f;
    for (k = 0; k < 4; k++) {
        float x = c[3 * k], y = c[3 * k + 1], z = c[3 * k + 2];
        q->c[0] += x / 4; q->c[1] += y / 4; q->c[2] += z / 4;
        x0 = fminf(x0, x); x1 = fmaxf(x1, x);
        y0 = fminf(y0, y); y1 = fmaxf(y1, y);
        z0 = fminf(z0, z); z1 = fmaxf(z1, z);
    }
    q->w = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    q->h = z1 - z0;
    q->n[0] = nn[0]; q->n[1] = nn[1]; q->n[2] = nn[2];
}

static void table_build(int map)
{
    uintptr_t mgr = spaces_mgr();
    int s, inner = 0, outside = 0, blocked = 0, disabled = 0, spaces = 0;
    g_count = 0; g_overflow = 0; g_map = map;
    if (!mgr) return;
    for (s = 0; s < (int)SPACE_SLOTS; s++) {
        uintptr_t sp = space_at(mgr, s), pl;
        int plane = 0, guardP = 0;
        if (!sp || (int)rd32(sp + SP_MAP) != map) continue;
        spaces++;
        for (pl = rd32(sp + SP_PLANES); pl && readable4(pl + PL_PORTALS) && guardP++ < 256;
             pl = rd32(pl), plane++) {
            uintptr_t po;
            int idx = 0, guardQ = 0;
            for (po = rd32(pl + PL_PORTALS); po && readable4(po + PO_FLAGS) && guardQ++ < 64;
                 po = rd32(po), idx++) {
                if (g_count >= MAX_PORTALS) { g_overflow++; continue; }
                portal_fill(&g_po[g_count], po, pl, s, plane, idx);
                if (g_po[g_count].target < 0) outside++; else inner++;
                if (g_po[g_count].flags & 1u) blocked++;
                if (g_po[g_count].flags & 2u) disabled++;
                g_count++;
            }
        }
    }
    g_maps++;
    if (log_ok()) {
        g_lines++;
        u9log_line("PORTAL %7lu ms map %d: %d spaces, %d portals (%d inner, %d outside),"
                   " %d blocked, %d disabled%s; game mode %u, manager flags %08X",
                   (unsigned long)(u9time_now() - g_t0), map, spaces, g_count, inner,
                   outside, blocked, disabled, g_overflow ? " - TABLE FULL, some not watched" : "",
                   readable4(GAME_MODE) ? (unsigned)rd32(GAME_MODE) : 0u,
                   (unsigned)rd32(mgr + SM_FLAGS));
    }
}

static void ensure_table(void)
{
    int map = current_map();
    if (map >= 0 && map != g_map) table_build(map);
}

static void target_text(int target, char *buf, size_t n)
{
    char nm[40];
    if (target < 0) { snprintf(buf, n, "outside"); return; }
    snprintf(buf, n, "inner->%d '%s'", target, space_name(space_at(spaces_mgr(), target), nm, sizeof nm));
}

static const char *flag_text(uint32_t f)
{
    static char buf[48];
    snprintf(buf, sizeof buf, "%08X%s%s", (unsigned)f, (f & 1u) ? " BLOCKED" : "",
             (f & 2u) ? " DISABLED" : "");
    return buf;
}

static void check_links(void);
static void check_orphans(void);

static void sweep(void)
{
    uintptr_t mgr = spaces_mgr();
    int i, cam;
    g_sweeps++;
    ensure_table();
    for (i = 0; i < g_count; i++) {
        Portal *q = &g_po[i];
        uint32_t f;
        if (!readable4(q->p + PO_FLAGS)) continue;
        f = rd32(q->p + PO_FLAGS);
        if (f == q->flags) continue;
        g_flagChanges++;
        if (log_ok()) {
            char nm[40], tt[64], was[48];
            g_lines++;
            target_text(q->target, tt, sizeof tt);
            snprintf(was, sizeof was, "%s", flag_text(q->flags));
            u9log_line("PORTAL %7lu ms flag space %d '%s' plane %d portal %d (%s) at"
                       " %.0f,%.0f,%.0f: %s -> %s", (unsigned long)(u9time_now() - g_t0),
                       q->space, space_name(space_at(mgr, q->space), nm, sizeof nm), q->plane,
                       q->idx, tt, q->c[0], q->c[1], q->c[2], was, flag_text(f));
        }
        q->flags = f;
    }
    check_links();
    cam = (mgr && readable4(mgr + SM_CAM_SPACE)) ? (int)rd32(mgr + SM_CAM_SPACE) : -2;
    if (cam != g_camSpace) {
        uintptr_t sp = cam >= 0 ? space_at(mgr, cam) : 0;
        g_camChanges++;
        if (log_ok()) {
            char nm[40];
            g_lines++;
            u9log_line("PORTAL %7lu ms camera space %d -> %d '%s'%s",
                       (unsigned long)(u9time_now() - g_t0), g_camSpace, cam,
                       space_name(sp, nm, sizeof nm),
                       (sp && (*(const uint16_t *)(sp + SP_FLAGS) & 1u)) ? " hide_outside" : "");
        }
        g_camSpace = cam;
    }
}

void u9portal_tick(void)
{
    DWORD now;
    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;
    sweep();
}

static uintptr_t map_items_base(void)
{
    uintptr_t m = readable4(MAP_ITEMS_CUR) ? rd32(MAP_ITEMS_CUR) : 0;
    return (m && readable4(m + ITEMS_BASE)) ? rd32(m + ITEMS_BASE) : 0;
}

static void pool_sync(void)
{
    uintptr_t base = map_items_base();
    if (base != g_doorsBase) { g_nDoors = 0; g_doorsBase = base; g_gen++; }
}

static void door_remember(uintptr_t obj)
{
    int i;
    pool_sync();
    for (i = 0; i < g_nDoors; i++) if (g_doors[i] == obj) return;
    if (g_nDoors < MAX_DOORS) g_doors[g_nDoors++] = obj;
    else g_doorsFull++;
}

static int door_valid(uintptr_t obj, uintptr_t base)
{
    return base && obj >= base && obj - base < ARENA_SIZE && readable4(obj + OBJ_PROPS) &&
           is_door(obj);
}

static uint32_t prop_safe(uintptr_t obj, uintptr_t base, unsigned id)
{
    uintptr_t p;
    unsigned guard = 0;
    if (!readable4(obj + OBJ_PROPS) || (rd32(obj + OBJ_FLAGS) & 1u)) return 0;
    p = rd32(obj + OBJ_PROPS) ? base + rd32(obj + OBJ_PROPS) : 0;
    while (p && guard++ < 256 && readable4(p) && readable4(p + 12)) {
        uint32_t head = rd32(p);
        unsigned n = head & 0xFFu, c;
        if (n < 4) {
            uintptr_t next = 0;
            for (c = 0; c < n; c++) {
                unsigned cid = (head >> (8 * (c + 1))) & 0xFFu;
                uint32_t v = rd32(p + 4 + 4 * c);
                if (cid == id) return v;
                if (cid == 0xFFu) next = v ? base + v : 0;
            }
            p = next;
        } else {
            if (n == id) return 0;
            p = (head >> 8) ? base + (head >> 8) : 0;
        }
    }
    return 0;
}

static uint32_t obj_prop(uintptr_t obj, int id)
{
    return ((ObjPropFn)OBJECT_GET_PROPERTY)(obj, id);
}

static uintptr_t link_portal(uint32_t link)
{
    uintptr_t sp = space_at(spaces_mgr(), (int)(link >> 16)), pl, po;
    unsigned plane = (link >> 8) & 0xFFu, k;
    int idx = (int)(link & 0xFFu) - 1;
    if (!sp || idx < 0) return 0;
    pl = rd32(sp + SP_PLANES);
    for (k = 0; pl && k < plane; k++) pl = readable4(pl) ? rd32(pl) : 0;
    if (!pl || !readable4(pl + PL_PORTALS)) return 0;
    po = rd32(pl + PL_PORTALS);
    for (; po && idx > 0; idx--) po = readable4(po) ? rd32(po) : 0;
    return (po && readable4(po + PO_FLAGS)) ? po : 0;
}

static void link_text(uint32_t link, char *buf, size_t n)
{
    uintptr_t po;
    char tt[64];
    if (!link) { snprintf(buf, n, "none"); return; }
    po = link_portal(link);
    if (!po) {
        snprintf(buf, n, "%08X (space %u plane %u portal %d: not found)", (unsigned)link,
                 (unsigned)(link >> 16), (unsigned)((link >> 8) & 0xFFu), (int)(link & 0xFFu) - 1);
        return;
    }
    target_text((int)rd32(po + PO_TARGET), tt, sizeof tt);
    snprintf(buf, n, "space %u plane %u portal %d (%s) %s", (unsigned)(link >> 16),
             (unsigned)((link >> 8) & 0xFFu), (int)(link & 0xFFu) - 1, tt,
             flag_text(rd32(po + PO_FLAGS)));
}

static double door_yaw(uintptr_t obj)
{
    const int16_t *s = (const int16_t *)(obj + 0x0C);
    double z = s[2] / 32768.0, w = s[3] / 32768.0;
    if (s[3] == -32768 && !s[0] && !s[1] && !s[2]) w = 1.0;
    return 2.0 * atan2(z, w) * (180.0 / 3.14159265358979);
}

static void nearest_text(uintptr_t obj, const int pos[3], char *buf, size_t n)
{
    int i, best = -1;
    double bestD = 1e30;
    for (i = 0; i < g_count; i++) {
        double dx = pos[0] - g_po[i].c[0], dy = pos[1] - g_po[i].c[1], dz = pos[2] - g_po[i].c[2];
        double d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) { snprintf(buf, n, "no portal on this map"); return; }
    {
        Portal *q = &g_po[best];
        double dx = pos[0] - q->c[0], dy = pos[1] - q->c[1], dz = pos[2] - q->c[2];
        double off = dx * q->n[0] + dy * q->n[1] + dz * q->n[2];
        double lat = sqrt(fabs(dx * dx + dy * dy - (dx * q->n[0] + dy * q->n[1]) *
                                                    (dx * q->n[0] + dy * q->n[1])));
        double nyaw = atan2(q->n[1], q->n[0]) * (180.0 / 3.14159265358979);
        double rel = fmod(fabs(door_yaw(obj) - nyaw), 180.0);
        char nm[40], tt[64];
        target_text(q->target, tt, sizeof tt);
        snprintf(buf, n, "nearest space %d '%s' plane %d portal %d (%s) %s, %.0f away:"
                 " off-plane %.1f, along %.1f of half-width %.1f, height %.0f (dz %.0f),"
                 " door yaw %.1f vs normal %.1f (rel %.1f mod 180)",
                 q->space, space_name(space_at(spaces_mgr(), q->space), nm, sizeof nm), q->plane,
                 q->idx, tt, flag_text(rd32(q->p + PO_FLAGS)), sqrt(bestD), off, lat, q->w / 2,
                 q->h, dz, door_yaw(obj), nyaw, rel);
    }
}

static void door_pos(uintptr_t obj, int pos[3])
{
    struct { int32_t x, y; int16_t z, pad; } p = { 0, 0, 0, 0 };
    ((WorldPosFn)OBJECT_WORLD_POSITION)(obj, &p);
    pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
}

static const char *caller_name(uint32_t ret)
{
    if (ret >= 0x004CEE60u && ret < 0x004D0700u) return "WorldObj_Relocate";
    if (ret >= 0x004CD290u && ret < 0x004CD500u) return "FUN_004CD290";
    if (ret >= 0x004CBA30u && ret < 0x004CBB90u) return "Scene_AddObject";
    return "?";
}

static void walk_claims(void);
static int  portal_named(uintptr_t po);

static int claim_live(const Claim *c)
{
    return c->link && link_portal(c->link) == c->portal;
}

static Claim *claim_of(uintptr_t po)
{
    int i, keep = 0, old = 0;
    for (i = 0; i < g_nPc; i++) if (g_pc[i].portal == po) return &g_pc[i];
    if (g_nPc >= MAX_PORTAL_CLAIMS) {
        for (i = 0; i < g_nPc; i++) if (claim_live(&g_pc[i])) g_pc[keep++] = g_pc[i];
        g_pcDead += (unsigned)(g_nPc - keep);
        g_nPc = keep;
        if (g_nPc >= MAX_PORTAL_CLAIMS) {
            for (i = 1; i < g_nPc; i++) if (g_pc[i].gen < g_pc[old].gen) old = i;
            g_pc[old] = g_pc[--g_nPc];
            g_pcFull++;
        }
    }
    memset(&g_pc[g_nPc], 0, sizeof g_pc[0]);
    g_pc[g_nPc].portal = po;
    return &g_pc[g_nPc++];
}

static int same_pos(const int a[3], const int b[3])
{
    return abs(a[0] - b[0]) <= 2 && abs(a[1] - b[1]) <= 2 && abs(a[2] - b[2]) <= 2;
}

static void stale_door(uintptr_t obj, uint32_t a8, uint32_t a9)
{
    uintptr_t now[2];
    uint32_t link[2];
    int pos[3], i, k, map = current_map();
    pool_sync();
    door_pos(obj, pos);
    link[0] = a8; link[1] = a9;
    now[0] = a8 ? link_portal(a8) : 0;
    now[1] = a9 ? link_portal(a9) : 0;
    for (k = 0; k < 2; k++) {
        Claim *c;
        if (!now[k] || !(c = claim_of(now[k]))) continue;
        c->pos[0] = pos[0]; c->pos[1] = pos[1]; c->pos[2] = pos[2];
        c->gen = g_gen; c->linked = 1; c->link = link[k]; c->map = map;
    }
    for (i = 0; i < g_nPc; i++) {
        Claim *c = &g_pc[i];
        if (c->portal == now[0] || c->portal == now[1] || c->map != map || !same_pos(c->pos, pos))
            continue;
        c->gen = g_gen; c->linked = 0;
        if (!claim_live(c)) { g_pcGone++; continue; }
        if (!readable4(c->portal + PO_FLAGS) || !(rd32(c->portal + PO_FLAGS) & 1u)) continue;
        walk_claims();
        if (portal_named(c->portal)) continue;
        g_staleSeen++;
        if (g_fix) {
            *(volatile uint32_t *)(c->portal + PO_FLAGS) = rd32(c->portal + PO_FLAGS) & ~1u;
            g_staleCleared++;
        }
        if ((g_on && log_ok()) || (!g_on && g_fixLines < MAX_FIX_LINES)) {
            Portal *q = 0;
            char nm[40], tt[64];
            int j;
            if (g_on) g_lines++; else g_fixLines++;
            for (j = 0; j < g_count; j++) if (g_po[j].p == c->portal) q = &g_po[j];
            if (q) target_text(q->target, tt, sizeof tt); else snprintf(tt, sizeof tt, "?");
            u9log_line("PORTAL %7lu ms stale: door type %u at %d,%d,%d %s is no longer in"
                       " space %d '%s' plane %d portal %d (%s), still blocked - %s",
                       (unsigned long)(u9time_now() - g_t0),
                       (unsigned)*(const uint16_t *)(obj + OBJ_TYPE), pos[0], pos[1], pos[2],
                       (rd32(obj + OBJ_FLAGS) & DOOR_OPEN_BIT) ? "OPEN" : "shut",
                       q ? q->space : -1,
                       q ? space_name(space_at(spaces_mgr(), q->space), nm, sizeof nm) : "?",
                       q ? q->plane : -1, q ? q->idx : -1, tt,
                       g_fix ? "cleared (fix_door_portals)" : "left blocked (fix off)");
        }
    }
}

static void U9CALL h_assign(void *spaces, uintptr_t scene, uintptr_t obj)
{
    uint32_t ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uint32_t p8 = 0, p9 = 0, a8, a9;
    unsigned mode;
    int door;

    g_assigns++;
    door = is_door(obj);
    if (door) {
        p8 = obj_prop(obj, 8);
        p9 = obj_prop(obj, 9);
    }
    o_assign(spaces, scene, obj);
    if (!door) return;

    g_doorAssigns++;
    a8 = obj_prop(obj, 8);
    a9 = obj_prop(obj, 9);
    if (a8 || a9) { g_doorLinked++; door_remember(obj); }
    {
        uintptr_t base = map_items_base();
        uint32_t s8 = prop_safe(obj, base, 8), s9 = prop_safe(obj, base, 9);
        g_readChecks++;
        if (s8 != a8 || s9 != a9) {
            g_readMismatch++;
            if (g_readMismatch <= 5 && (g_on ? log_ok() : g_fixLines < MAX_FIX_LINES)) {
                if (g_on) g_lines++; else g_fixLines++;
                u9log_line("PORTAL property read MISMATCH on door type %u: engine p8 %08X p9 %08X,"
                           " in place %08X %08X - the reset fix would read the wrong link",
                           (unsigned)*(const uint16_t *)(obj + OBJ_TYPE), (unsigned)a8,
                           (unsigned)a9, (unsigned)s8, (unsigned)s9);
            }
        }
    }
    if ((p8 || p9) && !a8 && !a9) g_doorCleared++;
    stale_door(obj, a8, a9);
    mode = readable4(GAME_MODE) ? rd32(GAME_MODE) & 3u : 0u;
    if (g_assignLines[mode] < MAX_ASSIGN && log_ok()) {
        char b8[160], b9[160], c8[160], near_[400];
        int pos[3];
        unsigned flags = rd32(obj + OBJ_FLAGS);
        ensure_table();
        door_pos(obj, pos);
        link_text(p8, b8, sizeof b8);
        link_text(a8, c8, sizeof c8);
        link_text(a9, b9, sizeof b9);
        if (!a8 && !a9) nearest_text(obj, pos, near_, sizeof near_);
        else near_[0] = 0;
        g_assignLines[mode]++; g_lines++;
        u9log_line("PORTAL %7lu ms assign door type %u at %d,%d,%d %s yaw %.1f via %s ret %08X"
                   " mode %u | before p8 %s%s | after p8 %s, p9 %s | scene space %d flags %08X%s%s",
                   (unsigned long)(u9time_now() - g_t0), (unsigned)*(const uint16_t *)(obj + OBJ_TYPE),
                   pos[0], pos[1], pos[2], (flags & DOOR_OPEN_BIT) ? "OPEN" : "shut",
                   door_yaw(obj), caller_name(ret), (unsigned)ret,
                   mode, b8,
                   p9 ? " (and p9)" : "", c8, b9,
                   scene && readable4(scene + SCENE_SPACE) ? (int)rd32(scene + SCENE_SPACE) : -9,
                   scene && readable4(scene + SCENE_FLAGS) ? (unsigned)rd32(scene + SCENE_FLAGS) : 0u,
                   near_[0] ? " | " : "", near_);
    }
}

void u9portal_door_note(uintptr_t obj, unsigned handle, const char *when)
{
    char b8[160], b9[160];
    if (!g_on || !obj || g_doorNotes >= MAX_DOOR_NOTES || !log_ok()) return;
    g_doorNotes++; g_lines++;
    link_text(obj_prop(obj, 8), b8, sizeof b8);
    link_text(obj_prop(obj, 9), b9, sizeof b9);
    u9log_line("PORTAL %7lu ms door handle %u %s %s yaw %.1f | p8 %s | p9 %s",
               (unsigned long)(u9time_now() - g_t0), handle, when,
               (rd32(obj + OBJ_FLAGS) & DOOR_OPEN_BIT) ? "OPEN" : "shut", door_yaw(obj), b8, b9);
}

typedef struct {
    unsigned doors, stale, links, unblocked, already, badLink;
    uintptr_t firstDoor[3];
    uint32_t  firstLink[3];
} Walk;

#define MAX_CLAIMS 2048
static uintptr_t g_claim[MAX_CLAIMS];
static int       g_nClaim;
static int       g_lastOrphans = -1, g_lastStale = -1;
static unsigned  g_orphanMax, g_staleMax;

static void walk_links(int apply, int prune, Walk *w)
{
    uintptr_t base = map_items_base();
    int i, keep = 0;
    memset(w, 0, sizeof *w);
    g_nClaim = 0;
    if (base != g_doorsBase) {
        w->stale = (unsigned)g_nDoors;
        if (prune) { g_nDoors = 0; g_doorsBase = base; g_gen++; }
        return;
    }
    for (i = 0; i < g_nDoors; i++) {
        uintptr_t obj = g_doors[i];
        unsigned k;
        if (!door_valid(obj, base)) { w->stale++; continue; }
        if (prune) g_doors[keep++] = obj;
        w->doors++;
        for (k = 8; k <= 9; k++) {
            uint32_t link = prop_safe(obj, base, k);
            uintptr_t po;
            if (!link) continue;
            w->links++;
            po = link_portal(link);
            if (!po) { w->badLink++; continue; }
            if (g_nClaim < MAX_CLAIMS) g_claim[g_nClaim++] = po;
            if (rd32(po + PO_FLAGS) & 1u) { w->already++; continue; }
            if (w->unblocked < 3) { w->firstDoor[w->unblocked] = obj; w->firstLink[w->unblocked] = link; }
            w->unblocked++;
            if (apply) *(volatile uint32_t *)(po + PO_FLAGS) = rd32(po + PO_FLAGS) | 1u;
        }
    }
    if (prune) g_nDoors = keep;
}

static void walk_claims(void)
{
    Walk w;
    walk_links(0, 0, &w);
}

static int portal_named(uintptr_t po)
{
    int j;
    for (j = 0; j < g_nClaim; j++) if (g_claim[j] == po) return 1;
    return 0;
}

static void walk_detail(const Walk *w, const char *what)
{
    unsigned i;
    for (i = 0; i < w->unblocked && i < 3 && log_ok(); i++) {
        char lt[160];
        int pos[3];
        door_pos(w->firstDoor[i], pos);
        link_text(w->firstLink[i], lt, sizeof lt);
        g_lines++;
        u9log_line("PORTAL           %s: door type %u at %d,%d,%d %s names %s", what,
                   (unsigned)*(const uint16_t *)(w->firstDoor[i] + OBJ_TYPE), pos[0], pos[1], pos[2],
                   (rd32(w->firstDoor[i] + OBJ_FLAGS) & DOOR_OPEN_BIT) ? "OPEN" : "shut", lt);
    }
}

static void U9CALL h_reset(void *spaces)
{
    Walk w;
    o_reset(spaces);
    g_resets++;
    walk_links(g_fix, 1, &w);
    if (g_fix) g_reblocked += w.unblocked;
    if ((g_on && log_ok()) || (!g_on && g_fixLines < MAX_FIX_LINES)) {
        if (g_on) g_lines++; else g_fixLines++;
        u9log_line("PORTAL %7lu ms reset #%u: the engine cleared every portal's flags; %u doors"
                   " remembered (%u stale dropped), %u links: %u portals %s, %u already blocked,"
                   " %u naming no portal", (unsigned long)(u9time_now() - g_t0), g_resets, w.doors,
                   w.stale, w.links, w.unblocked,
                   g_fix ? "re-blocked (fix_door_portals)" : "left clear (fix off - would re-block)",
                   w.already, w.badLink);
        if (g_on) walk_detail(&w, g_fix ? "re-blocked" : "left clear");
    }
    g_lastDisagree = -1;
    g_lastOrphans = -1;
}

static void check_links(void)
{
    Walk w;
    walk_links(0, 0, &w);
    g_checks++;
    if (w.unblocked > g_checkMax) g_checkMax = w.unblocked;
    if ((int)w.unblocked != g_lastDisagree && log_ok()) {
        g_lastDisagree = (int)w.unblocked;
        g_lines++;
        u9log_line("PORTAL %7lu ms check: %u doors remembered, %u links, %u portals blocked as named,"
                   " %u NOT blocked%s", (unsigned long)(u9time_now() - g_t0), w.doors, w.links,
                   w.already, w.unblocked, w.unblocked ? " - door and portal disagree" : " - all agree");
        walk_detail(&w, "disagrees");
    }
    check_orphans();
}

static const char *orphan_kind(uintptr_t po)
{
    int i;
    for (i = 0; i < g_nPc; i++)
        if (g_pc[i].portal == po)
            return (g_pc[i].gen == g_gen && !g_pc[i].linked) ? "STALE" :
                   g_pc[i].gen == g_gen ? "named-then" : "not-seen";
    return "no-claimant";
}

static void check_orphans(void)
{
    int i, n = 0, stale = 0, shown = 0;
    for (i = 0; i < g_count; i++) {
        Portal *q = &g_po[i];
        if (!readable4(q->p + PO_FLAGS) || !(rd32(q->p + PO_FLAGS) & 1u) || portal_named(q->p)) continue;
        n++;
        if (!strcmp(orphan_kind(q->p), "STALE")) stale++;
    }
    if (n > (int)g_orphanMax) g_orphanMax = (unsigned)n;
    if (stale > (int)g_staleMax) g_staleMax = (unsigned)stale;
    if (n == g_lastOrphans && stale == g_lastStale) return;
    g_lastOrphans = n;
    g_lastStale = stale;
    if (!log_ok()) return;
    g_lines++;
    u9log_line("PORTAL %7lu ms check: %d portals blocked that no remembered door names: %d STALE"
               " (its door seen since the load without the link), %d whose door was not seen"
               " since the load%s", (unsigned long)(u9time_now() - g_t0), n, stale, n - stale,
               stale ? " - door open, portal shut" : "");
    for (i = 0; i < g_count && shown < 5 && n; i++) {
        Portal *q = &g_po[i];
        char nm[40], tt[64];
        if (!readable4(q->p + PO_FLAGS) || !(rd32(q->p + PO_FLAGS) & 1u) || portal_named(q->p)) continue;
        if (!log_ok()) break;
        shown++; g_lines++;
        target_text(q->target, tt, sizeof tt);
        u9log_line("PORTAL           unclaimed %s: space %d '%s' plane %d portal %d (%s) at %.0f,%.0f,%.0f",
                   orphan_kind(q->p), q->space, space_name(space_at(spaces_mgr(), q->space), nm, sizeof nm),
                   q->plane, q->idx, tt, q->c[0], q->c[1], q->c[2]);
    }
}

void u9portal_install(void)
{
    static const uint8_t expAssign[7] = { 0x51, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x10 };
    static const U9PatchSpec specAssign = {
        "SPACES_ASSIGN_OBJECT", U9PATCH_INLINE, U9SYM_SPACES_ASSIGN_OBJECT, 7, expAssign, 7,
        U9PF_EXCLUSIVE
    };
    static const uint8_t expReset[8] = { 0x83, 0xEC, 0x0C, 0x53, 0x89, 0x4C, 0x24, 0x0C };
    static const U9PatchSpec specReset = {
        "SPACES_RESET_PORTALS", U9PATCH_INLINE, U9SYM_SPACES_RESET_PORTALS, 8, expReset, 8,
        U9PF_EXCLUSIVE
    };
    g_on  = u9cfg_int("diag_portal_watch", 0);
    g_fix = u9cfg_int("fix_door_portals", 1);
    if (!g_on && !g_fix) {
        u9log_line("door portal fix off - the first load leaves shut doors' portals unblocked and"
                   " later loads can leave open doors' portals blocked, as stock");
        return;
    }
    g_every = (DWORD)u9cfg_int("diag_portal_watch_ms", 1000);
    if (g_every < 100) g_every = 100;
    g_t0 = u9time_now();
    g_next = g_t0;
    o_assign = (AssignFn)u9patch_install(&specAssign, (void *)h_assign);
    o_reset  = o_assign ? (ResetFn)u9patch_install(&specReset, (void *)h_reset) : 0;
    if (g_fix)
        u9log_line("door portal fix %s: after the engine clears the portals on the first load, each"
                   " portal a shut door still names is blocked again, and a portal whose door is"
                   " no longer in it is cleared (space assignment %s,"
                   " reset %s)", o_assign && o_reset ? "on" : "INCOMPLETE",
                   o_assign ? "hooked" : "REFUSED", o_reset ? "hooked" : "REFUSED");
    if (g_on)
        u9log_line("PORTAL watch on: flags swept and door links checked every %lu ms on the frame"
                   " tick; space assignment %s, reset %s - each door-type object assigned is logged"
                   " with its portal links before and after, or the nearest portal when it links none",
                   (unsigned long)g_every, o_assign ? "hooked" : "REFUSED", o_reset ? "hooked" : "REFUSED");
}

void u9portal_report(void)
{
    if (g_fix)
        u9log_line("door portal fix: %u resets, %u portals re-blocked, %u stale blocks cleared,"
                   " %u doors not remembered (list full), %u dead claims dropped and %u old"
                   " ones replaced (table full), %u claims whose portal had gone;"
                   " in-place property read checked %u times, %u mismatches", g_resets, g_reblocked,
                   g_staleCleared, g_doorsFull, g_pcDead, g_pcFull, g_pcGone, g_readChecks,
                   g_readMismatch);
    if (!g_on) return;
    u9log_line("PORTAL check: %u checks, at most %u door links disagreeing with their portal,"
               " at most %u blocked portals no remembered door named, at most %u of them STALE;"
               " %u stale blocks met at assignment", g_checks, g_checkMax, g_orphanMax, g_staleMax,
               g_staleSeen);
    u9log_line("PORTAL totals: %u assignments (%u of doors: %u linked a portal, %u lost their"
               " link), %u flag changes, %u camera space changes, %u sweeps, %u map tables,"
               " %u lines", g_assigns, g_doorAssigns, g_doorLinked, g_doorCleared, g_flagChanges,
               g_camChanges, g_sweeps, g_maps, g_lines);
}
