#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../fix/fixmem.h"
#include "glspaces.h"

#define SPACES_MGR      0x00AD1340u
#define SM_SPACES       0x08u
#define SPACE_SLOTS     0x2000
#define GAME_OBJ        0x00850228u
#define SP_MAP          0x20u
#define SP_PLANES       0x24u
#define SP_PRIORITY     0x2Cu
#define SP_LINK         0x30u
#define PL_NORMAL       0x10u
#define PL_PORTALS      0x20u
#define PO_CORNERS      0x04u
#define PO_TARGET       0x34u
#define PO_FLAGS        0x40u
#define MAX_MAP_SPACES  512
#define MAX_MAP_PLANES  8192
#define ROOM_PLANES     64
#define ROOM_PORTALS    32
#define INSIDE_SLACK    2.0f

static int       g_map = -1;
static uintptr_t g_mgr;
static uintptr_t g_sp[MAX_MAP_SPACES];
static int       g_spPlane0[MAX_MAP_SPACES], g_spPlanes[MAX_MAP_SPACES];
static int       g_spSlot[MAX_MAP_SPACES], g_spLink[MAX_MAP_SPACES];
static float     g_planes[MAX_MAP_PLANES][4];
static int       g_nSp, g_nPlanes;
static int       g_slotMap[SPACE_SLOTS];
static unsigned  g_rebuilds;

static int current_map(void)
{
    uintptr_t g = readable4(GAME_OBJ) ? rd32(GAME_OBJ) : 0, m;
    if (!g || !readable4(g + 0xCC)) return -1;
    m = rd32(g + 0xCC);
    return (m && readable4(m + 0x20)) ? (int)rd32(m + 0x20) : -1;
}

static int spaces_current(uintptr_t mgr)
{
    int j;
    if (!mgr || mgr != g_mgr || !readable4(mgr + SM_SPACES) ||
        !readable4(mgr + SM_SPACES + (SPACE_SLOTS - 1) * 4u)) return 0;
    for (j = 0; j < g_nSp; j++)
        if (rd32(mgr + SM_SPACES + (uintptr_t)g_spSlot[j] * 4u) != g_sp[j]) return 0;
    return 1;
}

static void map_spaces(void)
{
    uintptr_t mgr = readable4(SPACES_MGR) ? rd32(SPACES_MGR) : 0;
    int map = current_map(), s;
    if (map == g_map && g_nSp && spaces_current(mgr)) return;
    if (map == g_map && g_nSp) g_rebuilds++;
    g_map = map;
    g_mgr = mgr;
    g_nSp = g_nPlanes = 0;
    for (s = 0; s < SPACE_SLOTS; s++) g_slotMap[s] = -1;
    if (!mgr || map < 0) return;
    for (s = 0; s < SPACE_SLOTS && g_nSp < MAX_MAP_SPACES; s++) {
        uintptr_t a = mgr + SM_SPACES + (uintptr_t)s * 4u, sp, pl;
        unsigned guard = 0;
        if (!readable4(a) || !(sp = rd32(a)) || !readable4(sp + SP_PRIORITY)) continue;
        if ((int)rd32(sp + SP_MAP) != map) continue;
        g_spPlane0[g_nSp] = g_nPlanes;
        for (pl = rd32(sp + SP_PLANES); pl && guard++ < ROOM_PLANES && readable4(pl + PL_PORTALS) &&
                                        g_nPlanes < MAX_MAP_PLANES; pl = rd32(pl))
            memcpy(g_planes[g_nPlanes++], (const void *)(pl + PL_NORMAL), 16);
        g_spPlanes[g_nSp] = g_nPlanes - g_spPlane0[g_nSp];
        g_spSlot[g_nSp] = s;
        g_slotMap[s] = g_nSp;
        g_spLink[g_nSp] = (int)*(const int16_t *)(sp + SP_LINK);
        g_sp[g_nSp++] = sp;
    }
    for (s = 0; s < g_nSp; s++) {
        int j, link = g_spLink[s];
        g_spLink[s] = -1;
        for (j = 0; link >= 0 && j < g_nSp; j++)
            if (g_spSlot[j] == link) { g_spLink[s] = j; break; }
    }
}

static int linked_group(int j, int *g)
{
    int n = 0, k, t = g_spLink[j];
    g[n++] = j;
    if (t >= 0) g[n++] = t;
    for (k = 0; k < g_nSp && n < U9SP_LINKED; k++) {
        int m, dup = 0;
        if (k == j || k == t) continue;
        if (g_spLink[k] != j && (t < 0 || g_spLink[k] != t)) continue;
        for (m = 0; m < n; m++) dup |= g[m] == k;
        if (!dup) g[n++] = k;
    }
    return n;
}

static float outside_by(int j, const float p[3])
{
    float worst = 0.0f;
    int k;
    for (k = 0; k < g_spPlanes[j]; k++) {
        const float *n = g_planes[g_spPlane0[j] + k];
        float e = n[0] * p[0] + n[1] * p[1] + n[2] * p[2] + n[3];
        if (-e > worst) worst = -e;
    }
    return worst;
}

static int inside(int j, const float p[3])
{
    return g_spPlanes[j] > 0 && outside_by(j, p) <= INSIDE_SLACK;
}

static int add_space(int j, U9SpaceSet *set)
{
    uintptr_t sp = g_sp[j], pl;
    unsigned guard = 0;
    int i = set->nSpaces, p0 = set->nPlanes, q0 = set->nPortals, roomPortals = 0;
    float *h;
    if (i >= U9SP_MAX_SPACES) return -1;
    if (p0 + g_spPlanes[j] > U9SP_MAX_PLANES) return -1;
    for (pl = rd32(sp + SP_PLANES); pl && guard++ < ROOM_PLANES && readable4(pl + PL_PORTALS); pl = rd32(pl)) {
        uintptr_t po;
        unsigned g2 = 0;
        if (set->nPlanes >= U9SP_MAX_PLANES) {
            set->nPlanes = p0;
            set->nPortals = q0;
            return -1;
        }
        memcpy(&set->tex[(U9SP_PLANE0 + set->nPlanes) * 4], (const void *)(pl + PL_NORMAL), 16);
        set->nPlanes++;
        for (po = rd32(pl + PL_PORTALS); po && g2++ < 16 && readable4(po + PO_FLAGS); po = rd32(po)) {
            float *q;
            int k;
            if (set->nPortals >= U9SP_MAX_PORTALS || roomPortals >= ROOM_PORTALS) break;
            q = &set->tex[(U9SP_PORTAL0 + set->nPortals * 4) * 4];
            for (k = 0; k < 4; k++) {
                memcpy(&q[k * 4], (const void *)(po + PO_CORNERS + k * 12), 12);
                q[k * 4 + 3] = 0.0f;
            }
            q[3] = (rd32(po + PO_FLAGS) & 3u) ? 0.0f : 1.0f;
            q[7] = (float)(int)rd32(po + PO_TARGET);
            set->nPortals++;
            roomPortals++;
        }
    }
    {
        int c;
        for (c = 0; c < 32; c++) {
            char ch = *(const char *)(sp + c);
            if (!ch) break;
            set->name[i][c] = (ch >= 32 && ch < 127) ? ch : '?';
        }
        set->name[i][c] = 0;
    }
    h = &set->tex[i * 2 * 4];
    h[0] = (float)(U9SP_PLANE0 + p0);
    h[1] = (float)(set->nPlanes - p0);
    h[2] = (float)(U9SP_PORTAL0 + q0 * 4);
    h[3] = (float)(set->nPortals - q0);
    h[4] = (float)(int)rd32(sp + SP_PRIORITY);
    h[5] = h[6] = h[7] = 0.0f;
    set->nSpaces++;
    return i;
}

static int portal_target(const U9SpaceSet *set, int i)
{
    return (int)set->tex[(U9SP_PORTAL0 + i * 4) * 4 + 7];
}

static void resolve_targets(U9SpaceSet *set, const int *inSet)
{
    int i;
    for (i = 0; i < set->nPortals; i++) {
        int t = portal_target(set, i), j = (t >= 0 && t < SPACE_SLOTS) ? g_slotMap[t] : -1;
        set->tex[(U9SP_PORTAL0 + i * 4) * 4 + 7] = t < 0 ? -1.0f : (j >= 0 && inSet[j] >= 0) ? (float)inSet[j] : -2.0f;
    }
}

static int best_space(const float p[3])
{
    int best = -1, bestPri = 0, j;
    for (j = 0; j < g_nSp; j++) {
        int pri;
        if (!inside(j, p)) continue;
        pri = (int)rd32(g_sp[j] + SP_PRIORITY);
        if (best < 0 || pri > bestPri) { best = j; bestPri = pri; }
    }
    return best;
}

int u9spaces_for_lights(const float (*base)[3], const float (*pos)[3], const float *reach, int n,
                        int keepOut, const float cam[3], int (*lightRooms)[U9SP_LINKED], U9SpaceSet *set,
                        float camReach)
{
    static int inSet[MAX_MAP_SPACES];
    static int cand[MAX_MAP_SPACES];
    static float dist[MAX_MAP_SPACES];
    int i, j, nCand = 0;
    set->nSpaces = set->nPlanes = set->nPortals = 0;
    set->roomFull = set->nearFull = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < U9SP_LINKED; j++) lightRooms[i][j] = -1;
    map_spaces();
    if (!g_nSp) return 0;
    for (j = 0; j < g_nSp; j++) inSet[j] = -1;
    for (i = 0; i < n; i++) {
        int best = best_space(base[i]), g[U9SP_LINKED], ng, k, r = 0;
        if (best < 0) best = best_space(pos[i]);
        if (best < 0) continue;
        ng = linked_group(best, g);
        for (k = 0; k < ng; k++) {
            if (inSet[g[k]] < 0) inSet[g[k]] = add_space(g[k], set);
            if (inSet[g[k]] >= 0) lightRooms[i][r++] = inSet[g[k]];
            else set->roomFull = 1;
        }
        if (inSet[best] < 0) lightRooms[i][0] = -1;
    }
    {
        int nRooms = set->nSpaces, q = 0, r;
        for (r = 0; r < nRooms; r++) {
            const float *h = &set->tex[r * 2 * 4];
            int q0 = ((int)h[2] - U9SP_PORTAL0) / 4, nq = (int)h[3];
            for (q = q0; q < q0 + nq; q++) {
                int t = portal_target(set, q), j = (t >= 0 && t < SPACE_SLOTS) ? g_slotMap[t] : -1;
                if (j >= 0 && inSet[j] < 0 && (inSet[j] = add_space(j, set)) < 0) break;
            }
        }
    }
    if (!keepOut && camReach <= 0.0f) {
        resolve_targets(set, inSet);
        return 1;
    }
    for (j = 0; j < g_nSp; j++) {
        int k;
        float d;
        if (inSet[j] >= 0 || !g_spPlanes[j]) continue;
        for (i = 0; i < n && keepOut; i++)
            if (lightRooms[i][0] < 0 && outside_by(j, pos[i]) <= reach[i]) break;
        if ((!keepOut || i >= n) && !(camReach > 0.0f && outside_by(j, cam) <= camReach)) continue;
        d = outside_by(j, cam);
        for (k = nCand; k > 0 && dist[k - 1] > d; k--) {
            cand[k] = cand[k - 1];
            dist[k] = dist[k - 1];
        }
        cand[k] = j;
        dist[k] = d;
        nCand++;
    }
    for (i = 0; i < nCand; i++) {
        inSet[cand[i]] = add_space(cand[i], set);
        if (inSet[cand[i]] < 0) { set->nearFull = 1; break; }
    }
    resolve_targets(set, inSet);
    return 1;
}

int u9spaces_at(const float p[3], char *name, int cap, int *count)
{
    int j, c;
    map_spaces();
    if (count) *count = g_nSp;
    if (name && cap > 0) name[0] = 0;
    j = best_space(p);
    if (j < 0 || !name) return j;
    for (c = 0; c < 32 && c < cap - 1; c++) {
        char ch = *(const char *)(g_sp[j] + c);
        if (!ch) break;
        name[c] = (ch >= 32 && ch < 127) ? ch : '?';
    }
    name[c] = 0;
    return j;
}

void u9spaces_describe(int j, void (*out)(const char *fmt, ...))
{
    uintptr_t pl;
    unsigned guard = 0;
    int plane = 0, portals = 0, open = 0, g[U9SP_LINKED], ng, k;
    if (j < 0 || j >= g_nSp) return;
    out("  this map's spaces re-read %u times on the same map: the manager's slots had changed",
        g_rebuilds);
    out("  %d planes (inside where n.p + w >= 0), priority %d", g_spPlanes[j], (int)rd32(g_sp[j] + SP_PRIORITY));
    for (pl = rd32(g_sp[j] + SP_PLANES); pl && guard++ < ROOM_PLANES && readable4(pl + PL_PORTALS); pl = rd32(pl), plane++) {
        uintptr_t po;
        unsigned g2 = 0;
        const float *n = (const float *)(pl + PL_NORMAL);
        out("  plane %d: n (%.2f, %.2f, %.2f) w %.0f", plane, (double)n[0], (double)n[1], (double)n[2], (double)n[3]);
        for (po = rd32(pl + PL_PORTALS); po && g2++ < 16 && readable4(po + PO_FLAGS); po = rd32(po)) {
            const float *c = (const float *)(po + PO_CORNERS);
            float lo[3], hi[3], mid[3];
            unsigned f = rd32(po + PO_FLAGS);
            for (k = 0; k < 3; k++) {
                lo[k] = hi[k] = c[k];
                lo[k] = c[3 + k] < lo[k] ? c[3 + k] : lo[k];  hi[k] = c[3 + k] > hi[k] ? c[3 + k] : hi[k];
                lo[k] = c[6 + k] < lo[k] ? c[6 + k] : lo[k];  hi[k] = c[6 + k] > hi[k] ? c[6 + k] : hi[k];
                lo[k] = c[9 + k] < lo[k] ? c[9 + k] : lo[k];  hi[k] = c[9 + k] > hi[k] ? c[9 + k] : hi[k];
                mid[k] = (lo[k] + hi[k]) * 0.5f;
            }
            out("    portal: centre %.0f,%.0f,%.0f, spans %.0f x %.0f x %.0f, flags 0x%X: %s", (double)mid[0], (double)mid[1],
                (double)mid[2], (double)(hi[0] - lo[0]), (double)(hi[1] - lo[1]), (double)(hi[2] - lo[2]), f,
                (f & 3u) ? "shut" : "open");
            portals++;
            open += !(f & 3u);
        }
    }
    ng = linked_group(j, g);
    out("  %d portals, %d open%s", portals, open, ng > 1 ? "; linked spaces:" : "");
    for (k = 1; k < ng; k++) {
        char name[33];
        int c;
        for (c = 0; c < 32; c++) {
            char ch = *(const char *)(g_sp[g[k]] + c);
            if (!ch) break;
            name[c] = (ch >= 32 && ch < 127) ? ch : '?';
        }
        name[c] = 0;
        out("    '%s', %d planes", name, g_spPlanes[g[k]]);
    }
}
