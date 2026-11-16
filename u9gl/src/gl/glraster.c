#include <windows.h>
#include <stdlib.h>
#include <math.h>
#include <GL/gl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "glpost.h"
#include "gltwosided.h"
#include "glaa.h"
#include "glrays.h"
#include "glshadow.h"
#include "gleffects.h"
#include "glfog.h"
#include "glbeam.h"
#include "glshot.h"
#include "metrics.h"

#include "u9renderer.h"
#include "../../addresses.h"
#include "../diag/log.h"
#include "../common/u9time.h"
#include "../injection/config.h"
#include "../texture/texoverride.h"

int u9gl_texture_ident(unsigned glTex, int *id, int *id2, unsigned *fmt,
                       int *w, int *h);
int u9gl_magenta_keyed(int texId);
unsigned u9gl_texture_for(const void *renderer, const void *mat, int id,
                          int id2, unsigned fmt, unsigned key);
unsigned u9gl_format_for_material(const void *mat);
void u9gl_prefetch(const void *renderer, const void *mat);
void u9gl_upload_pending(void);
int  u9gl_mips_enabled(void);
int  u9gl_texture_look(unsigned tex, unsigned *rgb, unsigned *opaquePct);
GLint u9gl_min_filter(void);
float u9gl_anisotropy(void);
void u9gl_mip_report(void);
void u9gl_source_report(int force);
#define U9GL_NO_KEY 0xFFFFFFFFu

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
void     u9gl_texture_stats(unsigned *uploads, unsigned *failed, unsigned *cached);

#define U9GL_MAX_VERTS  (64 * 1024 * 3)
#define U9GL_MAX_RUNS   4096

#define U9GL_Z_REF      1.0f

typedef struct {
    float x, y, z;
    float sq, tq, rq, q;
    float fog;
    unsigned char rgba[4];
} GlVert;

typedef struct {
    unsigned first, count;
    int      depthWrite;
    const void *mat;
    int      texId;
    int      texId2;
    unsigned glTex;
    float    x0, y0, x1, y1;
    int      src;
    unsigned texFmt;
    unsigned flags;
    unsigned chromaKey;
    int      blendAdd;
    int      alphaTest;
    int      chroma;
    int      clampS, clampT;
    int      pointFilter;
    int      deferred;
    float    zMin;
    unsigned aSum, aN;
    unsigned aRawMin, aRawMax;
    unsigned epoch;
    unsigned drawPos;
    int      depthTest;
    int      modulate;
    int      book;
    float    bloomShare;
    const char *bloomWhy;
    int      anim;
} GlRun;

#define U9GL_BF_VERTS   (64 * 1024 * 3)
#define U9GL_BF_RUNS    2048
static GlVert   g_verts[U9GL_MAX_VERTS + U9GL_BF_VERTS];
static unsigned g_count;
static unsigned g_dropped;

static GlRun    g_runs[U9GL_MAX_RUNS];
static unsigned g_runCount;
static GlRun    g_bfRuns[U9GL_BF_RUNS], g_lastBfRuns[U9GL_BF_RUNS];
static unsigned g_bfRunCount, g_lastBfRunCount, g_bfCount, g_bfDropped, g_lastBfTris;
static unsigned g_bfPolys;
static volatile unsigned char g_u9BackFace;
static volatile unsigned char g_u9Anim;
static int      g_waterCover = -1;
static unsigned g_waterCovered;
void u9gl_water_cover_command(const char *v, void (*out)(const char *fmt, ...))
{
    if (g_waterCover < 0) g_waterCover = u9cfg_int("gl_water_cover", 1);
    if (v && !strcmp(v, "on")) g_waterCover = 1;
    else if (v && !strcmp(v, "off")) g_waterCover = 0;
    if (v && *v) u9log_line("GLRAST water cover %s", g_waterCover ? "on" : "off");
    out("translucent layers in front of the water hide its reflection by their opacity: %s; %u runs last frame",
        g_waterCover ? "on" : "off", g_waterCovered);
}
volatile unsigned char *u9gl_anim_flag(void) { return &g_u9Anim; }
static int g_casterAnim;
int u9gl_caster_anim(void) { return g_casterAnim; }
void u9gl_anim_draw_frame_end(void);
volatile unsigned char *u9gl_back_face_flag(void) { return &g_u9BackFace; }
int u9gl_caster_drawn(unsigned first) { return first < U9GL_MAX_VERTS; }
void u9gl_back_face_stats(unsigned *tris, unsigned *runs, unsigned *dropped, unsigned *polys)
{
    *tris = g_lastBfTris;
    *runs = g_lastBfRunCount;
    *dropped = g_bfDropped;
    *polys = g_bfPolys;
}
static int      g_depthWrite = 1;
static int      g_depthTest  = 1;
static int      g_followDepthTest;

static void run_begin(void);

static GLenum g_depthFunc = GL_LEQUAL;

void u9gl_set_depth_func(unsigned glideFunc)
{
    static const GLenum MAP[8] = {
        GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
        GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS
    };
    if (glideFunc < 8) g_depthFunc = MAP[glideFunc];
}

void u9gl_set_depth_test(int enable)
{
    enable = enable ? 1 : 0;
    if (enable == g_depthTest) return;
    g_depthTest = enable;
    run_begin();
}
static const void *g_mat;
static int16_t     g_matId = -1, g_matId2 = -1;
static unsigned    g_matFlags;
static unsigned    g_matRecycled;
static const void *g_renderer;

static unsigned g_epoch;

void u9gl_flush_epoch(void) { g_epoch++; }
static int g_src;

void u9gl_source(int slot) { g_src = slot; }

static int g_listPass;

void u9gl_list_pool_pass(int on)
{
    on = on ? 1 : 0;
    if (on == g_listPass) return;
    g_listPass = on;
    run_begin();
}
static int g_surfW, g_surfH;
static int g_surfScale = 1;

void u9gl_set_surface(int w, int h, int scale)
{
    g_surfW = w;
    g_surfH = h;
    g_surfScale = scale > 1 ? scale : 1;
}

static int g_vpX, g_vpY, g_vpW, g_vpH;
int u9gl_last_viewport(int *x, int *y, int *w, int *h)
{
    if (g_vpW <= 0 || g_vpH <= 0) return 0;
    *x = g_vpX / g_surfScale; *y = g_vpY / g_surfScale;
    *w = g_vpW / g_surfScale; *h = g_vpH / g_surfScale;
    return 1;
}

void u9gl_picture_box(unsigned engineW, unsigned engineH, int surfW, int surfH,
                      int *x, int *y, int *w, int *h)
{
    int vw = surfW, vh = surfH, vx = 0, vy = 0;

    if (!u9cfg_int("gl_stretch", 0) &&
        engineW && engineH && surfW > 0 && surfH > 0) {
        double want = (double)engineW / (double)engineH;
        double have = (double)surfW   / (double)surfH;
        if (have > want) {
            vw = (int)((double)surfH * want + 0.5);
            vx = (surfW - vw) / 2;
        } else if (have < want) {
            vh = (int)((double)surfW / want + 0.5);
            vy = (surfH - vh) / 2;
        }
    }
    *x = vx; *y = vy; *w = vw; *h = vh;
}

static int g_cfgRead;
static int g_useClamp, g_useFog, g_useTex, g_useDepth, g_useBlend, g_useChroma;
static int g_useFogTable;
static int g_labelTex;
static int g_labelMode;
static int g_labelMinArea = 900;
static int g_useGeom;
static int g_forceFormat;
static int g_pointMode = 1;

void u9gl_set_point_mode(int m) { g_pointMode = m; }
int  u9gl_point_mode(void)      { return g_pointMode; }

static float g_zMin =  3.0e38f;
static float g_zMax = -3.0e38f;

static unsigned g_lastTris, g_lastRunCount;
static unsigned g_lastColN, g_lastColSum, g_lastColMax, g_lastAlphaSum, g_lastClipped;
static GlRun    g_lastRuns[U9GL_MAX_RUNS];

static unsigned g_lastAddTris, g_lastAlphaTestTris;
static int      g_wasAdditive;
static unsigned g_frameNo;

static unsigned g_stateChanges;

#define U9GL_MAX_SEEN 64
static unsigned g_seen[U9GL_MAX_SEEN];
static unsigned g_seenCount;

static void account_frame(void);

static void read_switches(void)
{
    if (g_cfgRead) return;
    g_cfgRead     = 1;
    g_useDepth    = u9cfg_int("depth",     1);
    g_useBlend    = u9cfg_int("blend",     1);
    g_useTex      = u9cfg_int("textures",  1);
    g_useGeom     = u9cfg_int("geometry",  1);
    g_useClamp    = u9cfg_int("clamp",     1);
    g_useFog      = u9cfg_int("fog",       1);
    g_useFogTable = u9cfg_int("fog_table", 1);
    g_useChroma   = u9cfg_int("chromakey", 1);
    g_forceFormat = u9cfg_int("force_format", 0);
    g_pointMode   = u9cfg_int("point_filter", 1);
    g_followDepthTest = u9cfg_int("depth_test_follow", 0);
    u9log_line("GLRAST depth=%d blend=%d textures=%d clamp=%d fog=%d "
               "chromakey=%d force_format=0x%02X point_filter=%d",
               g_useDepth, g_useBlend, g_useTex, g_useClamp, g_useFog,
               g_useChroma, g_forceFormat, g_pointMode);
}

static unsigned char to_byte(float v)
{
    int i = (int)(v + 0.5f);
    if (i < 0)   i = 0;
    if (i > 255) i = 255;
    return (unsigned char)i;
}

static int   g_depthMode = -1;
static float g_depthFar  = 0.0f;
static float g_engineFar = 0.0f;

static float bucket_scale(void)
{
    if (g_renderer) {
        float s = *(const float *)((const char *)g_renderer + U9_OFF_BUCKET_SCALE);
        if (s > 0.0f && s < 1.0f) return s;
    }
    return (g_depthFar > 16.0f) ? 31.0f / g_depthFar : 0.0f;
}

static void sample_engine_far(void)
{
    static float lastScale;
    static unsigned said;
    float s, farZ;
    if (!g_renderer) return;
    s = *(const float *)((const char *)g_renderer + U9_OFF_BUCKET_SCALE);
    if (s <= 0.0f || s >= 1.0f) return;
    farZ = 31.0f / s;
    if (farZ < 16.0f) return;
    g_engineFar = farZ;
    if (s == lastScale) return;
    if (said < 8u)
        u9log_line("GLDEPTH the engine's far distance is %.0f (scale %.8f at "
                   "+0x%X); depth_far = %.0f is the fallback%s",
                   farZ, s, U9_OFF_BUCKET_SCALE, g_depthFar,
                   lastScale == 0.0f ? "" : ", and it has changed");
    else if (said == 8u)
        u9log_line("GLDEPTH the far distance keeps changing; no more lines");
    said++;
    lastScale = s;
}

static float active_far(void)
{
    return (g_engineFar > 16.0f) ? g_engineFar : g_depthFar;
}

static float depth_of(float z)
{
    float d;
    if (g_depthMode < 0) {
        g_depthMode = u9cfg_int("depth_mode", 2);
        if (g_depthMode < 0 || g_depthMode > 2) g_depthMode = 2;
        g_depthFar  = (float)u9cfg_int("depth_far", 8192);
        if (g_depthFar < 16.0f) g_depthFar = 8192.0f;
    }
    if (g_depthMode) {
        d = (z - U9GL_Z_REF) / (active_far() - U9GL_Z_REF);
    } else {
        if (z <= U9GL_Z_REF) d = 0.0f;
        else                 d = 1.0f - (U9GL_Z_REF / z);
    }
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    return 1.0f - 2.0f * d;
}

const char *u9gl_toggle_tex_labels(void)
{
    g_labelMode = (g_labelMode + 1) % 4;
    g_labelTex = g_labelMode == 1;
    g_labelMinArea = u9cfg_int("label_min_area", 900);
    return g_labelMode == 1 ? "texture labels - id/frame drawn over each run"
         : g_labelMode == 2 ? "light markers - a yellow square on each light's point"
         : g_labelMode == 3 ? "water zones - a blue box round each breathable space; deep water outside"
                            : "off";
}

int u9gl_tex_labels_on(void) { return g_labelTex; }
int u9gl_label_mode(void) { return g_labelMode; }

const char *u9gl_depth_mode_name(void)
{
    if (g_depthMode < 0) (void)depth_of(1.0f);
    return g_depthMode == 2 ? "w-buffer" : g_depthMode ? "linear-w" : "1/z";
}

int u9gl_depth_mode(void)
{
    if (g_depthMode < 0) (void)depth_of(1.0f);
    return g_depthMode;
}

const char *u9gl_cycle_depth_mode(void)
{
    if (g_depthMode < 0) (void)depth_of(1.0f);
    g_depthMode = (g_depthMode + 1) % 3;
    return g_depthMode == 2 ? "W-BUFFER - linear w per pixel, as slot 43 asks"
         : g_depthMode      ? "LINEAR W per vertex - wrong inside large triangles"
                            : "RECIPROCAL 1/z - right inside triangles, coarse far away";
}

#define U9GL_CENSUS_MAX 64
typedef struct {
    int      slot, texId, texId2;
    unsigned flags, nodes, verts, aMin, aMax, rgbMax;
    float    zMin, zMax, x0, y0, x1, y1;
} FlushCensus;
static FlushCensus g_census[U9GL_CENSUS_MAX], g_lastCensus[U9GL_CENSUS_MAX];
static unsigned    g_censusCount, g_censusLost, g_lastCensusCount, g_lastCensusLost;

void u9gl_flush_census(const void *renderer, int slot, unsigned count,
                       const void *verts, const void *mat)
{
    const float *f = (const float *)verts;
    FlushCensus *c = NULL;
    int      texId = -1, texId2 = -1;
    unsigned flags = 0, i, k;
    float    wScale = 0.0f;

    if (!verts || !count) return;
    if (mat && slot != 37) {
        texId  = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID);
        texId2 = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID2);
        flags  = *(const unsigned *)((const char *)mat + U9_MAT_FLAGS);
    }
    for (k = 0; k < g_censusCount; k++)
        if (g_census[k].slot == slot && g_census[k].texId == texId &&
            g_census[k].texId2 == texId2 && g_census[k].flags == flags) {
            c = &g_census[k];
            break;
        }
    if (!c) {
        if (g_censusCount >= U9GL_CENSUS_MAX) { g_censusLost++; return; }
        c = &g_census[g_censusCount++];
        c->slot = slot; c->texId = texId; c->texId2 = texId2; c->flags = flags;
        c->nodes = c->verts = c->aMax = c->rgbMax = 0u;
        c->aMin = 255u;
        c->zMin = c->x0 = c->y0 =  1.0e30f;
        c->zMax = c->x1 = c->y1 = -1.0e30f;
    }
    if (renderer)
        wScale = *(const float *)((const char *)renderer + U9_OFF_WSCALE);
    c->nodes++;
    c->verts += count;
    for (i = 0; i < count; i++) {
        const float *v = f + i * GRV_FLOATS;
        unsigned a = to_byte(v[GRV_A]);
        float z  = (v[GRV_OOW] != 0.0f && wScale != 0.0f) ? wScale / v[GRV_OOW]
                                                          : v[GRV_Z];
        float mx = v[GRV_R] > v[GRV_G] ? (v[GRV_R] > v[GRV_B] ? v[GRV_R] : v[GRV_B])
                                       : (v[GRV_G] > v[GRV_B] ? v[GRV_G] : v[GRV_B]);
        if (a < c->aMin) c->aMin = a;
        if (a > c->aMax) c->aMax = a;
        if (to_byte(mx) > c->rgbMax) c->rgbMax = to_byte(mx);
        if (z < c->zMin) c->zMin = z;
        if (z > c->zMax) c->zMax = z;
        if (v[GRV_X] < c->x0) c->x0 = v[GRV_X];
        if (v[GRV_Y] < c->y0) c->y0 = v[GRV_Y];
        if (v[GRV_X] > c->x1) c->x1 = v[GRV_X];
        if (v[GRV_Y] > c->y1) c->y1 = v[GRV_Y];
    }
}

static void run_fill(GlRun *r)
{
    r->anim       = g_u9Anim;
    r->depthWrite = g_depthWrite;
    r->depthTest  = g_depthTest;
    r->mat        = g_mat;
    r->glTex = 0;
    if (g_mat) {
        r->texId2    = *(const int16_t  *)((const char *)g_mat + U9_MAT_TEXID2);
        r->texId     = *(const int16_t  *)((const char *)g_mat + U9_MAT_TEXID);
        r->flags     = *(const unsigned *)((const char *)g_mat + U9_MAT_FLAGS);
        r->chromaKey = *(const unsigned *)((const char *)g_mat + U9_MAT_CHROMAKEY);

        if (g_renderer && u9cfg_int("own_renderer_life", 0))
            u9gl_prefetch(g_renderer, g_mat);
    } else {
        r->texId = r->texId2 = -1;
        r->flags = r->chromaKey = 0;
    }
    r->texFmt = g_forceFormat ? (unsigned)g_forceFormat
                              : u9gl_format_for_material(g_mat);
    r->blendAdd  = (r->flags & U9_MATF_ADDITIVE) ? 1 : 0;
    r->alphaTest = ((r->flags & U9_MATF_FMT_SELECT) &&
                    (r->flags & U9_MATF_ALPHATEST_B)) ? 1 : 0;
    r->chroma    = ((r->flags & U9_MATF_CHROMA_MASK) == U9_MATF_CHROMA_ON) ? 1 : 0;
    if (u9gl_magenta_keyed(r->texId) && !(r->flags & U9_MATF_TRANSLUCENT)) r->alphaTest = 1;
    r->clampS    = (r->flags & U9_MATF_CLAMP_S) ? 1 : 0;
    r->clampT    = (r->flags & U9_MATF_CLAMP_T) ? 1 : 0;
    {
        int mgr38 = 0;
        if (g_renderer) {
            const char *mgr = *(const char *const *)
                              ((const char *)g_renderer + U9_OFF_TEXMGR);
            if (mgr) mgr38 = *(const unsigned char *)(mgr + 0x38);
        }
    r->deferred = 0;
    if (g_renderer) {

        unsigned prim = *(const unsigned *)((const char *)g_renderer +
                                            U9_OFF_PRIM_FLAGS);
        unsigned mode = *(const unsigned *)((const char *)g_renderer + 0x40);
        r->deferred = (mode == 0 && (prim & 0x0E) && !(prim & 0x40)) ? 1 : 0;
    }
    {
        r->pointFilter = (!(r->flags & U9_MAT_POINT_FILTER) &&
                          (((r->flags & 0x0C) != 4) || mgr38)) ? 0 : 1;
    }
    }
    r->modulate  = (r->texId >= 0) ? 1 : 0;
    if (g_listPass) {
        r->blendAdd  = 1;
        r->alphaTest = 0;
        r->chroma    = 0;
        r->modulate  = 0;
        r->deferred  = 2;
    }
}

#define U9GL_MAX_MATS 24
static const void *g_matSeen[U9GL_MAX_MATS];
static unsigned    g_matCount;

static void material_census(const GlRun *r)
{
    unsigned i;
    for (i = 0; i < g_matCount; i++)
        if (g_matSeen[i] == r->mat) return;
    if (g_matCount >= U9GL_MAX_MATS) return;
    g_matSeen[g_matCount++] = r->mat;

    u9log_line("GLMAT  mat=0x%08X tex=%d flags=0x%08X key=0x%08X -> "
               "blend=%s alphaTest=%s chromakey=%s combine=%s",
               (unsigned)(uintptr_t)r->mat, r->texId, r->flags, r->chromaKey,
               r->blendAdd  ? "ADDITIVE" : "alpha",
               r->alphaTest ? "on"  : "off",
               r->chroma    ? "on"  : "off",
               r->modulate  ? "modulate" : "flat");
}

static int depth_bucket(float zMin)
{
    float s = bucket_scale();
    int b;
    if (s <= 0.0f) return 0;
    b = (int)(zMin * s);
    if (b < 0)  b = 0;
    if (b > 31) b = 31;
    return b;
}

static unsigned run_solidity(const GlRun *r)
{
    return r->aRawMax;
}

static int g_sortEffectDepth = -1, g_sortEffectDepthHit;

static int pooled_before(const GlRun *a, const GlRun *b)
{
    int ba = depth_bucket(a->zMin), bb = depth_bucket(b->zMin);
    if (ba != bb) return ba > bb;
    if (run_solidity(a) != run_solidity(b)) {
        if (g_sortEffectDepth < 0) g_sortEffectDepth = u9cfg_int("gl_sort_effect_depth", 1);
        if (!g_sortEffectDepth || (a->texFmt != 0x02 && b->texFmt != 0x02))
            return run_solidity(a) > run_solidity(b);
        g_sortEffectDepthHit = 1;
    }
    if (a->zMin != b->zMin) return a->zMin > b->zMin;
    return 0;
}

static void pooled_sort(unsigned *order, unsigned n)
{
    unsigned a;
    for (a = 1; a < n; a++) {
        unsigned key = order[a], j = a;
        while (j > 0 && pooled_before(&g_runs[key], &g_runs[order[j - 1]])) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
}

#define SL_MAX 32
static int g_slTex[SL_MAX], g_slGroup[SL_MAX], g_slRank[SL_MAX], g_slN = -1, g_slOn = 1;
static unsigned g_slMoved, g_slMovedLast;

static void sort_layers_read(void)
{
    const char *s = u9cfg_str("gl_sort_layers", "49,5869,5870,6396,6432");
    int group = 0, rank = 0;
    g_slOn = u9cfg_int("gl_sort_layers_on", 1);
    g_slN = 0;
    while (s && *s && g_slN < SL_MAX) {
        char *e;
        long v;
        if (*s == ';') { group++; rank = 0; s++; continue; }
        v = strtol(s, &e, 10);
        if (e == s) { s++; continue; }
        g_slTex[g_slN] = (int)v;
        g_slGroup[g_slN] = group;
        g_slRank[g_slN] = rank++;
        g_slN++;
        s = e;
    }
}

static int sort_layer_of(int texId, int *group)
{
    int i;
    for (i = 0; i < g_slN; i++)
        if (g_slTex[i] == texId) { *group = g_slGroup[i]; return g_slRank[i]; }
    return -1;
}

static void sort_layers_apply(unsigned *order, unsigned n)
{
    static unsigned at[U9GL_MAX_RUNS], run[U9GL_MAX_RUNS];
    static int rank[U9GL_MAX_RUNS];
    unsigned a, b, lo, hi, k, m;
    int g, grp, maxGroup = -1;
    if (g_slN < 0) sort_layers_read();
    if (!g_slOn || !g_slN) return;
    for (k = 0; k < (unsigned)g_slN; k++) if (g_slGroup[k] > maxGroup) maxGroup = g_slGroup[k];
    for (lo = 0; lo < n; lo = hi) {
        int bk = depth_bucket(g_runs[order[lo]].zMin);
        for (hi = lo + 1; hi < n && depth_bucket(g_runs[order[hi]].zMin) == bk; hi++) {}
        for (g = 0; g <= maxGroup; g++) {
            m = 0;
            for (a = lo; a < hi; a++) {
                int r = sort_layer_of(g_runs[order[a]].texId, &grp);
                if (r < 0 || grp != g) continue;
                at[m] = a;
                run[m] = order[a];
                rank[m] = r;
                m++;
            }
            if (m < 2) continue;
            for (a = 1; a < m; a++) {
                unsigned rr = run[a];
                int kk = rank[a];
                b = a;
                while (b > 0 && rank[b - 1] > kk) { run[b] = run[b - 1]; rank[b] = rank[b - 1]; b--; }
                run[b] = rr;
                rank[b] = kk;
            }
            for (a = 0; a < m; a++) {
                if (order[at[a]] != run[a]) g_slMoved++;
                order[at[a]] = run[a];
            }
        }
    }
}

void u9gl_sort_effects_command(const char *v, void (*out)(const char *fmt, ...))
{
    if (g_sortEffectDepth < 0) g_sortEffectDepth = u9cfg_int("gl_sort_effect_depth", 1);
    if (v && !strcmp(v, "on")) g_sortEffectDepth = 1;
    else if (v && !strcmp(v, "off")) g_sortEffectDepth = 0;
    else if (v && *v) { out("post sort effects on|off"); return; }
    if (v && *v) u9log_line("GLRAST sort effects by depth %s", g_sortEffectDepth ? "on" : "off");
    out("effect layers (ALPHA_8) sorted against other translucent runs by depth, not by vertex alpha: %s%s",
        g_sortEffectDepth ? "ON" : "off", g_sortEffectDepthHit ? " (has decided pairs)" : "");
}

void u9gl_sort_layers_command(const char *v, void (*out)(const char *fmt, ...))
{
    int i;
    char list[256];
    unsigned n = 0;
    if (g_slN < 0) sort_layers_read();
    if (v && !strcmp(v, "on")) g_slOn = 1;
    else if (v && !strcmp(v, "off")) g_slOn = 0;
    else if (v && *v) { out("post sort layers on|off"); return; }
    if (v && *v) u9log_line("GLRAST sort layers %s", g_slOn ? "on" : "off");
    list[0] = 0;
    for (i = 0; i < g_slN && n + 16 < sizeof list; i++)
        n += (unsigned)snprintf(list + n, sizeof list - n, "%s%d", i == 0 ? "" : g_slGroup[i] != g_slGroup[i - 1] ? "; " : ",", g_slTex[i]);
    out("layer groups %s: %s (drawn in that order within a bucket); %u runs moved last frame", g_slOn ? "ON" : "off",
        g_slN ? list : "(none)", g_slMovedLast);
}

#define SO_MIN_ALPHA 8u
#define SO_PAIRS 64
static struct { int early, late; unsigned n; } g_soPair[SO_PAIRS];
static int      g_soPairN;
static unsigned g_soTris, g_soBad, g_soFrames;
static DWORD    g_soLogAt;

static float tri_key(unsigned first)
{
    float q = g_verts[first].q, k;
    if (g_verts[first + 1].q > q) q = g_verts[first + 1].q;
    if (g_verts[first + 2].q > q) q = g_verts[first + 2].q;
    k = q > 1e-9f ? 1.0f / q : 1e30f;
    return k;
}

static int so_seen(const GlRun *r)
{
    return !r->blendAdd && (!r->aN || r->aSum / r->aN >= SO_MIN_ALPHA);
}

static void sort_order_watch(const unsigned *order, unsigned n)
{
    static float maxKey[U9GL_MAX_RUNS];
    unsigned a, b, t;
    for (a = 0; a < n; a++) {
        const GlRun *r = &g_runs[order[a]];
        float m = 0.0f;
        for (t = r->first; t + 2 < r->first + r->count; t += 3) {
            float k = tri_key(t);
            if (k > m) m = k;
        }
        maxKey[a] = m;
    }
    for (a = 0; a < n; a++) {
        const GlRun *r = &g_runs[order[a]];
        int bk = depth_bucket(r->zMin);
        float later = 0.0f;
        int lateTex = -1;
        if (!so_seen(r)) { g_soTris += r->count / 3; continue; }
        for (b = a + 1; b < n; b++) {
            const GlRun *s = &g_runs[order[b]];
            if (depth_bucket(s->zMin) != bk) break;
            if (!so_seen(s) || s->texId == r->texId ||
                s->x1 < r->x0 || s->x0 > r->x1 || s->y1 < r->y0 || s->y0 > r->y1) continue;
            if (maxKey[b] > later) { later = maxKey[b]; lateTex = s->texId; }
        }
        for (t = r->first; t + 2 < r->first + r->count; t += 3) {
            g_soTris++;
            if (lateTex >= 0 && tri_key(t) < later) {
                int p;
                g_soBad++;
                for (p = 0; p < g_soPairN; p++)
                    if (g_soPair[p].early == r->texId && g_soPair[p].late == lateTex) break;
                if (p < g_soPairN) g_soPair[p].n++;
                else if (g_soPairN < SO_PAIRS) {
                    g_soPair[g_soPairN].early = r->texId;
                    g_soPair[g_soPairN].late = lateTex;
                    g_soPair[g_soPairN].n = 1;
                    g_soPairN++;
                }
            }
        }
    }
}

static void sort_order_log(void)
{
    DWORD now = u9time_now();
    g_soFrames++;
    if (now - g_soLogAt < 2000u) return;
    if (g_soBad) {
        char line[512];
        int n = 0, shown, p, best;
        unsigned used[SO_PAIRS] = { 0 };
        n += snprintf(line + n, sizeof line - (size_t)n, "SORTORDER %u of %u sorted translucent triangles in %u frames drawn"
                      " before a farther, overlapping triangle of another run (%.1f%%); most:", g_soBad, g_soTris, g_soFrames,
                      g_soTris ? 100.0 * (double)g_soBad / (double)g_soTris : 0.0);
        for (shown = 0; shown < 6; shown++) {
            best = -1;
            for (p = 0; p < g_soPairN; p++)
                if (!used[p] && (best < 0 || g_soPair[p].n > g_soPair[best].n)) best = p;
            if (best < 0) break;
            used[best] = 1;
            n += snprintf(line + n, sizeof line - (size_t)n, " tex %d under %d: %u;", g_soPair[best].early,
                          g_soPair[best].late, g_soPair[best].n);
        }
        u9log_line("%s", line);
    }
    g_soLogAt = now;
    g_soTris = g_soBad = g_soFrames = 0;
    g_soPairN = 0;
}

static void run_begin(void)
{
    GlRun *r;
    if (g_runCount && g_runs[g_runCount - 1].count == 0) {
        GlRun *r = &g_runs[g_runCount - 1];
        r->epoch = g_epoch;
        r->src = g_src;
        run_fill(r);
        return;
    }
    if (g_runCount >= U9GL_MAX_RUNS) return;
    r = &g_runs[g_runCount++];
    r->first = g_count;
    r->count = 0;
    r->x0 = r->y0 =  1.0e30f;
    r->x1 = r->y1 = -1.0e30f;
    r->zMin = 1.0e30f;
    r->aSum = r->aN = 0;
    r->aRawMin = 255u; r->aRawMax = 0u;
    r->bloomShare = -1.0f;
    r->bloomWhy = "not drawn";
    r->epoch = g_epoch;
    r->src = g_src;
    run_fill(r);
    material_census(r);
}

void u9gl_set_depth_write(int enable)
{
    enable = enable ? 1 : 0;
    if (enable == g_depthWrite) return;
    g_depthWrite = enable;
    run_begin();
}

static void material_set(const void *mat)
{
    if (mat == g_mat) {
        if (!mat) return;
        if (*(const int16_t  *)((const char *)mat + U9_MAT_TEXID)    == g_matId  &&
            *(const int16_t  *)((const char *)mat + U9_MAT_TEXID2)   == g_matId2 &&
            *(const unsigned *)((const char *)mat + U9_MAT_FLAGS)    == g_matFlags)
            return;
        g_matRecycled++;
    }
    g_mat = mat;
    if (mat) {
        g_matId    = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID);
        g_matId2   = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID2);
        g_matFlags = *(const unsigned *)((const char *)mat + U9_MAT_FLAGS);
    } else {
        g_matId = g_matId2 = -1;
        g_matFlags = 0;
    }
    run_begin();
}

static void material_forget(void)
{
    g_mat = NULL;
    g_matId = g_matId2 = -1;
    g_matFlags = 0;
}

unsigned u9gl_material_recycles(void) { return g_matRecycled; }

void u9gl_set_render_state(unsigned shadow)
{
    (void)shadow;
    g_stateChanges++;
}

void u9gl_note_shadow(unsigned shadow)
{
    unsigned i;
    for (i = 0; i < g_seenCount; i++)
        if (g_seen[i] == shadow) return;
    if (g_seenCount >= U9GL_MAX_SEEN) return;
    g_seen[g_seenCount++] = shadow;

    u9log_line("GLSTATE NEW shadow=0x%08X  blend=%s  alphaTest=%s  "
               "colourCombine=%s alphaCombine=%s chromakey=%u",
               shadow,
               ((shadow & 0xE0u) == 0x20u || (shadow & 0xE0u) == 0x40u)
                   ? "ADDITIVE SRC_ALPHA,ONE (1,4,4,0)"
                   : "SRC_ALPHA,ONE_MINUS_SRC_ALPHA (1,5,4,0)",
               (shadow & 0x100u) ? "on" : "off",
               (shadow & 0x06u) == 0x06u ? "modulate" : "flat",
               (shadow & 0x18u) == 0x10u ? "modulate" : "flat",
               shadow & 1u);
}

static unsigned g_colN, g_colSum, g_colMax, g_colClipped, g_alphaSum;

static unsigned g_fogTouched;
static unsigned g_fogFullyOut;
static unsigned g_lastFogTouched, g_lastFogFullyOut;

static float fog_alpha(float z, float a)
{
    const char *r = (const char *)g_renderer;
    float far_, near_, range, out;

    if (!g_useFog) return a;
    if (!r || !*(const unsigned char *)(r + U9_OFF_FOG_ENABLE)) return a;
    far_  = *(const float *)(r + U9_OFF_FOG_FAR);
    near_ = *(const float *)(r + U9_OFF_FOG_NEAR);
    range = *(const float *)(r + U9_OFF_FOG_RANGE);
    if (z <= near_) return a;
    if (z >= far_ || range == 0.0f) { g_fogTouched++; g_fogFullyOut++; return 0.0f; }
    out = ((far_ - z) / range) * a;
    if (out != a) g_fogTouched++;
    return out;
}

#ifndef GL_FOG_COORDINATE_SOURCE_EXT
#define GL_FOG_COORDINATE_SOURCE_EXT   0x8450
#define GL_FOG_COORDINATE_EXT          0x8451
#define GL_FOG_COORDINATE_ARRAY_EXT    0x8457
#endif

typedef void (APIENTRY *FogCoordPointerFn)(GLenum, GLsizei, const GLvoid *);
static FogCoordPointerFn g_fogCoordPointer;
static int      g_fogBProbed;
static int      g_fogBActive;
static int      g_fogBMode;
static float    g_fogBColour[4] = { 0, 0, 0, 1 };
static unsigned char g_fogBTable[U9_FOG_TABLE_SIZE];
static float    g_fogBw[U9_FOG_TABLE_SIZE];
static unsigned g_fogBTouched, g_lastFogBTouched;
static float    g_fogBInvW = 1.0f;
static float    g_fogBwMin, g_fogBwMax, g_lastFogBwMin, g_lastFogBwMax;
static float    g_fogBdMax, g_lastFogBdMax;
static int      g_fogBSampled;

static void fogb_sample(void)
{
    const char *r = (const char *)g_renderer;
    const char *fo;
    unsigned i, c;

    g_fogBActive = 0;
    if (!g_useFogTable || !r) return;
    fo = *(const char *const *)(r + U9_OFF_FOG_OBJECT);
    if (!fo) return;
    if (!*(const unsigned *)(fo + U9_FOGOBJ_ENABLED)) return;

    if (!g_fogBw[U9_FOG_TABLE_SIZE - 1])
        for (i = 0; i < U9_FOG_TABLE_SIZE; i++)
            g_fogBw[i] = (float)(1u << (3 + (i >> 2))) / (float)(8 - (i & 3));

    {
        float wScale = *(const float *)(r + U9_OFF_WSCALE);
        g_fogBInvW = (wScale != 0.0f) ? 1.0f / wScale : 1.0f;
    }
    memcpy(g_fogBTable, fo + U9_FOGOBJ_TABLE, U9_FOG_TABLE_SIZE);
    g_fogBMode = (int)*(const unsigned *)(fo + U9_FOGOBJ_MODE);
    if (g_fogBMode != 1) {
        static int warned;
        if (!warned) {
            warned = 1;
            u9log_line("GLFOG fog mode %d at the frame's first draw. Mode 2 is "
                       "grFogMode(0x202) = GR_FOG_ADD2, (1-f)*Cin, fading to "
                       "black; the GL path applies it per run, to slot 37's "
                       "pass.", g_fogBMode);
        }
    }
    c = *(const unsigned *)(fo + U9_FOGOBJ_COLOUR);
    g_fogBColour[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    g_fogBColour[1] = (float)((c >>  8) & 0xFF) / 255.0f;
    g_fogBColour[2] = (float)( c        & 0xFF) / 255.0f;
    g_fogBColour[3] = 1.0f;
    g_fogBActive = 1;
}

static float fogb_density(float w)
{
    unsigned lo = 0, hi = U9_FOG_TABLE_SIZE - 1, mid;
    float t, v;

    if (!g_fogBActive) return 0.0f;
    if (!g_fogBwMax || w < g_fogBwMin) g_fogBwMin = w;
    if (w > g_fogBwMax) g_fogBwMax = w;
    if (w <= g_fogBw[0])  v = (float)g_fogBTable[0];
    else if (w >= g_fogBw[hi]) v = (float)g_fogBTable[hi];
    else {
        while (hi - lo > 1) { mid = (lo + hi) / 2;
                              if (w < g_fogBw[mid]) hi = mid; else lo = mid; }
        t = (w - g_fogBw[lo]) / (g_fogBw[hi] - g_fogBw[lo]);
        v = (float)g_fogBTable[lo] + t * ((float)g_fogBTable[hi] - (float)g_fogBTable[lo]);
    }
    if (v > 0.0f) g_fogBTouched++;
    if (v > g_fogBdMax) g_fogBdMax = v;
    return v / 255.0f;
}

static void fog_report(void)
{
    const char *r = (const char *)g_renderer;
    static int announced;
    const char *fo;
    if (announced || !r) return;
    announced = 1;
    u9log_line("GLFOG A (vertex fade) enable=%u near=%.3f far=%.3f range=%.3f  (renderer +0x29/+0x30/+0x2C/+0x34)",
               (unsigned)*(const unsigned char *)(r + U9_OFF_FOG_ENABLE),
               *(const float *)(r + U9_OFF_FOG_NEAR),
               *(const float *)(r + U9_OFF_FOG_FAR),
               *(const float *)(r + U9_OFF_FOG_RANGE));

    fo = *(const char *const *)(r + U9_OFF_FOG_OBJECT);
    if (!fo) { u9log_line("GLFOG B (table fog) no fog object at renderer +0x0C"); return; }
    {
        const unsigned char *t = (const unsigned char *)(fo + U9_FOGOBJ_TABLE);
        char line[U9_FOG_TABLE_SIZE * 4 + 8];
        unsigned i, n = 0, nonzero = 0;
        for (i = 0; i < U9_FOG_TABLE_SIZE; i++) {
            if (t[i]) nonzero++;
            if (n + 5 < sizeof line)
                n += (unsigned)sprintf(line + n, "%u%s", (unsigned)t[i],
                                       i + 1 < U9_FOG_TABLE_SIZE ? "," : "");
        }
        line[n] = 0;
        u9log_line("GLFOG B (table fog) object=%p enabled=%u mode=%u colour=0x%06X "
                   "table %u/%u non-zero  <- grFogColorValue/grFogMode/grFogTable",
                   (const void *)fo,
                   *(const unsigned *)(fo + U9_FOGOBJ_ENABLED),
                   *(const unsigned *)(fo + U9_FOGOBJ_MODE),
                   *(const unsigned *)(fo + U9_FOGOBJ_COLOUR) & 0xFFFFFFu,
                   nonzero, U9_FOG_TABLE_SIZE);
        u9log_line("GLFOG B table[64] = %s", line);
    }
}

static void run_depth(float z)
{
    GlRun *r;
    if (!g_runCount) return;
    r = &g_runs[g_runCount - 1];
    if (z < r->zMin) r->zMin = z;
}

static void run_alpha(unsigned a)
{
    GlRun *r;
    if (!g_runCount) return;
    r = &g_runs[g_runCount - 1];
    r->aSum += a; r->aN++;
}

static void bbox(float x, float y)
{
    GlRun *r;
    if (!g_runCount) return;
    r = &g_runs[g_runCount - 1];
    if (x < r->x0) r->x0 = x;
    if (y < r->y0) r->y0 = y;
    if (x > r->x1) r->x1 = x;
    if (y > r->y1) r->y1 = y;
}

static void run_alpha_raw(float a)
{
    GlRun   *r;
    unsigned b;
    if (!g_runCount) return;
    r = &g_runs[g_runCount - 1];
    b = (unsigned)to_byte(a);
    if (b < r->aRawMin) r->aRawMin = b;
    if (b > r->aRawMax) r->aRawMax = b;
}

static void push(const U9Vertex *v, float alpha)
{
    GlVert *o = &g_verts[g_count++];
    bbox(v->x, v->y);
    run_depth(v->z);
    o->x = v->x;
    o->y = v->y;
    o->z = depth_of(v->z);
    {
        float q = (v->z != 0.0f) ? (1.0f / v->z) : 1.0f;
        o->sq = v->s * q;
        o->tq = v->t * q;
        o->rq = 0.0f;
        o->q  = q;
    }
    o->fog = fogb_density(v->z * g_fogBInvW);
    o->rgba[0] = to_byte(v->r);
    o->rgba[1] = to_byte(v->g);
    o->rgba[2] = to_byte(v->b);
    {
        float mx = v->r > v->g ? (v->r > v->b ? v->r : v->b)
                               : (v->g > v->b ? v->g : v->b);
        g_colN++;
        g_colSum += (unsigned)((v->r + v->g + v->b) / 3.0f);
        if ((unsigned)mx > g_colMax) g_colMax = (unsigned)mx;
        if (mx > 255.0f) g_colClipped++;
        g_alphaSum += o->rgba[3];
    }
    run_alpha_raw(alpha >= 0.0f ? alpha : v->a);
    o->rgba[3] = to_byte(fog_alpha(v->z, alpha >= 0.0f ? alpha : v->a));
    run_alpha(o->rgba[3]);

    if (v->z < g_zMin) g_zMin = v->z;
    if (v->z > g_zMax) g_zMax = v->z;
}

static void bf_tri(const U9Vertex *const v[3], const void *mat)
{
    GlRun   *r = g_bfRunCount ? &g_bfRuns[g_bfRunCount - 1] : NULL;
    int16_t  id = -1, id2 = -1;
    unsigned fl = 0;
    int      k;
    if (g_bfCount + 3 > U9GL_BF_VERTS) { g_bfDropped++; return; }
    if (mat) {
        id  = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID);
        id2 = *(const int16_t  *)((const char *)mat + U9_MAT_TEXID2);
        fl  = *(const unsigned *)((const char *)mat + U9_MAT_FLAGS);
    }
    if (!r || r->mat != mat || r->texId != id || r->texId2 != id2 || r->flags != fl || r->depthWrite != g_depthWrite ||
        r->anim != g_u9Anim) {
        if (g_bfRunCount >= U9GL_BF_RUNS) { g_bfDropped++; return; }
        r = &g_bfRuns[g_bfRunCount++];
        memset(r, 0, sizeof *r);
        r->first = U9GL_MAX_VERTS + g_bfCount;
        r->x0 = r->y0 =  1.0e30f;
        r->x1 = r->y1 = -1.0e30f;
        r->zMin = 1.0e30f;
        r->depthWrite = g_depthWrite;
        r->depthTest  = g_depthTest;
        r->mat = mat;
        r->texId = id;
        r->texId2 = id2;
        r->flags = fl;
        r->chromaKey = mat ? *(const unsigned *)((const char *)mat + U9_MAT_CHROMAKEY) : 0;
        r->texFmt = g_forceFormat ? (unsigned)g_forceFormat : u9gl_format_for_material(mat);
        r->blendAdd  = (fl & U9_MATF_ADDITIVE) ? 1 : 0;
        r->alphaTest = ((fl & U9_MATF_FMT_SELECT) && (fl & U9_MATF_ALPHATEST_B)) ? 1 : 0;
        r->chroma    = ((fl & U9_MATF_CHROMA_MASK) == U9_MATF_CHROMA_ON) ? 1 : 0;
        if (u9gl_magenta_keyed(id) && !(fl & U9_MATF_TRANSLUCENT)) r->alphaTest = 1;
        r->src = g_src;
        r->epoch = g_epoch;
        r->bloomShare = -1.0f;
        r->bloomWhy = "a back face: a caster, never drawn";
        r->anim = g_u9Anim;
    }
    for (k = 0; k < 3; k++) {
        GlVert *o = &g_verts[U9GL_MAX_VERTS + g_bfCount++];
        const U9Vertex *p = v[k];
        float q = (p->z != 0.0f) ? (1.0f / p->z) : 1.0f;
        o->x = p->x;
        o->y = p->y;
        o->z = depth_of(p->z);
        o->sq = p->s * q;
        o->tq = p->t * q;
        o->rq = 0.0f;
        o->q  = q;
        o->fog = 0.0f;
        o->rgba[0] = o->rgba[1] = o->rgba[2] = o->rgba[3] = 255;
        if (p->x < r->x0) r->x0 = p->x;
        if (p->y < r->y0) r->y0 = p->y;
        if (p->x > r->x1) r->x1 = p->x;
        if (p->y > r->y1) r->y1 = p->y;
        if (p->z < r->zMin) r->zMin = p->z;
    }
    r->count += 3;
}

int      g_guardPct = -1;
static unsigned g_guardTris, g_lastGuardTris;

void u9gl_guard_tri(const void *renderer, const U9Vertex *v0, const U9Vertex *v1, const U9Vertex *v2, unsigned flags)
{
    const char *r = (const char *)renderer;
    const U9Vertex *v[3];
    float W, H, a, b, lo, hi, bx, by;
    int k, inside = 1;
    if (g_guardPct < 0) g_guardPct = u9cfg_int("gl_shadow_guard", 50);
    if (g_guardPct <= 0 || !r || g_u9BackFace) return;
    W = (float)*(const int *)(r + 0x44);
    H = (float)*(const int *)(r + 0x48);
    a = *(const float *)(r + 0x58);
    b = *(const float *)(r + ((flags & 0x20) ? 0x50 : 0x54));
    lo = a < b ? a : b;
    hi = a < b ? b : a;
    if (W < 16.0f || H < 16.0f) return;
    bx = W * (float)g_guardPct / 100.0f;
    by = H * (float)g_guardPct / 100.0f;
    v[0] = v0; v[1] = v1; v[2] = v2;
    for (k = 0; k < 3; k++) {
        if (v[k]->z < lo || v[k]->z > hi) return;
        if (v[k]->x < -bx || v[k]->x > W + bx || v[k]->y < -by || v[k]->y > H + by) return;
        if (v[k]->x < 0.0f || v[k]->x > W || v[k]->y < 0.0f || v[k]->y > H) inside = 0;
    }
    if (inside) return;
    g_guardTris++;
    bf_tri(v, *(const void *const *)(r + U9_OFF_CUR_MATERIAL));
}

void u9gl_guard_stats(unsigned *tris) { *tris = g_lastGuardTris; }

const void *u9gl_current_renderer(void) { return g_renderer; }

void u9gl_tri(const void *renderer,
              const U9Vertex *v0, const U9Vertex *v1, const U9Vertex *v2,
              float alpha, const void *mat)
{
    g_renderer = renderer;
    if (g_u9BackFace) {
        const U9Vertex *vv[3];
        vv[0] = v0; vv[1] = v1; vv[2] = v2;
        bf_tri(vv, mat);
        return;
    }
    if (!g_fogBSampled) { g_fogBSampled = 1; fogb_sample(); }
    if (g_count + 3 > U9GL_MAX_VERTS) { g_dropped++; return; }
    if (!g_runCount) run_begin();
    material_set(mat);
    if (g_runCount && g_runs[g_runCount - 1].anim != g_u9Anim) run_begin();

    {
        GlRun *r = &g_runs[g_runCount - 1];
        if (r->deferred == 1 && r->count && !u9beam_hides(r->texId)) {
            float tz = v0->z;
            if (v1->z < tz) tz = v1->z;
            if (v2->z < tz) tz = v2->z;
            if (depth_bucket(tz) != depth_bucket(r->zMin)) run_begin();
        }
    }

    push(v0, alpha);
    push(v1, alpha);
    push(v2, alpha);
    g_runs[g_runCount - 1].count += 3;
}

void u9gl_poly(const void *renderer, unsigned count, const void *verts,
               const void *mat)
{
    const float *f = (const float *)verts;
    float texW = 1.0f, texH = 1.0f;
    unsigned i;

    if (!verts || count < 3) return;
    g_renderer = renderer;
    if (g_u9BackFace) { g_bfPolys++; return; }
    if (!g_fogBSampled) { g_fogBSampled = 1; fogb_sample(); }
    if (g_listPass) mat = NULL;
    if (!g_runCount) run_begin();
    material_set(mat);
    if (g_runCount && g_runs[g_runCount - 1].anim != g_u9Anim) run_begin();

    if (mat) {
        unsigned w = *(const unsigned *)((const char *)mat + U9_MAT_TEXW);
        unsigned h = *(const unsigned *)((const char *)mat + U9_MAT_TEXH);
        if (w) texW = (float)w;
        if (h) texH = (float)h;
    }

    for (i = 1; i + 1 < count; i++) {
        const unsigned idx[3] = { 0, i, i + 1 };
        unsigned k;
        if (g_count + 3 > U9GL_MAX_VERTS) { g_dropped++; return; }
        for (k = 0; k < 3; k++) {
            const float *v = f + idx[k] * GRV_FLOATS;
            float oow = v[GRV_OOW];
            float eyeZ = v[GRV_Z];
            GlVert *o = &g_verts[g_count++];
            o->x = v[GRV_X];
            o->y = v[GRV_Y];
            bbox(o->x, o->y);
            if (oow != 0.0f && g_renderer) {
                float wScale = *(const float *)((const char *)g_renderer +
                                                U9_OFF_WSCALE);
                eyeZ = (wScale != 0.0f) ? (wScale / oow) : v[GRV_Z];
            } else {
                eyeZ = v[GRV_Z];
            }
            o->z = depth_of(eyeZ);
            run_depth(eyeZ);
            {
                float q = (eyeZ != 0.0f) ? 1.0f / eyeZ : 1.0f;
                float k = (oow != 0.0f) ? q / oow : q;
                o->sq = v[GRV_TMU0_SOW] / texW * k;
                o->tq = v[GRV_TMU0_TOW] / texH * k;
                o->rq = 0.0f;
                o->q  = q;
            }
            o->fog = fogb_density(oow != 0.0f ? 1.0f / oow
                                              : eyeZ * g_fogBInvW);
            o->rgba[0] = to_byte(v[GRV_R]);
            o->rgba[1] = to_byte(v[GRV_G]);
            o->rgba[2] = to_byte(v[GRV_B]);
            run_alpha_raw(v[GRV_A]);
            o->rgba[3] = to_byte(g_listPass ? v[GRV_A]
                                            : fog_alpha(eyeZ, v[GRV_A]));
            run_alpha(o->rgba[3]);
            if (eyeZ < g_zMin) g_zMin = eyeZ;
            if (eyeZ > g_zMax) g_zMax = eyeZ;
        }
        g_runs[g_runCount - 1].count += 3;
    }
}


#define U9GL_UNTEX_MAX 512
static struct { int id, id2; unsigned fmt, hits; } g_untex[U9GL_UNTEX_MAX];
static unsigned g_untexCount, g_untexDraws, g_untexOverflow;

static void note_untextured(int id, int id2, unsigned fmt)
{
    unsigned i;
    if (id < 0) return;
    g_untexDraws++;
    for (i = 0; i < g_untexCount; i++)
        if (g_untex[i].id == id && g_untex[i].id2 == id2) { g_untex[i].hits++; return; }
    if (g_untexCount >= U9GL_UNTEX_MAX) { g_untexOverflow++; return; }
    g_untex[g_untexCount].id   = id;
    g_untex[g_untexCount].id2  = id2;
    g_untex[g_untexCount].fmt  = fmt;
    g_untex[g_untexCount].hits = 1;
    g_untexCount++;
    if (g_untexCount <= 12)
        u9log_line("UNTEX id=%d/%d fmt=0x%02X drawn with NO texture bound"
                   " - vertex colours only", id, id2, fmt);
}

void u9gl_untextured_report(void)
{
    unsigned i;
    if (!g_untexCount) {
        u9log_line("UNTEX none - every run carrying a texture id got one bound");
        return;
    }
    u9log_line("UNTEX %u distinct ids drawn with no texture, %u draws total%s",
               g_untexCount, g_untexDraws,
               g_untexOverflow ? " -- TABLE FULL, count is a floor" : ":");
    for (i = 0; i < g_untexCount; i++)
        u9log_line("UNTEX   id=%d/%d fmt=0x%02X  %u draws",
                   g_untex[i].id, g_untex[i].id2, g_untex[i].fmt, g_untex[i].hits);
}

static unsigned g_dwFrames;

static void draw_watch(void)
{
    char line[700];
    unsigned i, n = 0, shown = 0;
    unsigned cap = (unsigned)u9cfg_int("draw_watch_frames", 900);

    if (!cap || g_dwFrames >= cap) return;
    for (i = 0; i < g_runCount && n + 24 < sizeof(line); i++) {
        int id = g_runs[i].texId;
        if (!g_runs[i].count) continue;
        if (id < 8000 || id >= 9024) continue;
        n += (unsigned)sprintf(line + n, "%s%d/%d.e%u%s", shown ? " " : "",
                               id, g_runs[i].texId2, g_runs[i].epoch,
                               g_runs[i].deferred ? "d" : "");
        shown++;
    }
    if (shown) {
        line[n] = 0;
        u9log_line("DW f%u runs=%u composed=%u: %s",
                   u9log_frame(), g_runCount, shown, line);
    }
    g_dwFrames++;
}

static unsigned g_twFrames;

static int boxes_overlap(const GlRun *a, const GlRun *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

static int unproject_world(const GlVert *v, float *eye, float out[3])
{
    const char *cam;
    const float *m;
    float z, cx, cu, sx, sy;
    if (!g_renderer || v->q == 0.0f) return 0;
    cam = *(const char *const *)((const char *)g_renderer + U9_OFF_VIEW);
    if (!cam) return 0;
    sx = *(const float *)(cam + U9_CAM_PROJ_SX);
    sy = *(const float *)(cam + U9_CAM_PROJ_SY);
    if (sx == 0.0f || sy == 0.0f) return 0;
    z  = 1.0f / v->q;
    cx = (v->x - *(const float *)(cam + U9_CAM_CENTRE_X)) * z / sx;
    cu = (*(const float *)(cam + U9_CAM_CENTRE_Y) - v->y) * z / sy;
    m  = (const float *)(cam + U9_CAM_TO_WORLD);
    out[0] = m[0] * cx + m[1] * z + m[2]  * cu + m[3];
    out[1] = m[4] * cx + m[5] * z + m[6]  * cu + m[7];
    out[2] = m[8] * cx + m[9] * z + m[10] * cu + m[11];
    *eye = z;
    return 1;
}

static void texture_watch(void)
{
    static int watch = -1;
    static unsigned cap;
    char line[900];
    unsigned i, n = 0, hits = 0, big = 0, bigCount = 0;
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    GlRun uni;

    if (watch < 0) {
        watch = u9cfg_int("watch_texture", 0);
        cap = (unsigned)u9cfg_int("watch_texture_frames", 600);
        if (watch > 0)
            u9log_line("TW watching texture %d for %u frames - stand still for "
                       "comparable samples", watch, cap);
    }
    if (watch <= 0 || g_twFrames >= cap) return;

    {
        int u9gl_texture_is_override(unsigned glTex);
        unsigned perFrame[16], tris = 0, ov = 0, eng = 0, add = 0, atest = 0, def = 0, zw = 0;
        unsigned aRawLo = 255, aRawHi = 0, aLo = 255, aHi = 0, flagsOr = 0, flagsAnd = ~0u;
        float eyeLo = 1e9f, eyeHi = -1e9f, wzLo = 1e9f, wzHi = -1e9f;
        unsigned nWorld = 0;
        memset(perFrame, 0, sizeof perFrame);
        for (i = 0; i < g_runCount; i++) {
            const GlRun *r = &g_runs[i];
            unsigned k;
            if (!r->count || r->texId != watch) continue;
            if (r->texId2 >= 0 && r->texId2 < 16) perFrame[r->texId2]++;
            tris += r->count / 3;
            {
                int o = u9gl_texture_is_override(r->glTex);
                if (o == 1) ov++; else if (o == 0) eng++;
            }
            if (r->blendAdd) add++;
            if (r->alphaTest) atest++;
            if (r->deferred) def++;
            if (r->depthWrite) zw++;
            flagsOr |= r->flags; flagsAnd &= r->flags;
            if (r->aRawMin < aRawLo) aRawLo = r->aRawMin;
            if (r->aRawMax > aRawHi) aRawHi = r->aRawMax;
            for (k = 0; k < r->count; k++) {
                const GlVert *gv = &g_verts[r->first + k];
                unsigned a = gv->rgba[3];
                float eye, w[3];
                if (a < aLo) aLo = a;
                if (a > aHi) aHi = a;
                if (unproject_world(gv, &eye, w)) {
                    if (eye < eyeLo) eyeLo = eye;
                    if (eye > eyeHi) eyeHi = eye;
                    if (w[2] < wzLo) wzLo = w[2];
                    if (w[2] > wzHi) wzHi = w[2];
                    nWorld++;
                }
            }
            if (r->x0 < bx0) bx0 = r->x0;
            if (r->y0 < by0) by0 = r->y0;
            if (r->x1 > bx1) bx1 = r->x1;
            if (r->y1 > by1) by1 = r->y1;
            if (r->count > bigCount) { bigCount = r->count; big = i; }
            hits++;
        }
        if (!hits) return;
        n = (unsigned)sprintf(line, "TW f%u tex %d: %u runs %u tris | bound: %u override %u engine"
                              " | raw a %u-%u drawn a %u-%u | add %u atest %u deferred %u depth-write %u"
                              " | flags and %04X or %04X | frames",
                              u9log_frame(), watch, hits, tris, ov, eng, aRawLo, aRawHi,
                              aLo, aHi, add, atest, def, zw, flagsAnd & 0xFFFFu, flagsOr & 0xFFFFu);
        for (i = 0; i < 16; i++)
            if (perFrame[i]) n += (unsigned)sprintf(line + n, " /%u:%u", i, perFrame[i]);
        if (nWorld)
            n += (unsigned)sprintf(line + n, " | eye z %.0f-%.0f world z %.1f-%.1f",
                                   eyeLo, eyeHi, wzLo, wzHi);
    }

    {
        const GlVert *v = &g_verts[g_runs[big].first];
        float q = (v->q != 0.0f) ? v->q : 1.0f;
        n += (unsigned)sprintf(line + n, " | v0 xy %.1f,%.1f st %.4f,%.4f rgba %u,%u,%u,%u",
                               v->x, v->y, v->sq / q, v->tq / q,
                               v->rgba[0], v->rgba[1], v->rgba[2], v->rgba[3]);
    }

    uni.x0 = bx0; uni.y0 = by0; uni.x1 = bx1; uni.y1 = by1;
    {
        struct { int id; unsigned tris, runs, drawnBefore; } grp[24];
        unsigned ng = 0, g, shown = 0, m;
        char over[512];
        for (i = 0; i < g_runCount; i++) {
            const GlRun *r = &g_runs[i];
            if (!r->count || r->texId == watch || !boxes_overlap(r, &uni)) continue;
            for (g = 0; g < ng && grp[g].id != r->texId; g++) ;
            if (g == ng) {
                if (ng == 24) continue;
                grp[ng].id = r->texId; grp[ng].tris = grp[ng].runs = grp[ng].drawnBefore = 0;
                ng++;
            }
            grp[g].tris += r->count / 3;
            grp[g].runs++;
            if (r->drawPos < g_runs[big].drawPos) grp[g].drawnBefore++;
        }
        m = (unsigned)sprintf(over, "TW f%u   over/under tex %d:", u9log_frame(), watch);
        while (shown < 10 && m + 40 < sizeof(over)) {
            unsigned best = ng;
            for (g = 0; g < ng; g++)
                if (grp[g].tris && (best == ng || grp[g].tris > grp[best].tris)) best = g;
            if (best == ng) break;
            m += (unsigned)sprintf(over + m, " %d:%ut/%ur(%u before)", grp[best].id,
                                   grp[best].tris, grp[best].runs, grp[best].drawnBefore);
            grp[best].tris = 0;
            shown++;
        }
        if (!shown) m += (unsigned)sprintf(over + m, " nothing");
        over[m] = 0;
        line[n] = 0;
        u9log_line("%s", line);
        u9log_line("%s", over);
    }
    g_twFrames++;
    if (g_twFrames == cap)
        u9log_line("TW done after %u frames", cap);
}

#define U9GL_NEAR_UI_Z 600.0f
static int near_ui(const GlRun *r)
{
    return r->book || (r->texId >= 8000 && r->zMin < U9GL_NEAR_UI_Z);
}

static int interface_run(const GlRun *r)
{
    return r->src == 34 || r->zMin <= 10.0f || near_ui(r) || r->texId == 1900;
}

static int glare_colour(const GlRun *r)
{
    const unsigned char *c = g_verts[r->first].rgba;
    return r->count && c[0] >= 250 && c[1] >= 250 && c[2] >= 160 && c[2] <= 180;
}

static int interface_mark(const GlRun *r, float engineW, float engineH)
{
    if (r->src == 34 && r->x1 - r->x0 >= engineW * 0.9f && r->y1 - r->y0 >= engineH * 0.9f) return 0;
    if (r->texId < 0 && r->x1 - r->x0 >= engineW * 0.9f && r->y1 - r->y0 >= engineH * 0.9f) {
        static unsigned logged;
        if (logged < 4) {
            const unsigned char *c = g_verts[r->first].rgba;
            logged++;
            u9log_line("GLWASH not marked as interface: untextured, %.0fx%.0f of %.0fx%.0f, src %d, z %.2f, rgba %u,%u,%u,%u",
                       (double)(r->x1 - r->x0), (double)(r->y1 - r->y0), (double)engineW, (double)engineH,
                       r->src, (double)r->zMin, c[0], c[1], c[2], c[3]);
        }
        return 0;
    }
    if (glare_colour(r)) return 0;
    return interface_run(r);
}

static int glare_scale(const GlRun *r)
{
    return (r->texId < 0 && r->zMin <= 10.0f && glare_colour(r)) ? u9rays_glare() : 100;
}

static int moon_phase(int texId)
{
    static const short MOON[] = { 4840, 4956, 6129, 6131, 6132, 6149, 6150, 6151,
                                  3071, 3632, 4827, 4829, 4830, 4831, 4834, 4836 };
    static const signed char CLS[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    unsigned k;
    for (k = 0; k < sizeof MOON / sizeof MOON[0]; k++) if (MOON[k] == texId) return CLS[k & 7];
    return -1;
}

static int moon_tex(int texId)
{
    return moon_phase(texId) >= 0;
}

static int moon_disc(int texId)
{
    static int on = -1;
    if (on < 0) on = u9cfg_int("gl_moon_glow", 1);
    return on && moon_tex(texId);
}

static int g_moonHaloEpoch = -1;

static void moon_frame(void)
{
    unsigned i;
    int first = -1;
    for (i = 0; i < g_runCount; i++)
        if (g_runs[i].count && moon_tex(g_runs[i].texId) &&
            (first < 0 || (int)g_runs[i].epoch < first))
            first = (int)g_runs[i].epoch;
    g_moonHaloEpoch = first > 0 ? first - 1 : -1;
}

static int moon_halo(const GlRun *r)
{
    return r->texId == 3182 && r->blendAdd && g_moonHaloEpoch >= 0 &&
           (int)r->epoch == g_moonHaloEpoch;
}

static void book_scan(unsigned engineW, unsigned engineH)
{
    unsigned i, epochs = 0;
    int open = 0;
    float big = (float)engineW * (float)engineH / 16.0f;
    for (i = 0; i < g_runCount; i++) {
        const GlRun *r = &g_runs[i];
        if (r->count && r->texId >= 8000 && r->zMin <= 10.0f && (r->x1 - r->x0) * (r->y1 - r->y0) >= big) open = 1;
    }
    for (i = 0; i < g_runCount; i++) {
        GlRun *r = &g_runs[i];
        r->book = 0;
        if (open && r->count && r->texId >= 8000 && r->zMin > 10.0f && r->zMin < U9GL_NEAR_UI_Z &&
            r->epoch < 32)
            epochs |= 1u << r->epoch;
    }
    if (epochs)
        for (i = 0; i < g_runCount; i++)
            if (g_runs[i].epoch < 32 && (epochs >> g_runs[i].epoch & 1u)) g_runs[i].book = 1;
}

static void book_watch(void)
{
    static int seen[128], nSeen;
    unsigned i;
    int open = 0, k;
    for (i = 0; i < g_runCount; i++)
        if (g_runs[i].count && near_ui(&g_runs[i]) &&
            (g_runs[i].x1 - g_runs[i].x0) * (g_runs[i].y1 - g_runs[i].y0) > 10000.0f) open = 1;
    if (!open) return;
    for (i = 0; i < g_runCount; i++) {
        const GlRun *r = &g_runs[i];
        if (!r->count || r->zMin >= U9GL_NEAR_UI_Z) continue;
        for (k = 0; k < nSeen && seen[k] != r->texId; k++) ;
        if (k < nSeen || nSeen == 128) continue;
        seen[nSeen++] = r->texId;
        u9log_line("BOOKWATCH book open; near the camera: tex %d/%d%s, nearest %.1f, %u triangles, box %.0fx%.0f at %.0f,%.0f,"
                   " depth write %d, pooled %d, additive %d", r->texId, r->texId2, near_ui(r) ? " (left as drawn)" : "",
                   (double)r->zMin, r->count / 3, (double)(r->x1 - r->x0), (double)(r->y1 - r->y0),
                   (double)r->x0, (double)r->y0, r->depthWrite, r->deferred, r->blendAdd);
    }
}

int  u9lava_glows(void);
int  u9lava_is(int texId);
int  u9glpost_bloom_lava(void);
float u9glpost_bloom_share(int texId, int effect, int additive);
const char *u9glpost_bloom_why(void);
float u9glpost_moon_bright(void);
float u9glpost_guardian_bright(int texId);
float u9glpost_moon_halo(void);
float u9glpost_moon_bloom(int halo);
float u9glpost_moon_phase(int cls);
float u9glpost_moon_bloom_setting(int halo);
void  u9glpost_exact_mask(int on);
void  u9glpost_bloom_mask(int on);
int   u9glpost_night_graded(int texId);
void u9gl_wbuf_rgb(float scale);
void u9gl_wbuf_selflit(int on);
void u9gl_wbuf_slack(int on);
void u9gl_wbuf_ui(int on);
void u9gl_wbuf_mark(int on);
int  u9glpost_water_mark_begin(void);
void u9glpost_water_mark_end(void);
void u9gl_wbuf_set_jitter(float x, float y);
void u9gl_wbuf_jitter(int on);
float u9fx_soot_scale(int texId);
float u9fx_glow_gain(int texId);
void u9gl_wbuf_alpha(float scale);
void u9gl_wbuf_rune(unsigned mask, const float tint[3]);
unsigned u9fx_rune_mask(int texId, unsigned glTex);
int  u9fx_rune_tint(int texId, float tint[3]);
int  u9gl_wbuf_begin(float zRef, float zFar, int fog);
void u9gl_wbuf_run(int textured, int alphaTest, float alphaRef, int lightable, int glow, float bloom,
                   float fade);
float u9glpost_effect_fade(void);
int   u9glpost_effect_depth_write(void);
int   u9glpost_depth_glide(void);
void u9gl_wbuf_end(void);

static U9FrameView g_view;

int u9gl_frame_view(U9FrameView *out)
{
    if (!g_view.valid) return 0;
    *out = g_view;
    return 1;
}

static int run_upright(const GlRun *r);

int u9gl_sheet_measure(int (*listed)(int texId), float lo[3], float hi[3], float *meanZ,
                       float rgba[4], unsigned *tris)
{
    unsigned i, t, n = 0;
    double zs = 0.0, cs[4] = { 0, 0, 0, 0 };
    int k;
    if (!g_view.valid) return 0;
    for (k = 0; k < 3; k++) { lo[k] = 1e30f; hi[k] = -1e30f; }
    *tris = 0;
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        if (!r->count || !listed(r->texId) || run_upright(r)) continue;
        *tris += r->count / 3;
        for (t = r->first; t < r->first + r->count; t++) {
            const GlVert *a = &g_verts[t];
            float w, e[3], wp[3];
            if (a->q == 0.0f) continue;
            w = 1.0f / a->q;
            e[0] = (a->x - g_view.cx) * w / g_view.sx;
            e[1] = w;
            e[2] = (g_view.cy - a->y) * w / g_view.sy;
            for (k = 0; k < 3; k++) {
                wp[k] = g_view.m[k * 4] * e[0] + g_view.m[k * 4 + 1] * e[1] + g_view.m[k * 4 + 2] * e[2] + g_view.m[k * 4 + 3];
                if (wp[k] < lo[k]) lo[k] = wp[k];
                if (wp[k] > hi[k]) hi[k] = wp[k];
            }
            zs += wp[2];
            for (k = 0; k < 4; k++) cs[k] += a->rgba[k];
            n++;
        }
    }
    if (!n) return 0;
    *meanZ = (float)(zs / n);
    for (k = 0; k < 4; k++) rgba[k] = (float)(cs[k] / n / 255.0);
    return 1;
}

unsigned u9gl_casters(void (*fn)(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout),
                      void *ctx)
{
    unsigned i, n = 0;
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        if (!r->count || !r->depthWrite || r->blendAdd || r->deferred == 2 || r->src == 34 ||
            r->zMin <= 10.0f || r->texId == 1900 ||
            r->texId == U9TEX_BLOB_SHADOW || r->texFmt == 0x02 ||
            r->texId == U9TEX_WATER ||
            near_ui(r))
            continue;
        g_casterAnim = r->anim;
        fn(ctx, r->first, r->count, r->glTex, r->alphaTest || r->chroma);
        n++;
    }
    for (i = 0; i < g_lastBfRunCount; i++) {
        const GlRun *r = &g_lastBfRuns[i];
        unsigned tex = 0, j;
        int cut;
        if (!r->count || !r->depthWrite || r->blendAdd || r->zMin <= 10.0f || r->texId == 1900 ||
            r->texId == U9TEX_BLOB_SHADOW || r->texFmt == 0x02 || r->texId == U9TEX_WATER)
            continue;
        cut = r->alphaTest || r->chroma;
        if (cut)
            for (j = 0; j < g_lastRunCount && !tex; j++)
                if (g_lastRuns[j].texId == r->texId && g_lastRuns[j].texId2 == r->texId2) tex = g_lastRuns[j].glTex;
        g_casterAnim = r->anim;
        fn(ctx, r->first, r->count, tex, cut);
        n++;
    }
    g_casterAnim = 0;
    return n;
}

void u9gl_all_runs(void (*fn)(void *ctx, unsigned first, unsigned count, int texId, int back, const char *why),
                   void *ctx)
{
    unsigned i;
    for (i = 0; i < g_lastRunCount + g_lastBfRunCount; i++) {
        int back = i >= g_lastRunCount;
        const GlRun *r = back ? &g_lastBfRuns[i - g_lastRunCount] : &g_lastRuns[i];
        const char *why = NULL;
        if (!r->count) continue;
        if (!r->depthWrite) why = "writes no depth";
        else if (r->blendAdd) why = "additive";
        else if (r->deferred == 2) why = "slot 37's second pass";
        else if (r->src == 34 && !back) why = "slot 34 polygons";
        else if (r->zMin <= 10.0f) why = "nearer than 10 (interface)";
        else if (r->texId == 1900) why = "the reticle";
        else if (r->texId == U9TEX_BLOB_SHADOW) why = "the blob shadow";
        else if (r->texFmt == 0x02) why = "ALPHA_8 (an effect)";
        else if (r->texId == U9TEX_WATER) why = "water";
        else if (!back && near_ui(r)) why = "the journal";
        fn(ctx, r->first, r->count, r->texId, back, why);
    }
}

int u9gl_reticle_centre(float *x, float *y)
{
    unsigned i;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        if (!r->count || r->texId != 1900) continue;
        if (r->x0 < x0) x0 = r->x0;
        if (r->y0 < y0) y0 = r->y0;
        if (r->x1 > x1) x1 = r->x1;
        if (r->y1 > y1) y1 = r->y1;
    }
    if (x1 < x0) return 0;
    *x = 0.5f * (x0 + x1);
    *y = 0.5f * (y0 + y1);
    return 1;
}

void u9gl_ui_report(void (*out)(const char *fmt, ...))
{
    unsigned i;
    float px, py;
    if (!u9gl_reticle_centre(&px, &py)) { out("rays runs: no reticle drawn this frame"); return; }
    out("rays runs at the reticle %.0f,%.0f (engine %.0fx%.0f): run tex src zMin rgba dw add def ui mark box",
        (double)px, (double)py, (double)g_view.engineW, (double)g_view.engineH);
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        const unsigned char *c = g_verts[r->first].rgba;
        if (!r->count || px < r->x0 || px > r->x1 || py < r->y0 || py > r->y1) continue;
        out("  %u: %d src %d z %.1f rgba %u,%u,%u,%u dw %d add %d def %d ui %d mark %d box %.0f,%.0f-%.0f,%.0f",
            i, r->texId, r->src, (double)r->zMin, c[0], c[1], c[2], c[3], r->depthWrite, r->blendAdd, r->deferred,
            interface_run(r), interface_mark(r, g_view.engineW, g_view.engineH),
            (double)r->x0, (double)r->y0, (double)r->x1, (double)r->y1);
        u9log_line("RAYS run %u: tex %d src %d z %.1f rgba %u,%u,%u,%u dw %d add %d def %d ui %d mark %d box %.0f,%.0f-%.0f,%.0f",
                   i, r->texId, r->src, (double)r->zMin, c[0], c[1], c[2], c[3], r->depthWrite, r->blendAdd, r->deferred,
                   interface_run(r), interface_mark(r, g_view.engineW, g_view.engineH),
                   (double)r->x0, (double)r->y0, (double)r->x1, (double)r->y1);
    }
}

void u9gl_pick_report(void (*out)(const char *fmt, ...))
{
    typedef struct { float q; unsigned run; float c[7]; } Hit;
    Hit hit[16];
    int nh = 0, k;
    unsigned i, t;
    float px, py;
    if (!u9gl_reticle_centre(&px, &py)) { out("pick: no reticle drawn this frame"); return; }
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        float best = -1.0f, col[7] = { 0, 0, 0, 0, 0, 0, 0 };
        if (!r->count || r->texId == 1900 || r->zMin <= 10.0f) continue;
        if (px < r->x0 || px > r->x1 || py < r->y0 || py > r->y1) continue;
        for (t = r->first; t + 2 < r->first + r->count; t += 3) {
            const GlVert *a = &g_verts[t], *b = &g_verts[t + 1], *c = &g_verts[t + 2];
            float d = (b->y - c->y) * (a->x - c->x) + (c->x - b->x) * (a->y - c->y), wa, wb, wc;
            if (d > -1e-6f && d < 1e-6f) continue;
            wa = ((b->y - c->y) * (px - c->x) + (c->x - b->x) * (py - c->y)) / d;
            wb = ((c->y - a->y) * (px - c->x) + (a->x - c->x) * (py - c->y)) / d;
            wc = 1.0f - wa - wb;
            if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
            {
                float q = wa * a->q + wb * b->q + wc * c->q;
                int ch;
                if (q > best) {
                    best = q;
                    for (ch = 0; ch < 4; ch++)
                        col[ch] = wa * a->rgba[ch] + wb * b->rgba[ch] + wc * c->rgba[ch];
                    col[4] = wa * a->fog + wb * b->fog + wc * c->fog;
                    col[5] = (wa * a->sq + wb * b->sq + wc * c->sq) / q;
                    col[6] = (wa * a->tq + wb * b->tq + wc * c->tq) / q;
                }
            }
        }
        if (best <= 0.0f) continue;
        if (nh < 16) nh++;
        for (k = nh - 1; k > 0 && hit[k - 1].q < best; k--) hit[k] = hit[k - 1];
        if (k < 16) { hit[k].q = best; hit[k].run = i; memcpy(hit[k].c, col, sizeof col); }
    }
    if (!nh) { out("pick: nothing drawn under the reticle"); return; }
    out("pick under the reticle, nearest first:%s",
        u9tex_enabled() ? "" : "  (texture_override = 0: no replacements in use)");
    for (k = 0; k < nh && k < 8; k++) {
        const GlRun *r = &g_lastRuns[hit[k].run];
        u9log_line("GLPICK %d: tex %d/%d at eye depth %.0f - %u triangles, fmt 0x%02X, vertex rgba %.0f,%.0f,%.0f,%.0f, fog %.2f, gl tex %u, "
                   "flush %u, drawn %u of the frame, alpha raw %u..%u mean %u, bloom %.2f (%s), %s%s%s%s%s", k + 1, r->texId,
                   r->texId2, (double)(1.0f / hit[k].q), r->count / 3, r->texFmt,
                   (double)hit[k].c[0], (double)hit[k].c[1], (double)hit[k].c[2], (double)hit[k].c[3], (double)hit[k].c[4], r->glTex, r->epoch, r->drawPos,
                   r->aRawMin, r->aRawMax, r->aN ? r->aSum / r->aN : 0u, (double)r->bloomShare, r->bloomWhy ? r->bloomWhy : "?",
                   r->blendAdd ? "additive" : r->deferred ? "translucent (sorted)" : "opaque",
                   r->depthWrite ? ", writes depth" : "", r->alphaTest || r->chroma ? ", cutout" : "",
                   r->texId == U9TEX_WATER ? " - WATER (49)" : "",
                   u9gl_two_sided_tex(r->texId) ? ", drawn from both sides (gl_two_sided_textures)" : "");
        if (r->glTex) {
            GLint tw = 0, th = 0, was = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &was);
            glBindTexture(GL_TEXTURE_2D, r->glTex);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
            if (tw > 0 && th > 0 && tw * th <= 1024 * 1024) {
                unsigned char *px = (unsigned char *)malloc((size_t)tw * th * 4);
                if (px) {
                    double sum[4] = { 0, 0, 0, 0 };
                    int n = tw * th, p, ch;
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    int sx = (int)floorf(hit[k].c[5] * (float)tw), sy = (int)floorf(hit[k].c[6] * (float)th);
                    const unsigned char *tx;
                    for (p = 0; p < n; p++) for (ch = 0; ch < 4; ch++) sum[ch] += px[p * 4 + ch];
                    sx = ((sx % tw) + tw) % tw;
                    sy = ((sy % th) + th) % th;
                    tx = px + ((size_t)sy * tw + sx) * 4;
                    u9log_line("GLPICK %d:   gl tex %u is %dx%d, mean rgba %.0f,%.0f,%.0f,%.0f; at s,t %.3f,%.3f the texel is %u,%u,%u,%u",
                               k + 1, r->glTex, tw, th, sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n,
                               (double)hit[k].c[5], (double)hit[k].c[6], tx[0], tx[1], tx[2], tx[3]);
                    free(px);
                }
            }
            glBindTexture(GL_TEXTURE_2D, (GLuint)was);
        }
        out("%d  tex %d/%d  depth %.0f  %s%s", k + 1, r->texId, r->texId2, (double)(1.0f / hit[k].q),
            r->blendAdd ? "additive" : r->deferred ? "translucent" : "opaque",
            r->texId == U9TEX_WATER ? "  (water)" : "");
        if (r->texId >= 0) {
            char file[MAX_PATH];
            U9TexQuery q = u9tex_query(r->texId, r->texId2, file, sizeof file);
            if (q != U9TEX_Q_OFF && q != U9TEX_Q_NONE) {
                const char *what = q == U9TEX_Q_USED     ? "REPLACED by"
                                 : q == U9TEX_Q_REJECTED ? "replacement REJECTED (see the log):"
                                 : q == U9TEX_Q_UNTRIED  ? "replacement found, not loaded yet:"
                                 : "composed at runtime - cannot be replaced";
                out("   %s %s", what, file);
                u9log_line("GLPICK %d:   texture override: %s %s", k + 1, what, file);
            } else if (q == U9TEX_Q_NONE) {
                out("   not replaced (no %d_%d file in textures\\)", r->texId, r->texId2);
            }
        }
        if (moon_tex(r->texId)) {
            static const char *const PHASE[5] = { "new", "crescent", "half", "gibbous", "full" };
            int cls = moon_phase(r->texId);
            float f = u9glpost_moon_phase(cls);
            u9log_line("GLPICK %d:   moon %s - light %.0f%%, bloom %.0f%% (phase share %.0f%%)", k + 1,
                       PHASE[cls], (double)(u9glpost_moon_bright() * f * 100.0f),
                       (double)(moon_disc(r->texId) ? u9glpost_moon_bloom_setting(0) * f * 100.0f : 0.0f),
                       (double)(f * 100.0f));
            out("   moon %s: light %.0f%%, bloom %.0f%% (phase share %.0f%%, drawn at alpha %u)", PHASE[cls],
                (double)(u9glpost_moon_bright() * f * 100.0f),
                (double)(moon_disc(r->texId) ? u9glpost_moon_bloom_setting(0) * f * 100.0f : 0.0f),
                (double)(f * 100.0f), r->aN ? r->aSum / r->aN : 0u);
        } else if (moon_halo(r)) {
            out("   moon halo: strength %.0f%%, bloom %.0f%% (the game fades it by phase itself)",
                (double)(u9glpost_moon_halo() * 100.0f), (double)(u9glpost_moon_bloom_setting(1) * 100.0f));
        }
    }
}

static float glide_step(float w)
{
    return ldexpf(1.0f, (int)floorf(log2f(w > 1.0f ? w : 1.0f)) - 12);
}

static int run_writes_depth(const GlRun *r)
{
    return r->depthWrite && !r->blendAdd && !r->deferred;
}

void u9gl_pick_ties(void (*out)(const char *fmt, ...))
{
    typedef struct { unsigned a, b; unsigned n; float dmin, dmax, w; } Pair;
    Pair pr[48];
    int np = 0, k, col;
    float px, py;
    unsigned samples = 0;
    if (!u9gl_reticle_centre(&px, &py)) { out("pick ties: no reticle drawn this frame"); return; }
    for (col = -1; col <= 1; col++) {
        float sx = px + (float)col * 60.0f;
        int sy;
        for (sy = -120; sy <= 120; sy++) {
            float y = py + (float)sy;
            unsigned hr[48], ht[48];
            float hq[48];
            int nh = 0, i2, j2;
            unsigned i, t;
            for (i = 0; i < g_lastRunCount && nh < 48; i++) {
                const GlRun *r = &g_lastRuns[i];
                if (!r->count || r->texId == 1900 || r->zMin <= 10.0f || !r->depthTest) continue;
                if (sx < r->x0 || sx > r->x1 || y < r->y0 || y > r->y1) continue;
                for (t = r->first; t + 2 < r->first + r->count && nh < 48; t += 3) {
                    const GlVert *a = &g_verts[t], *b = &g_verts[t + 1], *c = &g_verts[t + 2];
                    float d = (b->y - c->y) * (a->x - c->x) + (c->x - b->x) * (a->y - c->y), wa, wb, wc, q;
                    if (d > -1e-6f && d < 1e-6f) continue;
                    wa = ((b->y - c->y) * (sx - c->x) + (c->x - b->x) * (y - c->y)) / d;
                    wb = ((c->y - a->y) * (sx - c->x) + (a->x - c->x) * (y - c->y)) / d;
                    wc = 1.0f - wa - wb;
                    if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                    q = wa * a->q + wb * b->q + wc * c->q;
                    if (q > 0.0f) { hr[nh] = i; ht[nh] = t; hq[nh] = q; nh++; }
                }
            }
            samples++;
            for (i2 = 0; i2 < nh; i2++)
                for (j2 = 0; j2 < nh; j2++) {
                    const GlRun *ra = &g_lastRuns[hr[i2]], *rb = &g_lastRuns[hr[j2]];
                    float wa = 1.0f / hq[i2], wb = 1.0f / hq[j2], dw = wb - wa;
                    int p;
                    if (i2 == j2) continue;
                    if (hr[i2] == hr[j2] ? ht[i2] >= ht[j2] : ra->drawPos >= rb->drawPos) continue;
                    if (hr[i2] == hr[j2]) {
                        int u, v, shared = 0;
                        for (u = 0; u < 3; u++) for (v = 0; v < 3; v++)
                            if (g_verts[ht[i2] + u].x == g_verts[ht[j2] + v].x && g_verts[ht[i2] + u].y == g_verts[ht[j2] + v].y) shared = 1;
                        if (shared) continue;
                    }
                    if (!run_writes_depth(ra)) continue;
                    { float st = glide_step(wa < wb ? wa : wb); if (dw < -16.0f * st || dw > 16.0f * st) continue; }
                    for (p = 0; p < np; p++) if (pr[p].a == hr[i2] && pr[p].b == hr[j2]) break;
                    if (p == np) {
                        if (np == 48) continue;
                        pr[np].a = hr[i2]; pr[np].b = hr[j2]; pr[np].n = 0;
                        pr[np].dmin = 1e9f; pr[np].dmax = -1e9f; pr[np].w = wa;
                        np++;
                    }
                    pr[p].n++;
                    if (dw < pr[p].dmin) pr[p].dmin = dw;
                    if (dw > pr[p].dmax) pr[p].dmax = dw;
                }
        }
    }
    if (!np) {
        out("pick ties: none in %u points round the reticle (depth glide %s)", samples, u9glpost_depth_glide() ? "on" : "off");
        u9log_line("GLTIES none in %u points (depth glide %s)", samples, u9glpost_depth_glide() ? "on" : "off");
        return;
    }
    for (k = 1; k < np; k++) {
        Pair x = pr[k];
        int m = k;
        while (m > 0 && pr[m - 1].n < x.n) { pr[m] = pr[m - 1]; m--; }
        pr[m] = x;
    }
    out("pick ties: %d pairs within 16 steps either way in %u points (depth glide %s), most first:", np, samples,
        u9glpost_depth_glide() ? "on" : "off");
    for (k = 0; k < np && k < 8; k++) {
        const GlRun *a = &g_lastRuns[pr[k].a], *b = &g_lastRuns[pr[k].b];
        const char *ka = a->blendAdd ? "additive" : a->deferred ? "translucent" : "opaque";
        const char *kb = b->blendAdd ? "additive" : b->deferred ? "translucent" : "opaque";
        out("%d  %u pts  tex %d/%d (%s) then %d/%d (%s)%s  at %.0f, apart %.3f-%.3f (step %.3f)", k + 1, pr[k].n,
            a->texId, a->texId2, ka, b->texId, b->texId2, kb, pr[k].a == pr[k].b ? " SAME RUN" : "", (double)pr[k].w,
            (double)pr[k].dmin, (double)pr[k].dmax, (double)glide_step(pr[k].w));
        u9log_line("GLTIES %d: %u pts, tex %d/%d %s fmt 0x%02X flags 0x%04X src %d pos %u, then tex %d/%d %s%s fmt 0x%02X flags 0x%04X src %d pos %u; at %.0f apart %.3f-%.3f, step %.3f",
                   k + 1, pr[k].n, a->texId, a->texId2, ka, a->texFmt, a->flags, a->src, a->drawPos,
                   b->texId, b->texId2, kb, run_writes_depth(b) ? " writes depth" : "", b->texFmt, b->flags, b->src,
                   b->drawPos, (double)pr[k].w, (double)pr[k].dmin, (double)pr[k].dmax, (double)glide_step(pr[k].w));
    }
}

void u9gl_beam_report(int texId, void (*out)(const char *fmt, ...))
{
    float pt[64][3];
    int np = 0, k, j;
    unsigned i, t, runs = 0, tris = 0, cw = 0, ccw = 0, flat = 0;
    if (!g_view.valid) { out("beam: no view this frame - is a world loaded?"); return; }
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        if (!r->count || r->texId != texId) continue;
        runs++;
        tris += r->count / 3;
        u9log_line("GLBEAM run %u: %u tris, slot %d, %s, depthW=%d, epoch %d, pooled %d, alpha %u, box %.0f,%.0f..%.0f,%.0f",
                   i, r->count / 3, r->src, r->blendAdd ? "additive" : "alpha", r->depthWrite, r->epoch,
                   r->deferred, r->aN ? r->aSum / r->aN : 0u,
                   (double)r->x0, (double)r->y0, (double)r->x1, (double)r->y1);
        for (t = r->first; t + 2 < r->first + r->count; t += 3) {
            const GlVert *v[3] = { &g_verts[t], &g_verts[t + 1], &g_verts[t + 2] };
            float d = (v[1]->x - v[0]->x) * (v[2]->y - v[0]->y) - (v[2]->x - v[0]->x) * (v[1]->y - v[0]->y);
            if (d > 0.5f) cw++; else if (d < -0.5f) ccw++; else flat++;
            for (j = 0; j < 3; j++) {
                float w, e[3], wp[3];
                if (v[j]->q == 0.0f) continue;
                w = 1.0f / v[j]->q;
                e[0] = (v[j]->x - g_view.cx) * w / g_view.sx;
                e[1] = w;
                e[2] = (g_view.cy - v[j]->y) * w / g_view.sy;
                for (k = 0; k < 3; k++)
                    wp[k] = g_view.m[k * 4] * e[0] + g_view.m[k * 4 + 1] * e[1] + g_view.m[k * 4 + 2] * e[2] + g_view.m[k * 4 + 3];
                for (k = 0; k < np; k++)
                    if (fabsf(pt[k][0] - wp[0]) < 2.0f && fabsf(pt[k][1] - wp[1]) < 2.0f && fabsf(pt[k][2] - wp[2]) < 2.0f)
                        break;
                if (k == np && np < 64) { memcpy(pt[np], wp, sizeof wp); np++; }
            }
        }
    }
    if (!runs) { out("beam: texture %d was not drawn this frame", texId); return; }
    for (k = 0; k < np; k++)
        u9log_line("GLBEAM corner %2d: %.0f,%.0f,%.0f", k, (double)pt[k][0], (double)pt[k][1], (double)pt[k][2]);
    out("beam: texture %d, %u runs, %u triangles - %u one winding, %u the other, %u edge-on; %d corners (in the log)",
        texId, runs, tris, cw, ccw, flat, np);
}

#define HIDE_MAX 32
static int      g_hide[HIDE_MAX], g_nHide;
static unsigned g_hiddenRuns;

static int hidden_tex(int id)
{
    int k;
    for (k = 0; k < g_nHide; k++) if (g_hide[k] == id) return 1;
    return 0;
}

void u9gl_hide_command(const char *arg, void (*out)(const char *fmt, ...))
{
    char buf[256];
    int k, n = 0;
    while (arg && *arg == ' ') arg++;
    if (arg && !strcmp(arg, "off")) {
        g_nHide = 0;
        out("nothing hidden");
        u9log_line("GLHIDE off");
        return;
    }
    while (arg && *arg) {
        char *e;
        long id = strtol(arg, &e, 10);
        if (e == arg) { arg++; continue; }
        if (!hidden_tex((int)id) && g_nHide < HIDE_MAX) g_hide[g_nHide++] = (int)id;
        arg = e;
        n++;
    }
    buf[0] = 0;
    for (k = 0; k < g_nHide; k++) {
        size_t len = strlen(buf);
        snprintf(buf + len, sizeof buf - len, k ? ", %d" : "%d", g_hide[k]);
    }
    if (n) u9log_line("GLHIDE textures hidden: %s", buf);
    out(g_nHide ? "hidden: %s (%u runs skipped so far) - post hide off shows them again"
                : "nothing hidden - post hide <texture ...> skips drawing those textures", buf, g_hiddenRuns);
}

unsigned u9gl_effect_runs(void (*fn)(void *ctx, const U9EffectRun *r), void *ctx)
{
    unsigned i, n = 0;
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        U9EffectRun e;
        if (!r->count || !(r->blendAdd || r->texFmt == 0x02) || r->deferred == 2 || r->src == 34 ||
            r->zMin <= 10.0f || r->texId == 1900 || r->texId == U9TEX_BLOB_SHADOW || near_ui(r))
            continue;
        e.texId = r->texId; e.texId2 = r->texId2;
        e.first = r->first; e.count = r->count;
        e.glTex = r->glTex; e.texFmt = r->texFmt;
        e.additive = r->blendAdd;
        fn(ctx, &e);
        n++;
    }
    return n;
}

static unsigned g_fadeRuns, g_fadeDepth, g_fadeOut;
static unsigned g_fadeLast[3];
static float    g_fadeSt[4], g_fadeStLast[4];

static int g_effectNow;

static int effect_layer(const GlRun *r)
{
    return (r->blendAdd || r->texFmt == 0x02) && r->deferred != 2 && r->texId != U9TEX_BLOB_SHADOW &&
           r->texId != 1900 && r->src != 34 && r->zMin > 10.0f && r->glTex != 0 && !near_ui(r);
}

int u9gl_effect_run_now(void)
{
    return g_effectNow;
}

static float fade_run(const GlRun *r)
{
    float w;
    unsigned v;
    g_effectNow = 0;
    if (!(r->blendAdd || r->texFmt == 0x02) || r->deferred == 2 || r->texId == U9TEX_BLOB_SHADOW ||
        r->texId == 1900 || r->src == 34 || r->zMin <= 10.0f || r->glTex == 0)
        return 0.0f;
    g_effectNow = 1;
    w = u9glpost_effect_fade();
    if (w == 0.0f) return 0.0f;
    if (!g_fadeRuns) { g_fadeSt[0] = g_fadeSt[2] = 1e30f; g_fadeSt[1] = g_fadeSt[3] = -1e30f; }
    g_fadeRuns++;
    if (r->depthWrite) g_fadeDepth++;
    for (v = r->first; v < r->first + r->count; v++) {
        float q = g_verts[v].q != 0.0f ? g_verts[v].q : 1.0f, s = g_verts[v].sq / q, t = g_verts[v].tq / q;
        if (s < -0.01f || s > 1.01f || t < -0.01f || t > 1.01f) { g_fadeOut++; break; }
        if (s < g_fadeSt[0]) g_fadeSt[0] = s;
        if (s > g_fadeSt[1]) g_fadeSt[1] = s;
        if (t < g_fadeSt[2]) g_fadeSt[2] = t;
        if (t > g_fadeSt[3]) g_fadeSt[3] = t;
    }
    return w;
}

void u9gl_fade_report(void (*out)(const char *fmt, ...))
{
    out("last frame: %u effect runs faded, %u of them write depth, %u with coordinates off 0..1;"
        " coordinates s %.2f..%.2f, t %.2f..%.2f", g_fadeLast[0], g_fadeLast[1], g_fadeLast[2],
        (double)g_fadeStLast[0], (double)g_fadeStLast[1], (double)g_fadeStLast[2], (double)g_fadeStLast[3]);
}

unsigned u9gl_runs_with_texture(int texId, void (*fn)(void *ctx, unsigned first, unsigned count, unsigned glTex,
                                                      int cutout), void *ctx)
{
    unsigned i, n = 0;
    for (i = 0; i < g_lastRunCount; i++) {
        const GlRun *r = &g_lastRuns[i];
        if (!r->count || r->texId != texId || r->zMin <= 10.0f) continue;
        fn(ctx, r->first, r->count, r->glTex, 0);
        n++;
    }
    return n;
}

const void *u9gl_caster_vertices(int *stride)
{
    *stride = (int)sizeof(GlVert);
    return &g_verts[0].x;
}

static void capture_view(unsigned engineW, unsigned engineH, int vx, int vy, int vw, int vh,
                         int mode)
{
    const char *cam;
    g_view.valid = 0;
    if (!g_renderer || !g_useDepth) return;
    cam = *(const char *const *)((const char *)g_renderer + U9_OFF_VIEW);
    if (!cam) return;
    g_view.sx = *(const float *)(cam + U9_CAM_PROJ_SX);
    g_view.sy = *(const float *)(cam + U9_CAM_PROJ_SY);
    if (g_view.sx == 0.0f || g_view.sy == 0.0f) return;
    g_view.cx = *(const float *)(cam + U9_CAM_CENTRE_X);
    g_view.cy = *(const float *)(cam + U9_CAM_CENTRE_Y);
    memcpy(g_view.m, cam + U9_CAM_TO_WORLD, sizeof g_view.m);
    g_view.depthMode = mode;
    g_view.zRef = U9GL_Z_REF;
    g_view.zFar = active_far();
    g_view.vx = vx; g_view.vy = vy; g_view.vw = vw; g_view.vh = vh;
    g_view.engineW = (float)engineW;
    g_view.engineH = (float)engineH;
    g_view.valid = 1;
}

static int run_extent(const GlRun *r, float lo[3], float hi[3])
{
    unsigned t;
    int k, n = 0;
    for (k = 0; k < 3; k++) { lo[k] = 1e30f; hi[k] = -1e30f; }
    if (!g_view.valid || !r->count) return 0;
    for (t = r->first; t < r->first + r->count; t++) {
        const GlVert *a = &g_verts[t];
        float w, e[3], wp;
        if (a->q == 0.0f) continue;
        w = 1.0f / a->q;
        e[0] = (a->x - g_view.cx) * w / g_view.sx;
        e[1] = w;
        e[2] = (g_view.cy - a->y) * w / g_view.sy;
        for (k = 0; k < 3; k++) {
            wp = g_view.m[k * 4] * e[0] + g_view.m[k * 4 + 1] * e[1] + g_view.m[k * 4 + 2] * e[2] + g_view.m[k * 4 + 3];
            if (wp < lo[k]) lo[k] = wp;
            if (wp > hi[k]) hi[k] = wp;
        }
        n++;
    }
    return n >= 3;
}

static int extent_upright(const float lo[3], const float hi[3])
{
    float zs = hi[2] - lo[2], gs = hi[0] - lo[0] > hi[1] - lo[1] ? hi[0] - lo[0] : hi[1] - lo[1];
    return zs > 100.0f && zs > 0.5f * gs;
}

static int run_upright(const GlRun *r)
{
    static int on = -1;
    float lo[3], hi[3];
    if (on < 0) on = u9cfg_int("gl_fog_volume_upright", 0);
    return on && run_extent(r, lo, hi) && extent_upright(lo, hi);
}

static int gate_run(const GlRun *r)
{
    float lo[3], hi[3], c[3];
    int k;
    if (!u9fx_gate_tex(r->texId) || !run_extent(r, lo, hi) || !extent_upright(lo, hi)) return 0;
    for (k = 0; k < 3; k++) c[k] = 0.5f * (lo[k] + hi[k]);
    return u9fx_gate_here(c);
}

void u9gl_flush_frame(unsigned engineW, unsigned engineH)
{
    static int announced;
    unsigned i;

    g_view.valid = 0;

    read_switches();
    fog_report();

    if (!announced && g_count) {
        announced = 1;
        u9log_line("GLRAST first frame: %u triangles in %u runs, "
                   "ortho 0..%u x 0..%u, z %.3f..%.3f -> %s depth, "
                   "GL_LEQUAL (slot 46 = GR_CMP_LEQUAL, slot 43 = W-buffer)",
                   g_count / 3, g_runCount, engineW, engineH, g_zMin, g_zMax,
                   u9gl_depth_mode_name());
    }

    u9gl_upload_pending();

    if (g_count) {
        int vw, vh, vx, vy;
        u9gl_picture_box(engineW, engineH, g_surfW, g_surfH, &vx, &vy, &vw, &vh);
        glViewport(vx, vy, vw, vh);
        g_vpX = vx; g_vpY = vy; g_vpW = vw; g_vpH = vh;
        {
            static int aspectLogged;
            if (!aspectLogged) { aspectLogged = 1;
            u9log_line("GLRAST aspect: engine %ux%u in surface %dx%d -> viewport "
                       "%dx%d at %d,%d (%s)",
                       engineW, engineH, g_surfW, g_surfH, vw, vh, vx, vy,
                       (vx || vy) ? "bars preserved, matching Glide"
                                  : "exact fit, no bars"); }
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, (double)engineW, (double)engineH, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        if (g_useDepth) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(g_depthFunc);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDisable(GL_CULL_FACE);
        glShadeModel(GL_SMOOTH);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
        if (g_useBlend) glEnable(GL_BLEND);
        else            glDisable(GL_BLEND);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(GlVert), &g_verts[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GlVert), &g_verts[0].rgba[0]);
        glTexCoordPointer(4, GL_FLOAT, sizeof(GlVert), &g_verts[0].sq);

        if (!g_fogBProbed) {
            g_fogBProbed = 1;
            g_fogCoordPointer = (FogCoordPointerFn)(void *)
                wglGetProcAddress("glFogCoordPointerEXT");
            if (!g_fogCoordPointer)
                g_fogCoordPointer = (FogCoordPointerFn)(void *)
                    wglGetProcAddress("glFogCoordPointer");
            u9log_line("GLFOG B fog-coord entry point %s",
                       g_fogCoordPointer ? "present - table fog available"
                                         : "MISSING - table fog stays off");
        }
        if (g_fogBActive && g_fogCoordPointer) {
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, 0.0f);
            glFogf(GL_FOG_END,   1.0f);
            glFogfv(GL_FOG_COLOR, g_fogBColour);
            glFogi(GL_FOG_COORDINATE_SOURCE_EXT, GL_FOG_COORDINATE_EXT);
            glEnableClientState(GL_FOG_COORDINATE_ARRAY_EXT);
            g_fogCoordPointer(GL_FLOAT, sizeof(GlVert), &g_verts[0].fog);
            glEnable(GL_FOG);
        } else {
            glDisable(GL_FOG);
            glDisableClientState(GL_FOG_COORDINATE_ARRAY_EXT);
        }

        static const GLfloat kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        int pass, fogBlack = 0, wbuf = 0;
        float bloom;
        g_fadeLast[0] = g_fadeRuns; g_fadeLast[1] = g_fadeDepth; g_fadeLast[2] = g_fadeOut;
        if (g_fadeRuns) memcpy(g_fadeStLast, g_fadeSt, sizeof g_fadeSt);
        g_fadeRuns = g_fadeDepth = g_fadeOut = 0;
        unsigned order[U9GL_MAX_RUNS], n = 0, a, ep, maxEpoch = 0;
        unsigned drawn = 0;
        int noUi = u9shot_pending();
        draw_watch();
        if (g_depthMode == 2 && g_useDepth) {
            wbuf = u9gl_wbuf_begin(U9GL_Z_REF, active_far(),
                                   g_fogBActive && g_fogCoordPointer);
            if (!wbuf) g_depthMode = 1;
        }
        if (wbuf) {
            float jit[2];
            u9aa_taa_jitter(vw, vh, jit);
            u9gl_wbuf_set_jitter(jit[0], jit[1]);
        }
        capture_view(engineW, engineH, vx, vy, vw, vh, wbuf ? 2 : g_depthMode);
        book_scan(engineW, engineH);
        for (i = 0; i < g_runCount; i++)
            if (g_runs[i].count && g_runs[i].epoch > maxEpoch)
                maxEpoch = g_runs[i].epoch;
        moon_frame();
        sort_order_log();
        g_slMovedLast = g_slMoved;
        g_slMoved = 0;
        for (ep = 0; ep <= maxEpoch; ep++)
        for (pass = 0; pass < 3; pass++) {
        n = 0;
        for (i = 0; i < g_runCount; i++)
            if (g_runs[i].count && g_runs[i].deferred == pass &&
                g_runs[i].epoch == ep) order[n++] = i;
        if (pass == 1) {
            pooled_sort(order, n);
            sort_layers_apply(order, n);
            sort_order_watch(order, n);
        }
        for (a = 0; a < n; a++) {
            i = order[a];
            if (!g_useGeom) continue;
            if (g_runs[i].texId == U9TEX_BLOB_SHADOW && u9shadow_hide_blob()) continue;
            if (g_nHide && hidden_tex(g_runs[i].texId)) { g_hiddenRuns++; continue; }
            if (u9fog_hides(g_runs[i].texId) && !run_upright(&g_runs[i]))
                continue;
            if (!glare_scale(&g_runs[i])) continue;
            if (u9beam_hides(g_runs[i].texId)) {
                g_runs[i].glTex = g_useTex ? u9gl_texture_for(g_renderer, g_runs[i].mat, g_runs[i].texId,
                                                              g_runs[i].texId2, g_runs[i].texFmt, U9GL_NO_KEY) : 0;
                continue;
            }
            if (noUi && interface_run(&g_runs[i])) continue;
            g_runs[i].drawPos = drawn++;
            {
                GLuint tex = g_useTex ? u9gl_texture_for(
                        g_renderer, g_runs[i].mat,
                        g_runs[i].texId, g_runs[i].texId2, g_runs[i].texFmt,
                        (g_runs[i].chroma && g_useChroma) ? g_runs[i].chromaKey
                                                          : U9GL_NO_KEY)
                                      : 0;
                g_runs[i].glTex = tex;
                if (!tex)
                    note_untextured(g_runs[i].texId, g_runs[i].texId2,
                                    g_runs[i].texFmt);
                if (tex) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    {
                        int pt = (g_pointMode == 2) ? 1 :
                                 (g_pointMode == 0) ? 0 : g_runs[i].pointFilter;
                        GLint f = pt ? GL_NEAREST : GL_LINEAR;
                        GLint mn = pt ? GL_NEAREST : u9gl_min_filter();
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mn);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
                        {
                            float an = u9gl_anisotropy();
                            if (an > 1.0f)
                                glTexParameterf(GL_TEXTURE_2D,
                                                0x84FE ,
                                                pt ? 1.0f : an);
                        }
                    }
                    if (g_useClamp) {
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                                        g_runs[i].clampS ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                                        g_runs[i].clampT ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                    }
                } else {
                    glDisable(GL_TEXTURE_2D);
                }
            }
            if (g_useDepth) glEnable(GL_DEPTH_TEST);
            else            glDisable(GL_DEPTH_TEST);
            {
                int dw = g_runs[i].depthWrite && g_useDepth &&
                         !g_runs[i].blendAdd &&
                         !g_runs[i].deferred &&
                         !(g_runs[i].texFmt == 0x02 && !u9glpost_effect_depth_write() &&
                           g_runs[i].src != 34 && g_runs[i].zMin > 10.0f && !near_ui(&g_runs[i]));
                glDepthMask(dw ? GL_TRUE : GL_FALSE);
                if (wbuf) u9gl_wbuf_slack(!dw);
                u9glpost_exact_mask(dw || (wbuf && interface_mark(&g_runs[i], (float)engineW, (float)engineH)));
                u9glpost_bloom_mask(!(wbuf && interface_mark(&g_runs[i], (float)engineW, (float)engineH)));
            }
            glBlendFunc(GL_SRC_ALPHA,
                        g_runs[i].blendAdd ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            if (g_runs[i].alphaTest || g_runs[i].chroma) {
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER,
                            (float)u9cfg_int("alpha_ref", 128) / 255.0f);
            } else {
                glDisable(GL_ALPHA_TEST);
            }
            if (g_fogBActive && g_fogCoordPointer &&
                (g_runs[i].deferred == 2) != fogBlack) {
                fogBlack = (g_runs[i].deferred == 2);
                glFogfv(GL_FOG_COLOR, fogBlack ? kBlack : g_fogBColour);
            }
            bloom = wbuf ? u9glpost_bloom_share(g_runs[i].texId, effect_layer(&g_runs[i]),
                                                g_runs[i].blendAdd) : 0.0f;
            g_runs[i].bloomWhy = wbuf ? u9glpost_bloom_why() : "no shader";
            if (wbuf && gate_run(&g_runs[i]))
                { bloom = u9fx_gate_glow(); g_runs[i].bloomWhy = "black gate"; }
            if (wbuf && moon_disc(g_runs[i].texId))
                { bloom = u9glpost_moon_bloom(0) * u9glpost_moon_phase(moon_phase(g_runs[i].texId)); g_runs[i].bloomWhy = "moon disc"; }
            else if (wbuf && moon_halo(&g_runs[i])) { bloom = u9glpost_moon_bloom(1); g_runs[i].bloomWhy = "moon halo"; }
            g_runs[i].bloomShare = wbuf ? bloom : -1.0f;
            if (wbuf) u9gl_wbuf_alpha(moon_halo(&g_runs[i]) ? u9glpost_moon_halo()
                                      : u9fx_soot_scale(g_runs[i].texId)
                                        * (g_runs[i].blendAdd ? u9fx_glow_gain(g_runs[i].texId) : 1.0f));
            if (wbuf) u9gl_wbuf_rgb(moon_tex(g_runs[i].texId)
                                    ? u9glpost_moon_bright() * u9glpost_moon_phase(moon_phase(g_runs[i].texId))
                                    : u9glpost_guardian_bright(g_runs[i].texId));
            if (wbuf) {
                float tint[3] = { 1.0f, 1.0f, 1.0f };
                unsigned rmask = g_runs[i].glTex ? u9fx_rune_mask(g_runs[i].texId, g_runs[i].glTex) : 0u;
                if (rmask) u9fx_rune_tint(g_runs[i].texId, tint);
                else if (g_runs[i].glTex && (rmask = u9fx_glyph_mask(g_runs[i].texId, g_runs[i].glTex)) != 0u)
                    u9fx_glyph_tint(g_runs[i].texId, tint);
                u9gl_wbuf_rune(rmask, tint);
            }
            if (wbuf) u9gl_wbuf_selflit(u9lava_is(g_runs[i].texId) && u9lava_glows());
            if (wbuf) u9gl_wbuf_jitter(!interface_run(&g_runs[i]));
            if (wbuf) u9gl_wbuf_ui(!interface_mark(&g_runs[i], (float)engineW, (float)engineH) ? 0
                                   : g_runs[i].depthWrite && g_useDepth && !g_runs[i].blendAdd && !g_runs[i].deferred
                                   ? 2 : 1);
            g_effectNow = 0;
            if (wbuf && (near_ui(&g_runs[i]) ||
                         (u9lava_is(g_runs[i].texId) && u9lava_glows()) ||
                         moon_disc(g_runs[i].texId)))
                u9gl_wbuf_run(g_runs[i].glTex != 0, g_runs[i].alphaTest || g_runs[i].chroma,
                              (float)u9cfg_int("alpha_ref", 128) / 255.0f, 0, 1, bloom, 0.0f);
            else if (wbuf)
                u9gl_wbuf_run(g_runs[i].glTex != 0,
                              g_runs[i].alphaTest || g_runs[i].chroma,
                              (float)u9cfg_int("alpha_ref", 128) / 255.0f,
                              g_runs[i].depthWrite && g_useDepth && !g_runs[i].blendAdd &&
                              g_runs[i].deferred != 2 &&
                              g_runs[i].texId != U9TEX_BLOB_SHADOW &&
                              g_runs[i].texFmt != 0x02,
                              (g_runs[i].blendAdd || g_runs[i].texFmt == 0x02) &&
                              g_runs[i].deferred != 2 && g_runs[i].texId != U9TEX_BLOB_SHADOW &&
                              !u9glpost_night_graded(g_runs[i].texId),
                              bloom,
                              fade_run(&g_runs[i]));
            u9metrics_draw((unsigned)g_runs[i].count);
            {
                int gs = glare_scale(&g_runs[i]);
                static unsigned char keep[1024];
                unsigned k, n = g_runs[i].count <= sizeof keep ? g_runs[i].count : 0;
                GlVert *gv = &g_verts[g_runs[i].first];
                if (gs != 100)
                    for (k = 0; k < n; k++) {
                        unsigned a = gv[k].rgba[3] * (unsigned)gs / 100u;
                        keep[k] = gv[k].rgba[3];
                        gv[k].rgba[3] = (unsigned char)(a > 255u ? 255u : a);
                    }
                glDrawArrays(GL_TRIANGLES, (GLint)g_runs[i].first,
                             (GLsizei)g_runs[i].count);
                if (gs != 100)
                    for (k = 0; k < n; k++) gv[k].rgba[3] = keep[k];
            }
        }
        }
        if (g_waterDepth && g_useDepth) {
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            u9glpost_exact_mask(1);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
            if (wbuf) { u9gl_wbuf_run(0, 0, 0.0f, 0, 0, 0.0f, 0.0f); u9gl_wbuf_alpha(1.0f); u9gl_wbuf_rgb(1.0f); u9gl_wbuf_rune(0u, NULL); u9gl_wbuf_selflit(0); u9gl_wbuf_ui(0); u9gl_wbuf_slack(0); }
            for (i = 0; i < g_runCount; i++) {
                const GlRun *r = &g_runs[i];
                if (!r->count || r->texId != U9TEX_WATER || !r->deferred || r->deferred == 2 || r->blendAdd)
                    continue;
                glDrawArrays(GL_TRIANGLES, (GLint)r->first, (GLsizei)r->count);
            }
            if (wbuf && u9glpost_water_mark_begin()) {
                GLint df = GL_LESS;
                glGetIntegerv(GL_DEPTH_FUNC, &df);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
                glDepthFunc(GL_LEQUAL);
                u9gl_wbuf_mark(1);
                for (i = 0; i < g_runCount; i++) {
                    const GlRun *r = &g_runs[i];
                    if (!r->count || r->texId != U9TEX_WATER || r->deferred == 2 || r->blendAdd)
                        continue;
                    glDrawArrays(GL_TRIANGLES, (GLint)r->first, (GLsizei)r->count);
                }
                u9gl_wbuf_mark(0);
                if (g_waterCover < 0) g_waterCover = u9cfg_int("gl_water_cover", 1);
                g_waterCovered = 0;
                if (g_waterCover) {
                    float ref = (float)u9cfg_int("alpha_ref", 128) / 255.0f;
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
                    u9gl_wbuf_mark(2);
                    for (i = 0; i < g_runCount; i++) {
                        const GlRun *r = &g_runs[i];
                        if (!r->count || r->deferred != 1 || r->texId == U9TEX_WATER || r->blendAdd ||
                            near_ui(r) || u9fog_hides(r->texId) || u9beam_hides(r->texId)) continue;
                        u9gl_wbuf_run(r->glTex != 0, r->alphaTest || r->chroma, ref, 0, 0, 0.0f, 0.0f);
                        if (r->glTex) glBindTexture(GL_TEXTURE_2D, r->glTex);
                        glDrawArrays(GL_TRIANGLES, (GLint)r->first, (GLsizei)r->count);
                        g_waterCovered++;
                    }
                    u9gl_wbuf_mark(0);
                    u9gl_wbuf_run(0, 0, 0.0f, 0, 0, 0.0f, 0.0f);
                    glDisable(GL_BLEND);
                }
                glDepthFunc((GLenum)df);
                glDepthMask(GL_TRUE);
                u9glpost_water_mark_end();
            }
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        u9glpost_bloom_mask(1);
        if (fogBlack) glFogfv(GL_FOG_COLOR, g_fogBColour);
        if (wbuf) u9gl_wbuf_end();
        if (g_labelTex && u9metrics_text_ready()) {
            unsigned li;
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_FOG);
            for (li = 0; li < g_runCount; li++) {
                const GlRun *r = &g_runs[li];
                char lbl[32];
                float aw = (r->x1 - r->x0), ah = (r->y1 - r->y0);
                if (!r->count || r->texId < 0) continue;
                if (aw * ah < (float)g_labelMinArea) continue;
                if (r->zMin < 1.0f) continue;
                wsprintfA(lbl, "%d/%d", r->texId, r->texId2);
                glColor3f(1.0f, 1.0f, 0.0f);
                u9metrics_text((r->x0 + r->x1) * 0.5f,
                               (r->y0 + r->y1) * 0.5f, lbl);
            }
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        }

        texture_watch();
        book_watch();

        glDepthMask(GL_TRUE);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_FOG);
        glDisableClientState(GL_FOG_COORDINATE_ARRAY_EXT);

        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    if (u9cfg_int("ui_watch", 0)) {
        static unsigned psig, pn, reports;
        unsigned n = 0, i, sig = 2166136261u;
        for (i = 0; i < g_runCount; i++) {
            const GlRun *r = &g_runs[i];
            unsigned f[5], k;
            if (r->zMin > 10.0f) continue;
            n++;
            f[0] = (unsigned)r->texId;   f[1] = (unsigned)(int)r->x0;
            f[2] = (unsigned)(int)r->y0; f[3] = (unsigned)(int)(r->x1 - r->x0);
            f[4] = (unsigned)(int)(r->y1 - r->y0);
            for (k = 0; k < 5; k++) { sig ^= f[k]; sig *= 16777619u; }
        }
        if (n && sig != psig) {
            if (reports < 80u) {
                reports++;
                u9log_line("UIWATCH f=%u  %u UI runs, sig 0x%08X (was %u runs,"
                           " 0x%08X)", u9log_frame(), n, sig, pn, psig);
                for (i = 0; i < g_runCount; i++) {
                    const GlRun *r = &g_runs[i];
                    if (r->zMin > 10.0f) continue;
                    u9log_line("UIWATCH    tex=%-6d %4dx%-4d at %4d,%-4d fmt=0x%02X",
                               r->texId, (int)(r->x1 - r->x0), (int)(r->y1 - r->y0),
                               (int)r->x0, (int)r->y0, r->texFmt);
                }
            }
            psig = sig; pn = n;
        }
    }

    account_frame();
    g_lastColN = g_colN; g_lastColSum = g_colSum; g_lastColMax = g_colMax;
    g_lastAlphaSum = g_alphaSum; g_lastClipped = g_colClipped;
    g_colN = g_colSum = g_colMax = g_colClipped = g_alphaSum = 0;
    g_lastFogTouched = g_fogTouched; g_lastFogFullyOut = g_fogFullyOut;
    g_fogTouched = g_fogFullyOut = 0;
    g_lastFogBTouched = g_fogBTouched; g_fogBTouched = 0; g_fogBSampled = 0;
    g_lastFogBwMin = g_fogBwMin; g_lastFogBwMax = g_fogBwMax;
    g_lastFogBdMax = g_fogBdMax;
    g_fogBwMin = g_fogBwMax = g_fogBdMax = 0.0f;
    sample_engine_far();
    g_epoch = 0;
    g_lastTris     = g_count / 3;
    g_lastRunCount = g_runCount;
    memcpy(g_lastRuns, g_runs, g_runCount * sizeof(GlRun));
    g_lastCensusCount = g_censusCount; g_lastCensusLost = g_censusLost;
    memcpy(g_lastCensus, g_census, g_censusCount * sizeof(FlushCensus));
    g_censusCount = g_censusLost = 0;
    g_count    = 0;
    g_runCount = 0;
    g_lastBfTris = g_bfCount / 3;
    g_lastBfRunCount = g_bfRunCount;
    memcpy(g_lastBfRuns, g_bfRuns, g_bfRunCount * sizeof(GlRun));
    g_bfCount = g_bfRunCount = 0;
    u9gl_anim_draw_frame_end();
    g_u9Anim = 0;
    g_lastGuardTris = g_guardTris;
    g_guardTris = 0;
    material_forget();
    run_begin();
}

static void account_frame(void)
{
    unsigned i;
    g_lastAddTris = g_lastAlphaTestTris = 0;
    for (i = 0; i < g_runCount; i++) {
        if (g_runs[i].blendAdd)  g_lastAddTris       += g_runs[i].count / 3;
        if (g_runs[i].alphaTest) g_lastAlphaTestTris += g_runs[i].count / 3;
    }
    if ((g_lastAddTris > 0) != g_wasAdditive) {
        g_wasAdditive = g_lastAddTris > 0;
        u9log_line("GLADD frame %u: additive material %s (%u triangles)",
                   g_frameNo, g_wasAdditive ? "APPEARS" : "gone", g_lastAddTris);
    }
    g_frameNo++;
}

unsigned u9gl_frame_additive_tris(void)  { return g_lastAddTris; }
unsigned u9gl_frame_alphatest_tris(void) { return g_lastAlphaTestTris; }

void u9gl_discard_frame(void)
{
    account_frame();
    g_lastColN = g_colN; g_lastColSum = g_colSum; g_lastColMax = g_colMax;
    g_lastAlphaSum = g_alphaSum; g_lastClipped = g_colClipped;
    g_colN = g_colSum = g_colMax = g_colClipped = g_alphaSum = 0;
    g_lastFogTouched = g_fogTouched; g_lastFogFullyOut = g_fogFullyOut;
    g_fogTouched = g_fogFullyOut = 0;
    g_lastFogBTouched = g_fogBTouched; g_fogBTouched = 0; g_fogBSampled = 0;
    g_lastFogBwMin = g_fogBwMin; g_lastFogBwMax = g_fogBwMax;
    g_lastFogBdMax = g_fogBdMax;
    g_fogBwMin = g_fogBwMax = g_fogBdMax = 0.0f;
    sample_engine_far();
    g_epoch = 0;
    g_lastTris     = g_count / 3;
    g_lastRunCount = g_runCount;
    memcpy(g_lastRuns, g_runs, g_runCount * sizeof(GlRun));
    g_lastCensusCount = g_censusCount; g_lastCensusLost = g_censusLost;
    memcpy(g_lastCensus, g_census, g_censusCount * sizeof(FlushCensus));
    g_censusCount = g_censusLost = 0;
    g_count    = 0;
    g_runCount = 0;
    g_lastBfTris = g_bfCount / 3;
    g_lastBfRunCount = g_bfRunCount;
    memcpy(g_lastBfRuns, g_bfRuns, g_bfRunCount * sizeof(GlRun));
    g_bfCount = g_bfRunCount = 0;
    u9gl_anim_draw_frame_end();
    g_u9Anim = 0;
    g_lastGuardTris = g_guardTris;
    g_guardTris = 0;
    material_forget();
    run_begin();
}

void u9gl_log_runs(void)
{
    static unsigned order[U9GL_MAX_RUNS];
    unsigned i, j, n = 0;

    for (i = 0; i < g_lastRunCount; i++)
        if (g_lastRuns[i].count) order[n++] = i;

    for (i = 1; i < n; i++) {
        unsigned v = order[i];
        double av = (double)(g_lastRuns[v].x1 - g_lastRuns[v].x0) *
                    (double)(g_lastRuns[v].y1 - g_lastRuns[v].y0);
        j = i;
        while (j > 0) {
            const GlRun *p = &g_lastRuns[order[j-1]];
            double ap = (double)(p->x1 - p->x0) * (double)(p->y1 - p->y0);
            if (ap >= av) break;
            order[j] = order[j-1]; j--;
        }
        order[j] = v;
    }

    u9log_line("GLRUNS %u runs in the dumped frame, every one listed below by "
               "screen area: fog touched %u vertices, %u fully faded",
               g_lastRunCount, g_lastFogTouched, g_lastFogFullyOut);
    {
        float s = bucket_scale();
        u9log_line("GLSORT pooled runs ordered by the engine's 32 depth buckets: "
                   "scale %.8f = 31/%.0f (%s), then opaque before translucent, "
                   "then farther first. The depth buffer is scaled by %.0f too "
                   "(depth_far = %.0f is the fallback)",
                   s, s > 0.0f ? 31.0f / s : 0.0f,
                   g_renderer ? "the engine's own, +0x49CCC" : "ours, no renderer",
                   active_far(), g_depthFar);
    }
    u9log_line("GLFOGB this frame: %s, mode %d, colour %.2f/%.2f/%.2f, "
               "%u vertices carried non-zero density",
               g_fogBActive ? "table fog ON" : "table fog off", g_fogBMode,
               g_fogBColour[0], g_fogBColour[1], g_fogBColour[2],
               g_lastFogBTouched);
    u9gl_mip_report();
    u9gl_source_report(0);
    u9log_line("GLFOGB w domain sampled %.0f..%.0f (table ramps 4681..52429), "
               "peak density %.0f/255 = %.0f%% fog, 1/wScale=%.4f",
               g_lastFogBwMin, g_lastFogBwMax, g_lastFogBdMax,
               g_lastFogBdMax / 2.55f, g_fogBInvW);
    {
        char line[U9_FOG_TABLE_SIZE * 4 + 8];
        unsigned k, n = 0;
        for (k = 0; k < U9_FOG_TABLE_SIZE; k++)
            if (n + 5 < sizeof line)
                n += (unsigned)sprintf(line + n, "%u%s", (unsigned)g_fogBTable[k],
                                       k + 1 < U9_FOG_TABLE_SIZE ? "," : "");
        line[n] = 0;
        u9log_line("GLFOGB table now = %s", line);
    }
    {
        unsigned k;
        u9log_line("GLFLUSH %u groups drawn from pools in the dumped frame "
                   "(36 = pool A, 37 = list pass, 38 = sorted, 39 = pool B)%s",
                   g_lastCensusCount,
                   g_lastCensusLost ? ", more groups lost to the table limit" : "");
        for (k = 0; k < g_lastCensusCount; k++) {
            const FlushCensus *c = &g_lastCensus[k];
            u9log_line("GLFLUSH slot=%d tex=%d/%d flags=0x%08X nodes=%u verts=%u "
                       "a=%u..%u rgbMax=%u z=%.0f..%.0f box %.0fx%.0f at %.0f,%.0f",
                       c->slot, c->texId, c->texId2, c->flags, c->nodes,
                       c->verts, c->aMin, c->aMax, c->rgbMax, c->zMin, c->zMax,
                       c->x1 - c->x0, c->y1 - c->y0, c->x0, c->y0);
        }
    }
    if (g_lastColN)
        u9log_line("GLCOL  this frame: %u vertices, mean rgb %u/255, peak %u, "
                   "%u clipped, mean alpha %u/255",
                   g_lastColN, g_lastColSum / g_lastColN, g_lastColMax,
                   g_lastClipped, g_lastAlphaSum / g_lastColN);

    for (i = 0; i < n; i++) {
        const GlRun *r = &g_lastRuns[order[i]];
        int hid = -1, hid2 = -1, hw = 0, hh = 0; unsigned hfmt = 0;
        int known = u9gl_texture_ident(r->glTex, &hid, &hid2, &hfmt, &hw, &hh);
        char holds[96];
        if (!r->glTex)      lstrcpyA(holds, "none(untextured)");
        else if (!known)    wsprintfA(holds, "UNKNOWN glTex %u", r->glTex);
        else                wsprintfA(holds, "%d/%d fmt0x%02X %dx%d%s",
                                      hid, hid2, hfmt, hw, hh,
                                      (hid == r->texId && hid2 == r->texId2)
                                          ? "" : " ***MISMATCH***");
        {
            unsigned rgb = 0, op = 0;
            if (r->glTex && u9gl_texture_look(r->glTex, &rgb, &op))
                wsprintfA(holds + lstrlenA(holds), " mean#%06X op%u%%", rgb, op);
        }
        u9log_line("GLRUN %3u %6.0fx%-6.0f at %5.0f,%-5.0f %4u tris tex=%d/%d "
                   "fmt=0x%02X flags=0x%08X key=0x%08X slot=%d %s aTest=%d "
                   "chroma=%d clamp=%d%d depthW=%d depthT=%d phase=%d "
                   "zMin=%.0f alpha=%u aRaw=%u..%u ep=%u pos=%u holds=%s",
                   i, r->x1-r->x0, r->y1-r->y0, r->x0, r->y0, r->count/3,
                   r->texId, r->texId2, r->texFmt, r->flags, r->chromaKey,
                   r->src, r->blendAdd ? "ADD  " : "alpha", r->alphaTest,
                   r->chroma, r->clampS, r->clampT, r->depthWrite,
                   r->depthTest, r->deferred, r->zMin,
                   r->aN ? r->aSum / r->aN : 0,
                   r->aN ? r->aRawMin : 0u, r->aRawMax,
                   r->epoch, r->drawPos, holds);
    }
}

void u9gl_raster_stats(unsigned *tris, unsigned *runs, unsigned *dropped,
                       float *zMin, float *zMax)
{
    *tris    = g_lastTris;
    *runs    = g_lastRunCount;
    *dropped = g_dropped;
    *zMin    = g_zMin;
    *zMax    = g_zMax;
}

static const char *texfmt_name(unsigned f)
{
    switch (f) {
    case 0x02: return "ALPHA_8";
    case 0x04: return "ALPHA_INTENSITY_44";
    case 0x05: return "P_8 (palette)";
    case 0x0A: return "RGB_565";
    case 0x0B: return "ARGB_1555";
    case 0x0D: return "ALPHA_INTENSITY_88";
    default:   return "?";
    }
}

#define U9GL_MAX_TEXPROBE 14
static unsigned g_texProbeCount;

void u9gl_probe_texture(const void *renderer, const void *mat)
{
    const char *mgr, *rec;
    unsigned    small, large, aspect, fmt, tmu, start;
    int         recId, recId2, matId, matId2, log2w, log2h;

    if (!mat || g_texProbeCount >= U9GL_MAX_TEXPROBE) return;

    mgr = *(const char *const *)((const char *)renderer + U9_OFF_TEXMGR);
    if (!mgr) { u9log_line("GLTEX no texture manager"); g_texProbeCount++; return; }

    rec = *(const char *const *)(mgr + U9_TEXMGR_CURRENT);
    if (!rec) return;

    matId  = *(const int16_t *)((const char *)mat + U9_MAT_TEXID);
    matId2 = *(const int16_t *)((const char *)mat + U9_MAT_TEXID2);
    log2h  = *(const int16_t *)((const char *)mat + U9_MAT_LOG2H);
    log2w  = *(const int16_t *)((const char *)mat + U9_MAT_LOG2W);

    recId  = *(const int16_t *)(rec + U9_TEXREC_ID);
    recId2 = *(const int16_t *)(rec + U9_TEXREC_ID2);
    small  = *(const unsigned *)(rec + U9_TEXREC_SMALL_LOD);
    large  = *(const unsigned *)(rec + U9_TEXREC_LARGE_LOD);
    aspect = *(const unsigned *)(rec + U9_TEXREC_ASPECT);
    fmt    = *(const unsigned *)(rec + U9_TEXREC_FORMAT);
    tmu    = *(const unsigned *)(rec + U9_TEXREC_TMU);
    start  = *(const unsigned *)(rec + U9_TEXREC_START_ADDR);

    g_texProbeCount++;
    u9log_line("GLTEX rec=0x%08X id=%d/%d (mat %d/%d)%s lod=%u/%u aspect=%u "
               "fmt=0x%02X %-18s tmu=%u start=0x%08X  mat log2 %dx%d = %dx%d",
               (unsigned)(uintptr_t)rec, recId, recId2, matId, matId2,
               (recId == matId && recId2 == matId2) ? " MATCH" : " *** MISMATCH ***",
               small, large, aspect, fmt, texfmt_name(fmt), tmu, start,
               log2w, log2h,
               (log2w >= 0 && log2w <= 12) ? (1 << log2w) : -1,
               (log2h >= 0 && log2h <= 12) ? (1 << log2h) : -1);
}
