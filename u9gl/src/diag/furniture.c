#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../fix/fixmem.h"
#include "../gl/npcscale.h"
#include "objstats.h"
#include "furniture.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define WORLD           0x00850228u
#define WORLD_MAP_MGR   0xCCu
#define MGR_MAP         0x20u
#define OBJ_XY          0x04u
#define OBJ_Z           0x08u
#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define PAGE_BASE_X     0x0Cu
#define PAGE_BASE_Y     0x10u
#define REC_SCALE       0x6Cu
#define TYPES           8192
#define FURN_MAX        4096
#define ABOVE_MIN       12
#define ABOVE_MAX       160
#define ACT_MOVER       0x14u
#define VT_PLACENEAR    0xE8u
#define VT_GETYAW       0xCCu
#define MOVER_YAW       0x1A8u
#define NO_STOP_DIST    (-1.0f)

typedef struct { int x, y, z; uint16_t type; } Furn;

typedef void (__attribute__((thiscall)) *ApproachFn)(void *actor, float x, float y, float z,
                                                     float dist, int speed, int wait, int moveIfFailed);
typedef int  (__attribute__((thiscall)) *PlaceNearFn)(void *mover, const float *dest, float facing, int testOnly);

typedef struct {
    int      up;
    DWORD    since;
    DWORD    stillSince;
    int      x, y, z;
    uint16_t type;
    int      above;
    unsigned tries;
    DWORD    nextTry;
    int      dir;
} NpcState;

static int       g_on, g_tried;
static DWORD     g_every, g_next, g_nextSummary, g_t0;
static int       g_radius;
static uint8_t   g_isFurn[TYPES];
static uint8_t   g_isSurface[TYPES];
static int       g_walkoff, g_placeAfter;
static DWORD     g_retryMs, g_stillMs;
static unsigned  g_walks, g_down, g_placed;
static char      g_typeName[TYPES][24];
static Furn      g_furn[FURN_MAX];
static unsigned  g_furnN, g_furnOver;
static NpcState  g_npc[ACTOR_COUNT];
static unsigned  g_events, g_maxUp;

static void flog(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("FURN %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static int current_map(void)
{
    uintptr_t w = readable4(WORLD) ? rd32(WORLD) : 0, m;
    m = (w && readable4(w + WORLD_MAP_MGR)) ? rd32(w + WORLD_MAP_MGR) : 0;
    return (m && readable4(m + MGR_MAP)) ? (int)rd32(m + MGR_MAP) : -1;
}

static int name_listed(const char *lname, const char *list)
{
    char word[32];
    const char *p = list;
    while (*p) {
        size_t n = 0;
        while (*p == ',' || *p == ' ') p++;
        while (*p && *p != ',' && n + 1 < sizeof word) word[n++] = (char)tolower((unsigned char)*p++);
        while (n && word[n - 1] == ' ') n--;
        word[n] = 0;
        if (n && strstr(lname, word)) return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

static int load_types(const char *list, const char *skip, const char *surfaces)
{
    char path[MAX_PATH], *slash;
    HANDLE f;
    DWORD size, got = 0;
    unsigned char *b;
    unsigned t, n, matched = 0;
    char shown[300];
    size_t m = 0;

    GetModuleFileNameA(NULL, path, sizeof path);
    slash = strrchr(path, 92);
    if (!slash) return -1;
    snprintf(slash + 1, (size_t)(path + sizeof path - slash - 1), "static%cTYPENAME.FLX", 92);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    size = GetFileSize(f, NULL);
    b = (size != INVALID_FILE_SIZE && size > 0x80 && size < (8u << 20)) ? malloc(size) : NULL;
    if (!b || !ReadFile(f, b, size, &got, NULL) || got != size) { CloseHandle(f); free(b); return -1; }
    CloseHandle(f);

    n = *(const uint32_t *)(b + 0x50);
    if (n > TYPES) n = TYPES;
    shown[0] = 0;
    for (t = 0; t < n; t++) {
        uint32_t off = *(const uint32_t *)(b + 0x80 + t * 8), len = *(const uint32_t *)(b + 0x84 + t * 8);
        char lname[24];
        size_t i;
        if (0x80 + t * 8 + 8 > size || len <= 6 || off + len > size) continue;
        for (i = 0; i < sizeof g_typeName[0] - 1 && i < len - 6 && b[off + 6 + i]; i++) {
            g_typeName[t][i] = (char)b[off + 6 + i];
            lname[i] = (char)tolower(b[off + 6 + i]);
        }
        g_typeName[t][i] = 0;
        lname[i] = 0;
        if (i && name_listed(lname, list) && !name_listed(lname, skip)) {
            g_isFurn[t] = 1;
            g_isSurface[t] = (uint8_t)name_listed(lname, surfaces);
            matched++;
            if (m + 32 < sizeof shown)
                m += (size_t)snprintf(shown + m, sizeof shown - m, " %u:%s", t, g_typeName[t]);
        }
    }
    free(b);
    flog("%u furniture types from '%s', less '%s'; first:%s", matched, list, skip, shown);
    return (int)matched;
}

static int obj_pos(const unsigned char *obj, int *x, int *y, int *z)
{
    uintptr_t page = (uintptr_t)obj & ~(uintptr_t)0xFFFu;
    uint32_t xy;
    if (!readable4((uintptr_t)obj + OBJ_FLAGS) || (rd32((uintptr_t)obj + OBJ_FLAGS) & 0x200u)) return 0;
    if (!readable4(page + PAGE_BASE_Y)) return 0;
    xy = rd32((uintptr_t)obj + OBJ_XY);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + PAGE_BASE_X);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + PAGE_BASE_Y);
    *z = *(const volatile int16_t *)(obj + OBJ_Z);
    return 1;
}

static void collect(const unsigned char *obj, int fixed, int moving, void *ctx)
{
    uint16_t type = *(const volatile uint16_t *)(obj + OBJ_TYPE);
    Furn *f;
    (void)fixed; (void)moving; (void)ctx;
    if (type >= TYPES || !g_isFurn[type]) return;
    if (g_furnN >= FURN_MAX) { g_furnOver++; return; }
    f = &g_furn[g_furnN];
    if (!obj_pos(obj, &f->x, &f->y, &f->z)) return;
    f->type = type;
    g_furnN++;
}

static const Furn *under(int x, int y, int z, int *above, int *across, char *all, size_t cap,
                         const Furn **surface)
{
    const Furn *best = NULL;
    double bestSurf = 1e30;
    double bestD = 1e30;
    unsigned i;
    size_t m = 0;
    all[0] = 0;
    *surface = NULL;
    for (i = 0; i < g_furnN; i++) {
        const Furn *f = &g_furn[i];
        int dz = z - f->z;
        double d;
        if (abs(f->x - x) > g_radius || abs(f->y - y) > g_radius) continue;
        if (dz < ABOVE_MIN || dz > ABOVE_MAX) continue;
        d = hypot((double)(f->x - x), (double)(f->y - y));
        if (d > g_radius) continue;
        if (m + 40 < cap)
            m += (size_t)snprintf(all + m, cap - m, "%s%s %d up %d across", m ? ", " : "",
                                  g_typeName[f->type], dz, (int)d);
        if (d < bestD) { bestD = d; best = f; *above = dz; *across = (int)d; }
        if (g_isSurface[f->type] && d < bestSurf) { bestSurf = d; *surface = f; }
    }
    return best;
}

static void walk_off(unsigned npc, const U9NpcPlace *p, const Furn *f, NpcState *s, DWORD now)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0, act = 0, vt;
    double base, ang;
    float tx, ty, tz;
    int step = u9cfg_int("fix_furniture_step", 160);
    int turn = ((s->dir + 1) / 2) * ((s->dir & 1) ? 1 : -1);

    s->nextTry = now + g_retryMs;
    if (tbl && readable4(tbl + npc * 4)) act = rd32(tbl + npc * 4);
    if (!act || !readable4(act) || !readable4(act + 0x150)) return;
    vt = rd32(act);
    if (vt < IMAGE_LO || vt >= IMAGE_HI) return;

    base = (p->x == f->x && p->y == f->y) ? 0.0 : atan2((double)(p->y - f->y), (double)(p->x - f->x));
    ang = base + turn * (3.14159265358979 / 4.0);
    tx = (float)(p->x + cos(ang) * step);
    ty = (float)(p->y + sin(ang) * step);
    tz = (float)f->z;
    s->dir = (s->dir + 1) % 8;
    s->tries++;

    if (g_placeAfter > 0 && s->tries > (unsigned)g_placeAfter) {
        uintptr_t mover = readable4(act + ACT_MOVER) ? rd32(act + ACT_MOVER) : 0, mvt;
        float dest[3], facing = 0.0f;
        dest[0] = tx; dest[1] = ty; dest[2] = tz;
        if (mover && readable4(mover) && (mvt = rd32(mover)) && readable4(mvt + VT_PLACENEAR) &&
            rd32(mvt + VT_PLACENEAR) == u9sym(U9SYM_MOVER_PLACENEAR)) {
            if (readable4(mvt + VT_GETYAW) && rd32(mvt + VT_GETYAW) == u9sym(U9SYM_MOVER_GETYAW)) {
                uint32_t v = rd32(mover + MOVER_YAW);
                memcpy(&facing, &v, 4);
                if (!(facing > -100.0f && facing < 100.0f)) facing = 0.0f;
            }
            ((PlaceNearFn)u9sym(U9SYM_MOVER_PLACENEAR))((void *)mover, dest, facing, 0);
            g_placed++;
            flog("PLACE npc %u %s: still up after %u walks - placed near %.0f,%.0f,%.0f",
                 npc, p->name, s->tries - 1, tx, ty, tz);
            return;
        }
    }
    ((ApproachFn)u9sym(U9SYM_ACTOR_PATH_TO_POINT))((void *)act, tx, ty, tz, NO_STOP_DIST, 0, 0, 0);
    g_walks++;
    flog("WALK npc %u %s: try %u, off %s toward %.0f,%.0f,%.0f (%d out, turned %+d deg)",
         npc, p->name, s->tries, g_typeName[f->type], tx, ty, tz, step, turn * 45);
}

void u9furn_tick(void)
{
    DWORD now;
    int map;
    unsigned npc, up = 0;
    uintptr_t recs;

    if (!g_tried) {
        const char *list;
        g_tried = 1;
        if (!u9cfg_int("diag_furniture_watch", 0) && !u9cfg_int("fix_furniture_walkoff", 1)) return;
        g_t0 = u9time_now();
        g_every = (DWORD)u9cfg_int("diag_furniture_watch_ms", 2000);
        if (g_every < 500) g_every = 500;
        g_radius = u9cfg_int("fix_furniture_radius", 96);
        list = u9cfg_str("fix_furniture_names", "table,counter,chair,bench,stool,desk,bed,dresser,cabinet");
        if (load_types(list, u9cfg_str("fix_furniture_skip", "vegetable,leaf"),
                       u9cfg_str("fix_furniture_surfaces", "table,counter,desk,dresser,cabinet,workbench")) <= 0) {
            flog("no furniture types - watch off");
            return;
        }
        g_walkoff    = u9cfg_int("fix_furniture_walkoff", 1);
        g_retryMs    = (DWORD)u9cfg_int("fix_furniture_retry_ms", 3000);
        g_stillMs    = (DWORD)u9cfg_int("fix_furniture_still_s", 5) * 1000u;
        g_placeAfter = u9cfg_int("fix_furniture_place_after", 8);
        if (g_retryMs < 1000) g_retryMs = 1000;
        g_on = 1;
        g_next = g_t0;
        g_nextSummary = g_t0 + 60000;
        flog("furniture watch on: every %lu ms, within %d across and %d..%d above a piece's base;"
             " walk-off %s (still %lus, retry %lus, place after %d walks%s)",
             (unsigned long)g_every, g_radius, ABOVE_MIN, ABOVE_MAX, g_walkoff ? "ON" : "off",
             (unsigned long)(g_stillMs / 1000), (unsigned long)(g_retryMs / 1000), g_placeAfter,
             g_placeAfter ? "" : " = never");
    }
    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;
    if ((map = current_map()) < 0) return;
    recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!recs) return;

    g_furnN = 0;
    u9area_each_object(collect, NULL);

    for (npc = 1; npc < ACTOR_COUNT; npc++) {
        NpcState *s = &g_npc[npc];
        U9NpcPlace p;
        const Furn *f = NULL, *surf = NULL;
        int above = 0, across = 0;
        char all[200];

        if (u9npc_live_place(npc, map, &p)) {
            if (p.x != s->x || p.y != s->y || p.z != s->z) s->stillSince = now;
            s->x = p.x; s->y = p.y; s->z = p.z;
            f = under(p.x, p.y, p.z, &above, &across, all, sizeof all, &surf);
        }
        if (f && !s->up) {
            uintptr_t r = recs + npc * RECORD_STRIDE;
            unsigned sc = readable4(r + REC_SCALE) ? *(const volatile uint8_t *)(r + REC_SCALE) : 0;
            char combat[120];
            u9npc_combat_text(npc, combat, sizeof combat);
            s->up = 1; s->since = now; s->type = f->type; s->above = above;
            s->tries = 0; s->dir = 0; s->nextTry = now;
            g_events++;
            flog("ON  npc %u %s: nearest %s (type %u) %d above its base, %d across [%s]; at %d,%d,%d map %d;"
                 " still %lus; %s; scale %u%%",
                 npc, p.name[0] ? p.name : "(no name)", g_typeName[f->type], f->type, above, across,
                 all, p.x, p.y, p.z, map, (unsigned long)((now - s->stillSince) / 1000),
                 combat, sc ? sc : 100);
        } else if (!f && s->up) {
            s->up = 0;
            if (s->tries && p.live) g_down++;
            flog("OFF npc %u %s: off %s after %lus%s; %u walk%s asked", npc, p.name[0] ? p.name : "(no name)",
                 g_typeName[s->type], (unsigned long)((now - s->since) / 1000),
                 p.live ? "" : " (left the map or the area)", s->tries, s->tries == 1 ? "" : "s");
        }
        if (g_walkoff && s->up && f && surf && p.live && (LONG)(now - s->nextTry) >= 0 &&
            (s->tries || now - s->stillSince >= g_stillMs))
            walk_off(npc, &p, surf, s, now);
        if (s->up) up++;
    }
    if (up > g_maxUp) g_maxUp = up;
    if ((LONG)(now - g_nextSummary) >= 0) {
        g_nextSummary = now + 60000;
        flog("%u NPC%s up on furniture now; %u furniture pieces in the area%s", up, up == 1 ? "" : "s",
             g_furnN, g_furnOver ? " (some not tracked: table full)" : "");
    }
}

void u9furn_report(void)
{
    unsigned npc;
    if (!g_on) return;
    flog("furniture watch: %u times an NPC got up; at most %u at once", g_events, g_maxUp);
    if (g_walkoff)
        flog("walk-off: %u walks asked, %u NPCs came down after being asked, %u placed",
             g_walks, g_down, g_placed);
    for (npc = 1; npc < ACTOR_COUNT; npc++)
        if (g_npc[npc].up)
            flog("  still up: npc %u on %s, %d above, for %lus", npc, g_typeName[g_npc[npc].type],
                 g_npc[npc].above, (unsigned long)((u9time_now() - g_npc[npc].since) / 1000));
}
