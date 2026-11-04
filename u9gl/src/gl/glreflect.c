#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "../common/u9time.h"
#include "metrics.h"
#include "glshadow.h"
#include "glreflect.h"

int u9glpost_running(void);

#define GL_FRAMEBUFFER               0x8D40
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_DEPTH_ATTACHMENT          0x8D00
#define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_VERTEX_SHADER             0x8B31
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_TEXTURE0                  0x84C0
#define GL_CLAMP_TO_EDGE             0x812F
#define GL_DEPTH_COMPONENT24         0x81A6

typedef void   (APIENTRY *GenFramebuffersFn)(GLsizei, GLuint *);
typedef void   (APIENTRY *DeleteFramebuffersFn)(GLsizei, const GLuint *);
typedef void   (APIENTRY *BindFramebufferFn)(GLenum, GLuint);
typedef void   (APIENTRY *FramebufferTexture2DFn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *CheckFramebufferStatusFn)(GLenum);
typedef GLuint (APIENTRY *CreateShaderFn)(GLenum);
typedef void   (APIENTRY *ShaderSourceFn)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void   (APIENTRY *CompileShaderFn)(GLuint);
typedef void   (APIENTRY *GetivFn)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *GetInfoLogFn)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *CreateProgramFn)(void);
typedef void   (APIENTRY *AttachShaderFn)(GLuint, GLuint);
typedef void   (APIENTRY *LinkProgramFn)(GLuint);
typedef void   (APIENTRY *UseProgramFn)(GLuint);
typedef void   (APIENTRY *DeleteFn)(GLuint);
typedef GLint  (APIENTRY *GetUniformLocationFn)(GLuint, const char *);
typedef void   (APIENTRY *Uniform1iFn)(GLint, GLint);
typedef void   (APIENTRY *Uniform1fFn)(GLint, GLfloat);
typedef void   (APIENTRY *Uniform2fFn)(GLint, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform4fFn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *ActiveTextureFn)(GLenum);

static GenFramebuffersFn        fGenFramebuffers;
static DeleteFramebuffersFn     fDeleteFramebuffers;
static BindFramebufferFn        fBindFramebuffer;
static FramebufferTexture2DFn   fFramebufferTexture2D;
static CheckFramebufferStatusFn fCheckFramebufferStatus;
static CreateShaderFn           fCreateShader;
static ShaderSourceFn           fShaderSource;
static CompileShaderFn          fCompileShader;
static GetivFn                  fGetShaderiv, fGetProgramiv;
static GetInfoLogFn             fGetShaderInfoLog, fGetProgramInfoLog;
static CreateProgramFn          fCreateProgram;
static AttachShaderFn           fAttachShader;
static LinkProgramFn            fLinkProgram;
static UseProgramFn             fUseProgram;
static DeleteFn                 fDeleteProgram, fDeleteShader;
static GetUniformLocationFn     fGetUniformLocation;
static Uniform1iFn              fUniform1i;
static Uniform1fFn              fUniform1f;
static Uniform2fFn              fUniform2f;
static Uniform4fFn              fUniform4f;
static ActiveTextureFn          fActiveTexture;

static int    g_on = 1;
static int    g_keepS = 30;
static int    g_reach = 6000;

static int    g_loaded, g_failed;
static GLuint g_prog, g_fbo, g_tex, g_depth;
static int    g_w, g_h;
static GLint  uProj, uM0, uM1, uM2, uEngine, uK, uTex, uTextured, uCut, uAlphaRef;

#define RM_MAX   65536
#define RM_HASH  (1 << 17)
#define RM_EVERY 4
typedef struct {
    float    p[9], st[6], area;
    unsigned char col[12];
    GLuint   tex;
    unsigned keyA, keyB, pass, partial;
    DWORD    seen;
    unsigned char cut, live;
} RTri;
static RTri  *g_mem;
static int   *g_hash;
static int    g_n, g_dead;
static unsigned g_pass, g_tick;
static RTri **g_list;
static int    g_listN, g_listCap;
static float  g_cam[3];
static int    g_haveCam;
static float  g_waterZ;
static int    g_haveWater;

static float *g_pos, *g_tc;
static unsigned char *g_col;
static int    g_vN, g_vCap;
static GLuint *g_runTex;
static unsigned char *g_runCut;
static int   *g_runStart, g_runs, g_runCap;

static unsigned g_frames, g_built, g_fresh, g_fromMem, g_new, g_dropped, g_logNew, g_logDropped;
static DWORD  g_logAt;
static int    g_logNow;
static const char *g_why = "not run yet";

static const char *const k_vs =
    "#version 120\n"
    "uniform vec4 u_proj;\n"
    "uniform vec4 u_m0, u_m1, u_m2;\n"
    "uniform vec2 u_engine;\n"
    "uniform vec4 u_k;\n"
    "varying float v_z;\n"
    "void main() {\n"
    "    vec3 w = gl_Vertex.xyz;\n"
    "    v_z = w.z;\n"
    "    vec3 d = vec3(w.xy, 2.0 * u_k.x - w.z) - vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    vec3 c = vec3(dot(vec3(u_m0.x, u_m1.x, u_m2.x), d), dot(vec3(u_m0.y, u_m1.y, u_m2.y), d),\n"
    "                  dot(vec3(u_m0.z, u_m1.z, u_m2.z), d));\n"
    "    float f = c.y;\n"
    "    gl_Position = vec4(2.0 * (u_proj.z * f + c.x * u_proj.x) / u_engine.x - f,\n"
    "                       f - 2.0 * (u_proj.w * f - c.z * u_proj.y) / u_engine.y,\n"
    "                       ((f - u_k.y) / (u_k.z - u_k.y) * 2.0 - 1.0) * f, f);\n"
    "    gl_TexCoord[0] = vec4(gl_MultiTexCoord0.xy, 0.0, 1.0);\n"
    "    gl_FrontColor = gl_Color;\n"
    "}\n";

static const char *const k_fs =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform int   u_textured;\n"
    "uniform int   u_cut;\n"
    "uniform float u_alphaRef;\n"
    "uniform vec4  u_k;\n"
    "varying float v_z;\n"
    "void main() {\n"
    "    if (v_z < u_k.x - 1.0) discard;\n"
    "    vec4 t = u_textured != 0 ? texture2D(u_tex, gl_TexCoord[0].st) : vec4(0.6, 0.6, 0.6, 1.0);\n"
    "    if (u_cut != 0 && t.a <= u_alphaRef) discard;\n"
    "    vec3 c = t.rgb * gl_Color.rgb;\n"
    "    c = mix(c, gl_Fog.color.rgb, clamp(gl_Color.a, 0.0, 1.0));\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

void u9reflect_init(void)
{
    g_on    = u9cfg_int("gl_water_reflect_memory", 1);
    g_keepS = u9cfg_int("gl_water_reflect_keep", 30);
    g_reach = u9cfg_int("gl_water_reflect_reach", 6000);
}

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLREFL off for this session: %s", why);
    g_failed = 1;
}

static GLuint compile(GLenum type, const char *src, const char *what)
{
    GLuint s = fCreateShader(type);
    GLint  ok = 0;
    if (!s) return 0;
    fShaderSource(s, 1, &src, NULL);
    fCompileShader(s);
    fGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei n = 0;
        fGetShaderInfoLog(s, sizeof log - 1, &n, log);
        log[n > 0 ? n : 0] = 0;
        u9log_line("GLREFL %s shader did not compile: %s", what, log);
        fDeleteShader(s);
        return 0;
    }
    return s;
}

static int load(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    if (g_loaded) return !g_failed;
    g_loaded = 1;
    fGenFramebuffers        = (GenFramebuffersFn)gl_proc("glGenFramebuffers");
    fDeleteFramebuffers     = (DeleteFramebuffersFn)gl_proc("glDeleteFramebuffers");
    fBindFramebuffer        = (BindFramebufferFn)gl_proc("glBindFramebuffer");
    fFramebufferTexture2D   = (FramebufferTexture2DFn)gl_proc("glFramebufferTexture2D");
    fCheckFramebufferStatus = (CheckFramebufferStatusFn)gl_proc("glCheckFramebufferStatus");
    fCreateShader           = (CreateShaderFn)gl_proc("glCreateShader");
    fShaderSource           = (ShaderSourceFn)gl_proc("glShaderSource");
    fCompileShader          = (CompileShaderFn)gl_proc("glCompileShader");
    fGetShaderiv            = (GetivFn)gl_proc("glGetShaderiv");
    fGetProgramiv           = (GetivFn)gl_proc("glGetProgramiv");
    fGetShaderInfoLog       = (GetInfoLogFn)gl_proc("glGetShaderInfoLog");
    fGetProgramInfoLog      = (GetInfoLogFn)gl_proc("glGetProgramInfoLog");
    fCreateProgram          = (CreateProgramFn)gl_proc("glCreateProgram");
    fAttachShader           = (AttachShaderFn)gl_proc("glAttachShader");
    fLinkProgram            = (LinkProgramFn)gl_proc("glLinkProgram");
    fUseProgram             = (UseProgramFn)gl_proc("glUseProgram");
    fDeleteProgram          = (DeleteFn)gl_proc("glDeleteProgram");
    fDeleteShader           = (DeleteFn)gl_proc("glDeleteShader");
    fGetUniformLocation     = (GetUniformLocationFn)gl_proc("glGetUniformLocation");
    fUniform1i              = (Uniform1iFn)gl_proc("glUniform1i");
    fUniform1f              = (Uniform1fFn)gl_proc("glUniform1f");
    fUniform2f              = (Uniform2fFn)gl_proc("glUniform2f");
    fUniform4f              = (Uniform4fFn)gl_proc("glUniform4f");
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D ||
        !fCheckFramebufferStatus || !fCreateShader || !fShaderSource || !fCompileShader ||
        !fGetShaderiv || !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog ||
        !fCreateProgram || !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram ||
        !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform1f || !fUniform2f ||
        !fUniform4f || !fActiveTexture) {
        fail("missing GL entry points");
        return 0;
    }
    vs = compile(GL_VERTEX_SHADER, k_vs, "vertex");
    fs = vs ? compile(GL_FRAGMENT_SHADER, k_fs, "fragment") : 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        fail("the mirror's shaders did not build");
        return 0;
    }
    g_prog = fCreateProgram();
    fAttachShader(g_prog, vs);
    fAttachShader(g_prog, fs);
    fLinkProgram(g_prog);
    fDeleteShader(vs);
    fDeleteShader(fs);
    fGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei n = 0;
        fGetProgramInfoLog(g_prog, sizeof log - 1, &n, log);
        log[n > 0 ? n : 0] = 0;
        u9log_line("GLREFL program did not link: %s", log);
        fDeleteProgram(g_prog);
        g_prog = 0;
        fail("the mirror's program did not link");
        return 0;
    }
    uProj     = fGetUniformLocation(g_prog, "u_proj");
    uM0       = fGetUniformLocation(g_prog, "u_m0");
    uM1       = fGetUniformLocation(g_prog, "u_m1");
    uM2       = fGetUniformLocation(g_prog, "u_m2");
    uEngine   = fGetUniformLocation(g_prog, "u_engine");
    uK        = fGetUniformLocation(g_prog, "u_k");
    uTex      = fGetUniformLocation(g_prog, "u_tex");
    uTextured = fGetUniformLocation(g_prog, "u_textured");
    uCut      = fGetUniformLocation(g_prog, "u_cut");
    uAlphaRef = fGetUniformLocation(g_prog, "u_alphaRef");
    u9log_line("GLREFL ready: memory %s, kept %d s, reach %d", g_on ? "on" : "off", g_keepS, g_reach);
    return 1;
}

static void delete_targets(void)
{
    if (g_fbo) { fDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_tex) {
        glDeleteTextures(1, &g_tex);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_tex = 0;
    }
    if (g_depth) {
        glDeleteTextures(1, &g_depth);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_depth = 0;
    }
    g_w = g_h = 0;
}

static int make_targets(int w, int h)
{
    GLenum status;
    delete_targets();
    glPushAttrib(GL_TEXTURE_BIT);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    u9metrics_tex_created((unsigned)(w * h * 4));
    glGenTextures(1, &g_depth);
    glBindTexture(GL_TEXTURE_2D, g_depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    u9metrics_tex_created((unsigned)(w * h * 4));
    glPopAttrib();
    g_w = w; g_h = h;
    fGenFramebuffers(1, &g_fbo);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_depth, 0);
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        u9log_line("GLREFL target %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
        delete_targets();
        fail("the mirror's target was incomplete");
        return 0;
    }
    u9log_line("GLREFL target %dx%d, RGBA8 and 24-bit depth", w, h);
    return 1;
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
    for (k = 0; k < 3; k++)
        out[k] = v->m[k * 4] * cs[0] + v->m[k * 4 + 1] * cs[1] + v->m[k * 4 + 2] * cs[2] + v->m[k * 4 + 3];
    return 1;
}

static int on_screen(const U9FrameView *v, const float p[3])
{
    float d[3], cx, cy, cz, ex, ey, mx = v->engineW * 0.03f, my = v->engineH * 0.03f;
    d[0] = p[0] - v->m[3]; d[1] = p[1] - v->m[7]; d[2] = p[2] - v->m[11];
    cx = v->m[0] * d[0] + v->m[4] * d[1] + v->m[8] * d[2];
    cy = v->m[1] * d[0] + v->m[5] * d[1] + v->m[9] * d[2];
    cz = v->m[2] * d[0] + v->m[6] * d[1] + v->m[10] * d[2];
    if (cy < 32.0f || cy > v->zFar * 0.9f) return 0;
    ex = v->cx + cx * v->sx / cy;
    ey = v->cy - cz * v->sy / cy;
    return ex > mx && ex < v->engineW - mx && ey > my && ey < v->engineH - my;
}

static int whole_on_screen(const U9FrameView *v, const float p[9])
{
    return on_screen(v, p) && on_screen(v, p + 3) && on_screen(v, p + 6);
}

#define WZ_MAX 1024
typedef struct { const U9FrameView *v; float z[WZ_MAX]; int n; } WaterCtx;

static void water_z(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    WaterCtx *c = (WaterCtx *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned i;
    (void)glTex; (void)cutout;
    for (i = first; i < first + count && c->n < WZ_MAX; i++) {
        float w[3];
        if (world_of(c->v, (const float *)(base + (size_t)i * (size_t)stride), w)) c->z[c->n++] = w[2];
    }
}

static int fcmp(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static int grow(int tris, int runs)
{
    if (tris > g_vCap) {
        int nc = tris + 8192;
        float *p2 = (float *)realloc(g_pos, (size_t)nc * 9 * sizeof *p2);
        float *t2 = p2 ? (float *)realloc(g_tc, (size_t)nc * 6 * sizeof *t2) : NULL;
        unsigned char *c2 = t2 ? (unsigned char *)realloc(g_col, (size_t)nc * 12) : NULL;
        if (p2) g_pos = p2;
        if (t2) g_tc = t2;
        if (c2) g_col = c2;
        if (!p2 || !t2 || !c2) return 0;
        g_vCap = nc;
    }
    if (runs + 1 > g_runCap) {
        int nc = runs + 1024;
        GLuint *r2 = (GLuint *)realloc(g_runTex, (size_t)nc * sizeof *r2);
        unsigned char *u2 = r2 ? (unsigned char *)realloc(g_runCut, (size_t)nc) : NULL;
        int *s2 = u2 ? (int *)realloc(g_runStart, (size_t)(nc + 1) * sizeof *s2) : NULL;
        if (r2) g_runTex = r2;
        if (u2) g_runCut = u2;
        if (s2) g_runStart = s2;
        if (!r2 || !u2 || !s2) return 0;
        g_runCap = nc;
    }
    return 1;
}

static void put(const float p[9], const float st[6], const unsigned char col[12], GLuint tex, int cut)
{
    if (!grow(g_vN + 1, g_runs + 1)) return;
    if (!g_runs || g_runTex[g_runs - 1] != tex || g_runCut[g_runs - 1] != (unsigned char)cut) {
        g_runTex[g_runs] = tex;
        g_runCut[g_runs] = (unsigned char)cut;
        g_runStart[g_runs] = g_vN;
        g_runs++;
    }
    memcpy(g_pos + g_vN * 9, p, 9 * sizeof(float));
    memcpy(g_tc + g_vN * 6, st, 6 * sizeof(float));
    memcpy(g_col + g_vN * 12, col, 12);
    g_vN++;
}

typedef struct {
    const U9FrameView *v;
    float camX, camY, reach2, minZ;
    DWORD now;
    int   file;
} RCtx;

static void file_tri(RCtx *c, const float w[9], const float st[6], const unsigned char col[12], GLuint tex,
                     int cut, int clipped)
{
    float e1[3], e2[3], n[3], area, scale;
    unsigned a = 2166136261u, b = 16777619u, s;
    int k, q;
    for (k = 0; k < 3; k++) { e1[k] = w[3 + k] - w[k]; e2[k] = w[6 + k] - w[k]; }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    area = 0.5f * (float)sqrt((double)(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
    if (clipped && area < 64.0f) return;
    scale = clipped ? 1.0f / 32.0f : 0.25f;
    for (k = 0; k < 9; k++) {
        float f = w[k] * scale;
        q = (int)f;
        q -= f < (float)q;
        a = (a ^ (unsigned)q) * 16777619u;
        b = (b + (unsigned)q) * 2246822519u;
    }
    b ^= tex ^ (clipped ? 0x9E3779B9u : 0u);
    for (s = (a * 2654435761u) & (RM_HASH - 1); g_hash[s]; s = (s + 1) & (RM_HASH - 1)) {
        RTri *e = &g_mem[g_hash[s] - 1];
        if (e->keyA == a && e->keyB == b) break;
    }
    if (!g_hash[s]) {
        if (g_n >= RM_MAX) return;
        g_hash[s] = ++g_n;
        g_mem[g_n - 1].keyA = a;
        g_mem[g_n - 1].keyB = b;
        g_mem[g_n - 1].live = 0;
        g_new++;
    }
    {
        RTri *e = &g_mem[g_hash[s] - 1];
        if (e->live && clipped && area < e->area * 0.9f) {
            e->seen = c->now;
            e->partial = g_pass;
            return;
        }
        e->partial = 0;
        e->live = 1;
        memcpy(e->p, w, sizeof e->p);
        memcpy(e->st, st, sizeof e->st);
        memcpy(e->col, col, sizeof e->col);
        e->area = area;
        e->tex = tex;
        e->cut = (unsigned char)(cut != 0);
        e->pass = g_pass;
        e->seen = c->now;
    }
}

static void collect(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    RCtx *c = (RCtx *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    float edgeX = c->v->engineW - 1.5f, edgeY = c->v->engineH - 1.5f;
    unsigned t;
    for (t = first; t + 2 < first + count; t += 3) {
        float w[9], st[6], mx, my;
        unsigned char col[12];
        int k, clipped = 0;
        for (k = 0; k < 3; k++) {
            const float *f = (const float *)(base + (size_t)(t + (unsigned)k) * (size_t)stride);
            const unsigned char *rgba = (const unsigned char *)(f + 8);
            float fog = f[7];
            if (!world_of(c->v, f, w + k * 3)) break;
            st[k * 2]     = f[3] / f[6];
            st[k * 2 + 1] = f[4] / f[6];
            col[k * 4]     = rgba[0];
            col[k * 4 + 1] = rgba[1];
            col[k * 4 + 2] = rgba[2];
            col[k * 4 + 3] = (unsigned char)(fog <= 0.0f ? 0 : fog >= 1.0f ? 255 : (int)(fog * 255.0f + 0.5f));
            if (f[0] < 1.5f || f[0] > edgeX || f[1] < 1.5f || f[1] > edgeY) clipped = 1;
        }
        if (k < 3) continue;
        if (w[2] < c->minZ && w[5] < c->minZ && w[8] < c->minZ) continue;
        mx = (w[0] + w[3] + w[6]) / 3.0f - c->camX;
        my = (w[1] + w[4] + w[7]) / 3.0f - c->camY;
        if (mx * mx + my * my > c->reach2) continue;
        put(w, st, col, (GLuint)glTex, cutout);
        g_fresh++;
        if (c->file) file_tri(c, w, st, col, (GLuint)glTex, cutout, clipped);
    }
}

static void clear_memory(void)
{
    g_n = g_dead = g_listN = 0;
    if (g_hash) memset(g_hash, 0, RM_HASH * sizeof *g_hash);
}

static void rehash(void)
{
    int i, n = 0;
    for (i = 0; i < g_n; i++) if (g_mem[i].live) g_mem[n++] = g_mem[i];
    g_n = n;
    g_dead = 0;
    memset(g_hash, 0, RM_HASH * sizeof *g_hash);
    for (i = 0; i < g_n; i++) {
        unsigned s;
        for (s = (g_mem[i].keyA * 2654435761u) & (RM_HASH - 1); g_hash[s]; s = (s + 1) & (RM_HASH - 1)) {}
        g_hash[s] = i + 1;
    }
}

static int lcmp(const void *x, const void *y)
{
    const RTri *a = *(const RTri *const *)x, *b = *(const RTri *const *)y;
    if (a->tex != b->tex) return a->tex < b->tex ? -1 : 1;
    return (int)a->cut - (int)b->cut;
}

#define GROUP_HASH 4096
static void group_list(void)
{
    static GLuint key[GROUP_HASH];
    static int    slot[GROUP_HASH], start[GROUP_HASH + 1];
    static RTri **tmp;
    static int   *bucket;
    static int    tmpCap;
    int i, k = 0;
    if (tmpCap < g_listN) {
        RTri **t2 = (RTri **)realloc(tmp, (size_t)g_listN * sizeof *t2);
        int *b2 = t2 ? (int *)realloc(bucket, (size_t)g_listN * sizeof *b2) : NULL;
        if (t2) tmp = t2;
        if (b2) bucket = b2;
        if (!t2 || !b2) { qsort(g_list, (size_t)g_listN, sizeof *g_list, lcmp); return; }
        tmpCap = g_listN;
    }
    memset(slot, 0, sizeof slot);
    memset(start, 0, sizeof start);
    for (i = 0; i < g_listN; i++) {
        GLuint t = g_list[i]->tex * 2u + g_list[i]->cut;
        unsigned s = (t * 2654435761u) & (GROUP_HASH - 1);
        while (slot[s] && key[s] != t) s = (s + 1) & (GROUP_HASH - 1);
        if (!slot[s]) {
            if (k == GROUP_HASH - 1) { qsort(g_list, (size_t)g_listN, sizeof *g_list, lcmp); return; }
            key[s] = t;
            slot[s] = ++k;
        }
        bucket[i] = slot[s] - 1;
        start[slot[s]]++;
    }
    for (i = 1; i <= k; i++) start[i] += start[i - 1];
    for (i = 0; i < g_listN; i++) tmp[start[bucket[i]]++] = g_list[i];
    memcpy(g_list, tmp, (size_t)g_listN * sizeof *g_list);
}

static void update(const U9FrameView *v, float reach2, DWORD now)
{
    int i, n = 0, full = g_n > RM_MAX - 4096;
    if (g_listCap < g_n) {
        RTri **l2 = (RTri **)realloc(g_list, (size_t)(g_n + 4096) * sizeof *l2);
        if (!l2) { g_listN = 0; return; }
        g_list = l2;
        g_listCap = g_n + 4096;
    }
    for (i = 0; i < g_n; i++) {
        RTri *e = &g_mem[i];
        float mid[3], dx, dy;
        int k;
        if (!e->live) continue;
        if (e->pass != g_pass) {
            for (k = 0; k < 3; k++) mid[k] = (e->p[k] + e->p[3 + k] + e->p[6 + k]) / 3.0f;
            dx = mid[0] - v->m[3]; dy = mid[1] - v->m[7];
            if (dx * dx + dy * dy > reach2 || (e->partial != g_pass && whole_on_screen(v, e->p)) ||
                now - e->seen > (DWORD)(g_keepS > 1 ? g_keepS : 1) * 1000u || (full && now - e->seen > 5000u)) {
                e->live = 0;
                g_dead++;
                g_dropped++;
                continue;
            }
        }
        g_list[n++] = e;
    }
    g_listN = n;
    if (g_dead > 4096 && g_dead > g_n / 2) {
        rehash();
        n = 0;
        for (i = 0; i < g_n; i++) g_list[n++] = &g_mem[i];
        g_listN = n;
    }
    group_list();
}

static void from_memory(const U9FrameView *v)
{
    int i;
    for (i = 0; i < g_listN; i++) {
        const RTri *e = g_list[i];
        if (!e->live) continue;
        if (e->partial != g_pass && whole_on_screen(v, e->p)) continue;
        put(e->p, e->st, e->col, e->tex, e->cut);
        g_fromMem++;
    }
}

unsigned u9reflect_build(const U9FrameView *v, int w, int h, int scale)
{
    int rw, rh, r;
    float reach, cam[3];
    DWORD now;
    if (!g_on) { g_why = "off (post water memory on)"; return 0; }
    if (g_failed || !v || !v->valid || v->depthMode != 2 || w < 4 || h < 4 || !load()) {
        g_why = g_failed ? "FAILED, see the log" : "no frame view";
        return 0;
    }
    {
        static WaterCtx wc;
        wc.v = v;
        wc.n = 0;
        u9gl_runs_with_texture(U9TEX_WATER, water_z, &wc);
        if (wc.n >= 3) {
            qsort(wc.z, (size_t)wc.n, sizeof wc.z[0], fcmp);
            g_waterZ = wc.z[wc.n / 2];
            g_haveWater = 1;
        }
    }
    if (!g_haveWater) { g_why = "no water seen yet"; return 0; }
    if (!g_mem) {
        g_mem = (RTri *)malloc(RM_MAX * sizeof *g_mem);
        g_hash = (int *)calloc(RM_HASH, sizeof *g_hash);
        if (!g_mem || !g_hash) {
            free(g_mem); free(g_hash); g_mem = NULL; g_hash = NULL;
            fail("no memory for the triangles");
            return 0;
        }
    }
    g_frames++;
    now = u9time_now();
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
    reach = (float)(g_reach > 500 ? g_reach : 500);
    if (g_haveCam) {
        float dx = cam[0] - g_cam[0], dy = cam[1] - g_cam[1], dz = cam[2] - g_cam[2];
        if (dx * dx + dy * dy + dz * dz > 3000.0f * 3000.0f) clear_memory();
    }
    memcpy(g_cam, cam, sizeof g_cam);
    g_haveCam = 1;

    g_vN = g_runs = 0;
    g_fresh = g_fromMem = 0;
    {
        RCtx c;
        c.v = v;
        c.camX = cam[0];
        c.camY = cam[1];
        c.reach2 = reach * reach;
        c.minZ = g_waterZ - 2.0f;
        c.now = now;
        c.file = (g_tick++ % RM_EVERY) == 0 || !g_listN;
        if (c.file) g_pass++;
        u9gl_casters(collect, &c);
        if (c.file) update(v, c.reach2, now);
    }
    from_memory(v);
    g_logNow = now - g_logAt >= 5000u;
    if (g_logNow) {
        int i, front = 0, inside = 0;
        float zf = v->zFar > 1000.0f ? v->zFar : 32768.0f;
        for (i = 0; i < g_vN; i++) {
            const float *p = g_pos + i * 9;
            float d[3], cx, cy, cz, ex, ey;
            d[0] = (p[0] + p[3] + p[6]) / 3.0f - v->m[3];
            d[1] = (p[1] + p[4] + p[7]) / 3.0f - v->m[7];
            d[2] = 2.0f * g_waterZ - (p[2] + p[5] + p[8]) / 3.0f - v->m[11];
            cx = v->m[0] * d[0] + v->m[4] * d[1] + v->m[8] * d[2];
            cy = v->m[1] * d[0] + v->m[5] * d[1] + v->m[9] * d[2];
            cz = v->m[2] * d[0] + v->m[6] * d[1] + v->m[10] * d[2];
            if (cy <= 16.0f || cy >= zf) continue;
            front++;
            ex = v->cx + cx * v->sx / cy;
            ey = v->cy - cz * v->sy / cy;
            if (ex >= 0.0f && ex < v->engineW && ey >= 0.0f && ey < v->engineH) inside++;
        }
        u9log_line("GLREFL water at z %.0f; %d triangles held, %u new and %u forgotten in %u ms; drawn %d (%u from memory),"
                   " %d in front once mirrored, %d of them on the screen; camera z %.0f",
                   (double)g_waterZ, g_listN, g_new - g_logNew, g_dropped - g_logDropped, (unsigned)(now - g_logAt),
                   g_vN, g_fromMem, front, inside, (double)v->m[11]);
        g_logAt = now;
        g_logNew = g_new;
        g_logDropped = g_dropped;
    }

    if (scale < 1) scale = 1;
    rw = v->vw / (2 * scale); rh = v->vh / (2 * scale);
    if (rw < 2) rw = 2;
    if (rh < 2) rh = 2;
    if ((rw != g_w || rh != g_h) && !make_targets(rw, rh)) return 0;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glDisableClientState(0x8457 );
    glDisableClientState(GL_NORMAL_ARRAY);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, rw, rh);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_LIGHTING);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (g_logNow) {
        int k;
        for (k = 0; k < 16 && glGetError() != GL_NO_ERROR; k++) {}
    }
    if (g_vN) {
        fUseProgram(g_prog);
        fUniform4f(uProj, v->sx, v->sy, v->cx, v->cy);
        fUniform4f(uM0, v->m[0], v->m[1], v->m[2], v->m[3]);
        fUniform4f(uM1, v->m[4], v->m[5], v->m[6], v->m[7]);
        fUniform4f(uM2, v->m[8], v->m[9], v->m[10], v->m[11]);
        fUniform2f(uEngine, v->engineW, v->engineH);
        fUniform4f(uK, g_waterZ, 16.0f, v->zFar > 1000.0f ? v->zFar : 32768.0f, 0.0f);
        fUniform1i(uTex, 0);
        fUniform1f(uAlphaRef, (float)u9cfg_int("alpha_ref", 128) / 255.0f);
        fActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, g_pos);
        glTexCoordPointer(2, GL_FLOAT, 0, g_tc);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, g_col);
        g_runStart[g_runs] = g_vN;
        for (r = 0; r < g_runs; r++) {
            int n = g_runStart[r + 1] - g_runStart[r];
            if (n <= 0) continue;
            glBindTexture(GL_TEXTURE_2D, g_runTex[r]);
            fUniform1i(uTextured, g_runTex[r] != 0);
            fUniform1i(uCut, g_runCut[r] && g_runTex[r]);
            glDrawArrays(GL_TRIANGLES, g_runStart[r] * 3, n * 3);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        fUseProgram(0);
    }
    if (g_logNow) {
        unsigned char *px = (unsigned char *)malloc((size_t)rw * (size_t)rh * 4);
        if (px) {
            int x, y, n = 0, x0 = rw, y0 = rh, x1 = -1, y1 = -1;
            GLenum err;
            glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, px);
            for (y = 0; y < rh; y++)
                for (x = 0; x < rw; x++)
                    if (px[((size_t)y * (size_t)rw + (size_t)x) * 4 + 3]) {
                        n++;
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (y < y0) y0 = y;
                        if (y > y1) y1 = y;
                    }
            err = glGetError();
            u9log_line("GLREFL target: %d of %d pixels drawn, x %d..%d, y %d..%d (0 the bottom); GL error 0x%04X", n,
                       rw * rh, x0, x1, y0, y1, (unsigned)err);
            free(px);
        }
    }
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPopClientAttrib();
    glPopAttrib();
    g_built++;
    g_why = "on";
    return g_tex;
}

int u9reflect_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strcmp(v, "memory on")) g_on = 1;
    else if (!strcmp(v, "memory off")) g_on = 0;
    else if (!strcmp(v, "memory forget")) clear_memory();
    else if (!strncmp(v, "keep ", 5)) g_keepS = atoi(v + 5);
    else if (!strncmp(v, "reach ", 6)) g_reach = atoi(v + 6);
    else return 0;
    u9log_line("GLREFL '%s' - memory %s, kept %d s, reach %d", v, g_on ? "on" : "off", g_keepS, g_reach);
    (void)out;
    return 1;
}

void u9reflect_report(void (*out)(const char *fmt, ...))
{
    out("mirror from memory %s%s: kept %d s unseen, reach %d; last frame %s, water at z %.0f, %d triangles held,"
        " %d drawn (%u from memory); built %u of %u frames",
        g_on ? "on" : "off (post water memory on)", g_failed ? " - FAILED, see the log" : "", g_keepS, g_reach, g_why,
        (double)g_waterZ, g_listN, g_vN, g_fromMem, g_built, g_frames);
}

void u9reflect_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_targets();
        if (g_prog) fDeleteProgram(g_prog);
    }
    g_fbo = g_tex = g_depth = 0;
    g_prog = 0;
    g_w = g_h = 0;
    g_loaded = 0;
    g_failed = 0;
}
