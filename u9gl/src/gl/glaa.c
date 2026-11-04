#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "metrics.h"
#include "glpost.h"
#include "glaa.h"

int u9glpost_running(void);
int u9avatar_position(int *x, int *y, int *z);

#define GL_FRAMEBUFFER               0x8D40
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_VERTEX_SHADER             0x8B31
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_TEXTURE0                  0x84C0
#define GL_CLAMP_TO_EDGE             0x812F
#ifndef GL_RGBA8
#define GL_RGBA8                     0x8058
#endif

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
typedef void   (APIENTRY *Uniform2fFn)(GLint, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform3fFn)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform4fFn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform1fFn)(GLint, GLfloat);
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
static Uniform2fFn              fUniform2f;
static Uniform3fFn              fUniform3f;
static Uniform4fFn              fUniform4f;
static Uniform1fFn              fUniform1f;
static ActiveTextureFn          fActiveTexture;

static int      g_on = 1;
static int      g_threshold = 125;
static int      g_subpix = 75;
static int      g_view;
static int      g_scale = 1;
static int      g_scaleRefused;
static int      g_loaded, g_failed;
static GLuint   g_prog, g_tex, g_fbo;
static GLint    g_uTex, g_uTexel, g_uK, g_uView;
static GLuint   g_resolve;
static GLint    g_uRTex, g_uRTexel, g_uRScale;
static int      g_w, g_h;
static int      g_active;
static unsigned g_frames, g_ssFrames;

static int      g_taa = 0;
static int      g_taaTried;
static int      g_taaBlend = 10;
static int      g_taaSharpen = 25;
static int      g_taaView;
static GLuint   g_taaProg, g_copyProg;
static GLint    g_tCur, g_tHist, g_tExact, g_tSize, g_tEng, g_tVp, g_tProj, g_tPproj, g_tX, g_tF, g_tU,
                g_tPX, g_tPF, g_tPU, g_tMove, g_tBlend, g_tHave, g_tView,
                g_tAvRel, g_tAvMove, g_tAvOn;
static GLint    g_cTex, g_cTexel, g_cK;
static GLuint   g_hist[2], g_histFbo[2];
static int      g_histW, g_histH, g_histCur, g_histHave;
static U9FrameView g_prevView;
static int      g_prevValid;
static float    g_prevAv[3];
static int      g_prevAvOk;
static unsigned g_taaFrames, g_taaResets, g_jitIdx;
static int      g_taaJitOn;

static const char *const k_vs =
    "#version 120\n"
    "void main() {\n"
    "    gl_Position    = gl_Vertex;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "}\n";

static const char *const k_fs =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "uniform vec3 u_k;\n"
    "uniform int  u_view;\n"
    "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "float lumaAt(vec2 p) { return luma(texture2D(u_tex, p).rgb); }\n"
    "float walk(int i) { return i < 5 ? 1.0 : i == 5 ? 1.5 : i < 9 ? 2.0 : i == 9 ? 4.0 : 8.0; }\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st, t = u_texel;\n"
    "    vec4 c = texture2D(u_tex, uv);\n"
    "    if (c.a < 0.5) {\n"
    "        gl_FragColor = vec4(u_view != 0 ? mix(c.rgb, vec3(0.1, 0.3, 1.0), 0.35) : c.rgb, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    float lM = luma(c.rgb);\n"
    "    float lN = lumaAt(uv + vec2(0.0, t.y)), lS = lumaAt(uv - vec2(0.0, t.y));\n"
    "    float lE = lumaAt(uv + vec2(t.x, 0.0)), lW = lumaAt(uv - vec2(t.x, 0.0));\n"
    "    float lMax = max(lM, max(max(lN, lS), max(lE, lW)));\n"
    "    float lMin = min(lM, min(min(lN, lS), min(lE, lW)));\n"
    "    float range = lMax - lMin;\n"
    "    if (range < max(u_k.y, lMax * u_k.x)) { gl_FragColor = vec4(c.rgb, 1.0); return; }\n"
    "    float lNE = lumaAt(uv + t), lSW = lumaAt(uv - t);\n"
    "    float lNW = lumaAt(uv + vec2(-t.x, t.y)), lSE = lumaAt(uv + vec2(t.x, -t.y));\n"
    "    float edgeH = abs(lNW + lSW - 2.0 * lW) + 2.0 * abs(lN + lS - 2.0 * lM) + abs(lNE + lSE - 2.0 * lE);\n"
    "    float edgeV = abs(lNW + lNE - 2.0 * lN) + 2.0 * abs(lW + lE - 2.0 * lM) + abs(lSW + lSE - 2.0 * lS);\n"
    "    bool horz = edgeH >= edgeV;\n"
    "    float l1 = horz ? lS : lW, l2 = horz ? lN : lE;\n"
    "    float g1 = abs(l1 - lM), g2 = abs(l2 - lM);\n"
    "    bool toward1 = g1 >= g2;\n"
    "    float gate = 0.25 * max(g1, g2);\n"
    "    float across = horz ? t.y : t.x;\n"
    "    float avg = 0.5 * (lM + (toward1 ? l1 : l2));\n"
    "    if (toward1) across = -across;\n"
    "    vec2 mid = uv + (horz ? vec2(0.0, across * 0.5) : vec2(across * 0.5, 0.0));\n"
    "    vec2 along = horz ? vec2(t.x, 0.0) : vec2(0.0, t.y);\n"
    "    vec2 p1 = mid - along, p2 = mid + along;\n"
    "    float e1 = lumaAt(p1) - avg, e2 = lumaAt(p2) - avg;\n"
    "    bool end1 = abs(e1) >= gate, end2 = abs(e2) >= gate;\n"
    "    for (int i = 1; i < 11; i++) {\n"
    "        if (end1 && end2) break;\n"
    "        float s = walk(i);\n"
    "        if (!end1) { p1 -= along * s; e1 = lumaAt(p1) - avg; end1 = abs(e1) >= gate; }\n"
    "        if (!end2) { p2 += along * s; e2 = lumaAt(p2) - avg; end2 = abs(e2) >= gate; }\n"
    "    }\n"
    "    float d1 = horz ? uv.x - p1.x : uv.y - p1.y;\n"
    "    float d2 = horz ? p2.x - uv.x : p2.y - uv.y;\n"
    "    bool near1 = d1 < d2;\n"
    "    float off = 0.5 - min(d1, d2) / max(d1 + d2, 1e-6);\n"
    "    if (((near1 ? e1 : e2) < 0.0) == (lM < avg)) off = 0.0;\n"
    "    float lAll = (2.0 * (lN + lS + lE + lW) + lNE + lNW + lSE + lSW) / 12.0;\n"
    "    float sub = clamp(abs(lAll - lM) / range, 0.0, 1.0);\n"
    "    sub = (3.0 - 2.0 * sub) * sub * sub;\n"
    "    off = max(off, sub * sub * u_k.z);\n"
    "    vec2 q = uv + (horz ? vec2(0.0, off * across) : vec2(off * across, 0.0));\n"
    "    vec3 o = texture2D(u_tex, q).rgb;\n"
    "    if (u_view != 0) o = mix(o, vec3(1.0, 0.1, 0.1), 0.5);\n"
    "    gl_FragColor = vec4(o, 1.0);\n"
    "}\n";

static const char *const k_taa =
    "#version 120\n"
    "#define AV_R 70.0\n"
    "#define AV_LO 6.0\n"
    "#define AV_HI 230.0\n"
    "#define AV_BLEND 0.2\n"
    "uniform sampler2D u_cur, u_hist, u_exact;\n"
    "uniform vec2 u_size, u_eng;\n"
    "uniform vec4 u_vp, u_proj, u_pproj;\n"
    "uniform vec3 u_X, u_F, u_U, u_pX, u_pF, u_pU, u_move;\n"
    "uniform vec3 u_avRel, u_avMove;\n"
    "uniform float u_blend;\n"
    "uniform int  u_have, u_view, u_avOn;\n"
    "bool back(vec3 d, out vec2 pp) {\n"
    "    float pf = dot(d, u_pF);\n"
    "    if (pf <= 0.01) return false;\n"
    "    vec2 pe = vec2(u_pproj.z + dot(d, u_pX) / pf * u_pproj.x, u_pproj.w - dot(d, u_pU) / pf * u_pproj.y);\n"
    "    pp = vec2(u_vp.x + pe.x / u_eng.x * u_vp.z, u_vp.y + (1.0 - pe.y / u_eng.y) * u_vp.w);\n"
    "    return pp.x >= u_vp.x && pp.y >= u_vp.y && pp.x < u_vp.x + u_vp.z && pp.y < u_vp.y + u_vp.w;\n"
    "}\n"
    "vec3 toY(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }\n"
    "vec3 toR(vec3 y) { return vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st, t = 1.0 / u_size;\n"
    "    vec4 c = texture2D(u_cur, uv);\n"
    "    if (u_have == 0 || c.a < 0.5) { gl_FragColor = vec4(c.rgb, 1.0); return; }\n"
    "    vec2 p = uv * u_size;\n"
    "    vec2 e = vec2((p.x - u_vp.x) / u_vp.z * u_eng.x, (1.0 - (p.y - u_vp.y) / u_vp.w) * u_eng.y);\n"
    "    vec2 r = vec2((e.x - u_proj.z) / u_proj.x, (u_proj.w - e.y) / u_proj.y);\n"
    "    vec3 dir = u_X * r.x + u_F + u_U * r.y;\n"
    "    float f = texture2D(u_exact, uv).r;\n"
    "    vec3 d = (f < 60.0) ? dir : u_move + dir * f;\n"
    "    vec2 pp, pa;\n"
    "    bool ok = back(d, pp);\n"
    "    vec3 a = dir * f - u_avRel;\n"
    "    bool onAv = u_avOn != 0 && f >= 60.0 && dot(a.xy, a.xy) < AV_R * AV_R && a.z > AV_LO && a.z < AV_HI;\n"
    "    bool okA = onAv && back(d - u_avMove, pa);\n"
    "    if (!ok && !okA) { gl_FragColor = vec4(u_view != 0 ? mix(c.rgb, vec3(1.0, 0.0, 0.0), 0.5) : c.rgb, 1.0); return; }\n"
    "    vec3 yc = toY(c.rgb), lo = yc, hi = yc;\n"
    "    for (int j = -1; j <= 1; j++)\n"
    "        for (int i = -1; i <= 1; i++) {\n"
    "            if (i == 0 && j == 0) continue;\n"
    "            vec3 n = toY(texture2D(u_cur, uv + vec2(float(i), float(j)) * t).rgb);\n"
    "            lo = min(lo, n); hi = max(hi, n);\n"
    "        }\n"
    "    vec3 h = toY(texture2D(u_hist, (ok ? pp : pa) / u_size).rgb);\n"
    "    if (ok && okA) {\n"
    "        vec3 ha = toY(texture2D(u_hist, pa / u_size).rgb);\n"
    "        if (dot(ha - yc, ha - yc) < dot(h - yc, h - yc)) h = ha;\n"
    "    }\n"
    "    h = clamp(h, lo, hi);\n"
    "    gl_FragColor = vec4(toR(mix(h, yc, onAv ? max(u_blend, AV_BLEND) : u_blend)), 1.0);\n"
    "}\n";

static const char *const k_copy =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "uniform float u_k;\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st;\n"
    "    vec3 c = texture2D(u_tex, uv).rgb;\n"
    "    if (u_k > 0.0) {\n"
    "        vec3 n = texture2D(u_tex, uv + vec2(u_texel.x, 0.0)).rgb + texture2D(u_tex, uv - vec2(u_texel.x, 0.0)).rgb\n"
    "               + texture2D(u_tex, uv + vec2(0.0, u_texel.y)).rgb + texture2D(u_tex, uv - vec2(0.0, u_texel.y)).rgb;\n"
    "        c = max(c + (c - 0.25 * n) * u_k, 0.0);\n"
    "    }\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static const char *const k_resolve =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "uniform int  u_scale;\n"
    "void main() {\n"
    "    vec2 base = gl_TexCoord[0].st - 0.5 * float(u_scale - 1) * u_texel;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    for (int y = 0; y < 3; y++) {\n"
    "        if (y >= u_scale) break;\n"
    "        for (int x = 0; x < 3; x++) {\n"
    "            if (x >= u_scale) break;\n"
    "            sum += texture2D(u_tex, base + vec2(float(x), float(y)) * u_texel).rgb;\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = vec4(sum / float(u_scale * u_scale), 1.0);\n"
    "}\n";

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLAA off for this session: %s - the post pass draws to the window as before", why);
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
        u9log_line("GLAA %s shader did not compile: %s", what, log);
        fDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint build_program(const char *fsrc, const char *what)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_vs, "vertex");
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fsrc, what) : 0;
    GLuint p;
    GLint  ok = 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        return 0;
    }
    p = fCreateProgram();
    fAttachShader(p, vs);
    fAttachShader(p, fs);
    fLinkProgram(p);
    fDeleteShader(vs);
    fDeleteShader(fs);
    fGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei n = 0;
        fGetProgramInfoLog(p, sizeof log - 1, &n, log);
        log[n > 0 ? n : 0] = 0;
        u9log_line("GLAA %s program did not link: %s", what, log);
        fDeleteProgram(p);
        return 0;
    }
    return p;
}

static void taa_build(void)
{
    if (g_taaTried) return;
    g_taaTried = 1;
    g_taaProg  = build_program(k_taa, "taa");
    g_copyProg = g_taaProg ? build_program(k_copy, "taa copy") : 0;
    if (!g_copyProg) {
        if (g_taaProg) fDeleteProgram(g_taaProg);
        g_taaProg = 0;
        u9log_line("GLAA *** TAA's shaders did not build - TAA off, the edge filter instead");
    } else {
        g_tCur   = fGetUniformLocation(g_taaProg, "u_cur");
        g_tHist  = fGetUniformLocation(g_taaProg, "u_hist");
        g_tExact = fGetUniformLocation(g_taaProg, "u_exact");
        g_tSize  = fGetUniformLocation(g_taaProg, "u_size");
        g_tEng   = fGetUniformLocation(g_taaProg, "u_eng");
        g_tVp    = fGetUniformLocation(g_taaProg, "u_vp");
        g_tProj  = fGetUniformLocation(g_taaProg, "u_proj");
        g_tPproj = fGetUniformLocation(g_taaProg, "u_pproj");
        g_tX     = fGetUniformLocation(g_taaProg, "u_X");
        g_tF     = fGetUniformLocation(g_taaProg, "u_F");
        g_tU     = fGetUniformLocation(g_taaProg, "u_U");
        g_tPX    = fGetUniformLocation(g_taaProg, "u_pX");
        g_tPF    = fGetUniformLocation(g_taaProg, "u_pF");
        g_tPU    = fGetUniformLocation(g_taaProg, "u_pU");
        g_tMove  = fGetUniformLocation(g_taaProg, "u_move");
        g_tBlend = fGetUniformLocation(g_taaProg, "u_blend");
        g_tHave  = fGetUniformLocation(g_taaProg, "u_have");
        g_tView  = fGetUniformLocation(g_taaProg, "u_view");
        g_tAvRel = fGetUniformLocation(g_taaProg, "u_avRel");
        g_tAvMove = fGetUniformLocation(g_taaProg, "u_avMove");
        g_tAvOn  = fGetUniformLocation(g_taaProg, "u_avOn");
        g_cTex   = fGetUniformLocation(g_copyProg, "u_tex");
        g_cTexel = fGetUniformLocation(g_copyProg, "u_texel");
        g_cK     = fGetUniformLocation(g_copyProg, "u_k");
    }
}

static int load(void)
{
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
    fUniform2f              = (Uniform2fFn)gl_proc("glUniform2f");
    fUniform3f              = (Uniform3fFn)gl_proc("glUniform3f");
    fUniform4f              = (Uniform4fFn)gl_proc("glUniform4f");
    fUniform1f              = (Uniform1fFn)gl_proc("glUniform1f");
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D ||
        !fCheckFramebufferStatus || !fCreateShader || !fShaderSource || !fCompileShader ||
        !fGetShaderiv || !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog ||
        !fCreateProgram || !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram ||
        !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform2f || !fUniform3f ||
        !fUniform4f || !fUniform1f || !fActiveTexture) {
        fail("missing GL entry points");
        return 0;
    }
    if (!(g_prog = build_program(k_fs, "filter"))) { fail("the filter shader did not build"); return 0; }
    if (!(g_resolve = build_program(k_resolve, "resolve"))) {
        fDeleteProgram(g_prog);
        g_prog = 0;
        fail("the supersampling shader did not build");
        return 0;
    }
    g_uTex    = fGetUniformLocation(g_prog, "u_tex");
    g_uTexel  = fGetUniformLocation(g_prog, "u_texel");
    g_uK      = fGetUniformLocation(g_prog, "u_k");
    g_uView   = fGetUniformLocation(g_prog, "u_view");
    g_uRTex   = fGetUniformLocation(g_resolve, "u_tex");
    g_uRTexel = fGetUniformLocation(g_resolve, "u_texel");
    g_uRScale = fGetUniformLocation(g_resolve, "u_scale");
    if (g_taa) taa_build();
    u9log_line("GLAA ready: filter threshold %d per mille, sub-pixel %d%%; supersampling %dx; TAA %s, blend %d%%, "
               "sharpen %d%%", g_threshold, g_subpix, g_scale, g_taa && !g_taaProg ? "unavailable" : g_taa ? "on" : "off (parked)",
               g_taaBlend, g_taaSharpen);
    return 1;
}

static void delete_target(void)
{
    if (g_fbo) { fDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_tex) {
        glDeleteTextures(1, &g_tex);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_tex = 0;
    }
    g_w = g_h = 0;
}

static int make_target(int w, int h)
{
    GLenum status;
    delete_target();
    glPushAttrib(GL_TEXTURE_BIT);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glPopAttrib();
    u9metrics_tex_created((unsigned)(w * h * 4));
    g_w = w; g_h = h;
    fGenFramebuffers(1, &g_fbo);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        u9log_line("GLAA target %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
        delete_target();
        fail("its target was incomplete");
        return 0;
    }
    u9log_line("GLAA target %dx%d, RGBA8", w, h);
    return 1;
}

static void delete_hist(void)
{
    int k;
    for (k = 0; k < 2; k++) {
        if (g_histFbo[k]) { fDeleteFramebuffers(1, &g_histFbo[k]); g_histFbo[k] = 0; }
        if (g_hist[k]) {
            glDeleteTextures(1, &g_hist[k]);
            u9metrics_tex_destroyed((unsigned)(g_histW * g_histH * 8));
            g_hist[k] = 0;
        }
    }
    g_histW = g_histH = 0;
    g_histHave = 0;
}

static int make_hist(int w, int h)
{
    int k;
    delete_hist();
    glPushAttrib(GL_TEXTURE_BIT);
    for (k = 0; k < 2; k++) {
        GLenum status;
        glGenTextures(1, &g_hist[k]);
        glBindTexture(GL_TEXTURE_2D, g_hist[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, 0x881A , w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        u9metrics_tex_created((unsigned)(w * h * 8));
        fGenFramebuffers(1, &g_histFbo[k]);
        fBindFramebuffer(GL_FRAMEBUFFER, g_histFbo[k]);
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_hist[k], 0);
        status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glPopAttrib();
            u9log_line("GLAA TAA history %dx%d incomplete: status 0x%04X - TAA off, the edge filter instead",
                       w, h, (unsigned)status);
            g_histW = w; g_histH = h;
            delete_hist();
            if (g_taaProg) fDeleteProgram(g_taaProg);
            g_taaProg = 0;
            return 0;
        }
    }
    glPopAttrib();
    g_histW = w; g_histH = h;
    g_histHave = 0;
    u9log_line("GLAA TAA history 2 x %dx%d, RGBA16F", w, h);
    return 1;
}

static float halton(int i, int b)
{
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= (float)b; r += f * (float)(i % b); i /= b; }
    return r;
}

static int taa_wanted(void)
{
    if (g_taa && !g_failed && load()) taa_build();
    return g_taa && !g_failed && g_loaded && g_taaProg && u9aa_scale() < 2 && u9glpost_running() &&
           u9glpost_exact_tex() != 0;
}

void u9aa_taa_jitter(int vw, int vh, float out[2])
{
    int i;
    out[0] = out[1] = 0.0f;
    g_taaJitOn = 0;
    if (vw < 2 || vh < 2 || !taa_wanted()) return;
    i = (int)(g_jitIdx++ % 8u) + 1;
    out[0] = 2.0f * (halton(i, 2) - 0.5f) / (float)vw;
    out[1] = 2.0f * (halton(i, 3) - 0.5f) / (float)vh;
    g_taaJitOn = 1;
}

static void quad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
}

static int taa_end(int w, int h)
{
    U9FrameView v;
    int next = g_histCur ^ 1, have, ax, ay, az, avOk;
    float move[3], dist2, av[3];
    if (!g_taaJitOn || g_w != w || g_h != h || !u9gl_frame_view(&v) || !v.valid) return 0;
    if ((g_histW != w || g_histH != h) && !make_hist(w, h)) return 0;
    move[0] = v.m[3] - g_prevView.m[3];
    move[1] = v.m[7] - g_prevView.m[7];
    move[2] = v.m[11] - g_prevView.m[11];
    dist2 = move[0] * move[0] + move[1] * move[1] + move[2] * move[2];
    have = g_histHave && g_prevValid && g_prevView.engineW == v.engineW && g_prevView.engineH == v.engineH &&
           dist2 < 1000.0f * 1000.0f;
    if (g_histHave && !have) g_taaResets++;
    avOk = u9avatar_position(&ax, &ay, &az);
    av[0] = (float)ax; av[1] = (float)ay; av[2] = (float)az;

    fBindFramebuffer(GL_FRAMEBUFFER, g_histFbo[next]);
    glViewport(0, 0, w, h);
    fUseProgram(g_taaProg);
    fActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, (GLuint)u9glpost_exact_tex());
    fActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, g_hist[g_histCur]);
    fActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    fUniform1i(g_tCur, 0);
    fUniform1i(g_tHist, 1);
    fUniform1i(g_tExact, 2);
    fUniform2f(g_tSize, (float)w, (float)h);
    fUniform2f(g_tEng, v.engineW, v.engineH);
    fUniform4f(g_tVp, (float)v.vx, (float)v.vy, (float)v.vw, (float)v.vh);
    fUniform4f(g_tProj, v.sx, v.sy, v.cx, v.cy);
    fUniform4f(g_tPproj, g_prevView.sx, g_prevView.sy, g_prevView.cx, g_prevView.cy);
    fUniform3f(g_tX, v.m[0], v.m[4], v.m[8]);
    fUniform3f(g_tF, v.m[1], v.m[5], v.m[9]);
    fUniform3f(g_tU, v.m[2], v.m[6], v.m[10]);
    fUniform3f(g_tPX, g_prevView.m[0], g_prevView.m[4], g_prevView.m[8]);
    fUniform3f(g_tPF, g_prevView.m[1], g_prevView.m[5], g_prevView.m[9]);
    fUniform3f(g_tPU, g_prevView.m[2], g_prevView.m[6], g_prevView.m[10]);
    fUniform3f(g_tMove, move[0], move[1], move[2]);
    fUniform3f(g_tAvRel, av[0] - v.m[3], av[1] - v.m[7], av[2] - v.m[11]);
    fUniform3f(g_tAvMove, av[0] - g_prevAv[0], av[1] - g_prevAv[1], av[2] - g_prevAv[2]);
    fUniform1i(g_tAvOn, avOk && g_prevAvOk);
    fUniform1f(g_tBlend, (float)(g_taaBlend < 1 ? 1 : g_taaBlend > 100 ? 100 : g_taaBlend) / 100.0f);
    fUniform1i(g_tHave, have);
    fUniform1i(g_tView, g_taaView);
    quad();

    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    fUseProgram(g_copyProg);
    glBindTexture(GL_TEXTURE_2D, g_hist[next]);
    fUniform1i(g_cTex, 0);
    fUniform2f(g_cTexel, 1.0f / (float)w, 1.0f / (float)h);
    fUniform1f(g_cK, (float)(g_taaSharpen < 0 ? 0 : g_taaSharpen > 200 ? 200 : g_taaSharpen) / 100.0f);
    quad();

    fActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, 0);
    fActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    fActiveTexture(GL_TEXTURE0);
    g_histCur = next;
    g_histHave = 1;
    g_prevView = v;
    g_prevValid = 1;
    g_prevAv[0] = av[0]; g_prevAv[1] = av[1]; g_prevAv[2] = av[2];
    g_prevAvOk = avOk;
    g_taaFrames++;
    return 1;
}

void u9aa_init(void)
{
    g_on        = u9cfg_int("gl_aa", 1);
    g_threshold = u9cfg_int("gl_aa_threshold", 125);
    g_subpix    = u9cfg_int("gl_aa_subpix", 75);
    g_scale     = u9cfg_int("gl_ss_scale", 1);
    if (g_scale < 1) g_scale = 1;
    if (g_scale > 3) g_scale = 3;
    g_taa        = u9cfg_int("gl_taa", 0);
    g_taaBlend   = u9cfg_int("gl_taa_blend", 10);
    g_taaSharpen = u9cfg_int("gl_taa_sharpen", 25);
}

int u9aa_scale(void)
{
    if (g_scale < 2 || g_scaleRefused || !load()) return 1;
    return g_scale;
}

void u9aa_scale_refused(const char *why)
{
    if (!g_scaleRefused)
        u9log_line("GLAA supersampling %dx off for this session: %s - the frame is drawn at the window's size",
                   g_scale, why);
    g_scaleRefused = 1;
}

int u9aa_begin(int w, int h, int scale)
{
    g_active = 0;
    if ((!g_on && !g_taaJitOn && scale < 2) || g_failed || w < 2 || h < 2 || !load()) return 0;
    if ((w != g_w || h != g_h) && !make_target(w, h)) return 0;
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, w, h);
    g_active = 1;
    return 1;
}

void u9aa_end(int w, int h)
{
    float th = (float)(g_threshold < 0 ? 0 : g_threshold > 1000 ? 1000 : g_threshold) / 1000.0f;
    float sp = (float)(g_subpix < 0 ? 0 : g_subpix > 100 ? 100 : g_subpix) / 100.0f;
    if (!g_active) return;
    if (g_taaJitOn && taa_end(w, h)) { fUseProgram(0); return; }
    g_prevValid = 0;
    if (!g_on && g_w <= w) {
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        fActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        fUseProgram(g_copyProg ? g_copyProg : g_resolve);
        if (g_copyProg) { fUniform1i(g_cTex, 0); fUniform1f(g_cK, 0.0f); }
        else { fUniform1i(g_uRTex, 0); fUniform2f(g_uRTexel, 1.0f / (float)g_w, 1.0f / (float)g_h); fUniform1i(g_uRScale, 1); }
        quad();
        fUseProgram(0);
        return;
    }
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    fActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    if (g_w > w) {
        fUseProgram(g_resolve);
        fUniform1i(g_uRTex, 0);
        fUniform2f(g_uRTexel, 1.0f / (float)g_w, 1.0f / (float)g_h);
        fUniform1i(g_uRScale, (g_w + w / 2) / w);
        g_ssFrames++;
    } else {
        fUseProgram(g_prog);
        fUniform1i(g_uTex, 0);
        fUniform2f(g_uTexel, 1.0f / (float)g_w, 1.0f / (float)g_h);
        fUniform3f(g_uK, th, 0.0312f, sp);
        fUniform1i(g_uView, g_view);
        g_frames++;
    }
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    fUseProgram(0);
}

unsigned u9aa_fbo(void)
{
    return g_active ? g_fbo : 0;
}

void u9aa_ss_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (*v >= '1' && *v <= '3' && !v[1]) {
        g_scale = *v - '0';
        u9log_line("GLAA supersampling set to %dx", g_scale);
    } else if (*v) {
        out("post ss 1|2|3: draw the frame at that many times the window's width and height, averaged down");
        return;
    }
    out("supersampling %dx%s; %u frames averaged down%s", g_scale,
        g_scale > 1 ? " (the edge filter stands down while it is on)" : " - off", g_ssFrames,
        g_scaleRefused ? " - REFUSED this session (see log)" : g_failed ? " - FAILED (see log)" : "");
}

void u9aa_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strcmp(v, "on"))                         g_on = 1;
    else if (!strcmp(v, "off"))                   g_on = 0;
    else if (!strcmp(v, "view"))                  g_view = !g_view;
    else if (!strncmp(v, "threshold ", 10))       g_threshold = atoi(v + 10);
    else if (!strncmp(v, "subpix ", 7))           g_subpix = atoi(v + 7);
    else if (*v) {
        out("post aa on|off | view | threshold <per mille of the brightest luma, 125> | subpix <percent, 75>");
        return;
    }
    if (g_threshold < 0) g_threshold = 0;
    if (g_threshold > 1000) g_threshold = 1000;
    if (g_subpix < 0) g_subpix = 0;
    if (g_subpix > 100) g_subpix = 100;
    if (*v) u9log_line("GLAA '%s' - %s, threshold %d, sub-pixel %d%%, view %s", v, g_on ? "on" : "off",
                       g_threshold, g_subpix, g_view ? "on" : "off");
    out("anti-aliasing %s: threshold %d per mille, sub-pixel %d%%; %u frames filtered%s%s",
        g_on ? "on" : "off", g_threshold, g_subpix, g_frames,
        g_view ? "; view on: smoothed red, left exact blue" : "",
        g_failed ? " - FAILED (see log)" : "");
    if (u9aa_scale() > 1) out("supersampling is on (%dx): the filter stands down while it is", u9aa_scale());
}

void u9aa_taa_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strcmp(v, "on"))                    g_taa = 1;
    else if (!strcmp(v, "off"))              { g_taa = 0; g_prevValid = 0; }
    else if (!strcmp(v, "view"))             g_taaView = !g_taaView;
    else if (!strncmp(v, "blend ", 6))       g_taaBlend = atoi(v + 6);
    else if (!strncmp(v, "sharpen ", 8))     g_taaSharpen = atoi(v + 8);
    else if (*v) {
        out("post taa on|off | blend <percent of the new frame, 10> | sharpen <percent, 25> | view (red: no history used)");
        return;
    }
    if (g_taaBlend < 1) g_taaBlend = 1;
    if (g_taaBlend > 100) g_taaBlend = 100;
    if (g_taaSharpen < 0) g_taaSharpen = 0;
    if (g_taaSharpen > 200) g_taaSharpen = 200;
    if (*v) u9log_line("GLAA TAA '%s' - %s, blend %d%%, sharpen %d%%, view %s", v, g_taa ? "on" : "off",
                       g_taaBlend, g_taaSharpen, g_taaView ? "on" : "off");
    out("TAA %s: blend %d%% of the new frame, sharpen %d%%; %u frames resolved, %u history restarts%s%s",
        g_taa ? "on" : "off", g_taaBlend, g_taaSharpen, g_taaFrames, g_taaResets,
        g_taaView ? "; view on: red where no history was used" : "",
        g_loaded && !g_taaProg ? " - UNAVAILABLE (its shaders or history failed, see the log)" : "");
    if (g_taa && u9aa_scale() > 1) out("supersampling is on (%dx): TAA stands down while it is", u9aa_scale());
    if (g_taa && g_loaded && !u9glpost_exact_tex()) out("no exact depth target this session: TAA cannot reproject");
}

void u9aa_report(void)
{
    if (g_taa || g_taaFrames)
        u9log_line("GLAA TAA totals: %u frames resolved, %u history restarts, blend %d%%, sharpen %d%%%s", g_taaFrames,
                   g_taaResets, g_taaBlend, g_taaSharpen, g_taaTried && !g_taaProg ? " - unavailable this session" : "");
    if (g_on || g_frames || g_ssFrames)
        u9log_line("GLAA totals: %u frames filtered, threshold %d, sub-pixel %d%%; %u frames supersampled %dx%s%s",
                   g_frames, g_threshold, g_subpix, g_ssFrames, g_scale,
                   g_scaleRefused ? " (refused, see above)" : "",
                   g_failed ? " - turned itself OFF (see the GLAA line above)" : "");
}

void u9aa_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_target();
        delete_hist();
        if (g_prog) fDeleteProgram(g_prog);
        if (g_resolve) fDeleteProgram(g_resolve);
        if (g_taaProg) fDeleteProgram(g_taaProg);
        if (g_copyProg) fDeleteProgram(g_copyProg);
    }
    g_hist[0] = g_hist[1] = g_histFbo[0] = g_histFbo[1] = 0;
    g_histW = g_histH = g_histHave = 0;
    g_taaProg = g_copyProg = 0;
    g_taaTried = 0;
    g_prevValid = 0;
    g_taaJitOn = 0;
    g_tex = g_fbo = g_prog = g_resolve = 0;
    g_w = g_h = 0;
    g_active = 0;
    g_loaded = 0;
    g_failed = 0;
}
