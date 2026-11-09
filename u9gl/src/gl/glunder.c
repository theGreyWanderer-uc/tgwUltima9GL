#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "../common/u9time.h"
#include "glpost.h"
#include "glshadow.h"
#include "glsun.h"
#include "glunder.h"
#include "../fix/fixmem.h"

#define WORLD_OBJ     0x00850228u
#define CM_UNDERWATER 0x6Cu
static int    g_engineFlag = 1;
static int    g_engineLast = -1;

static int engine_under(void)
{
    uintptr_t w = readable4(WORLD_OBJ) ? rd32(WORLD_OBJ) : 0, cm;
    if (!w || !readable4(w + 0xC0)) return -1;
    cm = rd32(w + 0xC0);
    if (!cm || !readable4(cm + CM_UNDERWATER)) return -1;
    return *(const volatile uint8_t *)(cm + CM_UNDERWATER) ? 1 : 0;
}

static int    g_on = 1;
static int    g_margin = 8;
static int    g_pad = 48;

static int    g_fog = 1;
static int    g_fogDensity = 80;
static int    g_fogDark = 100;
static int    g_fogReach = 6000;
static int    g_absorb[3] = { 300, 170, 80 };
static int    g_caus = 1;
static int    g_causStrength = 200;
static int    g_causSize = 300;
static int    g_causDepth = 600;
static int    g_causBlur = 300;
static int    g_shafts = 1;
static int    g_shaftStrength = 100;
static int    g_shaftSize = 250;
static int    g_shaftReach = 1500;
static DWORD  g_t0;
static int    g_glowFree = 1;
static int    g_glowHaze = 40;
static int    g_glowReach = 300;
static int    g_fogRgb[3] = { 10, 30, 70 };

static void triple(const char *s, int out[3], int lo, int hi)
{
    int t[3], k;
    if (!s || sscanf(s, "%d , %d , %d", &t[0], &t[1], &t[2]) != 3) return;
    for (k = 0; k < 3; k++) out[k] = t[k] < lo ? lo : t[k] > hi ? hi : t[k];
}

static float  g_waterZ;
static int    g_haveWater;
static int    g_overWater;
static DWORD  g_waterAt;
static unsigned g_waterTris;
static int    g_held;
static float  g_cam[3];
static int    g_haveCam;
static int    g_under;
static float  g_depth;
static DWORD  g_changedAt;
static unsigned g_changes, g_frames, g_underFrames;
static unsigned g_trisUnder, g_framesUnderWithWater;

#define GC_SIZE   32.0f
#define GC_SLOTS  65536
#define GC_PROBE  32
#define GC_NEAR   3000.0f
#define GC_TRI_MAX 4096
typedef struct {
    int   cx, cy;
    float z[2];
    int   n;
    DWORD seen;
} WCell;
static WCell g_gc[GC_SLOTS];

static void forget(void)
{
    memset(g_gc, 0, sizeof g_gc);
    g_held = 0;
    g_haveWater = 0;
    g_overWater = 0;
}

static unsigned cell_hash(int cx, int cy)
{
    return ((unsigned)cx * 73856093u) ^ ((unsigned)cy * 19349663u);
}

static const WCell *cell_find(int cx, int cy)
{
    unsigned h = cell_hash(cx, cy);
    int k;
    for (k = 0; k < GC_PROBE; k++) {
        const WCell *c = &g_gc[(h + (unsigned)k) % GC_SLOTS];
        if (!c->n) return NULL;
        if (c->cx == cx && c->cy == cy) return c;
    }
    return NULL;
}

static void cell_put(int cx, int cy, float z, DWORD now)
{
    unsigned h = cell_hash(cx, cy);
    int k, slot = -1, oldest = -1;
    for (k = 0; k < GC_PROBE; k++) {
        int s = (int)((h + (unsigned)k) % GC_SLOTS);
        WCell *c = &g_gc[s];
        if (!c->n) { slot = s; break; }
        if (c->cx == cx && c->cy == cy) {
            int i, same = -1;
            for (i = 0; i < c->n; i++) if (fabsf(c->z[i] - z) < 12.0f) same = i;
            if (same >= 0) c->z[same] = z;
            else if (c->n < 2) c->z[c->n++] = z;
            else c->z[fabsf(c->z[0] - z) < fabsf(c->z[1] - z) ? 0 : 1] = z;
            c->seen = now;
            return;
        }
        if (oldest < 0 || (now - c->seen) > (now - g_gc[oldest].seen)) oldest = s;
    }
    if (slot < 0) slot = oldest;
    else g_held++;
    g_gc[slot].cx = cx;
    g_gc[slot].cy = cy;
    g_gc[slot].z[0] = z;
    g_gc[slot].n = 1;
    g_gc[slot].seen = now;
}

static void remember(const float a[3], const float b[3], const float c[3], DWORD now)
{
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float d, mx, my, x0, y0, x1, y1;
    int cx0, cy0, cx1, cy1, cx, cy;
    if (nz * nz < 1.0f || nz * nz * 4.0f < nx * nx + ny * ny + nz * nz) return;
    mx = (a[0] + b[0] + c[0]) / 3.0f;
    my = (a[1] + b[1] + c[1]) / 3.0f;
    if ((mx - g_cam[0]) * (mx - g_cam[0]) + (my - g_cam[1]) * (my - g_cam[1]) > GC_NEAR * GC_NEAR) return;
    cell_put((int)floorf(mx / GC_SIZE), (int)floorf(my / GC_SIZE), (a[2] + b[2] + c[2]) / 3.0f, now);
    x0 = fminf(a[0], fminf(b[0], c[0])); x1 = fmaxf(a[0], fmaxf(b[0], c[0]));
    y0 = fminf(a[1], fminf(b[1], c[1])); y1 = fmaxf(a[1], fmaxf(b[1], c[1]));
    cx0 = (int)floorf(x0 / GC_SIZE); cx1 = (int)floorf(x1 / GC_SIZE);
    cy0 = (int)floorf(y0 / GC_SIZE); cy1 = (int)floorf(y1 / GC_SIZE);
    if ((long)(cx1 - cx0 + 1) * (long)(cy1 - cy0 + 1) > GC_TRI_MAX) return;
    d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if (fabsf(d) < 1e-3f) return;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            float x = ((float)cx + 0.5f) * GC_SIZE, y = ((float)cy + 0.5f) * GC_SIZE, u, v, w;
            u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d;
            v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d;
            w = 1.0f - u - v;
            if (u < -0.001f || v < -0.001f || w < -0.001f) continue;
            cell_put(cx, cy, u * a[2] + v * b[2] + w * c[2], now);
        }
}


typedef struct {
    U9FrameView v;
    unsigned tris;
    DWORD now;
} WCtx;

static int world_of(const U9FrameView *v, const float *f, float out[3])
{
    float q = f[6], w, cs[3];
    int k;
    if (q == 0.0f) return 0;
    w = 1.0f / q;
    cs[0] = (f[0] - v->cx) * w / v->sx;
    cs[1] = w;
    cs[2] = (v->cy - f[1]) * w / v->sy;
    for (k = 0; k < 3; k++)
        out[k] = v->m[k * 4] * cs[0] + v->m[k * 4 + 1] * cs[1] + v->m[k * 4 + 2] * cs[2] + v->m[k * 4 + 3];
    return 1;
}

static void water_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    WCtx *c = (WCtx *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned i;
    (void)glTex; (void)cutout;
    c->tris += count / 3;
    for (i = first; i + 2 < first + count; i += 3) {
        float a[3], b[3], d[3];
        if (!world_of(&c->v, (const float *)(base + (size_t)i * (size_t)stride), a) ||
            !world_of(&c->v, (const float *)(base + (size_t)(i + 1) * (size_t)stride), b) ||
            !world_of(&c->v, (const float *)(base + (size_t)(i + 2) * (size_t)stride), d)) continue;
        remember(a, b, d, c->now);
    }
}

void u9under_init(void)
{
    g_on     = u9cfg_int("gl_under_detect", 1);
    g_margin = u9cfg_int("gl_under_margin", 8);
    g_pad    = u9cfg_int("gl_under_pad", 48);
    g_engineFlag = u9cfg_int("gl_under_engine_flag", 1);
    if (g_margin < 0) g_margin = 0;
    if (g_pad < 0) g_pad = 0;
    g_fog        = u9cfg_int("gl_under_fog", 1);
    g_fogDensity = u9cfg_int("gl_under_fog_density", 80);
    g_fogDark    = u9cfg_int("gl_under_fog_dark", 100);
    g_fogReach   = u9cfg_int("gl_under_fog_reach", 6000);
    triple(u9cfg_str("gl_under_fog_absorb", "300,170,80"), g_absorb, 0, 1000);
    triple(u9cfg_str("gl_under_fog_colour", "10,30,70"), g_fogRgb, 0, 255);
    g_glowFree   = u9cfg_int("gl_under_glow_free", 1);
    g_glowHaze   = u9cfg_int("gl_under_glow_haze", 40);
    g_glowReach  = u9cfg_int("gl_under_glow_reach", 300);
    g_caus         = u9cfg_int("gl_under_caustics", 1);
    g_causStrength = u9cfg_int("gl_under_caustics_strength", 200);
    g_causSize     = u9cfg_int("gl_under_caustics_size", 300);
    g_causDepth    = u9cfg_int("gl_under_caustics_depth", 600);
    g_causBlur     = u9cfg_int("gl_under_caustics_blur", 300);
    g_shafts        = u9cfg_int("gl_under_shafts", 1);
    g_shaftStrength = u9cfg_int("gl_under_shafts_strength", 100);
    g_shaftSize     = u9cfg_int("gl_under_shafts_size", 250);
    g_shaftReach    = u9cfg_int("gl_under_shafts_reach", 1500);
    g_t0 = u9time_now();
    g_changedAt = g_waterAt = u9time_now();
}

static int water_over(const float cam[3], float *z, int *above, int *count)
{
    float lowAbove = 1e30f, highBelow = -1e30f, pad = (float)g_pad;
    int r = (int)ceilf(pad / GC_SIZE), ccx = (int)floorf(cam[0] / GC_SIZE), ccy = (int)floorf(cam[1] / GC_SIZE);
    int dx, dy, i, n = 0;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            const WCell *c;
            if (dx || dy) {
                float nxp = fmaxf(fabsf((float)dx) * GC_SIZE - GC_SIZE, 0.0f);
                float nyp = fmaxf(fabsf((float)dy) * GC_SIZE - GC_SIZE, 0.0f);
                if (nxp * nxp + nyp * nyp > pad * pad) continue;
            }
            if (!(c = cell_find(ccx + dx, ccy + dy))) continue;
            for (i = 0; i < c->n; i++) {
                float h = c->z[i];
                n++;
                if (h > cam[2]) { if (h < lowAbove) lowAbove = h; }
                else if (h > highBelow) highBelow = h;
            }
        }
    *count = n;
    if (!n) return 0;
    *above = lowAbove < 1e29f;
    *z = *above ? lowAbove : highBelow;
    return 1;
}

static void log_change(DWORD now, int over)
{
    U9Sun s;
    int sky = u9sun_read(&s);
    u9log_line("GLUNDER %s: camera at %.0f,%.0f,%.0f, water at z %.0f (%d heights over or under the camera,"
               " %d cells held, last drawn %u ms ago), %s %.0f; %u water triangles drawn this frame; underground flag %s;"
               " engine's underwater flag %s; after %u ms",
               g_under ? "UNDER the water" : "out of the water",
               (double)g_cam[0], (double)g_cam[1], (double)g_cam[2], (double)g_waterZ, over, g_held,
               (unsigned)(now - g_waterAt),
               g_under ? "depth" : "height over it", (double)(g_under ? g_depth : g_cam[2] - g_waterZ),
               g_waterTris, sky ? (s.underground ? "1" : "0") : "unread",
               g_engineLast < 0 ? (g_engineFlag ? "unread" : "not used") : g_engineLast ? "1" : "0",
               (unsigned)(now - g_changedAt));
}

void u9under_frame(void)
{
    WCtx *c;
    static WCtx ctx;
    DWORD now;
    int under = 0, above = 0, over = 0;
    float z = 0.0f;
    if (!g_on) { g_under = 0; g_depth = 0.0f; return; }
    c = &ctx;
    if (!u9gl_frame_view(&c->v) || !c->v.valid) return;
    now = u9time_now();
    g_frames++;

    {
        float cam[3];
        cam[0] = c->v.m[3]; cam[1] = c->v.m[7]; cam[2] = c->v.m[11];
        if (g_haveCam) {
            float dx = cam[0] - g_cam[0], dy = cam[1] - g_cam[1], dz = cam[2] - g_cam[2];
            if (dx * dx + dy * dy + dz * dz > 3000.0f * 3000.0f) forget();
        }
        memcpy(g_cam, cam, sizeof g_cam);
        g_haveCam = 1;
    }

    c->tris = 0;
    c->now = now;
    u9gl_runs_with_texture(U9TEX_WATER, water_run, c);
    g_waterTris = c->tris;
    if (c->tris) g_waterAt = now;
    g_haveWater = g_held > 0;

    g_overWater = g_haveWater && water_over(g_cam, &z, &above, &over);
    if (g_overWater) {
        g_waterZ = z;
        if (above) {
            float below = z - g_cam[2];
            under = g_under ? below > 0.0f : below > (float)g_margin;
        }
    }
    {
        int e = g_engineFlag ? engine_under() : -1;
        g_engineLast = e;
        if (e >= 0) {
            under = e;
            if (under && !g_overWater) z = g_waterZ;
            if (under && z <= g_cam[2]) under = 0;
        }
    }
    g_depth = under ? z - g_cam[2] : 0.0f;
    if (under) {
        g_underFrames++;
        g_trisUnder += g_waterTris;
        if (g_waterTris) g_framesUnderWithWater++;
    }
    if (under != g_under) {
        g_under = under;
        g_changes++;
        log_change(now, over);
        g_changedAt = now;
    }
}

void u9under_fog(U9UnderFog *o)
{
    int k, sky;
    U9Sun s;
    memset(o, 0, sizeof *o);
    o->on = g_on && g_under && (g_fog || g_caus || g_shafts);
    if (!o->on) return;
    o->glowFree = g_glowFree ? 1.0f : 0.0f;
    o->glowHaze = (float)(g_glowHaze < 0 ? 0 : g_glowHaze > 100 ? 100 : g_glowHaze) / 100.0f;
    if (!g_glowFree) o->glowHaze = 1.0f;
    o->glowReach = (float)(g_glowReach < 1 ? 1 : g_glowReach);
    if (!g_glowFree) o->glowReach = 1.0e9f;
    o->surface = g_waterZ;
    if (g_fog) {
        o->density = (float)(g_fogDensity < 0 ? 0 : g_fogDensity) / 100000.0f;
        o->dark = o->density * (float)(g_fogDark < 0 ? 0 : g_fogDark) / 100.0f;
    }
    {
        o->time = (float)(u9time_now() - g_t0) / 1000.0f;
        if (o->time > 3600.0f) {
            g_t0 += 3600000u;
            o->time -= 3600.0f;
        }
    }
    sky = u9sun_read(&s);
    if (g_caus && g_causStrength > 0 && !(sky && s.underground)) {
        o->caustic = (float)g_causStrength / 100.0f;
        o->cscale = 1.0f / (float)(g_causSize < 4 ? 4 : g_causSize);
        o->cdepth = (float)(g_causDepth < 10 ? 10 : g_causDepth);
        o->cblur = (float)(g_causBlur < 0 ? 0 : g_causBlur > 1000 ? 1000 : g_causBlur) / 100.0f;
    }
    if (g_shafts && g_shaftStrength > 0 && sky && !s.underground && !s.moonMap && s.elevation > 2.0f) {
        float e = (s.elevation - 2.0f) / 13.0f, h, st = s.storm < 0.0f ? 0.0f : s.storm > 1.0f ? 1.0f : s.storm;
        float dx = s.dir[0], dy = s.dir[1], dz = s.dir[U9SUN_UP];
        e = e > 1.0f ? 1.0f : e;
        e = e * e * (3.0f - 2.0f * e);
        o->shaft = (float)g_shaftStrength / 100.0f * e * (1.0f - 0.8f * st);
        o->sscale = 1.0f / (float)(g_shaftSize < 8 ? 8 : g_shaftSize);
        o->sreach = (float)(g_shaftReach < 100 ? 100 : g_shaftReach);
        h = sqrtf(dx * dx + dy * dy);
        if (h > 1e-4f) {
            float l = sqrtf(dx * dx + dy * dy + dz * dz), sw = h / l / 1.33f;
            o->sdir[0] = dx / h * sw;
            o->sdir[1] = dy / h * sw;
            o->sdir[2] = sqrtf(1.0f - sw * sw);
        } else {
            o->sdir[2] = 1.0f;
        }
    }
    o->reach = (float)(g_fogReach < 100 ? 100 : g_fogReach);
    for (k = 0; k < 3; k++) {
        o->absorb[k] = (float)g_absorb[k] / 100.0f;
        o->rgb[k] = (float)g_fogRgb[k] / 255.0f;
    }
}

int u9under_state(float *depth)
{
    if (depth) *depth = g_under ? g_depth : 0.0f;
    return g_under;
}

void u9under_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    DWORD now = u9time_now();
    while (*v == ' ') v++;
    if (!strcmp(v, "on")) g_on = 1;
    else if (!strcmp(v, "off")) { g_on = 0; g_under = 0; g_depth = 0.0f; }
    else if (!strncmp(v, "margin ", 7)) g_margin = atoi(v + 7) < 0 ? 0 : atoi(v + 7);
    else if (!strncmp(v, "pad ", 4)) g_pad = atoi(v + 4) < 0 ? 0 : atoi(v + 4);
    else if (!strcmp(v, "forget")) forget();
    else if (!strcmp(v, "fog on")) g_fog = 1;
    else if (!strcmp(v, "fog off")) g_fog = 0;
    else if (!strncmp(v, "fog density ", 12)) g_fogDensity = atoi(v + 12);
    else if (!strncmp(v, "fog dark ", 9)) g_fogDark = atoi(v + 9);
    else if (!strncmp(v, "fog reach ", 10)) g_fogReach = atoi(v + 10);
    else if (!strncmp(v, "fog absorb ", 11)) triple(v + 11, g_absorb, 0, 1000);
    else if (!strncmp(v, "fog colour ", 11)) triple(v + 11, g_fogRgb, 0, 255);
    else if (!strcmp(v, "glows on")) g_glowFree = 1;
    else if (!strcmp(v, "glows off")) g_glowFree = 0;
    else if (!strncmp(v, "glows haze ", 11)) g_glowHaze = atoi(v + 11);
    else if (!strncmp(v, "glows reach ", 12)) g_glowReach = atoi(v + 12);
    else if (!strcmp(v, "caustics on")) g_caus = 1;
    else if (!strcmp(v, "caustics off")) g_caus = 0;
    else if (!strncmp(v, "caustics strength ", 18)) g_causStrength = atoi(v + 18);
    else if (!strncmp(v, "caustics size ", 14)) g_causSize = atoi(v + 14);
    else if (!strncmp(v, "caustics depth ", 15)) g_causDepth = atoi(v + 15);
    else if (!strncmp(v, "caustics blur ", 14)) g_causBlur = atoi(v + 14);
    else if (!strcmp(v, "shafts on")) g_shafts = 1;
    else if (!strcmp(v, "shafts off")) g_shafts = 0;
    else if (!strncmp(v, "shafts strength ", 16)) g_shaftStrength = atoi(v + 16);
    else if (!strncmp(v, "shafts size ", 12)) g_shaftSize = atoi(v + 12);
    else if (!strncmp(v, "shafts reach ", 13)) g_shaftReach = atoi(v + 13);
    else if (strcmp(v, "fog") && strcmp(v, "caustics") && strcmp(v, "shafts")) {
        if (*v) {
            out("water [on|off | margin <units> | pad <units> | forget]   is the camera under the water");
            out("water fog [on|off | density <%% of 1 per 1000> | dark <%% of density> | reach <units>"
                " | absorb <r,g,b %%> | colour <r,g,b 0-255>]   the haze under it");
            out("water caustics [on|off | strength <%%> | size <units> | depth <units> | blur <%%>]   the light the ripples focus");
            out("water shafts [on|off | strength <%%> | size <units> | reach <units>]   sunlight slanting down through the water");
            out("water glows on|off | haze <percent>   lights and glows keep their own brightness at depth (on), or darken with it as lit surfaces do (off); haze: how much of the distance haze a glow takes");
            return;
        }
    }
    if (*v) u9log_line("GLUNDER '%s' - detect %s, margin %d, pad %d; fog %s, density %d, dark %d, reach %d,"
                       " absorb %d,%d,%d, colour %d,%d,%d", v, g_on ? "on" : "off", g_margin, g_pad,
                       g_fog ? "on" : "off", g_fogDensity, g_fogDark, g_fogReach,
                       g_absorb[0], g_absorb[1], g_absorb[2], g_fogRgb[0], g_fogRgb[1], g_fogRgb[2]);
    if (!strncmp(v, "shafts", 6)) {
        U9UnderFog f;
        u9under_fog(&f);
        u9log_line("GLUNDER shafts %s, strength %d%%, size %d, reach %d; now %.2f", g_shafts ? "on" : "off",
                   g_shaftStrength, g_shaftSize, g_shaftReach, (double)f.shaft);
        out("light shafts %s%s: strength %d%%, ripples %d units across, marched %d; now at %.0f%% (the sun's height,"
            " a storm)", g_shafts ? "on" : "off (water shafts on)", g_under ? ", drawing now" : ", not under the water",
            g_shaftStrength, g_shaftSize, g_shaftReach, (double)(f.shaft * 100.0f));
        return;
    }
    if (!strncmp(v, "caustics", 8)) {
        u9log_line("GLUNDER caustics %s, strength %d%%, size %d, depth %d, blur %d%%", g_caus ? "on" : "off",
                   g_causStrength, g_causSize, g_causDepth, g_causBlur);
        out("caustics %s%s: strength %d%%, cells %d units, fading to a third %d under the surface, blur %d%%",
            g_caus ? "on" : "off (water caustics on)", g_under ? ", drawing now" : ", not under the water",
            g_causStrength, g_causSize, g_causDepth, g_causBlur);
        return;
    }
    if (!strncmp(v, "fog", 3)) {
        out("under-water fog %s%s: density %d%% of 1 per 1000 units, dark %d%%, reach %d, absorb %d,%d,%d%%,"
            " colour %d,%d,%d", g_fog ? "on" : "off (water fog on)", g_under ? ", drawing now" : ", not under the water",
            g_fogDensity, g_fogDark, g_fogReach, g_absorb[0], g_absorb[1], g_absorb[2],
            g_fogRgb[0], g_fogRgb[1], g_fogRgb[2]);
        out("glows %s; they take %d%% of the distance haze (water glows haze <percent>), as if at most %d away"
            " (water glows reach <units>)",
            g_glowFree ? "keep their own light at depth (water glows off to compare)"
                       : "darken with depth as lit surfaces (water glows on to restore)", g_glowHaze, g_glowReach);
        return;
    }
    if (!g_on) { out("underwater detection off (water on)"); return; }
    if (!g_haveWater) {
        out("no water seen since the last jump - camera at z %.0f", (double)g_cam[2]);
        return;
    }
    if (!g_overWater) {
        out("out of the water: no remembered water over or under the camera (within %d); %d cells held,"
            " water last drawn %u ms ago, %u triangles this frame", g_pad, g_held, (unsigned)(now - g_waterAt),
            g_waterTris);
    } else {
        out("%s: camera z %.0f, water z %.0f, %s %.0f; water last drawn %u ms ago, %u triangles this frame",
            g_under ? "UNDER the water" : "out of the water", (double)g_cam[2], (double)g_waterZ,
            g_under ? "depth" : "height over it", (double)(g_under ? g_depth : g_cam[2] - g_waterZ),
            (unsigned)(now - g_waterAt), g_waterTris);
    }
    out("%d grid cells hold water, of %d; pad %d, margin %d; %u changes; %u of %u frames under, %u of them drew water"
        " (%u triangles)", g_held, GC_SLOTS, g_pad, g_margin,
        g_changes, g_underFrames, g_frames, g_framesUnderWithWater, g_trisUnder);
}
