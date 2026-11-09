#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "../fix/lightwatch.h"
#include "glshadow.h"
#include "gleffects.h"

int u9console_current_map(void);

#define FX_TEXTURES   512
#define FX_GROUPS     48
#define FX_GROUP_R    160.0f
#define FX_NEW_LINES  300u
#define FX_READ_MAX   512

typedef struct {
    int      texId;
    int      additive, alpha8;
    unsigned frames, streak, longest, lastFrame;
    unsigned maxTris;
    double   vrgba[4];
    int      texRead;
    float    texRgb[3], texCover;
    float    at[3];
    int      map, firstMap;
    int      frameSeen;
    unsigned trisNow;
    double   vNow[4];
    unsigned vN;
} FxTex;

typedef struct {
    int      texId, texId2, additive;
    float    c[3], r;
    float    first[3];
    unsigned tris;
    double   v[4];
    unsigned vN;
} FxGroup;

static int      g_on = -1;
static FxTex    g_tex[FX_TEXTURES];
static int      g_nTex;
static unsigned g_frame, g_dropped, g_newLines, g_reads;
static const U9FrameView *g_view;
static int      g_listWanted;
static void   (*g_listOut)(const char *fmt, ...);
static FxGroup  g_grp[FX_GROUPS];
static int      g_nGrp;
static unsigned g_grpDropped;

static FxTex *tex_of(int id)
{
    int i;
    for (i = 0; i < g_nTex; i++) if (g_tex[i].texId == id) return &g_tex[i];
    if (g_nTex >= FX_TEXTURES) { g_dropped++; return NULL; }
    memset(&g_tex[g_nTex], 0, sizeof g_tex[0]);
    g_tex[g_nTex].texId = id;
    g_tex[g_nTex].firstMap = u9console_current_map();
    return &g_tex[g_nTex++];
}

static void tex_read(FxTex *t, unsigned glTex)
{
    GLint w = 0, h = 0;
    unsigned char *px;
    double s[3] = { 0, 0, 0 }, a = 0;
    int i, n;
    t->texRead = -1;
    if (!glTex) return;
    glPushAttrib(GL_TEXTURE_BIT);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0 || w > FX_READ_MAX || h > FX_READ_MAX ||
        !(px = (unsigned char *)malloc((size_t)w * (size_t)h * 4u))) {
        glPopAttrib();
        return;
    }
    {
        GLint pack = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glPixelStorei(GL_PACK_ALIGNMENT, pack);
    }
    glPopAttrib();
    n = w * h;
    for (i = 0; i < n; i++) {
        double al = px[i * 4 + 3] / 255.0;
        s[0] += px[i * 4] * al; s[1] += px[i * 4 + 1] * al; s[2] += px[i * 4 + 2] * al;
        a += al;
    }
    free(px);
    t->texCover = (float)(a / n);
    for (i = 0; i < 3; i++) t->texRgb[i] = a > 0.0 ? (float)(s[i] / a) : 0.0f;
    t->texRead = 1;
    g_reads++;
}

static int world_of(const U9FrameView *v, const float *f, float out[3])
{
    float q = f[6], w, cs[3];
    int k;
    if (q == 0.0f) return 0;
    w = 1.0f / q;
    cs[0] = (f[0] - v->cx) * w / v->sx;
    cs[1] = w;
    cs[2] = (v->cy - f[1]) * w / v->sy;
    for (k = 0; k < 3; k++) out[k] = v->m[k * 4] * cs[0] + v->m[k * 4 + 1] * cs[1] + v->m[k * 4 + 2] * cs[2] + v->m[k * 4 + 3];
    return 1;
}

static void group_add(const U9EffectRun *r, const float p[3], const unsigned char *rgba)
{
    int g, k;
    FxGroup *G = NULL;
    for (g = 0; g < g_nGrp; g++) {
        FxGroup *q = &g_grp[g];
        float dx = p[0] - q->first[0], dy = p[1] - q->first[1], dz = p[2] - q->first[2];
        if (q->texId == r->texId && dx * dx + dy * dy + dz * dz < FX_GROUP_R * FX_GROUP_R) { G = q; break; }
    }
    if (!G) {
        if (g_nGrp >= FX_GROUPS) { g_grpDropped++; return; }
        G = &g_grp[g_nGrp++];
        memset(G, 0, sizeof *G);
        G->texId = r->texId; G->texId2 = r->texId2; G->additive = r->additive;
        for (k = 0; k < 3; k++) G->first[k] = p[k];
    }
    for (k = 0; k < 3; k++) G->c[k] += p[k];
    {
        float dx = p[0] - G->first[0], dy = p[1] - G->first[1], dz = p[2] - G->first[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > G->r) G->r = d;
    }
    G->tris++;
    for (k = 0; k < 4; k++) G->v[k] += rgba[k];
    G->vN++;
}

static void count_run(void *ctx, const U9EffectRun *r)
{
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    FxTex *t = tex_of(r->texId);
    unsigned i;
    (void)ctx;
    if (!t) return;
    if (!t->texRead) tex_read(t, r->glTex);
    if (r->additive) t->additive = 1; else t->alpha8 = 1;
    t->frameSeen = 1;
    t->trisNow += r->count / 3;
    for (i = r->first; i < r->first + r->count; i++) {
        const unsigned char *c = (const unsigned char *)(base + (size_t)i * (size_t)stride + 32);
        int k;
        for (k = 0; k < 4; k++) t->vNow[k] += c[k];
        t->vN++;
    }
    if (r->count >= 3 && g_view) {
        float p[3];
        if (world_of(g_view, (const float *)(base + (size_t)r->first * (size_t)stride), p)) {
            t->at[0] = p[0]; t->at[1] = p[1]; t->at[2] = p[2];
        }
    }
    if (g_listWanted && g_view) {
        unsigned v;
        for (v = r->first; v + 2 < r->first + r->count; v += 3) {
            float a[3], b[3], c[3], m[3];
            int k;
            if (!world_of(g_view, (const float *)(base + (size_t)v * (size_t)stride), a) ||
                !world_of(g_view, (const float *)(base + (size_t)(v + 1) * (size_t)stride), b) ||
                !world_of(g_view, (const float *)(base + (size_t)(v + 2) * (size_t)stride), c)) continue;
            for (k = 0; k < 3; k++) m[k] = (a[k] + b[k] + c[k]) / 3.0f;
            group_add(r, m, (const unsigned char *)(base + (size_t)v * (size_t)stride + 32));
        }
    }
}

static void tex_line(const char *what, const FxTex *t)
{
    double n = t->frames ? (double)t->frames : 1.0;
    u9log_line("GLFX %s tex %d: %s%s%s, %u frames (longest %u in a row), up to %u tris, vertex colour"
               " %.0f,%.0f,%.0f a %.0f, texture %s%.0f,%.0f,%.0f cover %.0f%%, map %d at %.0f,%.0f,%.0f",
               what, t->texId, t->additive ? "additive" : "", t->additive && t->alpha8 ? " and " : "",
               t->alpha8 ? "alpha-8" : "", t->frames, t->longest, t->maxTris, t->vrgba[0] / n,
               t->vrgba[1] / n, t->vrgba[2] / n, t->vrgba[3] / n,
               t->texRead > 0 ? "" : "(not read) ", (double)t->texRgb[0], (double)t->texRgb[1],
               (double)t->texRgb[2], (double)(t->texCover * 100.0f), t->map, (double)t->at[0],
               (double)t->at[1], (double)t->at[2]);
}

static void list_groups(void)
{
    static U9LightSnap all[128];
    int n = u9light_snapshot(all, 128), g, k;
    float cam[3];
    void (*out)(const char *fmt, ...) = g_listOut;
    cam[0] = g_view->m[3]; cam[1] = g_view->m[7]; cam[2] = g_view->m[11];
    if (!g_nGrp) { if (out) out("effects: none drawn this frame"); return; }
    if (out) out("effects this frame, by place (tex/frame, kind, triangles, centre, size, distance; nearest light):");
    for (g = 0; g < g_nGrp; g++) {
        FxGroup *G = &g_grp[g];
        const FxTex *t = tex_of(G->texId);
        float c[3], dd, best = 1e30f;
        int bi = -1;
        char lt[96];
        for (k = 0; k < 3; k++) c[k] = G->c[k] / (float)G->tris;
        dd = sqrtf((c[0] - cam[0]) * (c[0] - cam[0]) + (c[1] - cam[1]) * (c[1] - cam[1]) +
                   (c[2] - cam[2]) * (c[2] - cam[2]));
        for (k = 0; k < n; k++) {
            float dx = all[k].pos[0] - c[0], dy = all[k].pos[1] - c[1], dz = all[k].pos[2] - c[2];
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < best) { best = d; bi = k; }
        }
        if (bi >= 0 && best < 400.0f)
            snprintf(lt, sizeof lt, "light %.0f away: type %d, range %.0f, colour %.0f,%.0f,%.0f", (double)best,
                     all[bi].type, (double)all[bi].range, (double)(all[bi].rgb[0] * 255.0f),
                     (double)(all[bi].rgb[1] * 255.0f), (double)(all[bi].rgb[2] * 255.0f));
        else
            snprintf(lt, sizeof lt, "no light within 400");
        u9log_line("GLFX here %d: tex %d/%d %s, %u tris at %.0f,%.0f,%.0f, size %.0f, %.0f from the camera,"
                   " vertex colour %.0f,%.0f,%.0f a %.0f, texture %.0f,%.0f,%.0f; %s", g + 1, G->texId, G->texId2,
                   G->additive ? "additive" : "alpha-8", G->tris, (double)c[0], (double)c[1], (double)c[2],
                   (double)G->r, (double)dd, G->v[0] / G->vN, G->v[1] / G->vN, G->v[2] / G->vN, G->v[3] / G->vN,
                   t ? (double)t->texRgb[0] : 0.0, t ? (double)t->texRgb[1] : 0.0, t ? (double)t->texRgb[2] : 0.0, lt);
        if (out && g < 16)
            out("%2d  tex %d/%d %s  %u tris  %.0f,%.0f,%.0f  size %.0f  %.0f away; %s", g + 1, G->texId, G->texId2,
                G->additive ? "add" : "a8", G->tris, (double)c[0], (double)c[1], (double)c[2], (double)G->r,
                (double)dd, lt);
    }
    if (out && g_nGrp > 16) out("... %d more in the log (GLFX here)", g_nGrp - 16);
    if (g_grpDropped) u9log_line("GLFX here: %u triangles in places over the %d listed", g_grpDropped, FX_GROUPS);
}

#include "../common/u9time.h"

#define FIRE_MAX        256
#define FIRE_R          160.0f
#define FIRE_FORGET_MS  1000u
#define FIRE_TESTS      4
#define FIRE_COVER      24.0f
#define FIRE_TEX_MAX    64
#define FIRE_NEW_LINES  200u

typedef struct {
    int      used;
    float    p[3], rgb[3];
    DWORD    seen, born;
    unsigned tris;
    int      tex;
    double   fs[3], fc[3];
    unsigned fn;
} Fire;

static int      g_fireOn = -1, g_fireStr = 100, g_fireDunStr = 100, g_fireRange = 500;
static int      g_fireTex[FIRE_TEX_MAX], g_nFireTex;
static int      g_sootTex[FIRE_TEX_MAX], g_nSootTex, g_soot = 0;
static int      g_glowTex[FIRE_TEX_MAX], g_nGlowTex, g_glowOn = 1, g_glowStr = 100, g_glowRange = 600;
static int      g_gainTex[FIRE_TEX_MAX], g_nGainTex, g_glowGain = 100;
static Fire     g_fire[FIRE_MAX];
static int      g_nFire, g_fireMap = -1;
static unsigned g_fireLoads;
static unsigned g_fireLearnt, g_fireForgot, g_fireHidden, g_fireFull, g_fireLines, g_fireLitMax;

static int id_list(const char *s, int *out)
{
    int n = 0;
    while (s && *s && n < FIRE_TEX_MAX) {
        char *e;
        long id = strtol(s, &e, 10);
        if (e == s) { s++; continue; }
        out[n++] = (int)id;
        s = e;
    }
    return n;
}

static int fire_texture(int id);

static void fire_settings(void)
{
    const char *s;
    if (g_fireOn >= 0) return;
    g_fireOn     = u9cfg_int("gl_fire_light", 1);
    g_fireStr    = u9cfg_int("gl_fire_strength", 100);
    g_fireDunStr = u9cfg_int("gl_fire_dungeon_strength", 100);
    g_fireRange  = u9cfg_int("gl_fire_range", 500);
    g_soot       = u9cfg_int("gl_fire_soot", 0);
    s = u9cfg_str("gl_fire_textures", "6081,6082,4869,4870,4698,4699,5363,5364,5366,5367,5558,5559,5987");
    g_nFireTex = id_list(s, g_fireTex);
    g_nSootTex = id_list(u9cfg_str("gl_fire_soot_textures", "6080,3077,7240"), g_sootTex);
    g_glowOn   = u9cfg_int("gl_glow_light", 1);
    g_nGlowTex = id_list(u9cfg_str("gl_glow_light_textures", "3078,3130"), g_glowTex);
    g_glowStr   = u9cfg_int("gl_glow_light_strength", 100);
    g_glowRange = u9cfg_int("gl_glow_light_range", 600);
    g_nGainTex  = id_list(u9cfg_str("gl_glow_gain_textures", "6413,6414"), g_gainTex);
    g_glowGain  = u9cfg_int("gl_glow_gain", 100);
}

static int glow_texture(int id)
{
    int i;
    for (i = 0; i < g_nGlowTex; i++) if (g_glowTex[i] == id) return 1;
    return 0;
}

int   u9glpost_dome_tex(int texId);
float u9glpost_dome_strength(void);
float u9glpost_dome_range(void);
int   u9glpost_dome_here(int map, const float p[3]);
unsigned u9glpost_loads(void);

float u9fx_soot_scale(int texId)
{
    int i;
    fire_settings();
    if (g_soot == 100) return 1.0f;
    for (i = 0; i < g_nSootTex; i++) if (g_sootTex[i] == texId) return (float)g_soot / 100.0f;
    return 1.0f;
}

float u9fx_glow_gain(int texId)
{
    int i;
    fire_settings();
    if (g_glowGain == 100) return 1.0f;
    for (i = 0; i < g_nGainTex; i++) if (g_gainTex[i] == texId) return (float)g_glowGain / 100.0f;
    return 1.0f;
}

int u9fx_is_flame(int texId)
{
    fire_settings();
    return fire_texture(texId);
}

static int fire_texture(int id)
{
    int i;
    for (i = 0; i < g_nFireTex; i++) if (g_fireTex[i] == id) return 1;
    return 0;
}

static int on_screen(const U9FrameView *v, const float p[3])
{
    float d[3], cs[3], ex, ey;
    int j;
    for (j = 0; j < 3; j++) d[j] = p[j] - v->m[j * 4 + 3];
    for (j = 0; j < 3; j++) cs[j] = v->m[j] * d[0] + v->m[4 + j] * d[1] + v->m[8 + j] * d[2];
    if (cs[1] < 16.0f || cs[1] > 3000.0f) return 0;
    ex = v->cx + cs[0] * v->sx / cs[1];
    ey = v->cy - cs[2] * v->sy / cs[1];
    return ex >= 0.0f && ey >= 0.0f && ex < v->engineW && ey < v->engineH;
}

static void fire_run(void *ctx, const U9EffectRun *r)
{
    const U9FrameView *v = (const U9FrameView *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned t;
    if (!(g_fireOn && fire_texture(r->texId)) && !(g_glowOn && glow_texture(r->texId)) &&
        !(u9glpost_dome_tex(r->texId) && u9glpost_dome_strength() > 0.0f)) return;
    for (t = r->first; t + 2 < r->first + r->count; t += 3) {
        float a[3], b[3], c[3], m[3], col[3] = { 0, 0, 0 };
        int k, i, f = -1;
        if (!world_of(v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), c)) continue;
        for (k = 0; k < 3; k++) m[k] = (a[k] + b[k] + c[k]) / 3.0f;
        if (u9glpost_dome_tex(r->texId) && !(g_fireOn && fire_texture(r->texId)) &&
            !(g_glowOn && glow_texture(r->texId)) && !u9glpost_dome_here(g_fireMap, m)) continue;
        for (i = 0; i < 3; i++) {
            const unsigned char *q = (const unsigned char *)(base + (size_t)(t + i) * (size_t)stride + 32);
            float al = q[3] / 255.0f;
            for (k = 0; k < 3; k++) col[k] += q[k] / 255.0f * al;
        }
        for (i = 0; i < g_nFire; i++) {
            const Fire *F = &g_fire[i];
            float dx = m[0] - F->p[0], dy = m[1] - F->p[1], dz = m[2] - F->p[2];
            float R = u9glpost_dome_tex(r->texId) ? 400.0f : FIRE_R;
            if (F->used && dx * dx + dy * dy + dz * dz < R * R) { f = i; break; }
        }
        if (f < 0) {
            for (i = 0; i < g_nFire && g_fire[i].used; i++) ;
            if (i == g_nFire) {
                if (g_nFire >= FIRE_MAX) { g_fireFull++; continue; }
                g_nFire++;
            }
            f = i;
            memset(&g_fire[f], 0, sizeof g_fire[f]);
            g_fire[f].used = 1;
            for (k = 0; k < 3; k++) g_fire[f].p[k] = m[k];
            g_fire[f].born = u9time_now();
            g_fire[f].tex = r->texId;
            g_fireLearnt++;
            if (g_fireLines < FIRE_NEW_LINES) {
                g_fireLines++;
                u9log_line("GLFX fire %d learnt on map %d at %.0f,%.0f,%.0f from tex %d", f, g_fireMap,
                           (double)m[0], (double)m[1], (double)m[2], r->texId);
            }
        }
        for (k = 0; k < 3; k++) { g_fire[f].fs[k] += m[k]; g_fire[f].fc[k] += col[k]; }
        g_fire[f].fn++;
    }
}

static void fire_learn(const U9FrameView *v)
{
    DWORD now = u9time_now();
    int map = u9console_current_map(), i, k, tests = 0;
    fire_settings();
    if (!g_fireOn && !g_glowOn && u9glpost_dome_strength() <= 0.0f) return;
    if (map != g_fireMap || u9glpost_loads() != g_fireLoads) {
        int known = 0;
        for (i = 0; i < g_nFire; i++) known += g_fire[i].used;
        if (known) u9log_line("GLFX fire: %d fires known on map %d, forgotten (%s)", known, g_fireMap,
                              map != g_fireMap ? "map change" : "a save loaded");
        g_fireLoads = u9glpost_loads();
        memset(g_fire, 0, sizeof g_fire);
        g_nFire = 0;
        g_fireMap = map;
    }
    u9gl_effect_runs(fire_run, (void *)v);
    for (i = 0; i < g_nFire; i++) {
        Fire *F = &g_fire[i];
        if (!F->used) continue;
        if (F->fn) {
            float mx = 0.0f, c[3];
            for (k = 0; k < 3; k++) {
                F->p[k] = 0.5f * F->p[k] + 0.5f * (float)(F->fs[k] / F->fn);
                c[k] = (float)F->fc[k];
                if (c[k] > mx) mx = c[k];
            }
            if (mx > 0.0f)
                for (k = 0; k < 3; k++) F->rgb[k] = F->seen ? 0.9f * F->rgb[k] + 0.1f * c[k] / mx : c[k] / mx;
            F->seen = now;
            F->tris = F->fn;
            F->fn = 0;
            for (k = 0; k < 3; k++) F->fs[k] = F->fc[k] = 0.0;
        } else if (glow_texture(F->tex) || u9glpost_dome_tex(F->tex)) {
        } else if (now - F->seen > FIRE_FORGET_MS && on_screen(v, F->p)) {
            if (tests < FIRE_TESTS && u9glpost_point_covered(v, F->p, FIRE_COVER) == 1) {
                F->seen = now;
                g_fireHidden++;
            } else if (tests < FIRE_TESTS) {
                F->used = 0;
                g_fireForgot++;
            }
            tests++;
        }
    }
}

int u9fx_fire_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v, int dungeon)
{
    int best[U9FX_LIGHTS], nb = 0, k, j;
    float bd[U9FX_LIGHTS], ahead[3], s, t = (float)(u9time_now() % 100000u) / 1000.0f;
    fire_settings();
    s = (float)(dungeon ? g_fireDunStr : g_fireStr) / 100.0f;
    if ((!g_fireOn || s <= 0.0f) && (!g_glowOn || g_glowStr <= 0) && u9glpost_dome_strength() <= 0.0f) return n;
    for (k = 0; k < 3; k++) ahead[k] = v->m[k * 4 + 3] + v->m[k * 4 + 1] * 600.0f;
    for (k = 0; k < g_nFire; k++) {
        const Fire *F = &g_fire[k];
        float dx, dy, dz, dd;
        if (!F->used || !F->seen) continue;
        if (u9glpost_dome_tex(F->tex) ? u9glpost_dome_strength() <= 0.0f
            : glow_texture(F->tex) ? (!g_glowOn || g_glowStr <= 0) : (!g_fireOn || s <= 0.0f)) continue;
        dx = F->p[0] - ahead[0]; dy = F->p[1] - ahead[1]; dz = F->p[2] - ahead[2];
        dd = dx * dx + dy * dy + dz * dz;
        if (dd > 4000.0f * 4000.0f || (nb == U9FX_LIGHTS && dd >= bd[nb - 1])) continue;
        j = nb < U9FX_LIGHTS ? nb++ : U9FX_LIGHTS - 1;
        while (j > 0 && bd[j - 1] > dd) { bd[j] = bd[j - 1]; best[j] = best[j - 1]; j--; }
        bd[j] = dd; best[j] = k;
    }
    for (k = 0; k < nb && n < cap; k++) {
        const Fire *F = &g_fire[best[k]];
        U9LightSnap *l = &all[n++];
        float ph = (float)(best[k] % 97) * 0.61f;
        int dome = u9glpost_dome_tex(F->tex);
        int glow = dome || glow_texture(F->tex);
        float flick = glow ? 1.0f : 0.86f + 0.09f * sinf(t * 7.3f + ph) + 0.05f * sinf(t * 13.7f + 2.0f * ph);
        float str = dome ? u9glpost_dome_strength() : glow ? (float)g_glowStr / 100.0f : s;
        int c;
        memset(l, 0, sizeof *l);
        for (c = 0; c < 3; c++) {
            l->pos[c] = l->base[c] = F->p[c];
            l->rgb[c] = F->rgb[c] * str * flick;
        }
        memcpy(l->baseRgb, l->rgb, sizeof l->rgb);
        l->range = dome ? u9glpost_dome_range() : (float)(glow ? g_glowRange : g_fireRange);
        l->rec = 0xE1000000u + (unsigned)best[k];
        l->type = U9TYPE_FIRE_LIGHT;
        if (dome) l->flags |= U9LIGHT_SOFT;
    }
    if ((unsigned)nb > g_fireLitMax) g_fireLitMax = (unsigned)nb;
    return n;
}

void u9fx_fire_command(const char *arg, void (*out)(const char *fmt, ...))
{
    int i, known = 0;
    fire_settings();
    while (arg && *arg == ' ') arg++;
    if (!arg) arg = "";
    if (!strcmp(arg, "on")) g_fireOn = 1;
    else if (!strcmp(arg, "off")) g_fireOn = 0;
    else if (!strncmp(arg, "strength ", 9)) g_fireStr = atoi(arg + 9);
    else if (!strncmp(arg, "dungeon ", 8)) g_fireDunStr = atoi(arg + 8);
    else if (!strncmp(arg, "range ", 6)) g_fireRange = atoi(arg + 6);
    else if (!strcmp(arg, "forget")) { memset(g_fire, 0, sizeof g_fire); g_nFire = 0; }
    else if (!strncmp(arg, "soot ", 5)) g_soot = atoi(arg + 5);
    else if (*arg) {
        out("pool fire on|off | strength <percent> (Britannia, Earth) | dungeon <percent> (other maps) |"
            " range <units> | soot <percent of its opacity> | forget");
        return;
    }
    if (g_fireStr < 0) g_fireStr = 0;
    if (g_fireDunStr < 0) g_fireDunStr = 0;
    if (g_fireRange < 50) g_fireRange = 50;
    if (g_soot < 0) g_soot = 0;
    if (g_soot > 100) g_soot = 100;
    if (*arg) u9log_line("GLFX fire: '%s' - %s, strength %d%%, dungeon %d%%, range %d, soot %d%%", arg,
                         g_fireOn ? "on" : "off", g_fireStr, g_fireDunStr, g_fireRange, g_soot);
    for (i = 0; i < g_nFire; i++) known += g_fire[i].used;
    out("fire lights %s: %d fires known on map %d; strength %d%% (Britannia, Earth), dungeon %d%%, range %d;"
        " %u learnt, %u forgotten, %u kept while hidden this session; soot halo %d%% of its opacity",
        g_fireOn ? "on" : "off", known, g_fireMap, g_fireStr, g_fireDunStr, g_fireRange, g_fireLearnt,
        g_fireForgot, g_fireHidden, g_soot);
}

void u9fx_glow_command(const char *arg, void (*out)(const char *fmt, ...))
{
    int i, known = 0;
    fire_settings();
    while (arg && *arg == ' ') arg++;
    if (!arg) arg = "";
    if (!strcmp(arg, "on")) g_glowOn = 1;
    else if (!strcmp(arg, "off")) g_glowOn = 0;
    else if (!strncmp(arg, "strength ", 9)) g_glowStr = atoi(arg + 9);
    else if (!strncmp(arg, "range ", 6)) g_glowRange = atoi(arg + 6);
    else if (!strncmp(arg, "gain ", 5)) g_glowGain = atoi(arg + 5);
    else if (*arg) {
        out("pool glow on|off | strength <percent> | range <units> | gain <percent>   lights for glows the game"
            " draws unlit (gl_glow_light_textures); gain brightens faint glows (gl_glow_gain_textures); the fires are pool fire");
        return;
    }
    if (g_glowStr < 0) g_glowStr = 0;
    if (g_glowRange < 50) g_glowRange = 50;
    if (g_glowGain < 0) g_glowGain = 0;
    if (g_glowGain > 1000) g_glowGain = 1000;
    if (*arg) u9log_line("GLFX glow: '%s' - %s, strength %d%%, range %d, gain %d%%", arg, g_glowOn ? "on" : "off",
                         g_glowStr, g_glowRange, g_glowGain);
    for (i = 0; i < g_nFire; i++) known += g_fire[i].used && glow_texture(g_fire[i].tex);
    out("glow lights %s: %d known on map %d; strength %d%%, range %d; textures %d listed (gl_glow_light_textures);"
        " faint glows x%d%% (%d gl_glow_gain_textures)",
        g_glowOn ? "on" : "off", known, g_fireMap, g_glowStr, g_glowRange, g_nGlowTex, g_glowGain, g_nGainTex);
}

#define RUNE_MAX 16
typedef struct {
    int      texId;
    unsigned mask;
    int      tried;
    int      neutral;
    float    rgb[3];
} RuneTex;
static RuneTex  g_rune[RUNE_MAX];
static int      g_nRune, g_runeOn = -1, g_runeStr = 75, g_runeRange = 600;
static int      g_runeSat = 400;
static unsigned g_runeMasks, g_runeLitMax;

static void rune_settings(void)
{
    int ids[RUNE_MAX], n, k;
    if (g_runeOn >= 0) return;
    g_runeOn    = u9cfg_int("gl_rune_light", 1);
    g_runeStr   = u9cfg_int("gl_rune_light_strength", 75);
    g_runeRange = u9cfg_int("gl_rune_light_range", 600);
    g_runeSat   = u9cfg_int("gl_rune_light_saturation", 400);
    n = id_list(u9cfg_str("gl_rune_textures", "6812,6813,6814,6817,6819,6820,6821,6822"), ids);
    if (n > RUNE_MAX) n = RUNE_MAX;
    memset(g_rune, 0, sizeof g_rune);
    for (k = 0; k < n; k++) g_rune[k].texId = ids[k];
    g_nRune = n;
    {
        int nn = id_list(u9cfg_str("gl_rune_neutral_textures", "6814,6821"), ids), j;
        for (j = 0; j < nn; j++)
            for (k = 0; k < g_nRune; k++) if (g_rune[k].texId == ids[j]) g_rune[k].neutral = 1;
    }
}

static RuneTex *rune_of(int texId)
{
    int k;
    rune_settings();
    for (k = 0; k < g_nRune; k++) if (g_rune[k].texId == texId) return &g_rune[k];
    return NULL;
}

int u9fx_is_rune(int texId)
{
    return rune_of(texId) != NULL;
}

static int cmp_uc(const void *a, const void *b)
{
    return (int)*(const unsigned char *)a - (int)*(const unsigned char *)b;
}

static unsigned rune_build(RuneTex *r, unsigned glTex)
{
    GLint w = 0, h = 0, pack = 4;
    unsigned char *px, *ring[3], *mask;
    float *dist, *avg;
    int x, y, k, n, nr = 0;
    double sum[3] = { 0, 0, 0 }, msum = 0;
    unsigned char med[3];
    GLuint t = 0;

    glPushAttrib(GL_TEXTURE_BIT);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w < 8 || h < 8 || w > 512 || h > 512) { glPopAttrib(); return 0; }
    n = w * h;
    px = (unsigned char *)malloc((size_t)n * 4u);
    ring[0] = (unsigned char *)malloc((size_t)n * 3u);
    mask = (unsigned char *)malloc((size_t)n);
    dist = (float *)malloc((size_t)n * sizeof(float) * 2u);
    if (!px || !ring[0] || !mask || !dist) {
        free(px); free(ring[0]); free(mask); free(dist);
        glPopAttrib();
        return 0;
    }
    ring[1] = ring[0] + n; ring[2] = ring[0] + 2 * n;
    avg = dist + n;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            float fx = ((float)x + 0.5f) / (float)w - 0.5f, fy = ((float)y + 0.5f) / (float)h - 0.5f;
            float rr = sqrtf(fx * fx + fy * fy);
            if (rr > 0.33f && rr < 0.40f)
                for (k = 0; k < 3; k++) ring[k][nr] = px[(y * w + x) * 4 + k];
            if (rr > 0.33f && rr < 0.40f) nr++;
        }
    if (nr < 8) { free(px); free(ring[0]); free(mask); free(dist); glPopAttrib(); return 0; }
    for (k = 0; k < 3; k++) {
        qsort(ring[k], (size_t)nr, 1, cmp_uc);
        med[k] = ring[k][nr / 2];
    }
    for (k = 0; k < n; k++) {
        float d0 = (px[k * 4] - med[0]) / 255.0f, d1 = (px[k * 4 + 1] - med[1]) / 255.0f,
              d2 = (px[k * 4 + 2] - med[2]) / 255.0f;
        dist[k] = sqrtf(d0 * d0 + d1 * d1 + d2 * d2);
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            float s = 0.0f;
            int dx, dy;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
                    int yy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
                    s += dist[yy * w + xx];
                }
            avg[y * w + x] = s / 9.0f;
        }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            float fx = ((float)x + 0.5f) / (float)w - 0.5f, fy = ((float)y + 0.5f) / (float)h - 0.5f;
            float m = (avg[y * w + x] - 0.13f) / (0.26f - 0.13f);
            int i = y * w + x;
            m = m < 0.0f ? 0.0f : m > 1.0f ? 1.0f : m;
            m = m * m * (3.0f - 2.0f * m);
            if (sqrtf(fx * fx + fy * fy) >= 0.40f) m = 0.0f;
            mask[i] = (unsigned char)(m * 255.0f + 0.5f);
            for (k = 0; k < 3; k++) sum[k] += px[i * 4 + k] * m;
            msum += m;
        }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, mask);
    glPopAttrib();
    {
        float mx = 0.0f;
        for (k = 0; k < 3; k++) {
            r->rgb[k] = msum > 0.0 ? (float)(sum[k] / msum) : 255.0f;
            if (r->rgb[k] > mx) mx = r->rgb[k];
        }
        for (k = 0; k < 3; k++) r->rgb[k] = mx > 0.0f ? r->rgb[k] / mx : 1.0f;
    }
    u9log_line("GLFX rune %d: symbol mask %dx%d over %.0f%% of the tile, stone %u,%u,%u, symbol colour %.2f,%.2f,%.2f",
               r->texId, w, h, 100.0 * msum / n, med[0], med[1], med[2], (double)r->rgb[0], (double)r->rgb[1],
               (double)r->rgb[2]);
    free(px); free(ring[0]); free(mask); free(dist);
    g_runeMasks++;
    return t;
}

unsigned u9fx_rune_mask(int texId, unsigned glTex)
{
    RuneTex *r = rune_of(texId);
    if (!r) return 0;
    if (!r->tried) {
        r->tried = 1;
        r->mask = rune_build(r, glTex);
        if (!r->mask) u9log_line("GLFX rune %d: its texture could not be read - it blooms whole", texId);
    }
    return r->mask;
}

typedef struct { float c[3]; unsigned n; } RuneSpot;
typedef struct { const U9FrameView *v; RuneSpot spot[4]; int ns; } RuneCtx;

static void rune_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    RuneCtx *rc = (RuneCtx *)ctx;
    int stride, k, s;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned t;
    (void)glTex; (void)cutout;
    for (t = first; t + 2 < first + count; t += 3) {
        float a[3], b[3], c[3], m[3];
        if (!world_of(rc->v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(rc->v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(rc->v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), c)) continue;
        for (k = 0; k < 3; k++) m[k] = (a[k] + b[k] + c[k]) / 3.0f;
        for (s = 0; s < rc->ns; s++) {
            float dx = m[0] - rc->spot[s].c[0] / rc->spot[s].n, dy = m[1] - rc->spot[s].c[1] / rc->spot[s].n,
                  dz = m[2] - rc->spot[s].c[2] / rc->spot[s].n;
            if (dx * dx + dy * dy + dz * dz < 80.0f * 80.0f) break;
        }
        if (s == rc->ns) {
            if (rc->ns >= 4) continue;
            memset(&rc->spot[s], 0, sizeof rc->spot[s]);
            rc->ns++;
        }
        for (k = 0; k < 3; k++) rc->spot[s].c[k] += m[k];
        rc->spot[s].n++;
    }
}

static void rune_colour(const RuneTex *r, float col[3])
{
    float grey, sum = 0.0f, sat = (float)g_runeSat / 100.0f;
    int k;
    grey = (r->rgb[0] + r->rgb[1] + r->rgb[2]) / 3.0f;
    if (r->neutral) sat = 0.0f;
    for (k = 0; k < 3; k++) {
        col[k] = grey + (r->rgb[k] - grey) * sat;
        if (col[k] < 0.0f) col[k] = 0.0f;
        sum += col[k];
    }
    for (k = 0; k < 3; k++) col[k] = sum > 0.0f ? col[k] * 3.0f * grey / sum : 1.0f;
}

int u9fx_rune_tint(int texId, float tint[3])
{
    RuneTex *r = rune_of(texId);
    if (!r || !r->mask) return 0;
    rune_colour(r, tint);
    return 1;
}

#define GLYPH_MAX 16
typedef struct {
    int      texId;
    unsigned mask;
    int      tried;
    int      pillar;
    float    rgb[3];
} GlyphTex;
static GlyphTex g_glyph[GLYPH_MAX];
static int      g_nGlyph = -1;

#define PILLAR_MAX 16
typedef struct { double s[3]; double n; int used; } Pillar;
static Pillar   g_pillar[PILLAR_MAX];
static int      g_nPillar, g_pillarMap = -1, g_pillarFull;
static unsigned g_pillarLoads, g_pillarLitMax;
static int      g_pillarOn = -1, g_pillarGlow = 1000, g_pillarBright = 75, g_pillarReach = 800;
static float    g_pillarRgb[3] = { 1.0f, 0.52f, 0.34f };
static int      g_pillarRgbSet;
unsigned u9glpost_loads(void);

static void pillar_settings(void)
{
    if (g_pillarOn >= 0) return;
    g_pillarOn     = u9cfg_int("gl_pillar_light", 1);
    g_pillarGlow   = u9cfg_int("gl_pillar_glow", 1000);
    g_pillarBright = u9cfg_int("gl_pillar_light_bright", 75);
    g_pillarReach  = u9cfg_int("gl_pillar_light_reach", 800);
    if (g_pillarGlow < 0) g_pillarGlow = 0;
    if (g_pillarBright < 0) g_pillarBright = 0;
    if (g_pillarReach < 30) g_pillarReach = 30;
}

static GlyphTex *glyph_of(int texId)
{
    int k;
    if (g_nGlyph < 0) {
        int ids[FIRE_TEX_MAX], n = id_list(u9cfg_str("gl_glyph_textures", "4403"), ids), j;
        int pids[FIRE_TEX_MAX], np = id_list(u9cfg_str("gl_pillar_textures", "4421"), pids);
        if (n > GLYPH_MAX) n = GLYPH_MAX;
        memset(g_glyph, 0, sizeof g_glyph);
        for (k = 0; k < n; k++) g_glyph[k].texId = ids[k];
        for (j = 0; j < np && n < GLYPH_MAX; j++) {
            for (k = 0; k < n && g_glyph[k].texId != pids[j]; k++) ;
            if (k == n) g_glyph[n++].texId = pids[j];
            g_glyph[k].pillar = 1;
        }
        g_nGlyph = n;
    }
    for (k = 0; k < g_nGlyph; k++) if (g_glyph[k].texId == texId) return &g_glyph[k];
    return NULL;
}

int u9fx_is_pillar(int texId)
{
    GlyphTex *g = glyph_of(texId);
    return g && g->pillar;
}

float u9fx_pillar_glow(void)
{
    pillar_settings();
    return (float)g_pillarGlow / 100.0f;
}

int u9fx_is_glyph(int texId)
{
    return glyph_of(texId) != NULL;
}

static float glyph_step(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static unsigned glyph_build(GlyphTex *g, unsigned glTex)
{
    GLint w = 0, h = 0, pack = 4;
    unsigned char *px, *mask;
    double sum[3] = { 0, 0, 0 }, msum = 0;
    float mx = 0.0f;
    GLuint t = 0;
    int i, k, n;

    glPushAttrib(GL_TEXTURE_BIT);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w < 8 || h < 8 || w > 1024 || h > 1024) { glPopAttrib(); return 0; }
    n = w * h;
    px = (unsigned char *)malloc((size_t)n * 4u);
    mask = (unsigned char *)malloc((size_t)n);
    if (!px || !mask) { free(px); free(mask); glPopAttrib(); return 0; }
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    for (i = 0; i < n; i++) {
        float r = px[i * 4] / 255.0f, b = px[i * 4 + 2] / 255.0f;
        float m = glyph_step(0.12f, 0.25f, r - b) * glyph_step(0.25f, 0.45f, r);
        mask[i] = (unsigned char)(m * 255.0f + 0.5f);
        for (k = 0; k < 3; k++) sum[k] += px[i * 4 + k] * m;
        msum += m;
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, mask);
    glPopAttrib();
    for (k = 0; k < 3; k++) {
        g->rgb[k] = msum > 0.0 ? (float)(sum[k] / msum) : 255.0f;
        if (g->rgb[k] > mx) mx = g->rgb[k];
    }
    for (k = 0; k < 3; k++) g->rgb[k] = mx > 0.0f ? g->rgb[k] / mx : 1.0f;
    u9log_line("GLFX glyph %d: mask %dx%d over %.0f%% of the tile, glyph colour %.2f,%.2f,%.2f",
               g->texId, w, h, 100.0 * msum / n, (double)g->rgb[0], (double)g->rgb[1], (double)g->rgb[2]);
    free(px); free(mask);
    return t;
}

unsigned u9fx_glyph_mask(int texId, unsigned glTex)
{
    GlyphTex *g = glyph_of(texId);
    if (!g) return 0;
    if (!g->tried) {
        g->tried = 1;
        g->mask = glyph_build(g, glTex);
        if (!g->mask) u9log_line("GLFX glyph %d: its texture could not be read - it blooms whole", texId);
    }
    return g->mask;
}

int u9fx_glyph_tint(int texId, float tint[3])
{
    GlyphTex *g = glyph_of(texId);
    int k;
    if (!g || !g->mask) return 0;
    for (k = 0; k < 3; k++) tint[k] = g->rgb[k];
    return 1;
}

static void pillar_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    const U9FrameView *v = (const U9FrameView *)ctx;
    int stride, k, s;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned t;
    (void)glTex; (void)cutout;
    for (t = first; t + 2 < first + count; t += 3) {
        float a[3], b[3], c[3], m[3];
        if (!world_of(v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), c)) continue;
        for (k = 0; k < 3; k++) m[k] = (a[k] + b[k] + c[k]) / 3.0f;
        for (s = 0; s < g_nPillar; s++) {
            float dx, dy;
            if (!g_pillar[s].used) continue;
            dx = m[0] - (float)(g_pillar[s].s[0] / g_pillar[s].n);
            dy = m[1] - (float)(g_pillar[s].s[1] / g_pillar[s].n);
            if (dx * dx + dy * dy < 250.0f * 250.0f) break;
        }
        if (s == g_nPillar) {
            if (g_nPillar >= PILLAR_MAX) { g_pillarFull++; continue; }
            memset(&g_pillar[s], 0, sizeof g_pillar[s]);
            g_pillar[s].used = 1;
            g_nPillar++;
            u9log_line("GLFX pillar %d learnt on map %d at %.0f,%.0f,%.0f", s, g_pillarMap,
                       (double)m[0], (double)m[1], (double)m[2]);
        }
        if (g_pillar[s].n > 4000.0) {
            for (k = 0; k < 3; k++) g_pillar[s].s[k] *= 0.5;
            g_pillar[s].n *= 0.5;
        }
        for (k = 0; k < 3; k++) g_pillar[s].s[k] += m[k];
        g_pillar[s].n += 1.0;
    }
}

int u9fx_pillar_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v)
{
    int map = u9console_current_map(), s, k, i, lit = 0;
    float str;
    pillar_settings();
    if (map != g_pillarMap || u9glpost_loads() != g_pillarLoads) {
        if (g_nPillar) u9log_line("GLFX pillar: %d columns known on map %d, forgotten", g_nPillar, g_pillarMap);
        memset(g_pillar, 0, sizeof g_pillar);
        g_nPillar = 0;
        g_pillarMap = map;
        g_pillarLoads = u9glpost_loads();
    }
    glyph_of(0);
    for (i = 0; i < g_nGlyph; i++) {
        if (!g_glyph[i].pillar) continue;
        u9gl_runs_with_texture(g_glyph[i].texId, pillar_run, (void *)v);
        if (g_glyph[i].mask && !g_pillarRgbSet) {
            for (k = 0; k < 3; k++) g_pillarRgb[k] = g_glyph[i].rgb[k];
            g_pillarRgbSet = 1;
        }
    }
    str = (float)g_pillarBright / 100.0f;
    if (!g_pillarOn || str <= 0.0f) return n;
    for (s = 0; s < g_nPillar && n < cap && lit < U9FX_PILLAR_LIGHTS; s++) {
        U9LightSnap *l;
        if (!g_pillar[s].used || g_pillar[s].n <= 0.0) continue;
        l = &all[n++];
        memset(l, 0, sizeof *l);
        for (k = 0; k < 3; k++) {
            l->pos[k] = l->base[k] = (float)(g_pillar[s].s[k] / g_pillar[s].n);
            l->rgb[k] = g_pillarRgb[k] * str;
        }
        memcpy(l->baseRgb, l->rgb, sizeof l->rgb);
        l->range = (float)g_pillarReach;
        l->rec = 0xE3000000u + (unsigned)s;
        l->type = U9TYPE_PILLAR_LIGHT;
        l->flags |= U9LIGHT_SOFT;
        lit++;
    }
    if ((unsigned)lit > g_pillarLitMax) g_pillarLitMax = (unsigned)lit;
    (void)v;
    return n;
}

static int      g_gateOn = -1, g_gateGlow = 400, g_gateBright = 500, g_gateReach = 600, g_gateRadius = 400;
static int      g_gateMap = 46;
static float    g_gateAt[3] = { 7105.0f, 8777.0f, 1990.0f };
static float    g_gateRgb[3] = { 1.0f, 0.16f, 0.08f };
static int      g_gateTex[FIRE_TEX_MAX], g_nGateTex;

static void gate_settings(void)
{
    const char *s;
    int mp, r, g, b;
    float x, y, z;
    if (g_gateOn >= 0) return;
    g_gateOn     = u9cfg_int("gl_gate_light", 1);
    g_gateGlow   = u9cfg_int("gl_gate_glow", 400);
    g_gateBright = u9cfg_int("gl_gate_light_bright", 500);
    g_gateReach  = u9cfg_int("gl_gate_light_reach", 600);
    g_gateRadius = u9cfg_int("gl_gate_radius", 400);
    g_nGateTex   = id_list(u9cfg_str("gl_gate_textures", "6953"), g_gateTex);
    s = u9cfg_str("gl_gate_at", "46:7105,8777,1990");
    if (s && sscanf(s, "%d:%f,%f,%f", &mp, &x, &y, &z) == 4) {
        g_gateMap = mp; g_gateAt[0] = x; g_gateAt[1] = y; g_gateAt[2] = z;
    }
    s = u9cfg_str("gl_gate_light_colour", "255,40,20");
    if (s && sscanf(s, "%d,%d,%d", &r, &g, &b) == 3) {
        g_gateRgb[0] = (float)r / 255.0f; g_gateRgb[1] = (float)g / 255.0f; g_gateRgb[2] = (float)b / 255.0f;
    }
    if (g_gateGlow < 0) g_gateGlow = 0;
    if (g_gateBright < 0) g_gateBright = 0;
    if (g_gateReach < 30) g_gateReach = 30;
    if (g_gateRadius < 50) g_gateRadius = 50;
}

int u9fx_gate_tex(int texId)
{
    int k;
    gate_settings();
    for (k = 0; k < g_nGateTex; k++) if (g_gateTex[k] == texId) return 1;
    return 0;
}

int u9fx_gate_here(const float p[3])
{
    float dx, dy;
    gate_settings();
    if (u9console_current_map() != g_gateMap) return 0;
    dx = p[0] - g_gateAt[0]; dy = p[1] - g_gateAt[1];
    return dx * dx + dy * dy < (float)g_gateRadius * (float)g_gateRadius;
}

float u9fx_gate_glow(void)
{
    gate_settings();
    return (float)g_gateGlow / 100.0f;
}

int u9fx_gate_lights(U9LightSnap *all, int n, int cap)
{
    U9LightSnap *l;
    int k;
    gate_settings();
    if (!g_gateOn || g_gateBright <= 0 || n >= cap || u9console_current_map() != g_gateMap) return n;
    l = &all[n++];
    memset(l, 0, sizeof *l);
    for (k = 0; k < 3; k++) {
        l->pos[k] = l->base[k] = g_gateAt[k];
        l->rgb[k] = g_gateRgb[k] * (float)g_gateBright / 100.0f;
    }
    memcpy(l->baseRgb, l->rgb, sizeof l->rgb);
    l->range = (float)g_gateReach;
    l->rec = 0xE4000000u;
    l->type = U9TYPE_GATE_LIGHT;
    l->flags |= U9LIGHT_SOFT;
    return n;
}

void u9fx_gate_command(const char *arg, void (*out)(const char *fmt, ...))
{
    gate_settings();
    while (arg && *arg == ' ') arg++;
    if (!arg) arg = "";
    if (!strcmp(arg, "on")) g_gateOn = 1;
    else if (!strcmp(arg, "off")) g_gateOn = 0;
    else if (!strncmp(arg, "glow ", 5)) g_gateGlow = atoi(arg + 5);
    else if (!strncmp(arg, "bright ", 7)) g_gateBright = atoi(arg + 7);
    else if (!strncmp(arg, "reach ", 6)) g_gateReach = atoi(arg + 6);
    else if (*arg) {
        out("post blackgate on|off (the light) | glow <percent, 0 off> | bright <percent> | reach <units>");
        return;
    }
    if (g_gateGlow < 0) g_gateGlow = 0;
    if (g_gateGlow > 1000) g_gateGlow = 1000;
    if (g_gateBright < 0) g_gateBright = 0;
    if (g_gateBright > 2000) g_gateBright = 2000;
    if (g_gateReach < 30) g_gateReach = 30;
    if (g_gateReach > 5000) g_gateReach = 5000;
    if (*arg) u9log_line("GLFX black gate: '%s' - light %s, glow %d%%, bright %d%%, reach %d", arg,
                         g_gateOn ? "on" : "off", g_gateGlow, g_gateBright, g_gateReach);
    out("black gate: glow %d%% of the bloom; light %s, bright %d%%, reach %d, colour %.2f,%.2f,%.2f;"
        " at map %d %.0f,%.0f,%.0f, radius %d%s", g_gateGlow, g_gateOn ? "on" : "off", g_gateBright,
        g_gateReach, (double)g_gateRgb[0], (double)g_gateRgb[1], (double)g_gateRgb[2], g_gateMap,
        (double)g_gateAt[0], (double)g_gateAt[1], (double)g_gateAt[2], g_gateRadius,
        u9console_current_map() == g_gateMap ? "" : " (not this map)");
}

void u9fx_pillar_command(const char *arg, void (*out)(const char *fmt, ...))
{
    int s, known = 0;
    pillar_settings();
    while (arg && *arg == ' ') arg++;
    if (!arg) arg = "";
    if (!strcmp(arg, "on")) g_pillarOn = 1;
    else if (!strcmp(arg, "off")) g_pillarOn = 0;
    else if (!strncmp(arg, "glow ", 5)) g_pillarGlow = atoi(arg + 5);
    else if (!strncmp(arg, "bright ", 7)) g_pillarBright = atoi(arg + 7);
    else if (!strncmp(arg, "reach ", 6)) g_pillarReach = atoi(arg + 6);
    else if (!strcmp(arg, "forget")) { memset(g_pillar, 0, sizeof g_pillar); g_nPillar = 0; }
    else if (*arg) {
        out("post pillar on|off (the light) | glow <percent, 0 off> | bright <percent> | reach <units> | forget");
        return;
    }
    if (g_pillarGlow < 0) g_pillarGlow = 0;
    if (g_pillarGlow > 1000) g_pillarGlow = 1000;
    if (g_pillarBright < 0) g_pillarBright = 0;
    if (g_pillarBright > 2000) g_pillarBright = 2000;
    if (g_pillarReach < 30) g_pillarReach = 30;
    if (g_pillarReach > 5000) g_pillarReach = 5000;
    if (*arg) u9log_line("GLFX pillar: '%s' - light %s, glow %d%%, bright %d%%, reach %d", arg,
                         g_pillarOn ? "on" : "off", g_pillarGlow, g_pillarBright, g_pillarReach);
    for (s = 0; s < g_nPillar; s++) known += g_pillar[s].used;
    out("small columns: glow %d%% of the bloom; light %s, bright %d%%, reach %d, colour %.2f,%.2f,%.2f%s;"
        " %d columns known on map %d, up to %u lit at once",
        g_pillarGlow, g_pillarOn ? "on" : "off", g_pillarBright, g_pillarReach,
        (double)g_pillarRgb[0], (double)g_pillarRgb[1], (double)g_pillarRgb[2],
        g_pillarRgbSet ? "" : " (not yet read from a column)", known, g_pillarMap, g_pillarLitMax);
}

int u9fx_rune_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v)
{
    int r, s, k, lit = 0;
    float str;
    rune_settings();
    str = (float)g_runeStr / 100.0f;
    if (!g_runeOn || str <= 0.0f) return n;
    for (r = 0; r < g_nRune && n < cap && lit < U9FX_RUNE_LIGHTS; r++) {
        RuneCtx rc;
        if (!g_rune[r].tried || !g_rune[r].mask) continue;
        memset(&rc, 0, sizeof rc);
        rc.v = v;
        u9gl_runs_with_texture(g_rune[r].texId, rune_run, &rc);
        float col[3];
        rune_colour(&g_rune[r], col);
        for (s = 0; s < rc.ns && n < cap && lit < U9FX_RUNE_LIGHTS; s++) {
            U9LightSnap *l = &all[n++];
            memset(l, 0, sizeof *l);
            for (k = 0; k < 3; k++) {
                l->pos[k] = l->base[k] = rc.spot[s].c[k] / (float)rc.spot[s].n;
                l->rgb[k] = col[k] * str;
            }
            l->pos[2] += 12.0f;
            memcpy(l->baseRgb, l->rgb, sizeof l->rgb);
            l->range = (float)g_runeRange;
            l->rec = 0xE2000000u + (unsigned)(r * 4 + s);
            l->type = U9TYPE_RUNE_LIGHT;
            lit++;
        }
    }
    if ((unsigned)lit > g_runeLitMax) g_runeLitMax = (unsigned)lit;
    return n;
}

void u9fx_rune_command(const char *arg, void (*out)(const char *fmt, ...))
{
    int k, made = 0;
    rune_settings();
    while (arg && *arg == ' ') arg++;
    if (!arg) arg = "";
    if (!strcmp(arg, "on")) g_runeOn = 1;
    else if (!strcmp(arg, "off")) g_runeOn = 0;
    else if (!strncmp(arg, "strength ", 9)) g_runeStr = atoi(arg + 9);
    else if (!strncmp(arg, "range ", 6)) g_runeRange = atoi(arg + 6);
    else if (!strncmp(arg, "saturation ", 11)) g_runeSat = atoi(arg + 11);
    else if (*arg) { out("pool rune on|off | strength <percent> | range <units> | saturation <percent>"); return; }
    if (g_runeStr < 0) g_runeStr = 0;
    if (g_runeRange < 30) g_runeRange = 30;
    if (g_runeSat < 0) g_runeSat = 0;
    if (g_runeSat > 2000) g_runeSat = 2000;
    if (*arg) u9log_line("GLFX runes: '%s' - lights %s, strength %d%%, range %d, saturation %d%%", arg,
                         g_runeOn ? "on" : "off", g_runeStr, g_runeRange, g_runeSat);
    for (k = 0; k < g_nRune; k++) made += g_rune[k].mask != 0;
    out("rune lights %s, strength %d%%, range %d, saturation %d%%; %d of %d runes' symbols found so far"
        " (seen in this session)", g_runeOn ? "on" : "off", g_runeStr, g_runeRange, g_runeSat, made, g_nRune);
}

void u9fx_frame(const U9FrameView *v)
{
    int i, k, map;
    fire_learn(v);
    if (g_on < 0) g_on = u9cfg_int("diag_effect_census", 1);
    if (!g_on) {
        if (g_listWanted && g_listOut) g_listOut("the effects census is off (diag_effect_census = 0)");
        g_listWanted = 0;
        return;
    }
    g_view = v;
    g_frame++;
    g_nGrp = 0;
    g_grpDropped = 0;
    for (i = 0; i < g_nTex; i++) {
        g_tex[i].frameSeen = 0; g_tex[i].trisNow = 0; g_tex[i].vN = 0;
        for (k = 0; k < 4; k++) g_tex[i].vNow[k] = 0.0;
    }
    u9gl_effect_runs(count_run, NULL);
    map = u9console_current_map();
    for (i = 0; i < g_nTex; i++) {
        FxTex *t = &g_tex[i];
        if (!t->frameSeen) { t->streak = 0; continue; }
        t->frames++;
        t->streak = (t->lastFrame + 1 == g_frame) ? t->streak + 1 : 1;
        if (t->streak > t->longest) t->longest = t->streak;
        t->lastFrame = g_frame;
        if (t->trisNow > t->maxTris) t->maxTris = t->trisNow;
        if (t->vN) for (k = 0; k < 4; k++) t->vrgba[k] += t->vNow[k] / t->vN;
        t->map = map;
        if (t->frames == 1 && g_newLines < FX_NEW_LINES) { g_newLines++; tex_line("new", t); }
    }
    if (g_listWanted) {
        g_listWanted = 0;
        list_groups();
    }
}

void u9fx_command(const char *arg, void (*out)(const char *fmt, ...))
{
    while (arg && *arg == ' ') arg++;
    if (arg && !strcmp(arg, "census")) {
        int i, shown = 0;
        out("effects census: %d textures over %u frames (%u read back)%s", g_nTex, g_frame, g_reads,
            g_dropped ? " - TABLE FULL" : "");
        for (i = 0; i < g_nTex && shown < 24; i++, shown++) {
            const FxTex *t = &g_tex[i];
            double n = t->frames ? (double)t->frames : 1.0;
            out("tex %d  %s  %u frames (longest %u)  up to %u tris  colour %.0f,%.0f,%.0f", t->texId,
                t->additive ? (t->alpha8 ? "add+a8" : "add") : "a8", t->frames, t->longest, t->maxTris,
                t->vrgba[0] / n * t->texRgb[0] / 255.0, t->vrgba[1] / n * t->texRgb[1] / 255.0,
                t->vrgba[2] / n * t->texRgb[2] / 255.0);
        }
        return;
    }
    if (arg && *arg) { out("post effects | post effects census"); return; }
    g_listWanted = 1;
    g_listOut = out;
}

void u9fx_report(void)
{
    int i, shown;
    if (g_fireOn > 0)
        u9log_line("GLFX fire lights: %u fires learnt, %u forgotten (in sight and not drawn), %u times kept"
                   " while hidden; %u over the table's %d; at most %u lit in a frame; strength %d%%, dungeon %d%%,"
                   " range %d", g_fireLearnt, g_fireForgot, g_fireHidden, g_fireFull, FIRE_MAX, g_fireLitMax,
                   g_fireStr, g_fireDunStr, g_fireRange);
    if (g_on <= 0 || !g_nTex) return;
    u9log_line("GLFX census: %d effect textures over %u post frames, %u read back%s", g_nTex, g_frame, g_reads,
               g_dropped ? " - TABLE FULL, some not counted" : "");
    for (shown = 0; shown < 80; shown++) {
        int best = -1;
        for (i = 0; i < g_nTex; i++) {
            if (g_tex[i].frames == 0xFFFFFFFFu) continue;
            if (best < 0 || g_tex[i].frames > g_tex[best].frames) best = i;
        }
        if (best < 0) break;
        tex_line("census", &g_tex[best]);
        g_tex[best].frames = 0xFFFFFFFFu;
    }
    if (g_nTex > shown) u9log_line("GLFX census: %d more textures not listed", g_nTex - shown);
}
