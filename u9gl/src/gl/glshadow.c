#include <windows.h>
#include <GL/gl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "metrics.h"
#include "../common/u9time.h"
#include "glspaces.h"
#include "glshadow.h"
#include "glbackface.h"
#include "shadowcost.h"
#include "../common/perfclock.h"

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
#define GL_TEXTURE_CUBE_MAP          0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_R32F                      0x822E
#define GL_RED_                      0x1903
#define GL_DEPTH_COMPONENT24         0x81A6
#define GL_TEXTURE_SWIZZLE_G         0x8E43
#define GL_TEXTURE_SWIZZLE_B         0x8E44

typedef void   (APIENTRY *GenFramebuffersFn)(GLsizei, GLuint *);
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
typedef void   (APIENTRY *Uniform3fFn)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform4fFn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *ActiveTextureFn)(GLenum);

static GenFramebuffersFn        fGenFramebuffers;
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
static DeleteFn                 fDeleteShader;
static GetUniformLocationFn     fGetUniformLocation;
static Uniform1iFn              fUniform1i;
static Uniform1fFn              fUniform1f;
static Uniform2fFn              fUniform2f;
static Uniform3fFn              fUniform3f;
static Uniform4fFn              fUniform4f;
static ActiveTextureFn          fActiveTexture;

int      g_shadowOn;
int      g_shadowSize = 512;
int      g_shadowCutouts = 1;
int      g_shadowView;
float    g_shadowNear = 24.0f;
float    g_shadowFixture = 384.0f;
float    g_shadowColumn = 48.0f;
float    g_shadowPart = 96.0f;
int      g_shadowHideBlob = 1;
int      g_shadowCount = 8;
unsigned g_shadowBuilds, g_shadowTris, g_shadowFixtures;
int      g_shadowMem = 1;
int      g_shadowMemAnimated = 0;
int      g_shadowFire = 1;
unsigned g_shadowMemTris, g_shadowMemFix, g_shadowMemUs, g_shadowMemFrames;
static unsigned g_shadowMemPeakTris, g_shadowMemPeakUs;
static unsigned g_shadowMemPrevTris, g_shadowMemPrevFix, g_shadowMemPrevUs;
static unsigned g_shadowMemChosen;
static unsigned g_lsWhy[5];
static uint32_t g_sunBuiltFrame = 0xFFFFFFFFu;
int      g_sunView, g_sunDebug, g_sunOn;
float    g_sunStrength = 0.60f;
int      g_sunSize = 2048;
float    g_sunRange = 10000.0f;
float    g_sunNear = 1000.0f;
int      g_sunCascades = 1;
float    g_sunAhead = 0.6f;
float    g_sunMinElevation = 5.0f;
float    g_sunFadeFrom = 2500.0f, g_sunFadeTo = 4000.0f;
float    g_sunBias = 0.0f;
int      g_sunSlopeOcc = 1;
int      g_sunWalls = 1;
int      g_sunSteady = 1;
#define SUN_STEADY_DEG  3.0f
#define SUN_STEADY_AWAY 2000.0f
static float    g_sunPivot[3];
static float    g_sunRef[3];
static int      g_sunHaveRef;
static unsigned g_sunRebases;

int      g_sunFreeze;
int      g_sunHold;
static float g_sunHeld[3];
static int   g_sunHaveHeld;
int      g_sunSmooth = 1;
int      g_sunStepMs = 2000, g_sunXfadeMs = 1000;
#define SUN_XFADE_SNAP 1.0f
static float    g_xNew[3], g_xOld[3];
static int      g_xHave, g_xFading;
static DWORD    g_xAt;
static U9SunMap g_oldNear, g_oldFar;
static int      g_oldMaps;
static float    g_xMix = 1.0f;
static unsigned g_xSteps;
static U9SunMap g_frozenNear, g_frozenFar;
static int      g_frozenMaps;
#define TRACE_FRAMES 180
#define TRACE_TEX    512
static int      g_traceLeft;
static unsigned g_traceTex[2][TRACE_TEX], g_traceTris[2][TRACE_TEX];
static double   g_traceSum[2][TRACE_TEX];
static int      g_traceN[2], g_traceCur;
static DWORD    g_traceStart;
static double   g_traceX[2][TRACE_TEX], g_traceY[2][TRACE_TEX];
static unsigned g_follow;
typedef struct { unsigned tex, frames, changed, triLo, triHi; double x, y; } TraceSum;
static TraceSum g_traceSums[TRACE_TEX];
static int      g_traceSumN;
static float   *g_followV[2];
static unsigned g_followN[2], g_followCap;

static void trace_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    int stride, i, c = g_traceCur;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    double s = 0.0;
    unsigned t;
    (void)ctx;
    (void)cutout;
    double sx = 0.0, sy = 0.0;
    for (t = first; t < first + count; t++) {
        const float *f = (const float *)(base + (size_t)t * (size_t)stride);
        s += (double)f[0] * 1.0 + (double)f[1] * 3.0 + (double)f[6] * 1e5 + (double)f[3] * 7.0 + (double)f[4] * 11.0;
        sx += f[0];
        sy += f[1];
        if (glTex == g_follow && g_followN[c] + 3 <= g_followCap) {
            float *o = g_followV[c] + g_followN[c];
            o[0] = f[0]; o[1] = f[1]; o[2] = f[6];
            g_followN[c] += 3;
        }
    }
    for (i = 0; i < g_traceN[c] && g_traceTex[c][i] != glTex; i++) {}
    if (i == g_traceN[c]) {
        if (i == TRACE_TEX) return;
        g_traceTex[c][i] = glTex;
        g_traceTris[c][i] = 0;
        g_traceSum[c][i] = 0.0;
        g_traceX[c][i] = g_traceY[c][i] = 0.0;
        g_traceN[c]++;
    }
    g_traceTris[c][i] += count / 3;
    g_traceSum[c][i] += s;
    g_traceX[c][i] += sx;
    g_traceY[c][i] += sy;
}

static TraceSum *trace_sum(unsigned tex)
{
    int i;
    for (i = 0; i < g_traceSumN && g_traceSums[i].tex != tex; i++) {}
    if (i == g_traceSumN) {
        if (i == TRACE_TEX) return NULL;
        memset(&g_traceSums[i], 0, sizeof g_traceSums[i]);
        g_traceSums[i].tex = tex;
        g_traceSums[i].triLo = 0xFFFFFFFFu;
        g_traceSumN++;
    }
    return &g_traceSums[i];
}

static void trace_summary(void)
{
    int i, j, shown = 0;
    for (i = 1; i < g_traceSumN; i++) {
        TraceSum t = g_traceSums[i];
        for (j = i; j > 0 && g_traceSums[j - 1].changed < t.changed; j--) g_traceSums[j] = g_traceSums[j - 1];
        g_traceSums[j] = t;
    }
    u9log_line("SUNTRACE summary: %d textures cast; those that changed, most often first", g_traceSumN);
    for (i = 0; i < g_traceSumN && shown < 80; i++) {
        const TraceSum *t = &g_traceSums[i];
        if (!t->changed) continue;
        u9log_line("SUNTRACE   gl tex %u: changed in %u of %u frames, %u..%u triangles, at screen %d,%d", t->tex,
                   t->changed, t->frames, t->triLo, t->triHi, (int)t->x, (int)t->y);
        shown++;
    }
}

static void trace_frame(void)
{
    int c = g_traceCur, p = c ^ 1, i, j, changed = 0, appeared = 0, gone = 0, shown = 0;
    unsigned tris = 0;
    char line[700];
    int at;
    g_traceN[c] = 0;
    u9gl_casters(trace_run, NULL);
    for (i = 0; i < g_traceN[c]; i++) {
        TraceSum *ts = trace_sum(g_traceTex[c][i]);
        tris += g_traceTris[c][i];
        if (!ts) continue;
        ts->frames++;
        if (g_traceTris[c][i] < ts->triLo) ts->triLo = g_traceTris[c][i];
        if (g_traceTris[c][i] > ts->triHi) ts->triHi = g_traceTris[c][i];
        ts->x = g_traceX[c][i] / (double)(g_traceTris[c][i] * 3u);
        ts->y = g_traceY[c][i] / (double)(g_traceTris[c][i] * 3u);
        if (g_traceLeft < TRACE_FRAMES) {
            for (j = 0; j < g_traceN[p] && g_traceTex[p][j] != g_traceTex[c][i]; j++) {}
            if (j < g_traceN[p] && (g_traceTris[p][j] != g_traceTris[c][i] || g_traceSum[p][j] != g_traceSum[c][i]))
                ts->changed++;
        }
    }
    if (g_follow) {
        unsigned k, n = g_followN[c] < g_followN[p] ? g_followN[c] : g_followN[p];
        float dxy = 0.0f, dq = 0.0f;
        for (k = 0; k + 2 < n; k += 3) {
            float ex = g_followV[c][k] - g_followV[p][k], ey = g_followV[c][k + 1] - g_followV[p][k + 1];
            float eq = g_followV[c][k + 2] - g_followV[p][k + 2];
            if (ex < 0.0f) ex = -ex;
            if (ey < 0.0f) ey = -ey;
            if (eq < 0.0f) eq = -eq;
            if (ex > dxy) dxy = ex;
            if (ey > dxy) dxy = ey;
            if (eq > dq) dq = eq;
        }
        u9log_line("SUNTRACE     gl tex %u: %u vertices (%u the frame before), largest move %d/1000 px, q %d/1000000%s",
                   g_follow, g_followN[c] / 3, g_followN[p] / 3, (int)(dxy * 1000.0f), (int)(dq * 1e6f),
                   g_followN[c] == 0 ? " - NOT CAST this frame" : "");
        g_followN[p] = 0;
    }
    at = wsprintfA(line, "SUNTRACE %3d t+%5u ms: %u casters in %d textures;", TRACE_FRAMES - g_traceLeft,
                   (unsigned)(u9time_now() - g_traceStart), tris, g_traceN[c]);
    if (g_traceLeft < TRACE_FRAMES) {
        for (i = 0; i < g_traceN[c]; i++) {
            for (j = 0; j < g_traceN[p] && g_traceTex[p][j] != g_traceTex[c][i]; j++) {}
            if (j == g_traceN[p]) {
                appeared++;
                if (shown < 8 && at < 600) { at += wsprintfA(line + at, " +tex%u(%u)", g_traceTex[c][i], g_traceTris[c][i]); shown++; }
            } else if (g_traceTris[p][j] != g_traceTris[c][i] || g_traceSum[p][j] != g_traceSum[c][i]) {
                changed++;
                if (shown < 8 && at < 600) {
                    at += wsprintfA(line + at, " ~tex%u(%u->%u)", g_traceTex[c][i], g_traceTris[p][j], g_traceTris[c][i]);
                    shown++;
                }
            }
        }
        for (j = 0; j < g_traceN[p]; j++) {
            for (i = 0; i < g_traceN[c] && g_traceTex[c][i] != g_traceTex[p][j]; i++) {}
            if (i == g_traceN[c]) {
                gone++;
                if (shown < 8 && at < 600) { at += wsprintfA(line + at, " -tex%u(%u)", g_traceTex[p][j], g_traceTris[p][j]); shown++; }
            }
        }
        wsprintfA(line + at, "  [%d changed, %d new, %d gone]", changed, appeared, gone);
    }
    u9log_line("%s", line);
    g_traceCur = p;
    if (--g_traceLeft == 0) {
        trace_summary();
        u9log_line("SUNTRACE done");
    }
}

void u9shadow_sun_trace(unsigned followTex)
{
    g_traceLeft = TRACE_FRAMES;
    g_traceN[0] = g_traceN[1] = 0;
    g_traceSumN = 0;
    g_follow = followTex;
    g_followN[0] = g_followN[1] = 0;
    if (followTex && !g_followV[0]) {
        g_followCap = 3u * 30000u;
        g_followV[0] = (float *)malloc(g_followCap * sizeof(float));
        g_followV[1] = (float *)malloc(g_followCap * sizeof(float));
        if (!g_followV[0] || !g_followV[1]) { free(g_followV[0]); free(g_followV[1]); g_followV[0] = g_followV[1] = NULL; g_followCap = 0; }
    }
    g_traceStart = u9time_now();
    u9log_line("SUNTRACE start: %d frames of the sun's casters, per texture (gl texture ids; 'post pick' names them)",
               TRACE_FRAMES);
}
int      g_waterDepth = 1;
unsigned g_sunBuilds, g_sunTris;
int      g_sunCull = 1;
unsigned g_sunRunsDrawn[2], g_sunRunsCulled[2];
static uint32_t g_boxFrame = 0xFFFFFFFFu;
static GLuint g_sunFbo[4], g_sunTex[4], g_sunDepth[4];
static int    g_sunTexSize[4];
int      g_sunMemory = 1;
int      g_sunKeepS = 120;
float    g_sunAbove = -5000.0f;
unsigned g_sunMemLive, g_sunMemDrawn, g_sunMemDropped, g_sunMemNew, g_sunMemSkipped;
static DWORD g_memLogAt;
static unsigned g_memLogNew, g_memLogDropped;

static int    g_failed, g_loaded;
static GLuint g_prog, g_fbo, g_cube[U9SHADOW_MAX], g_cubeDepth;
static float  g_cubeReach[U9SHADOW_MAX];
static int    g_cubeSize;
static GLint  uProj, uM0, uM1, uM2, uLight, uFs, uFt, uFm, uNf, uMode, uTop, uTex, uCutout, uAlphaRef;

static const char *const k_vs =
    "#version 120\n"
    "uniform vec4 u_proj;\n"
    "uniform vec4 u_m0, u_m1, u_m2;\n"
    "uniform vec3 u_light;\n"
    "uniform vec3 u_fs, u_ft, u_fm;\n"
    "uniform vec2 u_nf;\n"
    "uniform int  u_mode;\n"
    "uniform vec4 u_top;\n"
    "varying vec3 v_rel;\n"
    "varying float v_h;\n"
    "void main() {\n"
    "    vec3 world;\n"
    "    if (u_mode == 3 || u_mode == 4) {\n"
    "        world = gl_Vertex.xyz;\n"
    "        gl_TexCoord[0] = vec4(gl_MultiTexCoord0.xy, 0.0, 1.0);\n"
    "    } else {\n"
    "        float q = gl_MultiTexCoord0.w;\n"
    "        float w = 1.0 / q;\n"
    "        vec4 cs = vec4((gl_Vertex.x - u_proj.z) * w / u_proj.x, w, (u_proj.w - gl_Vertex.y) * w / u_proj.y, 1.0);\n"
    "        world = vec3(dot(u_m0, cs), dot(u_m1, cs), dot(u_m2, cs));\n"
    "        gl_TexCoord[0] = vec4(gl_MultiTexCoord0.xy / q, 0.0, 1.0);\n"
    "    }\n"
    "    v_rel = world - u_light;\n"
    "    v_h = world.z;\n"
    "    if (u_mode == 0 || u_mode == 4) {\n"
    "        float ma = dot(u_fm, v_rel);\n"
    "        float n = u_nf.x, f = u_nf.y;\n"
    "        gl_Position = vec4(dot(u_fs, v_rel), dot(u_ft, v_rel), ma * (f + n) / (f - n) - 2.0 * f * n / (f - n), ma);\n"
    "    } else if (u_mode >= 2) {\n"
    "        gl_Position = vec4(dot(u_fs, v_rel) / u_nf.x, dot(u_ft, v_rel) / u_nf.x, -dot(u_fm, v_rel) / u_nf.y, 1.0);\n"
    "    } else {\n"
    "        gl_Position = vec4((world.xy - u_top.xy) / u_top.z, clamp((u_top.w - world.z) / 8192.0, -1.0, 1.0), 1.0);\n"
    "    }\n"
    "}\n";

static const char *const k_fs =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform int   u_cutout;\n"
    "uniform float u_alphaRef;\n"
    "uniform int   u_mode;\n"
    "uniform vec4  u_top;\n"
    "uniform vec2  u_nf;\n"
    "uniform vec3  u_fm;\n"
    "varying vec3 v_rel;\n"
    "varying float v_h;\n"
    "vec3 hue(float h) {\n"
    "    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    if (u_cutout != 0 && texture2D(u_tex, gl_TexCoord[0].st).a <= u_alphaRef) discard;\n"
    "    if (u_mode == 0 || u_mode == 4) { gl_FragColor = vec4(length(v_rel) / u_nf.y); return; }   /* of the reach */\n"
    "    if (u_mode >= 2) { gl_FragColor = vec4(0.5 - 0.5 * dot(u_fm, v_rel) / u_nf.y); return; }   /* 0 sunward */\n"
    "    if (v_h > u_top.w + 128.0) discard;\n"
    "    float band = abs(fract(v_h / 64.0 - 0.5) - 0.5) < fwidth(v_h / 64.0) ? 0.55 : 1.0;\n"
    "    gl_FragColor = vec4(hue(v_h / 512.0) * band, 1.0);\n"
    "}\n";

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLSHADOW off for this session: %s", why);
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
        u9log_line("GLSHADOW %s shader did not compile: %s", what, log);
        fDeleteShader(s);
        return 0;
    }
    return s;
}

static int load(void)
{
    GLuint vs, fs;
    GLint  ok = 0;
    if (g_loaded) return !g_failed;
    g_loaded = 1;
    fGenFramebuffers        = (GenFramebuffersFn)gl_proc("glGenFramebuffers");
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
    fDeleteShader           = (DeleteFn)gl_proc("glDeleteShader");
    fGetUniformLocation     = (GetUniformLocationFn)gl_proc("glGetUniformLocation");
    fUniform1i              = (Uniform1iFn)gl_proc("glUniform1i");
    fUniform1f              = (Uniform1fFn)gl_proc("glUniform1f");
    fUniform2f              = (Uniform2fFn)gl_proc("glUniform2f");
    fUniform3f              = (Uniform3fFn)gl_proc("glUniform3f");
    fUniform4f              = (Uniform4fFn)gl_proc("glUniform4f");
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    if (!fGenFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D || !fCheckFramebufferStatus ||
        !fCreateShader || !fShaderSource || !fCompileShader || !fGetShaderiv || !fGetProgramiv ||
        !fGetShaderInfoLog || !fGetProgramInfoLog || !fCreateProgram || !fAttachShader || !fLinkProgram ||
        !fUseProgram || !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform1f ||
        !fUniform2f || !fUniform3f || !fUniform4f || !fActiveTexture) {
        fail("missing GL entry points");
        return 0;
    }
    vs = compile(GL_VERTEX_SHADER, k_vs, "caster vertex");
    fs = vs ? compile(GL_FRAGMENT_SHADER, k_fs, "caster fragment") : 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        fail("the caster shader did not compile");
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
        u9log_line("GLSHADOW program did not link: %s", log);
        fail("the caster program did not link");
        return 0;
    }
    uProj     = fGetUniformLocation(g_prog, "u_proj");
    uM0       = fGetUniformLocation(g_prog, "u_m0");
    uM1       = fGetUniformLocation(g_prog, "u_m1");
    uM2       = fGetUniformLocation(g_prog, "u_m2");
    uLight    = fGetUniformLocation(g_prog, "u_light");
    uFs       = fGetUniformLocation(g_prog, "u_fs");
    uFt       = fGetUniformLocation(g_prog, "u_ft");
    uFm       = fGetUniformLocation(g_prog, "u_fm");
    uNf       = fGetUniformLocation(g_prog, "u_nf");
    uMode     = fGetUniformLocation(g_prog, "u_mode");
    uTop      = fGetUniformLocation(g_prog, "u_top");
    uTex      = fGetUniformLocation(g_prog, "u_tex");
    uCutout   = fGetUniformLocation(g_prog, "u_cutout");
    uAlphaRef = fGetUniformLocation(g_prog, "u_alphaRef");
    u9log_line("GLSHADOW caster program ready");
    return 1;
}

static int make_cube(int slot)
{
    int f, i, s = g_shadowSize;
    GLenum status;
    if (g_cubeDepth && g_cubeSize != s) {
        for (i = 0; i < U9SHADOW_MAX; i++)
            if (g_cube[i]) {
                glDeleteTextures(1, &g_cube[i]);
                u9metrics_tex_destroyed((unsigned)(g_cubeSize * g_cubeSize * 4 * 6));
                g_cube[i] = 0;
            }
        glDeleteTextures(1, &g_cubeDepth);
        u9metrics_tex_destroyed((unsigned)(g_cubeSize * g_cubeSize * 4));
        g_cubeDepth = 0;
    }
    if (g_cube[slot] && g_cubeDepth) return 1;
    if (!g_cube[slot]) {
        glGenTextures(1, &g_cube[slot]);
        glBindTexture(GL_TEXTURE_CUBE_MAP, g_cube[slot]);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        for (f = 0; f < 6; f++)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_R32F, s, s, 0, GL_RED_, GL_FLOAT, NULL);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        u9metrics_tex_created((unsigned)(s * s * 4 * 6));
        u9log_line("GLSHADOW cube %d: %d x %d x 6, R32F distances", slot, s, s);
    }
    if (g_cubeDepth) return 1;
    glGenTextures(1, &g_cubeDepth);
    glBindTexture(GL_TEXTURE_2D, g_cubeDepth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, s, s, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_cubeSize = s;
    u9metrics_tex_created((unsigned)(s * s * 4));
    if (!g_fbo) fGenFramebuffers(1, &g_fbo);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X, g_cube[slot], 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_cubeDepth, 0);
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        u9log_line("GLSHADOW cube %dx%d incomplete: status 0x%04X", s, s, (unsigned)status);
        fail("the shadow cube was incomplete");
        return 0;
    }
    return 1;
}

#define MAX_SKIP 16384
typedef struct {
    const U9FrameView *v;
    float    light[3], reach;
    unsigned tris, idx, fixtures, drawn, culled;
    int      useSkip, face;
    int      noFixture;
    int      sunMap;
    float    sc[3], spr[3], spu[3], shalf, cam[2], castReach2;
} DrawCtx;
static unsigned char g_skip[MAX_SKIP];
static unsigned      g_part[MAX_SKIP][2];
static GLuint       *g_index;
static unsigned      g_indexN, g_indexCap;
static float    g_box[MAX_SKIP][6];
static unsigned g_boxTris[MAX_SKIP];
static unsigned g_nBox;
unsigned g_shadowRunsDrawn, g_shadowRunsCulled;

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

static void box_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    DrawCtx *c = (DrawCtx *)ctx;
    int stride, k;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    float *lo, *hi, wp[3];
    unsigned i, at = c->idx++;
    (void)glTex; (void)cutout;
    if (at >= MAX_SKIP) return;
    lo = g_box[at]; hi = g_box[at] + 3;
    for (k = 0; k < 3; k++) { lo[k] = 1e30f; hi[k] = -1e30f; }
    g_boxTris[at] = count / 3;
    g_nBox = at + 1;
    for (i = first; i < first + count; i++) {
        if (!world_of(c->v, (const float *)(base + (size_t)i * (size_t)stride), wp)) continue;
        for (k = 0; k < 3; k++) {
            if (wp[k] < lo[k]) lo[k] = wp[k];
            if (wp[k] > hi[k]) hi[k] = wp[k];
        }
    }
}

static void mem_tick(const U9FrameView *v);

void u9shadow_new_frame(const U9FrameView *v)
{
    DrawCtx ctx;
    g_nBox = 0;
    if (g_shadowMemTris > g_shadowMemPeakTris) g_shadowMemPeakTris = g_shadowMemTris;
    if (g_shadowMemUs > g_shadowMemPeakUs) g_shadowMemPeakUs = g_shadowMemUs;
    g_shadowMemPrevTris = g_shadowMemTris;
    g_shadowMemPrevFix = g_shadowMemFix;
    g_shadowMemPrevUs = g_shadowMemUs;
    g_shadowMemTris = g_shadowMemFix = g_shadowMemUs = 0;
    if (!g_shadowOn || !v || !v->valid) return;
    memset(&ctx, 0, sizeof ctx);
    ctx.v = v;
    u9gl_casters(box_run, &ctx);
    g_boxFrame = u9log_frame();
    if (g_shadowMem) {
        if (!load()) return;
        mem_tick(v);
        g_shadowMemFrames++;
    }
}

void u9shadow_report_near(const float pos[3], float within, void (*line)(const char *fmt, ...))
{
    unsigned i, j, n = 0, best[12];
    float needs[12];
    int k;
    for (i = 0; i < g_nBox; i++) {
        const float *lo = g_box[i], *hi = g_box[i] + 3;
        float need = 0.0f, e;
        if (lo[0] > hi[0]) continue;
        for (k = 0; k < 3; k++) {
            e = pos[k] < lo[k] ? lo[k] - pos[k] : pos[k] > hi[k] ? pos[k] - hi[k] : 0.0f;
            if (e > need) need = e;
        }
        if (need > within || (n == 12 && need >= needs[11])) continue;
        j = n < 12 ? n++ : 11;
        while (j > 0 && needs[j - 1] > need) { needs[j] = needs[j - 1]; best[j] = best[j - 1]; j--; }
        needs[j] = need;
        best[j] = i;
    }
    for (j = 0; j < n; j++) {
        const float *lo = g_box[best[j]], *hi = g_box[best[j]] + 3;
        int col = g_shadowColumn > 0.0f &&
                  pos[0] >= lo[0] - g_shadowColumn && pos[0] <= hi[0] + g_shadowColumn &&
                  pos[1] >= lo[1] - g_shadowColumn && pos[1] <= hi[1] + g_shadowColumn;
        line("GLSHADOW near the light: run %u, %u triangles, box %.0f,%.0f,%.0f .. %.0f,%.0f,%.0f, the light %.0f outside it"
             " (%s)", best[j], g_boxTris[best[j]], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], needs[j],
             needs[j] <= g_shadowNear ? "fixture tested" : col ? "column tested" : "casts whole");
    }
}

static float edge2(const float p[3], const float q[3])
{
    float x = p[0] - q[0], y = p[1] - q[1], z = p[2] - q[2];
    return x * x + y * y + z * z;
}

static int sits_on(const float L[3], const float a[3], const float b[3], const float d[3])
{
    float u[3], w[3], n[3], len, below;
    int k;
    for (k = 0; k < 3; k++) { u[k] = b[k] - a[k]; w[k] = d[k] - a[k]; }
    n[0] = u[1] * w[2] - u[2] * w[1];
    n[1] = u[2] * w[0] - u[0] * w[2];
    n[2] = u[0] * w[1] - u[1] * w[0];
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-6f || fabsf(n[2]) < 0.9f * len) return 0;
    below = L[2] - (a[2] - (n[0] * (L[0] - a[0]) + n[1] * (L[1] - a[1])) / n[2]);
    return below >= 0.0f && below <= g_shadowNear;
}

static int fixture_tri(const float L[3], int inRun, int column, const float a[3], const float b[3], const float d[3])
{
    float r2 = g_shadowFixture * 0.5f * g_shadowFixture * 0.5f, rc2 = g_shadowColumn * g_shadowColumn;
    float rp2 = g_shadowPart * g_shadowPart, ra, rb, rd, cx, cy;
    ra = (a[0] - L[0]) * (a[0] - L[0]) + (a[1] - L[1]) * (a[1] - L[1]);
    rb = (b[0] - L[0]) * (b[0] - L[0]) + (b[1] - L[1]) * (b[1] - L[1]);
    rd = (d[0] - L[0]) * (d[0] - L[0]) + (d[1] - L[1]) * (d[1] - L[1]);
    cx = (a[0] + b[0] + d[0]) / 3.0f - L[0];
    cy = (a[1] + b[1] + d[1]) / 3.0f - L[1];
    return (inRun && cx * cx + cy * cy < r2 && ra < r2 && rb < r2 && rd < r2 &&
            edge2(a, b) < rp2 && edge2(b, d) < rp2 && edge2(d, a) < rp2) ||
           (column && ra < rc2 && rb < rc2 && rd < rc2) ||
           (inRun && sits_on(L, a, b, d));
}

static int mem_fixture_tri(const float L[3], const float a[3], const float b[3], const float d[3])
{
    float rf = g_shadowFixture * 0.5f, rf2 = rf * rf, rc2 = g_shadowColumn * g_shadowColumn;
    float rp2 = g_shadowPart * g_shadowPart;
    float ra = (a[0] - L[0]) * (a[0] - L[0]) + (a[1] - L[1]) * (a[1] - L[1]);
    float rb = (b[0] - L[0]) * (b[0] - L[0]) + (b[1] - L[1]) * (b[1] - L[1]);
    float rd = (d[0] - L[0]) * (d[0] - L[0]) + (d[1] - L[1]) * (d[1] - L[1]);
    float zlo = a[2] < b[2] ? a[2] : b[2], zhi = a[2] > b[2] ? a[2] : b[2];
    if (d[2] < zlo) zlo = d[2];
    if (d[2] > zhi) zhi = d[2];
    return (ra < rf2 && rb < rf2 && rd < rf2 && zlo >= L[2] - rf && zhi <= L[2] + rf &&
            edge2(a, b) < rp2 && edge2(b, d) < rp2 && edge2(d, a) < rp2) ||
           (rc2 > 0.0f && ra < rc2 && rb < rc2 && rd < rc2) ||
           (ra < rf2 && rb < rf2 && rd < rf2 && sits_on(L, a, b, d));
}

static void classify_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    DrawCtx *c = (DrawCtx *)ctx;
    const float *lo, *hi;
    int k, stride;
    const char *base;
    int inRun, column;
    unsigned at = c->idx++, t, kept = 0, off;
    (void)glTex; (void)cutout;
    if (at >= MAX_SKIP) return;
    g_skip[at] = 0;
    if (at >= g_nBox || c->noFixture) return;
    lo = g_box[at]; hi = g_box[at] + 3;
    inRun = 1;
    for (k = 0; k < 3; k++)
        if (c->light[k] < lo[k] - g_shadowNear || c->light[k] > hi[k] + g_shadowNear) inRun = 0;
    column = g_shadowColumn > 0.0f;
    for (k = 0; k < 2; k++)
        if (c->light[k] < lo[k] - g_shadowColumn || c->light[k] > hi[k] + g_shadowColumn) column = 0;
    if (!inRun && !column) return;
    if (g_indexN + count > g_indexCap) {
        unsigned cap = (g_indexN + count) * 2 + 4096;
        GLuint *p = (GLuint *)realloc(g_index, cap * sizeof *p);
        if (!p) return;
        g_index = p;
        g_indexCap = cap;
    }
    base = (const char *)u9gl_caster_vertices(&stride);
    off = g_indexN;
    for (t = first; t + 2 < first + count; t += 3) {
        float a[3], b[3], d[3];
        if (!world_of(c->v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), d)) continue;
        if (fixture_tri(c->light, inRun, column, a, b, d)) {
            c->fixtures++;
            continue;
        }
        g_index[g_indexN++] = t;
        g_index[g_indexN++] = t + 1;
        g_index[g_indexN++] = t + 2;
        kept++;
    }
    if (kept * 3 == count) { g_indexN = off; return; }
    g_skip[at] = 1;
    g_part[at][0] = off;
    g_part[at][1] = kept * 3;
}

static int sun_box_wanted(const DrawCtx *c, const float *lo, const float *hi)
{
    int k, out[4] = { 1, 1, 1, 1 };
    float dx, dy;
    if (c->castReach2 > 0.0f) {
        dx = c->cam[0] < lo[0] ? lo[0] - c->cam[0] : c->cam[0] > hi[0] ? c->cam[0] - hi[0] : 0.0f;
        dy = c->cam[1] < lo[1] ? lo[1] - c->cam[1] : c->cam[1] > hi[1] ? c->cam[1] - hi[1] : 0.0f;
        if (dx * dx + dy * dy > c->castReach2) return 0;
    }
    for (k = 0; k < 8; k++) {
        float p[3], s, t;
        p[0] = (k & 1 ? hi[0] : lo[0]) - c->sc[0];
        p[1] = (k & 2 ? hi[1] : lo[1]) - c->sc[1];
        p[2] = (k & 4 ? hi[2] : lo[2]) - c->sc[2];
        s = p[0] * c->spr[0] + p[1] * c->spr[1] + p[2] * c->spr[2];
        t = p[0] * c->spu[0] + p[1] * c->spu[1] + p[2] * c->spu[2];
        if (s >= -c->shalf) out[0] = 0;
        if (s <= c->shalf)  out[1] = 0;
        if (t >= -c->shalf) out[2] = 0;
        if (t <= c->shalf)  out[3] = 0;
    }
    return !(out[0] || out[1] || out[2] || out[3]);
}

static void draw_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    DrawCtx *c = (DrawCtx *)ctx;
    int cut = cutout && g_shadowCutouts && glTex, part = 0;
    unsigned at = c->idx++;
    if (c->sunMap && at < g_nBox && !sun_box_wanted(c, g_box[at], g_box[at] + 3)) { c->culled++; return; }
    if (c->useSkip && at < MAX_SKIP && g_skip[at]) {
        if (!g_part[at][1]) return;
        part = 1;
    }
    if (c->useSkip && at < g_nBox) {
        const float *lo = g_box[at], *hi = g_box[at] + 3;
        float d2 = 0.0f, e, ahead;
        int k, ax = c->face / 2;
        for (k = 0; k < 3; k++) {
            e = lo[k] > c->light[k] ? lo[k] - c->light[k] : c->light[k] > hi[k] ? c->light[k] - hi[k] : 0.0f;
            d2 += e * e;
        }
        ahead = (c->face & 1) ? c->light[ax] - lo[ax] : hi[ax] - c->light[ax];
        if (d2 > c->reach * c->reach || ahead < 4.0f) { c->culled++; return; }
    }
    c->drawn++;
    fUniform1i(uCutout, cut);
    if (cut) glBindTexture(GL_TEXTURE_2D, glTex);
    if (part) {
        glDrawElements(GL_TRIANGLES, (GLsizei)g_part[at][1], GL_UNSIGNED_INT, g_index + g_part[at][0]);
        c->tris += g_part[at][1] / 3;
    } else {
        glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
        c->tris += count / 3;
    }
}

static void frame_pointers(void)
{
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    glVertexPointer(3, GL_FLOAT, stride, base);
    glTexCoordPointer(4, GL_FLOAT, stride, base + 12);
}

static void begin_casters(const U9FrameView *v)
{
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    frame_pointers();
    fUseProgram(g_prog);
    fUniform4f(uProj, v->sx, v->sy, v->cx, v->cy);
    fUniform4f(uM0, v->m[0], v->m[1], v->m[2], v->m[3]);
    fUniform4f(uM1, v->m[4], v->m[5], v->m[6], v->m[7]);
    fUniform4f(uM2, v->m[8], v->m[9], v->m[10], v->m[11]);
    fUniform1i(uTex, 0);
    fUniform1f(uAlphaRef, (float)u9cfg_int("alpha_ref", 128) / 255.0f);
    fActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void end_casters(void)
{
    fUseProgram(0);
    glPopClientAttrib();
}

static unsigned lamp_select(const float L[3], float reach, int fixture, int slot);
static void     lamp_mem_draw(int face);

GLuint u9shadow_build(const U9FrameView *v, const float pos[3], float reach, int slot, int fixture)
{
    unsigned memTris = 0;
    static const float S[6][3] = { {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0} };
    static const float T[6][3] = { {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0} };
    static const float M[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    DrawCtx ctx;
    int f;
    uint64_t tin[U9SC_IN_N], tq = 0, tn;
#define SC_MARK(part) do { if (g_scostOn) { tn = u9perf_now(); tin[part] += tn - tq; tq = tn; } } while (0)
    if (!g_shadowOn || g_failed || !v || !v->valid || slot < 0 || slot >= U9SHADOW_MAX) return 0;
    if (g_scostOn) { memset(tin, 0, sizeof tin); tq = u9perf_now(); }
    if (!load()) return 0;
    if (g_shadowSize < 64) g_shadowSize = 64;
    if (g_shadowSize > 2048) g_shadowSize = 2048;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (!make_cube(slot)) { glPopAttrib(); return 0; }
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, g_cubeSize, g_cubeSize);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClearDepth(1.0);
    begin_casters(v);
    fUniform1i(uMode, 0);
    fUniform3f(uLight, pos[0], pos[1], pos[2]);
    fUniform2f(uNf, 4.0f, reach > 16.0f ? reach : 16.0f);
    g_cubeReach[slot] = reach > 16.0f ? reach : 16.0f;
    memset(&ctx, 0, sizeof ctx);
    ctx.v = v;
    ctx.light[0] = pos[0]; ctx.light[1] = pos[1]; ctx.light[2] = pos[2];
    ctx.reach = reach;
    ctx.noFixture = !fixture;
    g_indexN = 0;
    SC_MARK(U9SC_IN_SETUP);
    u9gl_casters(classify_run, &ctx);
    g_shadowFixtures = ctx.fixtures;
    ctx.useSkip = 1;
    SC_MARK(U9SC_IN_FIXTURE);
    if (g_shadowMem) memTris = lamp_select(pos, reach, fixture, slot);
    SC_MARK(U9SC_IN_SELECT);
    for (f = 0; f < 6; f++) {
        ctx.idx = 0;
        ctx.face = f;
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, g_cube[slot], 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        fUniform3f(uFs, S[f][0], S[f][1], S[f][2]);
        fUniform3f(uFt, T[f][0], T[f][1], T[f][2]);
        fUniform3f(uFm, M[f][0], M[f][1], M[f][2]);
        SC_MARK(U9SC_IN_SWITCH);
        u9gl_casters(draw_run, &ctx);
        SC_MARK(U9SC_IN_RUNS);
        if (memTris) lamp_mem_draw(f);
        SC_MARK(U9SC_IN_MEM);
    }
    end_casters();
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPopAttrib();
    SC_MARK(U9SC_IN_FINISH);
    if (g_scostOn) u9scost_inside(tin, ctx.idx, ctx.drawn, ctx.culled);
#undef SC_MARK
    g_shadowBuilds++;
    g_shadowTris = ctx.tris;
    g_shadowRunsDrawn = ctx.drawn;
    g_shadowRunsCulled = ctx.culled;
    return g_cube[slot];
}

static int make_sun(int i)
{
    int s = g_sunSize;
    GLenum status;
    if (g_sunTex[i] && g_sunTexSize[i] == s) return 1;
    if (g_sunTex[i]) {
        glDeleteTextures(1, &g_sunTex[i]);
        glDeleteTextures(1, &g_sunDepth[i]);
        u9metrics_tex_destroyed((unsigned)(g_sunTexSize[i] * g_sunTexSize[i] * 8));
        g_sunTex[i] = g_sunDepth[i] = 0;
    }
    glGenTextures(1, &g_sunTex[i]);
    glBindTexture(GL_TEXTURE_2D, g_sunTex[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, s, s, 0, GL_RED_, GL_FLOAT, NULL);
    glGenTextures(1, &g_sunDepth[i]);
    glBindTexture(GL_TEXTURE_2D, g_sunDepth[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, s, s, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    g_sunTexSize[i] = s;
    u9metrics_tex_created((unsigned)(s * s * 8));
    if (!g_sunFbo[i]) fGenFramebuffers(1, &g_sunFbo[i]);
    fBindFramebuffer(GL_FRAMEBUFFER, g_sunFbo[i]);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_sunTex[i], 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_sunDepth[i], 0);
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        u9log_line("GLSHADOW sun map %d, %dx%d incomplete: status 0x%04X", i, s, s, (unsigned)status);
        fail("the sun map was incomplete");
        return 0;
    }
    u9log_line("GLSHADOW sun map %d: %d x %d, R32F", i, s, s);
    return 1;
}

static void cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void unit3(float a[3])
{
    float l = (float)sqrt((double)(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]));
    if (l > 1e-6f) { a[0] /= l; a[1] /= l; a[2] /= l; }
}

#define SUN_MEM_MAX  65536
#define SUN_MEM_HASH (1 << 17)
typedef struct {
    float    p[3][3], st[3][2], area;
    unsigned keyA, keyB, frame, partial;
    DWORD    seen;
    GLuint   tex;
    unsigned char cutout, live;
    unsigned char anim;
    float    avD, camD;
} SunTri;
static SunTri *g_mem;
static int    *g_memHash;
static int     g_memN, g_memDead;
static unsigned g_memFrame;
static unsigned g_memTick;
#define SUN_MEM_EVERY 4
static float   g_memCam[3];
static int     g_memHaveCam;
static SunTri **g_memList;
static float   *g_memVtx, *g_memTc;
static int      g_memListN, g_memCap;
static unsigned g_memListVer = 1;
static unsigned g_memContent = 1;
static unsigned char *g_memFresh;
static unsigned char *g_memSkip;
static float   *g_memVtxB, *g_memTcB;
static int      g_memListB, g_memRunStartB[SUN_MEM_MAX + 1];
static int      g_memBetween;
static int      g_memPassNow;

static void mem_clear(void)
{
    g_memN = g_memDead = g_memListN = 0;
    g_memListVer++;
    g_memContent++;
    if (g_memHash) memset(g_memHash, 0, SUN_MEM_HASH * sizeof *g_memHash);
}

static unsigned mem_slot(unsigned a)
{
    return (a * 2654435761u) & (SUN_MEM_HASH - 1);
}

static void mem_rehash(void)
{
    int i, n = 0;
    for (i = 0; i < g_memN; i++) if (g_mem[i].live) g_mem[n++] = g_mem[i];
    g_memN = n;
    g_memDead = 0;
    memset(g_memHash, 0, SUN_MEM_HASH * sizeof *g_memHash);
    for (i = 0; i < g_memN; i++) {
        unsigned s = mem_slot(g_mem[i].keyA);
        while (g_memHash[s]) s = (s + 1) & (SUN_MEM_HASH - 1);
        g_memHash[s] = i + 1;
    }
}

typedef struct {
    const U9FrameView *v;
    float camX, camY, camZ, reach2;
    DWORD now;
    float av[3];
    int   haveAv;
} MemCtx;

int u9avatar_position(int *x, int *y, int *z);
unsigned g_memRefused;

#define MEM_DIAG_AV 80.0f
static unsigned g_memDiagAvKept, g_memDiagAvOldest;
static unsigned g_memDiagAnimKept, g_memDiagAnimOldest;
#define MEM_GHOST_MS 3000u
int  u9gl_caster_anim(void);
void u9gl_anim_draw_stats(unsigned *objects, unsigned *lastObjects);
int  u9gl_anim_draw_installed(void);

#define SUN_MEM_SHADOW_REACH 2000.0f
float g_sunMemMargin = 2000.0f;
static float mem_reach(float range)
{
    float r = range * 1.5f;
    if (g_sunFadeTo > g_sunFadeFrom && g_sunFadeTo + g_sunMemMargin < r) r = g_sunFadeTo + g_sunMemMargin;
    return r;
}

static void mem_collect(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    MemCtx *c = (MemCtx *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    float edgeX = c->v->engineW - 1.5f, edgeY = c->v->engineH - 1.5f;
    unsigned t;
    if (!g_shadowMemAnimated && u9gl_caster_anim()) return;
    for (t = first; t + 2 < first + count; t += 3) {
        float w[3][3], st[3][2], e1[3], e2[3], n[3], area, scale;
        unsigned a = 2166136261u, b = 16777619u, s;
        int k, q, clipped = 0;
        const float *f[3];
        for (k = 0; k < 3; k++) {
            f[k] = (const float *)(base + (size_t)(t + (unsigned)k) * (size_t)stride);
            if (!world_of(c->v, f[k], w[k]) || f[k][6] == 0.0f) break;
            st[k][0] = f[k][3] / f[k][6];
            st[k][1] = f[k][4] / f[k][6];
            if (f[k][0] < 1.5f || f[k][0] > edgeX || f[k][1] < 1.5f || f[k][1] > edgeY) clipped = 1;
        }
        if (k < 3) continue;
        if ((w[0][2] + w[1][2] + w[2][2]) / 3.0f <= c->camZ + g_sunAbove) continue;
        {
            float dx = (w[0][0] + w[1][0] + w[2][0]) / 3.0f - c->camX, dy = (w[0][1] + w[1][1] + w[2][1]) / 3.0f - c->camY;
            if (dx * dx + dy * dy > c->reach2) continue;
        }
        for (k = 0; k < 3; k++) { e1[k] = w[1][k] - w[0][k]; e2[k] = w[2][k] - w[0][k]; }
        cross3(e1, e2, n);
        area = 0.5f * (float)sqrt((double)(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
        if (clipped && area < 64.0f) { g_sunMemSkipped++; continue; }
        scale = clipped ? 1.0f / 32.0f : 0.25f;
        for (k = 0; k < 9; k++) {
            {
                float f = w[k / 3][k % 3] * scale;
                q = (int)f;
                q -= f < (float)q;
            }
            a = (a ^ (unsigned)q) * 16777619u;
            b = (b + (unsigned)q) * 2246822519u;
        }
        b ^= glTex ^ (clipped ? 0x9E3779B9u : 0u);
        for (s = mem_slot(a); g_memHash[s]; s = (s + 1) & (SUN_MEM_HASH - 1)) {
            SunTri *e = &g_mem[g_memHash[s] - 1];
            if (e->keyA == a && e->keyB == b) break;
        }
        if (!g_memHash[s]) {
            if (g_memN >= SUN_MEM_MAX) { g_memRefused++; continue; }
            g_memHash[s] = ++g_memN;
            g_mem[g_memN - 1].keyA = a;
            g_mem[g_memN - 1].keyB = b;
            g_mem[g_memN - 1].live = 0;
            g_sunMemNew++;
        }
        {
            SunTri *e = &g_mem[g_memHash[s] - 1];
            {
                float mx = (w[0][0] + w[1][0] + w[2][0]) / 3.0f, my = (w[0][1] + w[1][1] + w[2][1]) / 3.0f;
                float mz = (w[0][2] + w[1][2] + w[2][2]) / 3.0f, ax = mx - c->av[0], ay = my - c->av[1];
                float az = mz - c->av[2], cx = mx - c->camX, cy = my - c->camY, cz = mz - c->camZ;
                e->avD = c->haveAv ? sqrtf(ax * ax + ay * ay + az * az) : -1.0f;
                e->camD = sqrtf(cx * cx + cy * cy + cz * cz);
            }
            e->anim = (unsigned char)u9gl_caster_anim();
            if (e->live && clipped && area < e->area * 0.9f) {
                e->seen = c->now;
                e->partial = g_memFrame;
                continue;
            }
            e->partial = 0;
            e->live = 1;
            memcpy(e->p, w, sizeof w);
            memcpy(e->st, st, sizeof st);
            e->area = area;
            e->tex = glTex;
            e->cutout = (unsigned char)(cutout != 0);
            e->frame = g_memFrame;
            e->seen = c->now;
        }
    }
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

static int mem_cmp(const void *x, const void *y)
{
    const SunTri *a = *(const SunTri *const *)x, *b = *(const SunTri *const *)y;
    return a->tex < b->tex ? -1 : a->tex > b->tex;
}

static void mem_runs(void);
static void mem_content(void);

#define MEM_GROUP_TEX  1024
#define MEM_GROUP_HASH 2048
static int mem_group(int n)
{
    static GLuint key[MEM_GROUP_HASH];
    static int    slot[MEM_GROUP_HASH], start[MEM_GROUP_TEX + 1];
    static SunTri **tmp;
    static int    tmpCap;
    static unsigned char *bucket;
    int i, k = 0;
    if (tmpCap < n) {
        SunTri **t2 = (SunTri **)realloc(tmp, (size_t)n * sizeof *t2);
        unsigned char *b2;
        if (!t2) return 0;
        tmp = t2;
        b2 = (unsigned char *)realloc(bucket, (size_t)n * 2);
        if (!b2) return 0;
        bucket = b2;
        tmpCap = n;
    }
    memset(slot, 0, sizeof slot);
    memset(start, 0, sizeof start);
    for (i = 0; i < n; i++) {
        GLuint t = g_memList[i]->tex;
        unsigned s = (t * 2654435761u) & (MEM_GROUP_HASH - 1);
        while (slot[s] && key[s] != t) s = (s + 1) & (MEM_GROUP_HASH - 1);
        if (!slot[s]) {
            if (k == MEM_GROUP_TEX) return 0;
            key[s] = t;
            slot[s] = ++k;
        }
        bucket[i * 2] = (unsigned char)((slot[s] - 1) & 0xFF);
        bucket[i * 2 + 1] = (unsigned char)((slot[s] - 1) >> 8);
        start[slot[s]]++;
    }
    for (i = 1; i <= k; i++) start[i] += start[i - 1];
    for (i = 0; i < n; i++) {
        int b = bucket[i * 2] | (bucket[i * 2 + 1] << 8);
        tmp[start[b]++] = g_memList[i];
    }
    memcpy(g_memList, tmp, (size_t)n * sizeof *g_memList);
    return 1;
}

int             g_sunEvict = 1;
unsigned        g_memEvicted;
#define EVICT_ROOM  8192
#define EVICT_KEEP  (SUN_MEM_MAX - EVICT_ROOM)
static unsigned char g_evBin[SUN_MEM_MAX];

static int mem_evict(const U9FrameView *v, int n, float r2)
{
    unsigned hist[256], need, acc = 0;
    int i, k, t, out = 0, takeT;
    if (!g_sunEvict || n <= EVICT_KEEP || r2 <= 0.0f) return n;
    need = (unsigned)(n - EVICT_KEEP);
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++) {
        const SunTri *e = g_memList[i];
        float mx, my, d2;
        int b;
        if (e->frame == g_memFrame) { g_evBin[i] = 0; continue; }
        mx = (e->p[0][0] + e->p[1][0] + e->p[2][0]) / 3.0f - v->m[3];
        my = (e->p[0][1] + e->p[1][1] + e->p[2][1]) / 3.0f - v->m[7];
        d2 = mx * mx + my * my;
        b = (int)(d2 / r2 * 255.0f) + 1;
        if (b > 255) b = 255;
        if (b < 1) b = 1;
        g_evBin[i] = (unsigned char)b;
        hist[b]++;
    }
    for (t = 255; t >= 1; t--) {
        if (acc + hist[t] >= need) break;
        acc += hist[t];
    }
    if (t < 1) t = 1;
    takeT = (int)(need - acc);
    for (i = 0; i < n; i++) {
        SunTri *e = g_memList[i];
        k = g_evBin[i];
        if (k > t || (k == t && k >= 1 && takeT > 0)) {
            if (k == t) takeT--;
            e->live = 0;
            g_memDead++;
            g_sunMemDropped++;
            g_memEvicted++;
            if (g_sunMemLive) g_sunMemLive--;
            continue;
        }
        g_memList[out++] = e;
    }
    return out;
}

static void mem_update(const U9FrameView *v, float range)
{
    DWORD now = u9time_now();
    float reach = mem_reach(range), r2 = reach * reach;
    int i, n = 0, j, full = g_memN > SUN_MEM_MAX - 4096;
    float camAt[3];
    int inRoom;
    camAt[0] = v->m[3]; camAt[1] = v->m[7]; camAt[2] = v->m[11];
    inRoom = u9spaces_at(camAt, NULL, 0, NULL) >= 0;
    g_memListN = 0;
    g_memListVer++;
    if (g_memCap < g_memN) {
        int nc = g_memN + 4096;
        SunTri **l2 = (SunTri **)realloc(g_memList, (size_t)nc * sizeof *l2);
        float *v2 = (float *)realloc(g_memVtx, (size_t)nc * 9 * sizeof *v2);
        float *t2 = (float *)realloc(g_memTc, (size_t)nc * 6 * sizeof *t2);
        unsigned char *f2 = (unsigned char *)realloc(g_memFresh, (size_t)nc);
        unsigned char *k2 = (unsigned char *)realloc(g_memSkip, (size_t)nc);
        float *vb = (float *)realloc(g_memVtxB, (size_t)nc * 9 * sizeof *vb);
        float *tb = (float *)realloc(g_memTcB, (size_t)nc * 6 * sizeof *tb);
        if (l2) g_memList = l2;
        if (v2) g_memVtx = v2;
        if (t2) g_memTc = t2;
        if (f2) g_memFresh = f2;
        if (k2) g_memSkip = k2;
        if (vb) g_memVtxB = vb;
        if (tb) g_memTcB = tb;
        if (!l2 || !v2 || !t2 || !f2 || !k2 || !vb || !tb) return;
        g_memCap = nc;
    }
    g_sunMemLive = 0;
    g_memDiagAvKept = g_memDiagAvOldest = 0;
    g_memDiagAnimKept = g_memDiagAnimOldest = 0;
    for (i = 0; i < g_memN; i++) {
        SunTri *e = &g_mem[i];
        float mid[3], dx, dy;
        if (!e->live) continue;
        if (e->frame == g_memFrame) {
            g_sunMemLive++;
            g_memList[n++] = e;
            continue;
        }
        for (j = 0; j < 3; j++) mid[j] = (e->p[0][j] + e->p[1][j] + e->p[2][j]) / 3.0f;
        dx = mid[0] - v->m[3]; dy = mid[1] - v->m[7];
        if (dx * dx + dy * dy > r2 || (!inRoom && e->partial != g_memFrame && on_screen(v, mid)) ||
            (full && now - e->seen > (DWORD)g_sunKeepS * 1000u)) {
            e->live = 0;
            g_memDead++;
            g_sunMemDropped++;
            continue;
        }
        g_sunMemLive++;
        if (e->anim) {
            g_memDiagAnimKept++;
            if ((unsigned)(now - e->seen) > g_memDiagAnimOldest) g_memDiagAnimOldest = (unsigned)(now - e->seen);
        }
        if (e->avD >= 0.0f && e->avD < MEM_DIAG_AV) {
            g_memDiagAvKept++;
            if ((unsigned)(now - e->seen) > g_memDiagAvOldest) g_memDiagAvOldest = (unsigned)(now - e->seen);
        }
        g_memList[n++] = e;
    }
    n = mem_evict(v, n, r2);
    g_sunMemDrawn = (unsigned)n;
    if (n) {
        if (!mem_group(n)) qsort(g_memList, (size_t)n, sizeof *g_memList, mem_cmp);
        for (i = 0; i < n; i++) {
            memcpy(g_memVtx + i * 9, g_memList[i]->p, 9 * sizeof(float));
            memcpy(g_memTc + i * 6, g_memList[i]->st, 6 * sizeof(float));
            g_memFresh[i] = g_memList[i]->frame == g_memFrame;
        }
    }
    g_memListN = n;
    mem_runs();
    mem_content();
    if ((g_memDead > g_memN / 2 && g_memDead > 1024) || (g_memDead && g_memN > SUN_MEM_MAX - 1024))
        mem_rehash();
}

static GLuint g_memRunTex[SUN_MEM_MAX];
static int    g_memRunStart[SUN_MEM_MAX + 1], g_memRunCut[SUN_MEM_MAX], g_memRuns;

int             g_lmKeep = 1;
static unsigned g_memFpA, g_memFpB;
static int      g_memFpN = -1, g_memFpRuns = -1;
unsigned        g_keepKept, g_keepMade, g_keepChecks, g_keepMismatch;

static void mem_content(void)
{
    const unsigned *w = (const unsigned *)g_memVtx;
    unsigned a = 2166136261u, b = 0x9E3779B9u;
    int i, n = g_memListN * 9;
    for (i = 0; i < n; i++) {
        a = (a ^ w[i]) * 16777619u;
        b = (b + w[i]) * 0x85EBCA6Bu;
        b ^= b >> 13;
    }
    for (i = 0; i <= g_memRuns; i++) {
        a = (a ^ (unsigned)g_memRunStart[i]) * 16777619u;
        b = (b + (unsigned)g_memRunStart[i]) * 0xC2B2AE35u;
        b ^= b >> 16;
    }
    if (!g_lmKeep || a != g_memFpA || b != g_memFpB || g_memListN != g_memFpN || g_memRuns != g_memFpRuns)
        g_memContent++;
    g_memFpA = a; g_memFpB = b; g_memFpN = g_memListN; g_memFpRuns = g_memRuns;
}

static void mem_runs(void)
{
    int i, j;
    g_memRuns = 0;
    for (i = 0; i < g_memListN; i = j) {
        for (j = i + 1; j < g_memListN && g_memList[j]->tex == g_memList[i]->tex &&
                        g_memList[j]->cutout == g_memList[i]->cutout; j++) {}
        g_memRunTex[g_memRuns] = g_memList[i]->tex;
        g_memRunCut[g_memRuns] = g_memList[i]->cutout && g_shadowCutouts && g_memList[i]->tex;
        g_memRunStart[g_memRuns] = i;
        g_memRuns++;
    }
    g_memRunStart[g_memRuns] = g_memListN;
    g_memListVer++;
}

static uint32_t g_memBFrame = 0xFFFFFFFFu;

static void mem_between(const U9FrameView *v, int pass)
{
    int r, i, n = 0, ok = g_memListN && g_memFresh && g_memVtxB && g_memTcB;
    int wantB = !pass && ok, wantSkip = ok && g_shadowMem && g_memSkip;
    g_memBetween = wantB;
    if (!wantB && !wantSkip) return;
    for (r = 0; r < g_memRuns; r++) {
        if (wantB) g_memRunStartB[r] = n;
        for (i = g_memRunStart[r]; i < g_memRunStart[r + 1]; i++) {
            const float *p = g_memVtx + i * 9;
            float mid[3];
            int k, skip = 0;
            if (g_memFresh[i]) {
                for (k = 0; k < 3; k++) mid[k] = (p[k] + p[3 + k] + p[6 + k]) / 3.0f;
                skip = on_screen(v, mid);
            }
            if (wantSkip) g_memSkip[i] = (unsigned char)skip;
            if (skip || !wantB) continue;
            memcpy(g_memVtxB + n * 9, p, 9 * sizeof(float));
            memcpy(g_memTcB + n * 6, g_memTc + i * 6, 6 * sizeof(float));
            n++;
        }
    }
    if (wantB) {
        g_memRunStartB[g_memRuns] = n;
        g_memListB = n;
    }
    g_memBFrame = u9log_frame();
}

static void mem_render(void)
{
    int r;
    const int *start = g_memBetween ? g_memRunStartB : g_memRunStart;
    if (!(g_memBetween ? g_memListB : g_memListN)) return;
    fUniform1i(uMode, 3);
    glVertexPointer(3, GL_FLOAT, 0, g_memBetween ? g_memVtxB : g_memVtx);
    glTexCoordPointer(2, GL_FLOAT, 0, g_memBetween ? g_memTcB : g_memTc);
    for (r = 0; r < g_memRuns; r++) {
        if (start[r + 1] == start[r]) continue;
        fUniform1i(uCutout, g_memRunCut[r]);
        if (g_memRunCut[r]) glBindTexture(GL_TEXTURE_2D, g_memRunTex[r]);
        glDrawArrays(GL_TRIANGLES, start[r] * 3, (start[r + 1] - start[r]) * 3);
    }
}

static uint32_t g_memTickFrame = 0xFFFFFFFFu;

static void mem_tick(const U9FrameView *v)
{
    float cam[3], reach = g_sunRange < 500.0f ? 500.0f : g_sunRange;
    float sunNear = g_sunNear < 250.0f ? 250.0f : g_sunNear;
    int k, maps = g_sunCascades && sunNear < reach * 0.75f ? 2 : 1;
    int want = g_sunMemory || g_shadowMem;
    if (g_memTickFrame == u9log_frame()) return;
    g_memTickFrame = u9log_frame();
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
    if (!want) { g_memListN = g_memRuns = 0; g_memListVer++; g_memContent++; }
    g_memPassNow = 0;
    if (want) {
        if (!g_mem) {
            g_mem = (SunTri *)malloc(SUN_MEM_MAX * sizeof *g_mem);
            g_memHash = (int *)calloc(SUN_MEM_HASH, sizeof *g_memHash);
            if (!g_mem || !g_memHash) {
                free(g_mem); free(g_memHash); g_mem = NULL; g_memHash = NULL;
                g_sunMemory = 0;
                g_shadowMem = 0;
                u9log_line("GLSHADOW memory: no room for it - sun and lamp memory off");
            }
        }
        if (g_mem) {
            MemCtx mc;
            float dx = cam[0] - g_memCam[0], dy = cam[1] - g_memCam[1], dz = cam[2] - g_memCam[2];
            float jump = maps == 2 ? sunNear * 2.0f : reach;
            int fresh = 0;
            if (g_memHaveCam && dx * dx + dy * dy + dz * dz > jump * jump) { mem_clear(); fresh = 1; }
            for (k = 0; k < 3; k++) g_memCam[k] = cam[k];
            g_memHaveCam = 1;
            if (!fresh && g_memListN && (++g_memTick % SUN_MEM_EVERY) != 0) goto memory_done;
            g_memFrame++;
            g_memPassNow = 1;
            mc.v = v;
            mc.camX = cam[0];
            mc.camY = cam[1];
            mc.camZ = cam[2];
            mc.reach2 = mem_reach(reach) * mem_reach(reach);
            mc.now = u9time_now();
            {
                int ax, ay, az;
                mc.haveAv = u9avatar_position(&ax, &ay, &az);
                mc.av[0] = (float)ax; mc.av[1] = (float)ay; mc.av[2] = (float)az;
            }
            u9gl_casters(mem_collect, &mc);
            mem_update(v, reach);
            if (mc.now - g_memLogAt >= 2000u) {
                float fz = v->m[9];
                unsigned adNow, adLast;
                u9gl_anim_draw_stats(&adNow, &adLast);
                if (fz > 1.0f) fz = 1.0f;
                if (fz < -1.0f) fz = -1.0f;
                u9log_line("SUNMEM held %u, drawn %u this frame, %u new and %u forgotten in %u ms, %u slivers skipped;"
                           " camera z %.0f, pitch %.0f deg%s; lamps: %u triangles from it last frame, %u left as fixtures,"
                           " %u us choosing (peaks %u, %u us); choices redone %u - new %u, pass %u, moved %u, reach %u, setting %u;"
                           " kept though not drawn this pass and filed within 80 of the Avatar: %u (oldest %u ms);"
                           " from animated objects: %u (oldest %u ms); animated objects drawn last frame: %u%s",
                           g_sunMemLive, g_sunMemDrawn, g_sunMemNew - g_memLogNew, g_sunMemDropped - g_memLogDropped,
                           (unsigned)(mc.now - g_memLogAt), g_sunMemSkipped, (double)cam[2],
                           (double)(asinf(fz) * 57.29578f), g_sunMemory == 2 ? ", memory only" : "",
                           g_shadowMemPrevTris, g_shadowMemPrevFix, g_shadowMemPrevUs, g_shadowMemPeakTris, g_shadowMemPeakUs,
                           g_shadowMemChosen, g_lsWhy[0], g_lsWhy[1], g_lsWhy[2], g_lsWhy[3], g_lsWhy[4],
                           g_memDiagAvKept, g_memDiagAvOldest, g_memDiagAnimKept, g_memDiagAnimOldest, adLast,
                           u9gl_anim_draw_installed() ? "" : " (marking not installed)");
                if (g_memDiagAnimKept && g_memDiagAnimOldest > MEM_GHOST_MS)
                    u9log_line("SHADOWGHOST %u remembered triangles from animated objects still cast though not drawn;"
                               " the oldest was last drawn %u ms ago (camera %.0f,%.0f,%.0f)", g_memDiagAnimKept,
                               g_memDiagAnimOldest, (double)cam[0], (double)cam[1], (double)cam[2]);
                g_shadowMemChosen = 0;
                memset(g_lsWhy, 0, sizeof g_lsWhy);
                g_memLogAt = mc.now;
                g_memLogNew = g_sunMemNew;
                g_memLogDropped = g_sunMemDropped;
                g_sunMemSkipped = 0;
                g_shadowMemPeakTris = g_shadowMemPeakUs = 0;
            }
        }
    }
memory_done:
    if (want) mem_between(v, g_memPassNow);
}

typedef struct {
    int      valid, fixture;
    float    L[3], reach;
    unsigned pass, key, content;
    int     *tri, *run, n, cap;
    int      faceAt[7];
} LampSel;
static LampSel  g_lsel[U9SHADOW_MAX];
static unsigned g_lselFrame[U9SHADOW_MAX];
static GLuint  *g_lmIdx;
static unsigned g_lmIdxCap;
static int     *g_lmSegRun, *g_lmSegAt, g_lmSegs, g_lmSegCap;
static int      g_lmFaceSeg[7];

int             g_lmFaces = 1;
#define FACE_MARGIN 8.0f
static unsigned char g_lcMask[SUN_MEM_MAX];
static int      g_lcTri[SUN_MEM_MAX], g_lcRun[SUN_MEM_MAX];

static float min_abs(float a, float b)
{
    if (a <= 0.0f && b >= 0.0f) return 0.0f;
    a = fabsf(a); b = fabsf(b);
    return a < b ? a : b;
}

static unsigned face_mask(const float L[3], const float lo[3], const float hi[3])
{
    unsigned m = 0;
    int ax;
    if (!g_lmFaces) return 0x3Fu;
    for (ax = 0; ax < 3; ax++) {
        int o1 = (ax + 1) % 3, o2 = (ax + 2) % 3;
        float a1 = min_abs(lo[o1] - L[o1], hi[o1] - L[o1]) - FACE_MARGIN;
        float a2 = min_abs(lo[o2] - L[o2], hi[o2] - L[o2]) - FACE_MARGIN;
        float pos = hi[ax] - L[ax], neg = L[ax] - lo[ax];
        if (pos >= a1 && pos >= a2) m |= 1u << (ax * 2);
        if (neg >= a1 && neg >= a2) m |= 1u << (ax * 2 + 1);
    }
    return m;
}

static unsigned sel_key(void)
{
    unsigned k = 2166136261u;
    k = (k ^ (unsigned)(int)(g_shadowFixture * 16.0f)) * 16777619u;
    k = (k ^ (unsigned)(int)(g_shadowColumn * 16.0f)) * 16777619u;
    k = (k ^ (unsigned)(int)(g_shadowPart * 16.0f)) * 16777619u;
    k = (k ^ (unsigned)(int)(g_shadowNear * 16.0f)) * 16777619u;
    k = (k ^ (unsigned)g_shadowCutouts) * 16777619u;
    k = (k ^ (unsigned)g_lmFaces) * 16777619u;
    return k;
}

int             g_lmGrid = 0;
#define GRID_N      64
#define GRID_MIN    256.0f
#define GRID_BIG    16
static unsigned g_gridVer;
static int      g_gridW, g_gridH, g_gridAlwaysN;
static float    g_gridLo[2], g_gridCell;
static int     *g_gridStart, *g_gridCur, g_gridStartCap, g_gridCurCap;
static int     *g_gridTri, g_gridTriCap;
static int     *g_gridAlways, g_gridAlwaysCap;
static unsigned *g_gridStamp, g_gridStampNow;
static int      g_gridStampCap;
static float    g_gridHi[2];
static int     *g_gridCand, g_gridCap;
unsigned        g_gridChoices, g_gridTested, g_gridListed, g_gridChecks, g_gridMismatch;

static int grow_int(int **p, int *cap, int need)
{
    int *q;
    if (*cap >= need) return 1;
    q = (int *)realloc(*p, (size_t)(need + 4096) * sizeof *q);
    if (!q) return 0;
    *p = q;
    *cap = need + 4096;
    return 1;
}

static void tri_xy(int i, float lo[2], float hi[2])
{
    const float *a = g_memVtx + i * 9, *b = a + 3, *d = a + 6;
    int k;
    for (k = 0; k < 2; k++) {
        lo[k] = a[k] < b[k] ? a[k] : b[k]; if (d[k] < lo[k]) lo[k] = d[k];
        hi[k] = a[k] > b[k] ? a[k] : b[k]; if (d[k] > hi[k]) hi[k] = d[k];
    }
}

static int cell_of(float x, int axis)
{
    int c = (int)floorf((x - g_gridLo[axis]) / g_gridCell), n = axis ? g_gridH : g_gridW;
    return c < 0 ? 0 : c >= n ? n - 1 : c;
}

static int grid_build(void)
{
    int i, c, x, y, n = g_memListN, cells, total = 0, x0, x1, y0, y1;
    float mn[2], mx[2], lo[2], hi[2], ext;
    if (g_gridVer == g_memListVer) return g_gridW > 0;
    g_gridVer = g_memListVer;
    g_gridW = g_gridH = 0;
    g_gridAlwaysN = 0;
    if (n <= 0 || !g_memVtx) return 0;
    if (!grow_int(&g_gridCand, &g_gridCap, n)) return 0;
    if (g_gridStampCap < g_gridCap) {
        unsigned *st = (unsigned *)realloc(g_gridStamp, (size_t)g_gridCap * sizeof *st);
        if (!st) return 0;
        memset(st, 0, (size_t)g_gridCap * sizeof *st);
        g_gridStamp = st;
        g_gridStampCap = g_gridCap;
        g_gridStampNow = 0;
    }
    mn[0] = mn[1] = 3.0e38f; mx[0] = mx[1] = -3.0e38f;
    for (i = 0; i < n; i++) {
        tri_xy(i, lo, hi);
        for (c = 0; c < 2; c++) { if (lo[c] < mn[c]) mn[c] = lo[c]; if (hi[c] > mx[c]) mx[c] = hi[c]; }
    }
    ext = mx[0] - mn[0] > mx[1] - mn[1] ? mx[0] - mn[0] : mx[1] - mn[1];
    g_gridCell = ext / (float)GRID_N;
    if (g_gridCell < GRID_MIN) g_gridCell = GRID_MIN;
    g_gridLo[0] = mn[0]; g_gridLo[1] = mn[1];
    g_gridHi[0] = mx[0]; g_gridHi[1] = mx[1];
    g_gridW = (int)((mx[0] - mn[0]) / g_gridCell) + 1; if (g_gridW > GRID_N) g_gridW = GRID_N;
    g_gridH = (int)((mx[1] - mn[1]) / g_gridCell) + 1; if (g_gridH > GRID_N) g_gridH = GRID_N;
    cells = g_gridW * g_gridH;
    if (!grow_int(&g_gridStart, &g_gridStartCap, cells + 1) || !grow_int(&g_gridCur, &g_gridCurCap, cells + 1)) {
        g_gridW = 0;
        return 0;
    }
    memset(g_gridStart, 0, (size_t)(cells + 1) * sizeof *g_gridStart);
    for (i = 0; i < n; i++) {
        tri_xy(i, lo, hi);
        x0 = cell_of(lo[0], 0); x1 = cell_of(hi[0], 0);
        y0 = cell_of(lo[1], 1); y1 = cell_of(hi[1], 1);
        if (x1 - x0 >= GRID_BIG || y1 - y0 >= GRID_BIG) { g_gridAlwaysN++; continue; }
        for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) { g_gridStart[y * g_gridW + x]++; total++; }
    }
    if (!grow_int(&g_gridTri, &g_gridTriCap, total) || !grow_int(&g_gridAlways, &g_gridAlwaysCap, g_gridAlwaysN)) {
        g_gridW = 0;
        return 0;
    }
    for (c = 0, x = 0; c < cells; c++) { int k = g_gridStart[c]; g_gridStart[c] = x; g_gridCur[c] = x; x += k; }
    g_gridStart[cells] = x;
    g_gridAlwaysN = 0;
    for (i = 0; i < n; i++) {
        tri_xy(i, lo, hi);
        x0 = cell_of(lo[0], 0); x1 = cell_of(hi[0], 0);
        y0 = cell_of(lo[1], 1); y1 = cell_of(hi[1], 1);
        if (x1 - x0 >= GRID_BIG || y1 - y0 >= GRID_BIG) { g_gridAlways[g_gridAlwaysN++] = i; continue; }
        for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) g_gridTri[g_gridCur[y * g_gridW + x]++] = i;
    }
    return 1;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

static int grid_query(const float L[3], float R)
{
    int x, y, x0, x1, y0, y1, j, n = 0;
    if (++g_gridStampNow == 0) {
        memset(g_gridStamp, 0, (size_t)g_gridCap * sizeof *g_gridStamp);
        g_gridStampNow = 1;
    }
    if (L[0] + R >= g_gridLo[0] && L[1] + R >= g_gridLo[1] && L[0] - R <= g_gridHi[0] && L[1] - R <= g_gridHi[1]) {
        x0 = cell_of(L[0] - R, 0); x1 = cell_of(L[0] + R, 0);
        y0 = cell_of(L[1] - R, 1); y1 = cell_of(L[1] + R, 1);
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                int c = y * g_gridW + x;
                for (j = g_gridStart[c]; j < g_gridStart[c + 1]; j++) {
                    int i = g_gridTri[j];
                    if (g_gridStamp[i] == g_gridStampNow) continue;
                    g_gridStamp[i] = g_gridStampNow;
                    g_gridCand[n++] = i;
                }
            }
    }
    for (j = 0; j < g_gridAlwaysN; j++) g_gridCand[n++] = g_gridAlways[j];
    qsort(g_gridCand, (size_t)n, sizeof *g_gridCand, cmp_int);
    return n;
}

static int choose_tri(const float L[3], float R, int fixture, int i, int r, int *nc, int cnt[6], unsigned *fix)
{
    const float *a = g_memVtx + i * 9, *b = a + 3, *d = a + 6;
    float lo[3], hi[3], e, d2 = 0.0f;
    unsigned mask;
    int k, f;
    for (k = 0; k < 3; k++) {
        lo[k] = a[k] < b[k] ? a[k] : b[k]; if (d[k] < lo[k]) lo[k] = d[k];
        hi[k] = a[k] > b[k] ? a[k] : b[k]; if (d[k] > hi[k]) hi[k] = d[k];
        e = L[k] < lo[k] ? lo[k] - L[k] : L[k] > hi[k] ? L[k] - hi[k] : 0.0f;
        d2 += e * e;
    }
    if (d2 > R * R) return 1;
    if (fixture && mem_fixture_tri(L, a, b, d)) { (*fix)++; return 1; }
    if (*nc >= SUN_MEM_MAX) return 0;
    mask = face_mask(L, lo, hi);
    g_lcTri[*nc] = i;
    g_lcRun[*nc] = r;
    g_lcMask[*nc] = (unsigned char)mask;
    for (f = 0; f < 6; f++) if (mask & (1u << f)) cnt[f]++;
    (*nc)++;
    return 1;
}

static int choose_scan(const float L[3], float R, int fixture, int cnt[6], unsigned *fix)
{
    int r, i, nc = 0;
    for (r = 0; r < g_memRuns; r++)
        for (i = g_memRunStart[r]; i < g_memRunStart[r + 1]; i++)
            if (!choose_tri(L, R, fixture, i, r, &nc, cnt, fix)) return nc;
    return nc;
}

int             g_lmRunBox = 0;
static float   *g_rbBox;
static int      g_rbCap;
static unsigned g_rbVer;

static int runbox_build(void)
{
    int r, i, k;
    if (g_rbVer == g_memListVer) return 1;
    if (g_memRuns <= 0 || !g_memVtx) return 0;
    if (g_rbCap < g_memRuns) {
        float *b = (float *)realloc(g_rbBox, (size_t)(g_memRuns + 1024) * 6 * sizeof *b);
        if (!b) return 0;
        g_rbBox = b;
        g_rbCap = g_memRuns + 1024;
    }
    for (r = 0; r < g_memRuns; r++) {
        float *b = g_rbBox + r * 6;
        b[0] = b[1] = b[2] = 3.0e38f;
        b[3] = b[4] = b[5] = -3.0e38f;
        for (i = g_memRunStart[r] * 9; i < g_memRunStart[r + 1] * 9; i += 3)
            for (k = 0; k < 3; k++) {
                float x = g_memVtx[i + k];
                if (x < b[k]) b[k] = x;
                if (x > b[3 + k]) b[3 + k] = x;
            }
    }
    g_rbVer = g_memListVer;
    return 1;
}

static int choose_runs(const float L[3], float R, int fixture, int cnt[6], unsigned *fix, unsigned *tested)
{
    int r, i, k, nc = 0;
    for (r = 0; r < g_memRuns; r++) {
        const float *b = g_rbBox + r * 6;
        float e, d2 = 0.0f;
        for (k = 0; k < 3; k++) {
            e = L[k] < b[k] ? b[k] - L[k] : L[k] > b[3 + k] ? L[k] - b[3 + k] : 0.0f;
            d2 += e * e;
        }
        if (d2 > R * R) continue;
        *tested += (unsigned)(g_memRunStart[r + 1] - g_memRunStart[r]);
        for (i = g_memRunStart[r]; i < g_memRunStart[r + 1]; i++)
            if (!choose_tri(L, R, fixture, i, r, &nc, cnt, fix)) return nc;
    }
    return nc;
}

static int *g_gvTri, *g_gvRun, g_gvCap;
static unsigned char *g_gvMask;
static int grid_verify(const float L[3], float R, int fixture, int nc, int cnt[6], unsigned *fix)
{
    int n2, j, f, need = nc > 0 ? nc : 1;
    if (g_gvCap < need) {
        int cap = need + 4096;
        int *t2 = (int *)realloc(g_gvTri, (size_t)cap * sizeof *t2), *r2;
        unsigned char *m2;
        if (!t2) return nc;
        g_gvTri = t2;
        r2 = (int *)realloc(g_gvRun, (size_t)cap * sizeof *r2);
        if (!r2) return nc;
        g_gvRun = r2;
        m2 = (unsigned char *)realloc(g_gvMask, (size_t)cap);
        if (!m2) return nc;
        g_gvMask = m2;
        g_gvCap = cap;
    }
    memcpy(g_gvTri, g_lcTri, (size_t)nc * sizeof *g_gvTri);
    memcpy(g_gvRun, g_lcRun, (size_t)nc * sizeof *g_gvRun);
    memcpy(g_gvMask, g_lcMask, (size_t)nc);
    for (f = 0; f < 6; f++) cnt[f] = 0;
    *fix = 0;
    n2 = choose_scan(L, R, fixture, cnt, fix);
    g_gridChecks++;
    for (j = 0; j < nc && j < n2; j++)
        if (g_gvTri[j] != g_lcTri[j] || g_gvRun[j] != g_lcRun[j] || g_gvMask[j] != g_lcMask[j]) break;
    if (n2 != nc || j < nc) {
        g_gridMismatch++;
        if (g_gridMismatch <= 20)
            u9log_line("SHADOWGRID mismatch %u (%s): light %.0f,%.0f,%.0f reach %.0f - chose %d, the scan %d, first"
                       " difference at %d (grid %d cells %dx%d of %.0f, %d always)", g_gridMismatch,
                       g_lmGrid ? "grid" : "by run",
                       (double)L[0], (double)L[1], (double)L[2], (double)R, nc, n2, j, g_gridW * g_gridH,
                       g_gridW, g_gridH, (double)g_gridCell, g_gridAlwaysN);
    }
    return n2;
}

static void lamp_choose(LampSel *s, const float L[3], float reach, int fixture)
{
    int r, i, f, nc = 0, total, cnt[6] = { 0, 0, 0, 0, 0, 0 }, at[6];
    unsigned fix = 0;
    float R = reach * 1.02f + 8.0f;
    s->n = 0;
    if (g_lmGrid && grid_build()) {
        int nCand = grid_query(L, R), j;
        r = 0;
        for (j = 0; j < nCand; j++) {
            i = g_gridCand[j];
            while (r < g_memRuns && i >= g_memRunStart[r + 1]) r++;
            if (!choose_tri(L, R, fixture, i, r, &nc, cnt, &fix)) break;
        }
        g_gridChoices++;
        g_gridTested += (unsigned)nCand;
        g_gridListed += (unsigned)g_memListN;
        if (g_scostOn && (g_gridChoices & 63u) == 0) nc = grid_verify(L, R, fixture, nc, cnt, &fix);
    } else if (g_lmRunBox && runbox_build()) {
        unsigned tested = 0;
        nc = choose_runs(L, R, fixture, cnt, &fix, &tested);
        g_gridChoices++;
        g_gridTested += tested;
        g_gridListed += (unsigned)g_memListN;
        if (g_scostOn && (g_gridChoices & 63u) == 0) nc = grid_verify(L, R, fixture, nc, cnt, &fix);
    } else
        nc = choose_scan(L, R, fixture, cnt, &fix);
    total = cnt[0] + cnt[1] + cnt[2] + cnt[3] + cnt[4] + cnt[5];
    if (s->cap < total) {
        int cap = total + 4096;
        int *t = (int *)realloc(s->tri, (size_t)cap * sizeof *t), *u;
        if (!t) { s->valid = 0; return; }
        s->tri = t;
        u = (int *)realloc(s->run, (size_t)cap * sizeof *u);
        if (!u) { s->valid = 0; return; }
        s->run = u;
        s->cap = cap;
    }
    s->faceAt[0] = 0;
    for (f = 0; f < 6; f++) { s->faceAt[f + 1] = s->faceAt[f] + cnt[f]; at[f] = s->faceAt[f]; }
    for (i = 0; i < nc; i++)
        for (f = 0; f < 6; f++)
            if (g_lcMask[i] & (1u << f)) { s->tri[at[f]] = g_lcTri[i]; s->run[at[f]] = g_lcRun[i]; at[f]++; }
    s->n = total;
    memcpy(s->L, L, sizeof s->L);
    s->reach = reach;
    s->fixture = fixture;
    s->pass = g_memFrame;
    s->content = g_memContent;
    s->key = sel_key();
    s->valid = 1;
    g_shadowMemFix += fix;
    g_shadowMemChosen++;
}

static int *g_kvTri, *g_kvRun, g_kvCap;
static void keep_verify(LampSel *s)
{
    int n = s->n, at[7], j, same;
    float L[3], reach = s->reach;
    if (g_kvCap < n + 1) {
        int cap = n + 4096;
        int *t = (int *)realloc(g_kvTri, (size_t)cap * sizeof *t), *r;
        if (!t) return;
        g_kvTri = t;
        r = (int *)realloc(g_kvRun, (size_t)cap * sizeof *r);
        if (!r) return;
        g_kvRun = r;
        g_kvCap = cap;
    }
    memcpy(g_kvTri, s->tri, (size_t)n * sizeof *g_kvTri);
    memcpy(g_kvRun, s->run, (size_t)n * sizeof *g_kvRun);
    memcpy(at, s->faceAt, sizeof at);
    memcpy(L, s->L, sizeof L);
    lamp_choose(s, L, reach, s->fixture);
    g_keepChecks++;
    same = s->valid && s->n == n && !memcmp(s->faceAt, at, sizeof at);
    for (j = 0; same && j < n; j++) if (s->tri[j] != g_kvTri[j] || s->run[j] != g_kvRun[j]) same = 0;
    if (!same) {
        g_keepMismatch++;
        if (g_keepMismatch <= 20)
            u9log_line("SHADOWKEEP mismatch %u: light %.0f,%.0f,%.0f reach %.0f - kept %d entries, made again %d",
                       g_keepMismatch, (double)L[0], (double)L[1], (double)L[2], (double)reach, n, s->n);
    }
}

static unsigned lamp_select(const float L[3], float reach, int fixture, int slot)
{
    LARGE_INTEGER t0, t1, fq;
    LampSel *s;
    int j, cur = -1;
    unsigned cnt = 0;
    if (slot < 0 || slot >= U9SHADOW_MAX || !g_mem || g_memListN <= 0 || g_memRuns <= 0 || !g_memVtx) return 0;
    if (g_memBFrame != u9log_frame() || !g_memSkip) return 0;
    QueryPerformanceCounter(&t0);
    s = &g_lsel[slot];
    g_lselFrame[slot] = u9log_frame();
    {
        int why = !s->valid ? 0 : (g_lmKeep ? s->content != g_memContent : s->pass != g_memFrame) ? 1 :
                  (fabsf(s->L[0] - L[0]) > 4.0f || fabsf(s->L[1] - L[1]) > 4.0f || fabsf(s->L[2] - L[2]) > 4.0f) ? 2 :
                  (fabsf(s->reach - reach) > reach * 0.02f || s->fixture != fixture) ? 3 :
                  s->key != sel_key() ? 4 : -1;
        if (why >= 0) {
            g_lsWhy[why]++;
            lamp_choose(s, L, reach, fixture);
            g_keepMade++;
        } else if (s->pass != g_memFrame) {
            g_keepKept++;
            if (g_scostOn && (g_keepKept & 63u) == 0) keep_verify(s);
            s->pass = g_memFrame;
        }
    }
    if (!s->valid) return 0;
    if (g_lmIdxCap < (unsigned)s->n * 3u) {
        unsigned cap = (unsigned)s->n * 3u + 12288u;
        GLuint *p = (GLuint *)realloc(g_lmIdx, cap * sizeof *p);
        if (!p) return 0;
        g_lmIdx = p;
        g_lmIdxCap = cap;
    }
    if (g_lmSegCap < s->n + 8) {
        int cap = s->n + 4096;
        int *sa = (int *)realloc(g_lmSegRun, (size_t)cap * sizeof *sa), *sb;
        if (!sa) return 0;
        g_lmSegRun = sa;
        sb = (int *)realloc(g_lmSegAt, (size_t)(cap + 1) * sizeof *sb);
        if (!sb) return 0;
        g_lmSegAt = sb;
        g_lmSegCap = cap;
    }
    g_lmSegs = 0;
    {
        int f;
        for (f = 0; f < 6; f++) {
            g_lmFaceSeg[f] = g_lmSegs;
            cur = -1;
            for (j = s->faceAt[f]; j < s->faceAt[f + 1]; j++) {
                int i = s->tri[j];
                if (g_memSkip[i]) continue;
                if (s->run[j] != cur) {
                    cur = s->run[j];
                    g_lmSegRun[g_lmSegs] = cur;
                    g_lmSegAt[g_lmSegs] = (int)cnt;
                    g_lmSegs++;
                }
                g_lmIdx[cnt++] = (GLuint)(i * 3);
                g_lmIdx[cnt++] = (GLuint)(i * 3 + 1);
                g_lmIdx[cnt++] = (GLuint)(i * 3 + 2);
            }
        }
        g_lmFaceSeg[6] = g_lmSegs;
    }
    g_lmSegAt[g_lmSegs] = (int)cnt;
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&fq);
    if (fq.QuadPart > 0) g_shadowMemUs += (unsigned)((t1.QuadPart - t0.QuadPart) * 1000000 / fq.QuadPart);
    g_shadowMemTris += cnt / 3u;
    return cnt / 3u;
}

static void lamp_mem_draw(int face)
{
    int g;
    fUniform1i(uMode, 4);
    glVertexPointer(3, GL_FLOAT, 0, g_memVtx);
    glTexCoordPointer(2, GL_FLOAT, 0, g_memTc);
    if (face < 0 || face > 5) return;
    for (g = g_lmFaceSeg[face]; g < g_lmFaceSeg[face + 1]; g++) {
        int n = g_lmSegAt[g + 1] - g_lmSegAt[g], r = g_lmSegRun[g];
        if (n <= 0) continue;
        fUniform1i(uCutout, g_memRunCut[r]);
        if (g_memRunCut[r]) glBindTexture(GL_TEXTURE_2D, g_memRunTex[r]);
        glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, g_lmIdx + g_lmSegAt[g]);
    }
    fUniform1i(uMode, 0);
    frame_pointers();
}

unsigned u9shadow_sun_rebases(void)
{
    return g_sunRebases;
}

void u9shadow_sun_forget(void)
{
    mem_clear();
    g_memHaveCam = 0;
}

static int draw_sun_map(const U9FrameView *v, int i, const float r[3], const float u[3], const float l[3],
                        const float pr[3], const float pu[3], const float pl[3], const float o[3], float half,
                        U9SunMap *m)
{
    float texel, ahead[3], a, b, z;
    DrawCtx ctx;
    int k;
    memcpy(m->r, r, sizeof m->r);
    memcpy(m->u, u, sizeof m->u);
    memcpy(m->l, l, sizeof m->l);
    memcpy(m->pr, pr, sizeof m->pr);
    memcpy(m->pu, pu, sizeof m->pu);
    memcpy(m->pl, pl, sizeof m->pl);
    m->half = half;
    m->dz = half * 3.0f > 16384.0f ? half * 3.0f : 16384.0f;
    m->size = g_sunSize;
    texel = 2.0f * m->half / (float)m->size;
    ahead[0] = v->m[3] + v->m[1] * half * g_sunAhead;
    ahead[1] = v->m[7] + v->m[5] * half * g_sunAhead;
    ahead[2] = v->m[11];
    for (k = 0; k < 3; k++) ahead[k] -= o[k];
    a = (float)floor((double)((ahead[0] * pr[0] + ahead[1] * pr[1] + ahead[2] * pr[2]) / texel) + 0.5) * texel;
    b = (float)floor((double)((ahead[0] * pu[0] + ahead[1] * pu[1] + ahead[2] * pu[2]) / texel) + 0.5) * texel;
    z = ahead[0] * pl[0] + ahead[1] * pl[1] + ahead[2] * pl[2];
    for (k = 0; k < 3; k++) m->c[k] = o[k] + r[k] * a + u[k] * b + l[k] * z;
    m->texel = texel;
    if (!make_sun(i)) return 0;
    fBindFramebuffer(GL_FRAMEBUFFER, g_sunFbo[i]);
    glViewport(0, 0, g_sunTexSize[i], g_sunTexSize[i]);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    begin_casters(v);
    fUniform1i(uMode, 2);
    fUniform3f(uFs, pr[0], pr[1], pr[2]);
    fUniform3f(uFt, pu[0], pu[1], pu[2]);
    fUniform3f(uFm, pl[0], pl[1], pl[2]);
    fUniform3f(uLight, m->c[0], m->c[1], m->c[2]);
    fUniform2f(uNf, m->half, m->dz);
    memset(&ctx, 0, sizeof ctx);
    ctx.v = v;
    ctx.sunMap = g_sunCull;
    memcpy(ctx.sc, m->c, sizeof ctx.sc);
    memcpy(ctx.spr, pr, sizeof ctx.spr);
    memcpy(ctx.spu, pu, sizeof ctx.spu);
    ctx.shalf = m->half;
    ctx.cam[0] = v->m[3];
    ctx.cam[1] = v->m[7];
    if (g_sunFadeTo > g_sunFadeFrom) {
        float r = g_sunFadeTo + SUN_MEM_SHADOW_REACH;
        ctx.castReach2 = r * r;
    }
    if (g_sunMemory != 2) u9gl_casters(draw_run, &ctx);
    g_sunRunsDrawn[i & 1] = ctx.drawn;
    g_sunRunsCulled[i & 1] = ctx.culled;
    if (g_sunMemory) mem_render();
    end_casters();
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (i == 0) g_sunTris = ctx.tris;
    m->tex = g_sunTex[i];
    return 1;
}

static void sun_axes(const float l0[3], const float l[3], float r[3], float u[3], float pr[3], float pu[3], float pl[3])
{
    static const float worldY[3] = { 0.0f, 1.0f, 0.0f };
    float back[3], ll, rl, ul;
    int k;
    for (k = 0; k < 3; k++) back[k] = -l0[k];
    cross3(back, worldY, r);
    unit3(r);
    cross3(r, back, u);
    ll = l0[0] * l[0] + l0[1] * l[1] + l0[2] * l[2];
    rl = r[0] * l[0] + r[1] * l[1] + r[2] * l[2];
    ul = u[0] * l[0] + u[1] * l[1] + u[2] * l[2];
    for (k = 0; k < 3; k++) {
        pr[k] = r[k] - rl / ll * l0[k];
        pu[k] = u[k] - ul / ll * l0[k];
        pl[k] = l0[k] / ll;
    }
}

static float sun_step(float l[3], float old[3])
{
    DWORD now = u9time_now(), age;
    float x, step = (float)g_sunStepMs, fade = (float)g_sunXfadeMs;
    if (g_sunStepMs <= 0 || g_sunHold) {
        g_xHave = g_xFading = 0;
        return 1.0f;
    }
    if (fade > step) fade = step;
    age = now - g_xAt;
    if (!g_xHave || l[0] * g_xNew[0] + l[1] * g_xNew[1] + l[2] * g_xNew[2] < cosf(SUN_XFADE_SNAP * 0.017453292f)) {
        memcpy(g_xNew, l, sizeof g_xNew);
        g_xAt = now;
        g_xHave = 1;
        g_xFading = 0;
    } else if ((float)age >= step) {
        memcpy(g_xOld, g_xNew, sizeof g_xOld);
        memcpy(g_xNew, l, sizeof g_xNew);
        g_xAt = now;
        g_xFading = fade > 0.0f;
        g_xSteps++;
    }
    memcpy(l, g_xNew, sizeof g_xNew);
    if (!g_xFading) return 1.0f;
    x = (float)(DWORD)(now - g_xAt) / fade;
    if (x >= 1.0f) { g_xFading = 0; return 1.0f; }
    memcpy(old, g_xOld, sizeof g_xOld);
    return x;
}

int u9shadow_sun_fading(U9SunMap *nearOld, U9SunMap *farOld, float *mix)
{
    if (!g_oldMaps) return 0;
    *nearOld = g_oldNear;
    *farOld = g_oldFar;
    *mix = g_xMix;
    return g_oldMaps;
}

unsigned u9shadow_sun_steps(void)
{
    return g_xSteps;
}

int u9shadow_build_sun(const U9FrameView *v, const float dir[3], U9SunMap *nearMap, U9SunMap *farMap)
{
    float r[3], u[3], l[3], l0[3], pr[3], pu[3], pl[3], cam[3], o[3], lOld[3], reach;
    int k, maps;
    g_oldMaps = 0;
    g_xMix = 1.0f;
    if (g_failed || !v || !v->valid) return 0;
    if (!load()) return 0;
    if (g_traceLeft > 0) trace_frame();
    if (g_sunCull && g_boxFrame != u9log_frame()) {
        DrawCtx bc;
        g_nBox = 0;
        memset(&bc, 0, sizeof bc);
        bc.v = v;
        u9gl_casters(box_run, &bc);
        g_boxFrame = u9log_frame();
    }
    if (g_sunFreeze && g_frozenMaps) {
        *nearMap = g_frozenNear;
        *farMap = g_frozenFar;
        return g_frozenMaps;
    }
    if (g_sunSize < 256) g_sunSize = 256;
    if (g_sunSize > 4096) g_sunSize = 4096;
    if (g_sunRange < 500.0f) g_sunRange = 500.0f;
    if (g_sunNear < 250.0f) g_sunNear = 250.0f;
    maps = g_sunCascades && g_sunNear < g_sunRange * 0.75f ? 2 : 1;
    reach = g_sunRange;
    for (k = 0; k < 3; k++) l[k] = dir[k];
    unit3(l);
    if (g_sunHold) {
        if (!g_sunHaveHeld) { memcpy(g_sunHeld, l, sizeof g_sunHeld); g_sunHaveHeld = 1; }
        memcpy(l, g_sunHeld, sizeof l);
    } else {
        g_sunHaveHeld = 0;
    }
    g_xMix = sun_step(l, lOld);
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
    if (g_sunSteady) {
        float away = g_sunHaveRef ? (cam[0] - g_sunPivot[0]) * g_sunRef[0] + (cam[1] - g_sunPivot[1]) * g_sunRef[1]
                                    + (cam[2] - g_sunPivot[2]) * g_sunRef[2] : 0.0f;
        if (!g_sunHaveRef || l[0] * g_sunRef[0] + l[1] * g_sunRef[1] + l[2] * g_sunRef[2]
                             < cosf(SUN_STEADY_DEG * 0.017453292f) || away > SUN_STEADY_AWAY || away < -SUN_STEADY_AWAY) {
            memcpy(g_sunRef, l, sizeof g_sunRef);
            memcpy(g_sunPivot, cam, sizeof g_sunPivot);
            g_sunHaveRef = 1;
            g_sunRebases++;
        }
        memcpy(l0, g_sunRef, sizeof l0);
        memcpy(o, g_sunPivot, sizeof o);
    } else {
        memcpy(l0, l, sizeof l0);
        o[0] = o[1] = o[2] = 0.0f;
        g_sunHaveRef = 0;
    }
    sun_axes(l0, l, r, u, pr, pu, pl);
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];

    mem_tick(v);
    g_sunBuiltFrame = u9log_frame();

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (!draw_sun_map(v, 0, r, u, l, pr, pu, pl, o, maps == 2 ? g_sunNear : reach, nearMap)) { glPopAttrib(); return 0; }
    if (maps == 2 && !draw_sun_map(v, 1, r, u, l, pr, pu, pl, o, reach, farMap)) maps = 1;
    if (g_xMix < 1.0f) {
        float ro[3], uo[3], pro[3], puo[3], plo[3];
        if (!g_sunSteady) memcpy(l0, lOld, sizeof l0);
        sun_axes(l0, lOld, ro, uo, pro, puo, plo);
        if (draw_sun_map(v, 2, ro, uo, lOld, pro, puo, plo, o, maps == 2 ? g_sunNear : reach, &g_oldNear)) {
            g_oldMaps = 1;
            if (maps == 2 && draw_sun_map(v, 3, ro, uo, lOld, pro, puo, plo, o, reach, &g_oldFar)) g_oldMaps = 2;
        }
    }
    glPopAttrib();
    g_sunBuilds++;
    g_frozenNear = *nearMap;
    if (maps == 2) g_frozenFar = *farMap;
    g_frozenMaps = maps;
    return maps;
}

void u9shadow_draw_sun_view(int surfW, int surfH)
{
    int side, i = g_sunView == 2 ? 1 : 0;
    if (!g_sunView || !g_sunTex[i] || g_failed) return;
    side = (surfW < surfH ? surfW : surfH) * 45 / 100;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(8, surfH - side - 8, side, side);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    fActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_sunTex[i]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

void u9shadow_draw_view(const U9FrameView *v, int surfW, int surfH)
{
    DrawCtx ctx;
    int side;
    if (!g_shadowView || g_failed || !v || !v->valid) return;
    if (!load()) return;
    side = (surfW < surfH ? surfW : surfH) * 45 / 100;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(surfW - side - 8, surfH - side - 8, side, side);
    glEnable(GL_SCISSOR_TEST);
    glScissor(surfW - side - 8, surfH - side - 8, side, side);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    begin_casters(v);
    glEnable(GL_SCISSOR_TEST);
    fUniform1i(uMode, 1);
    fUniform4f(uTop, v->m[3], v->m[7], 1024.0f, v->m[11]);
    memset(&ctx, 0, sizeof ctx);
    u9gl_casters(draw_run, &ctx);
    end_casters();
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINES);
        glVertex2f(-0.03f, 0.0f); glVertex2f(0.03f, 0.0f);
        glVertex2f(0.0f, -0.03f); glVertex2f(0.0f, 0.03f);
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    glPopAttrib();
}

void u9shadow_init(void)
{
    g_shadowOn      = u9cfg_int("gl_light_shadows", 1);
    g_shadowSize    = u9cfg_int("gl_light_shadow_size", 512);
    g_shadowCutouts = u9cfg_int("gl_light_shadow_cutouts", 1);
    g_shadowHideBlob = u9cfg_int("gl_light_shadow_hide_blob", 1);
    g_shadowCount   = u9cfg_int("gl_light_shadow_count", 8);
    if (g_shadowCount < 1) g_shadowCount = 1;
    if (g_shadowCount > U9SHADOW_MAX) g_shadowCount = U9SHADOW_MAX;
    g_shadowMem        = u9cfg_int("gl_light_shadow_memory", 1);
    g_shadowFire       = u9cfg_int("gl_light_shadow_fire", 1);
    g_shadowMem = g_shadowMem != 0;
    g_shadowMemAnimated = u9cfg_int("gl_shadow_memory_skip_moving", 1) == 0;
    g_lmFaces = u9cfg_int("gl_light_shadow_memory_faces", 1) != 0;
    g_lmGrid = u9cfg_int("gl_light_shadow_memory_grid", 0) != 0;
    g_lmRunBox = u9cfg_int("gl_light_shadow_memory_runbox", 0) != 0;
    g_lmKeep = u9cfg_int("gl_light_shadow_memory_keep", 1) != 0;
    g_sunOn           = u9cfg_int("gl_sun_shadows", 1);
    g_sunStrength     = (float)u9cfg_int("gl_sun_shadow_strength", 60) / 100.0f;
    g_sunRange        = (float)u9cfg_int("gl_sun_shadow_range", 10000);
    g_sunNear         = (float)u9cfg_int("gl_sun_shadow_near", 1000);
    g_sunCascades     = u9cfg_int("gl_sun_shadow_cascades", 1);
    g_sunSize         = u9cfg_int("gl_sun_shadow_size", 2048);
    g_sunMinElevation = (float)u9cfg_int("gl_sun_shadow_min", 5);
    g_sunFadeFrom     = (float)u9cfg_int("gl_sun_shadow_fade_from", 2500);
    g_sunFadeTo       = (float)u9cfg_int("gl_sun_shadow_fade_to", 4000);
    g_sunBias         = (float)u9cfg_int("gl_sun_shadow_bias", 0);
    g_sunWalls        = u9cfg_int("gl_sun_shadow_walls", 1);
    g_sunCull         = u9cfg_int("gl_sun_shadow_cull", 1);
    g_sunStepMs       = u9cfg_int("gl_sun_shadow_step", 2000);
    g_sunXfadeMs      = u9cfg_int("gl_sun_shadow_xfade", 1000);
    if (g_sunStepMs < 0) g_sunStepMs = 0;
    if (g_sunXfadeMs < 0) g_sunXfadeMs = 0;
    g_waterDepth      = u9cfg_int("gl_water_depth", 1);
    g_sunAbove        = (float)u9cfg_int("gl_sun_shadow_above", -5000);
    g_sunMemMargin    = (float)u9cfg_int("gl_sun_shadow_memory_margin", 2000);
    g_sunEvict        = u9cfg_int("gl_sun_shadow_memory_evict", 1) != 0;
    if (g_sunMemMargin < 0.0f) g_sunMemMargin = 0.0f;
    if (g_sunBias < 0.0f) g_sunBias = 0.0f;
    g_sunMemory       = u9cfg_int("gl_sun_shadow_memory", 1);
    g_sunKeepS        = u9cfg_int("gl_sun_shadow_keep", 120);
}

int u9shadow_hide_blob(void)
{
    return g_shadowOn && g_shadowHideBlob && !g_failed;
}

static int ray_tri(const float o[3], const float dv[3], const float *a, const float *b, const float *c, float *t)
{
    float e1[3], e2[3], s[3], p[3], q[3], det, inv, u, w;
    int k;
    for (k = 0; k < 3; k++) { e1[k] = b[k] - a[k]; e2[k] = c[k] - a[k]; s[k] = o[k] - a[k]; }
    cross3(dv, e2, p);
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (det > -1e-9f && det < 1e-9f) return 0;
    inv = 1.0f / det;
    u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    cross3(s, e1, q);
    w = (dv[0] * q[0] + dv[1] * q[1] + dv[2] * q[2]) * inv;
    if (w < 0.0f || u + w > 1.0f) return 0;
    *t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return 1;
}

typedef struct {
    const U9FrameView *v;
    float L[3], dv[3], tMin, tMax, reach;
    int fixture;
    unsigned idx, hits;
    float best;
    unsigned run, tex, tris;
    int cut, fix, culled;
} RayCtx;

static void ray_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    RayCtx *c = (RayCtx *)ctx;
    int stride, k, inRun = 0, column = 0, culled = 0;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned at = c->idx++, t;
    const float *lo = NULL, *hi = NULL;
    if (at < g_nBox) {
        float d2 = 0.0f, e;
        lo = g_box[at]; hi = g_box[at] + 3;
        if (c->fixture) {
            inRun = 1;
            for (k = 0; k < 3; k++)
                if (c->L[k] < lo[k] - g_shadowNear || c->L[k] > hi[k] + g_shadowNear) inRun = 0;
            column = g_shadowColumn > 0.0f;
            for (k = 0; k < 2; k++)
                if (c->L[k] < lo[k] - g_shadowColumn || c->L[k] > hi[k] + g_shadowColumn) column = 0;
        }
        for (k = 0; k < 3; k++) {
            e = lo[k] > c->L[k] ? lo[k] - c->L[k] : c->L[k] > hi[k] ? c->L[k] - hi[k] : 0.0f;
            d2 += e * e;
        }
        if (d2 > c->reach * c->reach) culled = 1;
    }
    for (t = first; t + 2 < first + count; t += 3) {
        float a[3], b[3], d[3], tt;
        if (!world_of(c->v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), d)) continue;
        if (!ray_tri(c->L, c->dv, a, b, d, &tt) || tt <= c->tMin || tt >= c->tMax) continue;
        c->hits++;
        if (tt >= c->best) continue;
        c->best = tt;
        c->run = at;
        c->tex = glTex;
        c->tris = count / 3;
        c->cut = cutout;
        c->fix = (inRun || column) && fixture_tri(c->L, inRun, column, a, b, d);
        c->culled = culled;
        if (!culled && lo) {
            float h[3], m = 0.0f;
            int ax = 0;
            for (k = 0; k < 3; k++) {
                h[k] = c->dv[k] * tt;
                if (fabsf(h[k]) > m) { m = fabsf(h[k]); ax = k; }
            }
            if ((h[ax] < 0.0f ? c->L[ax] - lo[ax] : hi[ax] - c->L[ax]) < 4.0f) c->culled = 2;
        }
    }
}

void u9gl_all_runs(void (*fn)(void *ctx, unsigned first, unsigned count, int texId, int back, const char *why),
                   void *ctx);
typedef struct { float d, t; int tex, back; } NearMiss;
#define NEAR_KEEP 3

static float seg_dist(const float p[3], const float L[3], const float dv[3], float len2, float *tOut)
{
    float w[3], t, q[3];
    int k;
    for (k = 0; k < 3; k++) w[k] = p[k] - L[k];
    t = (w[0] * dv[0] + w[1] * dv[1] + w[2] * dv[2]) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    for (k = 0; k < 3; k++) q[k] = w[k] - dv[k] * t;
    *tOut = t;
    return sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

static void near_note(NearMiss *nm, const float *a, const float *b, const float *c, const float L[3], const float dv[3],
                      float len2, float tMin, float tMax, int tex, int back)
{
    float pts[7][3], best = 1e30f, bt = 0.0f, t, d;
    int k, j;
    for (k = 0; k < 3; k++) {
        pts[0][k] = a[k]; pts[1][k] = b[k]; pts[2][k] = c[k];
        pts[3][k] = 0.5f * (a[k] + b[k]); pts[4][k] = 0.5f * (b[k] + c[k]); pts[5][k] = 0.5f * (c[k] + a[k]);
        pts[6][k] = (a[k] + b[k] + c[k]) / 3.0f;
    }
    for (j = 0; j < 7; j++) {
        d = seg_dist(pts[j], L, dv, len2, &t);
        if (d < best) { best = d; bt = t; }
    }
    if (bt <= tMin || bt >= tMax || best > 96.0f) return;
    for (j = NEAR_KEEP - 1; j >= 0 && (nm[j].d < 0.0f || best < nm[j].d); j--) {
        if (j + 1 < NEAR_KEEP) nm[j + 1] = nm[j];
        nm[j].d = best; nm[j].t = bt; nm[j].tex = tex; nm[j].back = back;
    }
}

static void near_line(const char *what, const NearMiss *nm, float len, void (*line)(const char *fmt, ...))
{
    int j;
    char buf[256];
    int n = 0;
    if (nm[0].d < 0.0f) { line("GLSHADOW ray: %s - nothing passes within 96 of the line", what); return; }
    for (j = 0; j < NEAR_KEEP && nm[j].d >= 0.0f; j++)
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%s%.0f off the line %.0f from the light (texture %d%s)",
                      j ? "; " : "", (double)nm[j].d, (double)(nm[j].t * len), nm[j].tex, nm[j].back ? ", back face" : "");
    line("GLSHADOW ray: %s - closest: %s", what, buf);
}

typedef struct {
    const U9FrameView *v;
    float L[3], dv[3], tMin, tMax, best, len2;
    int tex, back, hits, excludedHits;
    const char *why;
    NearMiss nm[NEAR_KEEP];
} AnyCtx;

static void any_run(void *ctx, unsigned first, unsigned count, int texId, int back, const char *why)
{
    AnyCtx *c = (AnyCtx *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned t;
    for (t = first; t + 2 < first + count; t += 3) {
        float a[3], b[3], d[3], tt;
        if (!world_of(c->v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !world_of(c->v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), d)) continue;
        near_note(c->nm, a, b, d, c->L, c->dv, c->len2, c->tMin, c->tMax, texId, back);
        if (!ray_tri(c->L, c->dv, a, b, d, &tt) || tt <= c->tMin || tt >= c->tMax) continue;
        c->hits++;
        if (why) c->excludedHits++;
        if (tt >= c->best) continue;
        c->best = tt;
        c->tex = texId;
        c->back = back;
        c->why = why;
    }
}

void u9shadow_sun_probe_ray(const U9FrameView *v, const float P[3], const float N[3], const float l[3],
                            void (*out)(const char *fmt, ...))
{
    AnyCtx ac;
    float S[3], dv[3], best = 2.0f, reach = 6000.0f, cam[3];
    int k, i, bestI = -1, hits = 0;
    DWORD now = u9time_now();
    for (k = 0; k < 3; k++) { S[k] = P[k] + N[k] * 4.0f; dv[k] = l[k] * reach; }
    memset(&ac, 0, sizeof ac);
    ac.v = v;
    memcpy(ac.L, S, sizeof ac.L);
    memcpy(ac.dv, dv, sizeof ac.dv);
    ac.tMin = 0.0f;
    ac.tMax = 1.0f;
    ac.best = 2.0f;
    ac.len2 = reach * reach;
    for (k = 0; k < NEAR_KEEP; k++) ac.nm[k].d = -1.0f;
    u9gl_all_runs(any_run, &ac);
    if (ac.best < 2.0f) {
        u9log_line("GLSUN ray: frame - the nearest toward the light %.0f off, texture %d%s: %s", (double)(ac.best * reach),
                   ac.tex, ac.back ? ", a back face or the guard band" : "", ac.why ? ac.why : "a caster");
        if (out) out("toward the light, the frame: %.0f off, texture %d%s (%s)", (double)(ac.best * reach), ac.tex,
                     ac.back ? ", back face or guard" : "", ac.why ? ac.why : "a caster");
    } else {
        u9log_line("GLSUN ray: frame - nothing drawn toward the light within %.0f", (double)reach);
        if (out) out("toward the light, the frame: nothing drawn within %.0f", (double)reach);
    }
    for (i = 0; i < g_memN && g_mem; i++) {
        const SunTri *e = &g_mem[i];
        float t;
        if (!e->live || !ray_tri(S, dv, e->p[0], e->p[1], e->p[2], &t) || t <= 0.0f || t >= 1.0f) continue;
        hits++;
        if (t < best) { best = t; bestI = i; }
    }
    if (bestI < 0) {
        u9log_line("GLSUN ray: memory - nothing remembered toward the light within %.0f", (double)reach);
        if (out) out("toward the light, the memory: nothing within %.0f", (double)reach);
        return;
    }
    {
        const SunTri *e = &g_mem[bestI];
        float mid[3], dx, dy, mr = mem_reach(g_sunRange < 500.0f ? 500.0f : g_sunRange);
        int onS, inRoom;
        const char *why;
        for (k = 0; k < 3; k++) mid[k] = (e->p[0][k] + e->p[1][k] + e->p[2][k]) / 3.0f;
        cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
        onS = on_screen(v, mid);
        inRoom = u9spaces_at(cam, NULL, 0, NULL) >= 0;
        dx = mid[0] - cam[0]; dy = mid[1] - cam[1];
        why = e->frame == g_memFrame ? "drawn on the last pass - it is (or was just) there" :
              dx * dx + dy * dy > mr * mr ? "out of the memory's reach - forgotten next pass" :
              !onS ? "its spot is off screen (or behind, or past the far plane) - nothing can show it gone" :
              inRoom ? "on screen, but the camera is in a room - undrawn proves nothing there" :
              e->partial == g_memFrame ? "a smaller cut of it was drawn on the last pass" :
              "on screen and not drawn - forgotten on the next pass";
        u9log_line("GLSUN ray: memory - %d remembered on the line; the nearest %.0f off at %.0f,%.0f,%.0f, gl texture %u%s,"
                   " area %.0f, last drawn %u passes and %u ms ago, when the Avatar was %.0f off and the camera %.0f;"
                   " kept: %s", hits, (double)(best * reach), (double)mid[0], (double)mid[1], (double)mid[2], e->tex,
                   e->cutout ? " (cutout)" : "", (double)e->area, g_memFrame - e->frame, (unsigned)(now - e->seen),
                   (double)e->avD, (double)e->camD, why);
        if (out) {
            out("toward the light, the memory: %d on the line; nearest %.0f off at %.0f,%.0f,%.0f, gl tex %u, area %.0f,"
                " last drawn %u ms ago", hits, (double)(best * reach), (double)mid[0], (double)mid[1], (double)mid[2],
                e->tex, (double)e->area, (unsigned)(now - e->seen));
            out("  then the Avatar was %.0f off and the camera %.0f; still kept because %s", (double)e->avD,
                (double)e->camD, why);
        }
    }
}

void u9shadow_probe_ray(const U9FrameView *v, const float L[3], const float P[3], float range, int slot,
                        float weight, int fixture, float bias, float slope, void (*line)(const char *fmt, ...))
{
    RayCtx rc;
    float dv[3], len, reach = range * 1.0f, cubeD = -1.0f;
    int k;
    const char *what;
    if (!line || !v || !v->valid) return;
    for (k = 0; k < 3; k++) dv[k] = P[k] - L[k];
    len = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    if (len < 1.0f) return;
    if (slot >= 0 && slot < U9SHADOW_MAX && g_cube[slot] && g_cubeSize > 0) reach = g_cubeReach[slot];
    memset(&rc, 0, sizeof rc);
    rc.v = v;
    memcpy(rc.L, L, sizeof rc.L);
    memcpy(rc.dv, dv, sizeof rc.dv);
    rc.tMin = 4.0f / len;
    rc.tMax = (len - 8.0f) / len;
    rc.reach = reach;
    rc.fixture = fixture;
    rc.best = 2.0f;
    u9gl_casters(ray_run, &rc);
    if (rc.best < 2.0f) {
        what = rc.fix ? "LEFT OUT as the light's fixture" : rc.culled == 1 ? "LEFT OUT: its run's box is beyond the reach" :
               rc.culled == 2 ? "LEFT OUT: its run's box is wholly behind the cube face" : "drawn";
        line("GLSHADOW ray: frame - %u caster triangles on the line; the nearest %.0f from the light (the point %.0f), run %u"
             " (%u triangles, texture %u%s): %s", rc.hits, (double)(rc.best * len), (double)len, rc.run, rc.tris, rc.tex,
             rc.cut ? ", cutout - its holes let light through" : "", what);
    } else {
        line("GLSHADOW ray: frame - nothing on the line from the light to the point (%.0f) among this frame's casters",
             (double)len);
    }
    {
        AnyCtx ac;
        memset(&ac, 0, sizeof ac);
        ac.v = v;
        memcpy(ac.L, L, sizeof ac.L);
        memcpy(ac.dv, dv, sizeof ac.dv);
        ac.tMin = rc.tMin;
        ac.tMax = rc.tMax;
        ac.best = 2.0f;
        ac.len2 = len * len;
        for (k = 0; k < NEAR_KEEP; k++) ac.nm[k].d = -1.0f;
        u9gl_all_runs(any_run, &ac);
        near_line("frame, all runs", ac.nm, len, line);
        if (ac.best < 2.0f)
            line("GLSHADOW ray: all runs - %d triangles on the line (%d of them not casters); the nearest %.0f from the light,"
                 " texture %d%s: %s", ac.hits, ac.excludedHits, (double)(ac.best * len), ac.tex, ac.back ? ", a back face" : "",
                 ac.why ? ac.why : "a caster");
        else
            line("GLSHADOW ray: all runs - nothing the frame drew lies on the line, caster or not%s",
                 g_bfOn ? "" : " (back faces off: walls turned away are not drawn)");
    }
    {
        const float *vtx = NULL;
        const int *start = NULL;
        int n = 0, i, r, bestI = -1, hits = 0, inB = 0;
        float best = 2.0f;
        if (g_mem && g_memListN) { vtx = g_memVtx; start = g_memRunStart; n = g_memListN; }
        inB = g_memSkip && g_memBFrame == u9log_frame();
        NearMiss mnm[NEAR_KEEP];
        for (r = 0; r < NEAR_KEEP; r++) mnm[r].d = -1.0f;
        for (i = 0, r = 0; i < n && vtx; i++) {
            float tt;
            const float *a = vtx + i * 9;
            if (inB && g_memSkip[i]) continue;
            while (r < g_memRuns && g_memRunStart[r + 1] <= i) r++;
            near_note(mnm, a, a + 3, a + 6, L, dv, len * len, rc.tMin, rc.tMax, r < g_memRuns ? (int)g_memRunTex[r] : 0, 0);
            if (!ray_tri(L, dv, a, a + 3, a + 6, &tt) || tt <= rc.tMin || tt >= rc.tMax) continue;
            hits++;
            if (tt < best) { best = tt; bestI = i; }
        }
        if (vtx) near_line("memory", mnm, len, line);
        if (bestI >= 0) {
            const float *a = vtx + bestI * 9;
            int fix = fixture && mem_fixture_tri(L, a, a + 3, a + 6);
            for (r = 0; r < g_memRuns && start[r + 1] <= bestI; r++) {}
            line("GLSHADOW ray: memory (%s, %d triangles) - %d on the line; the nearest %.0f from the light, texture %u%s: %s",
                 inB ? "less what the frame has on screen" : "the whole list", n, hits, (double)(best * len),
                 r < g_memRuns ? g_memRunTex[r] : 0u, r < g_memRuns && g_memRunCut[r] ? ", cutout" : "",
                 !g_shadowMem ? "not used (pool shadow memory off)" : fix ? "LEFT OUT as the light's fixture" : "drawn");
        } else {
            line("GLSHADOW ray: memory (%d triangles%s) - nothing on the line", n,
                 vtx ? (inB ? ", less what the frame has on screen" : ", the whole list") : ", none this frame");
        }
    }
    if (slot < 0 || slot >= U9SHADOW_MAX || !g_cube[slot] || g_cubeSize <= 0) {
        line("GLSHADOW ray: this light has no shadow cube this frame - it lights the point through anything");
        return;
    }
    {
        float ax = fabsf(dv[0]), ay = fabsf(dv[1]), az = fabsf(dv[2]), sc, tc, ma, s, t, val = 0.0f;
        int face, ix, iy;
        GLint prev = 0;
        if (ax >= ay && ax >= az) { face = dv[0] > 0 ? 0 : 1; ma = ax; sc = dv[0] > 0 ? -dv[2] : dv[2]; tc = -dv[1]; }
        else if (ay >= az)        { face = dv[1] > 0 ? 2 : 3; ma = ay; sc = dv[0]; tc = dv[1] > 0 ? dv[2] : -dv[2]; }
        else                      { face = dv[2] > 0 ? 4 : 5; ma = az; sc = dv[2] > 0 ? dv[0] : -dv[0]; tc = -dv[1]; }
        s = 0.5f * (sc / ma + 1.0f);
        t = 0.5f * (tc / ma + 1.0f);
        ix = (int)(s * (float)g_cubeSize);
        iy = (int)(t * (float)g_cubeSize);
        if (ix < 0) ix = 0;
        if (iy < 0) iy = 0;
        if (ix >= g_cubeSize) ix = g_cubeSize - 1;
        if (iy >= g_cubeSize) iy = g_cubeSize - 1;
        glGetIntegerv(0x8CA6, &prev);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, g_cube[slot], 0);
        glReadPixels(ix, iy, 1, 1, GL_RED_, GL_FLOAT, &val);
        fBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
        cubeD = val * reach;
        line("GLSHADOW ray: cube %d (weight %.2f, reach %.0f) holds %.0f in that direction (face %d, texel %d,%d); the point is"
             " %.0f away - %s", slot, (double)weight, (double)reach, (double)cubeD, face, ix, iy, (double)len,
             cubeD < len - (bias + len * slope) ? "IN SHADOW by the cube" : "LIT by the cube");
    }
}

#define CEN_D 7
#define CEN_H 5
static unsigned char g_cenUsed[SUN_MEM_MAX];
void u9shadow_mem_census(void (*out)(const char *fmt, ...), const char *tag, unsigned refused, unsigned sunMaps)
{
    static const float dB[CEN_D - 1] = { 500.0f, 1000.0f, 2000.0f, 3000.0f, 4000.0f, 6000.0f };
    unsigned all[CEN_D] = { 0 }, used[CEN_D] = { 0 }, hAll[CEN_H] = { 0 }, hUsed[CEN_H] = { 0 }, nUsed = 0, fresh = 0;
    int i, k, ax, ay, az, avOk, n = g_memListN, sl;
    float av[3];
    unsigned frame = u9log_frame();
    if (!g_memVtx || n <= 0) {
        out("%s memory: empty | refused %u, sun maps built %u this second", tag, refused, sunMaps);
        return;
    }
    avOk = u9avatar_position(&ax, &ay, &az);
    av[0] = (float)ax; av[1] = (float)ay; av[2] = (float)az;
    memset(g_cenUsed, 0, (size_t)n);
    for (sl = 0; sl < U9SHADOW_MAX; sl++) {
        const LampSel *s = &g_lsel[sl];
        int j;
        if (!s->valid || s->content != g_memContent || g_lselFrame[sl] != frame) continue;
        for (j = 0; j < s->n; j++) if (s->tri[j] >= 0 && s->tri[j] < n) g_cenUsed[s->tri[j]] = 1;
    }
    for (i = 0; i < n; i++) {
        const float *p = g_memVtx + i * 9;
        float c[3], d2, dz;
        int b, h;
        for (k = 0; k < 3; k++) c[k] = (p[k] + p[3 + k] + p[6 + k]) / 3.0f;
        d2 = (c[0] - av[0]) * (c[0] - av[0]) + (c[1] - av[1]) * (c[1] - av[1]);
        for (b = 0; b < CEN_D - 1 && d2 > dB[b] * dB[b]; b++) {}
        if (!avOk) b = CEN_D - 1;
        dz = c[2] - av[2];
        h = dz < -1000.0f ? 0 : dz < -300.0f ? 1 : dz <= 300.0f ? 2 : dz <= 1000.0f ? 3 : 4;
        all[b]++; hAll[h]++;
        if (g_cenUsed[i]) { used[b]++; hUsed[h]++; nUsed++; }
        if (g_memFresh && g_memFresh[i]) fresh++;
    }
    out("%s memory round the Avatar%s, under 500/1000/2000/3000/4000/6000/beyond: all %u/%u/%u/%u/%u/%u/%u,"
        " used by a lamp %u/%u/%u/%u/%u/%u/%u | held %d, used %u, drawn this pass %u", tag, avOk ? "" : " (UNKNOWN)",
        all[0], all[1], all[2], all[3], all[4], all[5], all[6],
        used[0], used[1], used[2], used[3], used[4], used[5], used[6], n, nUsed, fresh);
    out("%s memory by height from the Avatar, below 1000/300-1000 below/within 300/300-1000 above/above 1000:"
        " all %u/%u/%u/%u/%u, used by a lamp %u/%u/%u/%u/%u | refused (full) %u, sun maps built %u this second",
        tag, hAll[0], hAll[1], hAll[2], hAll[3], hAll[4], hUsed[0], hUsed[1], hUsed[2], hUsed[3], hUsed[4],
        refused, sunMaps);
}

