#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../common/u9time.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "lightwatch.h"

void u9log_line(const char *fmt, ...);

#define LIGHT_SYS       0x008515F0u
#define LS_INFINITE     0x00u
#define LS_RANGED       0x10u
#define LS_AMBIENT      0x20u
#define LS_CAP          0x30u
#define LS_QUALITY      0x2Cu
#define LS_PERIOD       0x38u
#define LS_AVATAR_PERIOD 0x3Cu
#define LS_FLICKER_ON   0x48u
#define LS_FLICKER_RANGE 0x4Cu
#define LS_RANGED_ON    0x64u
#define LS_SCALE_POS    0x90u
#define LS_SCALE_NEG    0x94u
#define LIST_HEAD       0x04u
#define NODE_NEXT       0x04u
#define NODE_LIGHT      0x0Cu
#define NODE_DIST       0x10u
#define LR_FLAGS        0x00u
#define LR_ITEM         0x04u
#define LR_RGB          0x08u
#define LR_POS          0x0Cu
#define LR_SPREAD       0x24u
#define LR_RANGE        0x28u
#define LR_BASE_RGB     0x2Cu
#define LR_FLICKER_DEPTH 0x34u
#define GAME_OBJ        0x00850228u
#define GAME_CAMERA     0xBCu
#define CAM_LOOK        0xB8u
#define CAM_POS         0xC4u
#define CAM_BOOK_SCENE  0x13Cu

#define MAX_TRACK       1024
#define MAX_LINES       4000u
#define SUMMARY_MS      30000u
#define NEAREST         8
#define MOVE_UNITS      16.0f

typedef struct {
    uintptr_t rec;
    uint32_t  flags, item;
    uint8_t   rgb[3];
    float     pos[3], range;
    int       infinite;
    unsigned  seen;
} Track;

typedef struct {
    uintptr_t rec;
    uint32_t  flags, item;
    uint8_t   rgb[3];
    float     pos[3], range, spread, nodeDist;
    int       infinite;
} Light;

static int      g_on;
static DWORD    g_every = 1000, g_next, g_nextSummary, g_t0;
static Track    g_tr[MAX_TRACK];
static int      g_nTr;
static unsigned g_sweep, g_lines, g_new, g_gone, g_moved, g_recolour, g_full, g_maxRanged;

static int log_ok(void) { return g_lines < MAX_LINES; }

static int read_light(uintptr_t rec, int infinite, float nodeDist, Light *l)
{
    if (!rec || !readable4(rec + LR_RANGE)) return 0;
    l->rec = rec;
    l->flags = rd32(rec + LR_FLAGS);
    l->item = rd32(rec + LR_ITEM);
    memcpy(l->rgb, (const void *)(rec + LR_RGB), 3);
    memcpy(l->pos, (const void *)(rec + LR_POS), sizeof l->pos);
    memcpy(&l->spread, (const void *)(rec + LR_SPREAD), 4);
    memcpy(&l->range, (const void *)(rec + LR_RANGE), 4);
    l->nodeDist = nodeDist;
    l->infinite = infinite;
    return 1;
}

static int read_lights(Light *out, int max, int *ranged)
{
    unsigned k;
    int n = 0;
    *ranged = 0;
    for (k = 0; k < 2; k++) {
        uintptr_t list = LIGHT_SYS + (k ? LS_RANGED : LS_INFINITE), node;
        unsigned guard = 0;
        if (!readable4(list + LIST_HEAD)) continue;
        for (node = rd32(list + LIST_HEAD); node && guard++ < 4096 && readable4(node + NODE_DIST);
             node = rd32(node + NODE_NEXT)) {
            float d;
            memcpy(&d, (const void *)(node + NODE_DIST), 4);
            if (n < max && read_light(rd32(node + NODE_LIGHT), !k, d, &out[n])) {
                n++;
                if (k) (*ranged)++;
            }
        }
    }
    return n;
}

static int camera(float pos[3], float look[3])
{
    uintptr_t g = readable4(GAME_OBJ) ? rd32(GAME_OBJ) : 0, cam;
    if (!g || !readable4(g + GAME_CAMERA)) return 0;
    cam = rd32(g + GAME_CAMERA);
    if (!cam || !readable4(cam + CAM_POS + 8)) return 0;
    memcpy(pos, (const void *)(cam + CAM_POS), 12);
    memcpy(look, (const void *)(cam + CAM_LOOK), 12);
    return 1;
}

static const char *flag_text(uint32_t f)
{
    static char buf[64];
    snprintf(buf, sizeof buf, "%02X%s%s%s%s%s", (unsigned)(f & 0xFFu),
             (f & 0x01u) ? " flicker" : "", (f & 0x02u) ? " negative" : "",
             (f & 0x08u) ? " static" : "", (f & 0x10u) ? " scaled" : "",
             (f & 0x40u) ? " attached" : "");
    return buf;
}

static int item_type(uint32_t handle)
{
    uintptr_t tbl, obj;
    if (!handle || !readable4(ENTITY_TABLE) || !(tbl = rd32(ENTITY_TABLE))) return -1;
    if (handle > 0x100000u || !readable4(tbl + handle * 0x0Cu + 8)) return -1;
    obj = rd32(tbl + handle * 0x0Cu + 8);
    return (obj && readable4(obj + 0x0A)) ? (int)*(const uint16_t *)(obj + 0x0A) : -1;
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static float bearing(const float cam[3], const float look[3], const float p[3])
{
    float dx = p[0] - cam[0], dy = p[1] - cam[1];
    float a = atan2f(dy, dx) - atan2f(look[1], look[0]);
    while (a > 3.14159265f)  a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a * (180.0f / 3.14159265f);
}

static void light_text(const Light *l, const float cam[3], const float look[3], int haveCam,
                       char *buf, size_t n)
{
    char where[96];
    int type = item_type(l->item);
    if (l->infinite)
        snprintf(where, sizeof where, "infinite");
    else if (haveCam)
        snprintf(where, sizeof where, "%.0f away, bearing %+.0f, height %+.0f",
                 dist3(l->pos, cam), bearing(cam, look, l->pos), l->pos[2] - cam[2]);
    else
        snprintf(where, sizeof where, "no camera");
    snprintf(buf, n, "%s | at %.0f,%.0f,%.0f colour %u,%u,%u range %.0f spread %.2f flags %s"
             " | item %u type %d | node %.0f",
             where, l->pos[0], l->pos[1], l->pos[2], l->rgb[0], l->rgb[1], l->rgb[2], l->range,
             l->spread, flag_text(l->flags), (unsigned)l->item, type, l->nodeDist);
}

static int nearest(const Light *all, int n, const float cam[3], int count, int *idx)
{
    int i, j, m = 0;
    for (i = 0; i < n; i++) {
        float d;
        if (all[i].infinite) continue;
        d = dist3(all[i].pos, cam);
        for (j = 0; j < m && dist3(all[idx[j]].pos, cam) <= d; j++) {}
        if (j >= count) continue;
        if (m < count) m++;
        memmove(&idx[j + 1], &idx[j], (size_t)(m - 1 - j) * sizeof idx[0]);
        idx[j] = i;
    }
    return m;
}

static Light g_cur[MAX_TRACK];

static Track *track_of(uintptr_t rec)
{
    int i;
    for (i = 0; i < g_nTr; i++) if (g_tr[i].rec == rec) return &g_tr[i];
    return 0;
}

static void summary(const Light *all, int n, int ranged)
{
    float cam[3], look[3];
    int have = camera(cam, look), idx[NEAREST], m, i;
    unsigned flick = 0, neg = 0, stat = 0;
    for (i = 0; i < n; i++) {
        if (all[i].flags & 0x01u) flick++;
        if (all[i].flags & 0x02u) neg++;
        if (all[i].flags & 0x08u) stat++;
    }
    if (!log_ok()) return;
    g_lines++;
    {
        float sp, sn;
        u9light_range_scales(&sp, &sn);
        u9log_line("LIGHT %7lu ms range scale %.3f, negative %.3f (light system +0x90, +0x94)",
                   (unsigned long)(u9time_now() - g_t0), sp, sn);
    }
    u9log_line("LIGHT %7lu ms summary: %d infinite, %d ranged (%u flicker, %u negative, %u static);"
               " ambient %d,%d,%d; ranged lights %s, %d per element; camera %s %.0f,%.0f,%.0f look"
               " %.2f,%.2f,%.2f", (unsigned long)(u9time_now() - g_t0), n - ranged, ranged, flick,
               neg, stat, (int)rd32(LIGHT_SYS + LS_AMBIENT), (int)rd32(LIGHT_SYS + LS_AMBIENT + 4),
               (int)rd32(LIGHT_SYS + LS_AMBIENT + 8), rd32(LIGHT_SYS + LS_RANGED_ON) ? "on" : "OFF",
               (int)rd32(LIGHT_SYS + LS_CAP), have ? "at" : "unknown", have ? cam[0] : 0.0f,
               have ? cam[1] : 0.0f, have ? cam[2] : 0.0f, have ? look[0] : 0.0f,
               have ? look[1] : 0.0f, have ? look[2] : 0.0f);
    if (!have) return;
    m = nearest(all, n, cam, NEAREST, idx);
    for (i = 0; i < m && log_ok(); i++) {
        char t[300];
        light_text(&all[idx[i]], cam, look, 1, t, sizeof t);
        g_lines++;
        u9log_line("LIGHT          near %d: %s", i + 1, t);
    }
}

static void sweep(void)
{
    float cam[3], look[3];
    int have = camera(cam, look), ranged, n, i;
    DWORD now = u9time_now();
    g_sweep++;
    n = read_lights(g_cur, MAX_TRACK, &ranged);
    if ((unsigned)ranged > g_maxRanged) g_maxRanged = (unsigned)ranged;

    for (i = 0; i < n; i++) {
        const Light *l = &g_cur[i];
        Track *t = track_of(l->rec);
        char txt[300];
        if (!t) {
            if (g_nTr >= MAX_TRACK) { g_full++; continue; }
            t = &g_tr[g_nTr++];
            t->rec = l->rec;
            g_new++;
            if (log_ok()) {
                g_lines++;
                light_text(l, cam, look, have, txt, sizeof txt);
                u9log_line("LIGHT %7lu ms new %08X: %s", (unsigned long)(now - g_t0),
                           (unsigned)l->rec, txt);
            }
        } else {
            if (!l->infinite && dist3(t->pos, l->pos) > MOVE_UNITS) {
                g_moved++;
                if (log_ok()) {
                    g_lines++;
                    u9log_line("LIGHT %7lu ms moved %08X: %.0f,%.0f,%.0f -> %.0f,%.0f,%.0f (item %u"
                               " type %d)", (unsigned long)(now - g_t0), (unsigned)l->rec,
                               t->pos[0], t->pos[1], t->pos[2], l->pos[0], l->pos[1], l->pos[2],
                               (unsigned)l->item, item_type(l->item));
                }
            } else if (!(l->flags & 0x01u) && memcmp(t->rgb, l->rgb, 3)) {
                g_recolour++;
                if (log_ok()) {
                    g_lines++;
                    u9log_line("LIGHT %7lu ms colour %08X: %u,%u,%u -> %u,%u,%u%s",
                               (unsigned long)(now - g_t0), (unsigned)l->rec, t->rgb[0], t->rgb[1],
                               t->rgb[2], l->rgb[0], l->rgb[1], l->rgb[2],
                               l->infinite ? " (infinite)" : "");
                }
            }
        }
        t->flags = l->flags; t->item = l->item; t->range = l->range; t->infinite = l->infinite;
        memcpy(t->rgb, l->rgb, 3);
        memcpy(t->pos, l->pos, sizeof t->pos);
        t->seen = g_sweep;
    }

    {
        int keep = 0;
        for (i = 0; i < g_nTr; i++) {
            Track *t = &g_tr[i];
            if (t->seen != g_sweep) {
                g_gone++;
                if (log_ok()) {
                    g_lines++;
                    u9log_line("LIGHT %7lu ms gone %08X: was at %.0f,%.0f,%.0f colour %u,%u,%u"
                               " range %.0f flags %s", (unsigned long)(now - g_t0), (unsigned)t->rec,
                               t->pos[0], t->pos[1], t->pos[2], t->rgb[0], t->rgb[1], t->rgb[2],
                               t->range, flag_text(t->flags));
                }
                continue;
            }
            if (keep != i) g_tr[keep] = *t;
            keep++;
        }
        g_nTr = keep;
    }

    if ((LONG)(now - g_nextSummary) >= 0) {
        g_nextSummary = now + SUMMARY_MS;
        summary(g_cur, n, ranged);
    }
}

void u9light_tick(void)
{
    DWORD now;
    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;
    sweep();
}

void u9light_command(const char *arg, void (*out)(const char *fmt, ...))
{
    static Light all[MAX_TRACK];
    float cam[3], look[3];
    int count = (arg && *arg) ? atoi(arg) : NEAREST, idx[32], n, m, i, ranged;
    if (count < 1) count = 1;
    if (count > 32) count = 32;
    if (!camera(cam, look)) { out("no camera - is a world loaded?"); return; }
    n = read_lights(all, MAX_TRACK, &ranged);
    {
        float sp, sn;
        u9light_range_scales(&sp, &sn);
        out("%d lights (%d ranged); ambient %d,%d,%d; range scale %.2f (negative %.2f); camera at"
            " %.0f,%.0f,%.0f", n, ranged, (int)rd32(LIGHT_SYS + LS_AMBIENT),
            (int)rd32(LIGHT_SYS + LS_AMBIENT + 4), (int)rd32(LIGHT_SYS + LS_AMBIENT + 8), sp, sn,
            cam[0], cam[1], cam[2]);
    }
    m = nearest(all, n, cam, count, idx);
    for (i = 0; i < m; i++) {
        const Light *l = &all[idx[i]];
        out("%2d  %5.0f away  %+4.0f deg  %+5.0f up  %3u,%3u,%3u  range %4.0f  %s", i + 1,
            dist3(l->pos, cam), bearing(cam, look, l->pos), l->pos[2] - cam[2], l->rgb[0],
            l->rgb[1], l->rgb[2], l->range, flag_text(l->flags));
    }
    out("bearing: 0 ahead, + to the left, - to the right (from the camera's look; check it)");
    if (g_on) {
        char t[300];
        for (i = 0; i < m && log_ok(); i++) {
            light_text(&all[idx[i]], cam, look, 1, t, sizeof t);
            g_lines++;
            u9log_line("LIGHT %7lu ms lights %d: %s", (unsigned long)(u9time_now() - g_t0), i + 1, t);
        }
    }
}

int u9light_snapshot(U9LightSnap *out, int max)
{
    uintptr_t list = LIGHT_SYS + LS_RANGED, node;
    unsigned guard = 0;
    int n = 0, flickerOn = 0;
    float flickerRange = 0.0f;
    if (!readable4(list + LIST_HEAD)) return 0;
    if (readable4(LIGHT_SYS + LS_FLICKER_RANGE)) {
        flickerOn = rd32(LIGHT_SYS + LS_FLICKER_ON) != 0 && rd32(LIGHT_SYS + LS_PERIOD) == 1u &&
                    rd32(LIGHT_SYS + LS_AVATAR_PERIOD) == 1u && rd32(LIGHT_SYS + LS_QUALITY) == 3u;
        flickerRange = (float)(int32_t)rd32(LIGHT_SYS + LS_FLICKER_RANGE);
    }
    for (node = rd32(list + LIST_HEAD); node && n < max && guard++ < 4096 && readable4(node + NODE_DIST);
         node = rd32(node + NODE_NEXT)) {
        uintptr_t rec = rd32(node + NODE_LIGHT);
        float range;
        if (!rec || !readable4(rec + LR_RANGE)) continue;
        memcpy(&range, (const void *)(rec + LR_RANGE), 4);
        if (!(range > 0.0f)) continue;
        memcpy(out[n].pos, (const void *)(rec + LR_POS), 12);
        {
            const float *off = (const float *)(rec + 0x18);
            out[n].base[0] = out[n].pos[0] - off[0];
            out[n].base[1] = out[n].pos[1] - off[1];
            out[n].base[2] = out[n].pos[2] - off[2];
        }
        out[n].rgb[0] = *(const uint8_t *)(rec + LR_RGB) / 255.0f;
        out[n].rgb[1] = *(const uint8_t *)(rec + LR_RGB + 1) / 255.0f;
        out[n].rgb[2] = *(const uint8_t *)(rec + LR_RGB + 2) / 255.0f;
        out[n].flags = rd32(rec + LR_FLAGS);
        out[n].rec = (unsigned)rec;
        out[n].type = item_type(rd32(rec + LR_ITEM));
        out[n].baseRgb[0] = *(const uint8_t *)(rec + LR_BASE_RGB) / 255.0f;
        out[n].baseRgb[1] = *(const uint8_t *)(rec + LR_BASE_RGB + 1) / 255.0f;
        out[n].baseRgb[2] = *(const uint8_t *)(rec + LR_BASE_RGB + 2) / 255.0f;
        memcpy(&out[n].flickerDepth, (const void *)(rec + LR_FLICKER_DEPTH), 4);
        if (!(out[n].flickerDepth >= 0.0f && out[n].flickerDepth <= 1.0f)) out[n].flickerDepth = 0.0f;
        {
            float nd;
            memcpy(&nd, (const void *)(node + NODE_DIST), 4);
            out[n].stepped = (out[n].flags & 0x01u) && flickerOn && nd < flickerRange;
        }
        if (out[n].flags & 0x10u) {
            float sp, sn;
            u9light_range_scales(&sp, &sn);
            range *= (out[n].flags & 0x02u) ? sn : sp;
        }
        out[n].range = range;
        n++;
    }
    return n;
}

void u9light_range_scales(float *pos, float *neg)
{
    *pos = *neg = 1.0f;
    if (readable4(LIGHT_SYS + LS_SCALE_NEG)) {
        memcpy(pos, (const void *)(LIGHT_SYS + LS_SCALE_POS), 4);
        memcpy(neg, (const void *)(LIGHT_SYS + LS_SCALE_NEG), 4);
        if (!(*pos > 0.0f && *pos < 100.0f)) *pos = 1.0f;
        if (!(*neg > 0.0f && *neg < 100.0f)) *neg = 1.0f;
    }
}

unsigned u9light_ranged_switch(void)
{
    return readable4(LIGHT_SYS + LS_RANGED_ON) ? rd32(LIGHT_SYS + LS_RANGED_ON) : 1u;
}

void u9light_set_ranged_switch(unsigned v)
{
    if (readable4(LIGHT_SYS + LS_RANGED_ON))
        *(volatile uint32_t *)(LIGHT_SYS + LS_RANGED_ON) = v;
}

int u9light_ambient(int rgb[3])
{
    if (!readable4(LIGHT_SYS + LS_AMBIENT + 8)) return 0;
    rgb[0] = (int)rd32(LIGHT_SYS + LS_AMBIENT);
    rgb[1] = (int)rd32(LIGHT_SYS + LS_AMBIENT + 4);
    rgb[2] = (int)rd32(LIGHT_SYS + LS_AMBIENT + 8);
    return 1;
}

int u9light_book_open(void)
{
    uintptr_t g = readable4(GAME_OBJ) ? rd32(GAME_OBJ) : 0, cam;
    if (!g || !readable4(g + GAME_CAMERA)) return 0;
    cam = rd32(g + GAME_CAMERA);
    if (!cam || !readable4(cam + CAM_BOOK_SCENE)) return 0;
    return *(const uint8_t *)(cam + CAM_BOOK_SCENE) != 0;
}

int u9light_flicker_range(void)
{
    return readable4(LIGHT_SYS + LS_FLICKER_RANGE) ? (int)rd32(LIGHT_SYS + LS_FLICKER_RANGE) : 0;
}

void u9light_set_flicker_range(int v)
{
    if (readable4(LIGHT_SYS + LS_FLICKER_RANGE))
        *(volatile uint32_t *)(LIGHT_SYS + LS_FLICKER_RANGE) = (uint32_t)v;
}

void u9light_install(void)
{
    g_on = u9cfg_int("diag_light_watch", 0);
    if (!g_on) return;
    g_every = (DWORD)u9cfg_int("diag_light_watch_ms", 1000);
    if (g_every < 100) g_every = 100;
    g_t0 = u9time_now();
    g_next = g_t0;
    g_nextSummary = g_t0 + 5000;
    u9log_line("LIGHT watch on: the engine's light lists swept every %lu ms on the frame tick; a"
               " line as each light appears, moves, changes colour or goes, and every 30 s a"
               " summary with the %d lights nearest the camera", (unsigned long)g_every, NEAREST);
}

void u9light_report(void)
{
    if (!g_on) return;
    u9log_line("LIGHT totals: %u sweeps, %u new, %u gone, %u moved, %u colour changes, at most %u"
               " ranged lights, %u not tracked (table full), %u lines", g_sweep, g_new, g_gone,
               g_moved, g_recolour, g_maxRanged, g_full, g_lines);
}
