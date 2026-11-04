#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "metrics.h"
#include "glshadow.h"
#include "glbeam.h"
#include "../common/u9time.h"
#include "../fix/shrinewatch.h"
#include "glsun.h"

int u9glpost_running(void);

#define GL_FRAMEBUFFER               0x8D40
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_VERTEX_SHADER             0x8B31
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_TEXTURE0                  0x84C0
#define GL_CLAMP_TO_EDGE             0x812F
#define GL_RGBA16F                   0x881A
#define GL_FUNC_ADD                  0x8006

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
typedef void   (APIENTRY *Uniform4fFn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform3fFn)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform3fvFn)(GLint, GLsizei, const GLfloat *);
typedef void   (APIENTRY *ActiveTextureFn)(GLenum);
typedef void   (APIENTRY *BlendEquationFn)(GLenum);
typedef void   (APIENTRY *DrawBuffersFn)(GLsizei, const GLenum *);

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
static Uniform4fFn              fUniform4f;
static Uniform3fFn              fUniform3f;
static Uniform3fvFn             fUniform3fv;
static ActiveTextureFn          fActiveTexture;
static BlendEquationFn          fBlendEquation;
static DrawBuffersFn            fDrawBuffers;

#define BEAM_TEXTURES 8

static int    g_on = 1;
static int    g_tex[BEAM_TEXTURES], g_nTex;
static int    g_depth = 75;
static int    g_strength = 125;
static int    g_land = 15;
static int    g_soft = 6;
static int    g_view;

#define SHRINE_PAL    8
static int    g_sOn = 1;
static int    g_sRadius = 120;
static int    g_sTop = 20;
static int    g_sHeight = 2000;
static int    g_sDepth = 250;
static int    g_sStrength = 25;
static int    g_sDayStrength = 50;
static int    g_sPeak = 100;
static int    g_sPeakDay = 200;
static int    g_sPeakLead = 1000;
static int    g_sLand = 100;
static int    g_sStreaks = 25;
static int    g_sTurn = 25;
static int    g_sFade = 1500;
static int    g_sDelay = 2000;
static float  g_pal[9][SHRINE_PAL][3];
static int    g_nPal[9];
static const char *const VIRTUE_KEY[9] = { "", "valor", "sacrifice", "compassion", "justice",
                                           "honesty", "honor", "spirituality", "humility" };
static const char *const PAL_DEFAULT[9] = {
    "",
    "255,60,40 220,20,20 255,235,225 180,10,10 255,120,90 255,255,255",
    "255,140,30 230,100,10 255,240,220 255,170,60 200,80,0 255,255,255",
    "255,230,60 240,200,20 255,250,220 255,240,120 220,180,0 255,255,255",
    "60,230,90 20,180,60 230,255,235 120,255,140 10,140,50 255,255,255",
    "60,120,255 30,80,220 225,235,255 120,170,255 20,50,180 255,255,255",
    "170,70,255 130,40,210 240,225,255 200,130,255 100,20,170 255,255,255",
    "255,255,255 240,245,255 255,250,235 230,240,255 255,255,250 245,245,255",
    "170,170,175 120,120,128 200,200,205 90,90,96 150,150,155 210,210,215",
};
static int    g_sTest;
static GLuint g_sProg;
static GLint  g_sDepthU, g_sExactU, g_sHaveExactU, g_sDepthModeU, g_sZU, g_sProjU, g_sSizeU;
static GLint  g_sXU, g_sFU, g_sUU, g_sTopU, g_sBotU, g_sE1U, g_sE2U, g_sRadU, g_sKU, g_sStreakU, g_sPalU, g_sNPalU;
static unsigned g_sFrames;
static float  g_sLastLevel;
static int    g_sLastVirtue;

static int    g_loaded, g_failed;
static GLuint g_prog, g_fbo, g_target, g_inside;
static int    g_w, g_h;
static GLint  g_uTex, g_uDepth, g_uExact, g_uHaveExact, g_uDepthMode, g_uZ, g_uProj, g_uSize, g_uCentre;
static unsigned g_frames, g_framesDrawn, g_runs, g_tris;

static const char *const k_vs =
    "#version 120\n"
    "varying vec4 v_stq;\n"
    "varying vec2 v_e;\n"
    "varying vec3 v_col;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xy, 0.0, 1.0);\n"
    "    v_stq = gl_MultiTexCoord0;\n"
    "    v_e   = gl_Vertex.xy;\n"
    "    v_col = gl_Color.rgb;\n"
    "}\n";

static const char *const k_fs =
    "#version 120\n"
    "uniform sampler2D u_tex, u_depth, u_exact;\n"
    "uniform int  u_haveExact, u_depthMode;\n"
    "uniform vec2 u_z;\n"
    "uniform vec4 u_proj;\n"
    "uniform vec2 u_size;\n"
    "uniform vec4 u_centre;\n"
    "varying vec4 v_stq;\n"
    "varying vec2 v_e;\n"
    "varying vec3 v_col;\n"
    "void main() {\n"
    "    if (v_stq.w <= 0.0) discard;\n"
    "    vec2 uv = gl_FragCoord.xy / u_size;\n"
    "    float d = texture2D(u_depth, uv).r, ws = 1e9;\n"
    "    if (d < 1.0) {\n"
    "        ws = (u_depthMode == 0) ? u_z.x / max(1.0 - d, 1e-6) : u_z.x + d * (u_z.y - u_z.x);\n"
    "        if (u_haveExact != 0) { float e = texture2D(u_exact, uv).r; if (e > 0.0) ws = e; }\n"
    "    }\n"
    "    float f = min(1.0 / v_stq.w, ws);\n"
    "    vec2 r = vec2((v_e.x - u_proj.z) / u_proj.x, (u_proj.w - v_e.y) / u_proj.y);\n"
    "    float len = f * sqrt(1.0 + dot(r, r));\n"
    "    vec3 P = vec3(r.x, 1.0, r.y) / v_stq.w;\n"
    "    vec3 n = cross(dFdx(P), dFdy(P));\n"
    "    if (dot(n, P - u_centre.xyz) < 0.0) n = -n;\n"
    "    bool far = dot(n, P) > 0.0;\n"
    "    vec3 col = far ? texture2D(u_tex, v_stq.xy / v_stq.w).rgb * v_col : vec3(0.0);\n"
    "    gl_FragData[0] = vec4(far ? len : -len, col);\n"
    "    float behind = (1.0 / v_stq.w > ws - (4.0 + 0.01 * ws)) ? 1.0 : 0.0;\n"
    "    gl_FragData[1] = vec4(far ? behind : -behind, 0.0, 0.0, 0.0);\n"
    "}\n";

static const char *const k_svs =
    "#version 120\n"
    "varying vec2 v_e;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xy, 0.0, 1.0);\n"
    "    v_e = gl_Vertex.xy;\n"
    "}\n";

static const char *const k_sfs =
    "#version 120\n"
    "uniform sampler2D u_depth, u_exact;\n"
    "uniform int  u_haveExact, u_depthMode, u_npal;\n"
    "uniform vec2 u_z, u_size, u_rad, u_streak;\n"
    "uniform vec4 u_proj, u_k;\n"
    "uniform vec3 u_X, u_F, u_U, u_top, u_bot, u_e1, u_e2;\n"
    "uniform vec3 u_pal[8];\n"
    "varying vec2 v_e;\n"
    "float dens(vec3 P, vec3 A, vec3 a, float L, out float h, out vec3 rv) {\n"
    "    vec3 w = P - A;\n"
    "    float along = dot(w, a);\n"
    "    h = along / L;\n"
    "    rv = w - along * a;\n"
    "    if (h < 0.0 || h > 1.0) return 0.0;\n"
    "    float R = mix(u_rad.x, u_rad.y, h);\n"
    "    return (1.0 - smoothstep(0.5 * R, R, length(rv))) * smoothstep(0.0, 0.2, h);\n"
    "}\n"
    "vec3 pal(float s) {\n"
    "    float n = float(u_npal);\n"
    "    float x = mod(s, n);\n"
    "    float i0 = floor(x), f = smoothstep(0.15, 0.85, x - i0);\n"
    "    float i1 = mod(i0 + 1.0, n);\n"
    "    vec3 c0 = u_pal[0], c1 = u_pal[0];\n"
    "    for (int j = 0; j < 8; j++) {\n"
    "        if (float(j) == i0) c0 = u_pal[j];\n"
    "        if (float(j) == i1) c1 = u_pal[j];\n"
    "    }\n"
    "    return mix(c0, c1, f);\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy / u_size;\n"
    "    float d = texture2D(u_depth, uv).r, ws = 1e9;\n"
    "    if (d < 1.0) {\n"
    "        ws = (u_depthMode == 0) ? u_z.x / max(1.0 - d, 1e-6) : u_z.x + d * (u_z.y - u_z.x);\n"
    "        if (u_haveExact != 0) { float e = texture2D(u_exact, uv).r; if (e > 0.0) ws = e; }\n"
    "    }\n"
    "    vec2 r = vec2((v_e.x - u_proj.z) / u_proj.x, (u_proj.w - v_e.y) / u_proj.y);\n"
    "    vec3 dir = u_X * r.x + u_F + u_U * r.y;\n"
    "    vec3 A = u_top, ax = u_bot - u_top;\n"
    "    float L = length(ax);\n"
    "    vec3 a = ax / L;\n"
    "    float Rm = max(u_rad.x, u_rad.y);\n"
    "    vec3 dp = dir - dot(dir, a) * a, wp = -A + dot(A, a) * a;\n"
    "    float qa = dot(dp, dp), qb = dot(dp, wp), qc = dot(wp, wp) - Rm * Rm;\n"
    "    float disc = qb * qb - qa * qc;\n"
    "    float len = 0.0, inside = 0.0;\n"
    "    vec3 col = vec3(0.0);\n"
    "    if (disc > 0.0 && qa > 1e-9) {\n"
    "        float s = sqrt(disc);\n"
    "        float t0 = max((-qb - s) / qa, 0.0), t1 = min((-qb + s) / qa, ws);\n"
    "        if (t1 > t0) {\n"
    "            float dt = (t1 - t0) / 24.0, dl = dt * length(dir);\n"
    "            for (int i = 0; i < 24; i++) {\n"
    "                float h; vec3 rv;\n"
    "                float k = dens(dir * (t0 + (float(i) + 0.5) * dt), A, a, L, h, rv);\n"
    "                if (k <= 0.0) continue;\n"
    "                float phi = atan(dot(rv, u_e2), dot(rv, u_e1));\n"
    "                vec3 c = pal(phi / 6.2831853 * u_streak.x + u_streak.y + h * 0.6);\n"
    "                len += k * dl;\n"
    "                col += k * dl * c;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (len > 0.0) col /= len;\n"
    "    if (d < 1.0) {\n"
    "        float h; vec3 rv;\n"
    "        inside = dens(dir * ws, A, a, L, h, rv);\n"
    "        if (inside > 0.0 && len <= 0.0) {\n"
    "            float phi = atan(dot(rv, u_e2), dot(rv, u_e1));\n"
    "            col = pal(phi / 6.2831853 * u_streak.x + u_streak.y + h * 0.6);\n"
    "        }\n"
    "    }\n"
    "    gl_FragData[0] = vec4(len * u_k.x, col * u_k.y);\n"
    "    gl_FragData[1] = vec4(inside * u_k.z, 0.0, 0.0, 0.0);\n"
    "}\n";

static void pal_read(int v, const char *s)
{
    const char *p = s;
    int n = 0;
    while (*p && n < SHRINE_PAL) {
        int c[3] = { 0, 0, 0 }, k;
        while (*p == ' ') p++;
        if (!*p) break;
        for (k = 0; k < 3; k++) {
            c[k] = atoi(p);
            while (*p && *p != ',' && *p != ' ') p++;
            if (*p == ',') p++;
        }
        for (k = 0; k < 3; k++) g_pal[v][n][k] = (float)(c[k] < 0 ? 0 : c[k] > 255 ? 255 : c[k]) / 255.0f;
        n++;
    }
    g_nPal[v] = n;
}

static void list_read(const char *s)
{
    const char *p = s;
    g_nTex = 0;
    while (*p && g_nTex < BEAM_TEXTURES) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        g_tex[g_nTex] = atoi(p);
        if (g_tex[g_nTex] > 0) g_nTex++;
        while (*p && *p != ' ' && *p != ',') p++;
    }
}

static int listed(int texId)
{
    int i;
    for (i = 0; i < g_nTex; i++)
        if (g_tex[i] == texId) return 1;
    return 0;
}

void u9beam_init(void)
{
    g_on       = u9cfg_int("gl_beam_volume", 1);
    list_read(u9cfg_str("gl_beam_textures", "1760"));
    g_depth    = u9cfg_int("gl_beam_depth", 75);
    g_strength = u9cfg_int("gl_beam_strength", 125);
    g_land     = u9cfg_int("gl_beam_land", 15);
    g_soft     = u9cfg_int("gl_beam_soft", 6);
    g_sOn       = u9cfg_int("gl_shrine_beam", 1);
    g_sRadius   = u9cfg_int("gl_shrine_beam_radius", 120);
    g_sTop      = u9cfg_int("gl_shrine_beam_top", 20);
    g_sHeight   = u9cfg_int("gl_shrine_beam_height", 2000);
    g_sDepth    = u9cfg_int("gl_shrine_beam_depth", 250);
    g_sStrength = u9cfg_int("gl_shrine_beam_strength", 25);
    g_sDayStrength = u9cfg_int("gl_shrine_beam_day_strength", 50);
    g_sPeak     = u9cfg_int("gl_shrine_beam_peak_strength", 100);
    g_sPeakDay  = u9cfg_int("gl_shrine_beam_peak_day_strength", 200);
    g_sPeakLead = u9cfg_int("gl_shrine_beam_peak_lead", 1000);
    g_sLand     = u9cfg_int("gl_shrine_beam_land", 100);
    g_sStreaks  = u9cfg_int("gl_shrine_beam_streaks", 25);
    g_sTurn     = u9cfg_int("gl_shrine_beam_turn", 25);
    g_sFade     = u9cfg_int("gl_shrine_beam_fade", 1500);
    g_sDelay    = u9cfg_int("gl_shrine_beam_delay", 2000);
    {
        const char *s[9];
        int v;
        s[1] = u9cfg_str("gl_shrine_beam_valor", PAL_DEFAULT[1]);
        s[2] = u9cfg_str("gl_shrine_beam_sacrifice", PAL_DEFAULT[2]);
        s[3] = u9cfg_str("gl_shrine_beam_compassion", PAL_DEFAULT[3]);
        s[4] = u9cfg_str("gl_shrine_beam_justice", PAL_DEFAULT[4]);
        s[5] = u9cfg_str("gl_shrine_beam_honesty", PAL_DEFAULT[5]);
        s[6] = u9cfg_str("gl_shrine_beam_honor", PAL_DEFAULT[6]);
        s[7] = u9cfg_str("gl_shrine_beam_spirituality", PAL_DEFAULT[7]);
        s[8] = u9cfg_str("gl_shrine_beam_humility", PAL_DEFAULT[8]);
        for (v = 1; v <= 8; v++) {
            pal_read(v, s[v]);
            if (!g_nPal[v]) pal_read(v, PAL_DEFAULT[v]);
        }
    }
}

int u9beam_hides(int texId)
{
    return g_on && !g_failed && listed(texId) && u9glpost_running();
}

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLBEAM off for this session: %s - the game's shafts are drawn as sent", why);
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
        u9log_line("GLBEAM %s shader did not compile: %s", what, log);
        fDeleteShader(s);
        return 0;
    }
    return s;
}

static void shrine_load(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_svs, "shrine vertex");
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, k_sfs, "shrine fragment") : 0;
    GLint ok = 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        u9log_line("GLBEAM *** the shrine beam's shaders did not build - shrine beam off");
        return;
    }
    g_sProg = fCreateProgram();
    fAttachShader(g_sProg, vs);
    fAttachShader(g_sProg, fs);
    fLinkProgram(g_sProg);
    fDeleteShader(vs);
    fDeleteShader(fs);
    fGetProgramiv(g_sProg, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        GLsizei n = 0;
        fGetProgramInfoLog(g_sProg, sizeof log - 1, &n, log);
        log[n > 0 ? n : 0] = 0;
        u9log_line("GLBEAM *** the shrine beam's program did not link: %s - shrine beam off", log);
        fDeleteProgram(g_sProg);
        g_sProg = 0;
        return;
    }
    g_sDepthU     = fGetUniformLocation(g_sProg, "u_depth");
    g_sExactU     = fGetUniformLocation(g_sProg, "u_exact");
    g_sHaveExactU = fGetUniformLocation(g_sProg, "u_haveExact");
    g_sDepthModeU = fGetUniformLocation(g_sProg, "u_depthMode");
    g_sZU         = fGetUniformLocation(g_sProg, "u_z");
    g_sProjU      = fGetUniformLocation(g_sProg, "u_proj");
    g_sSizeU      = fGetUniformLocation(g_sProg, "u_size");
    g_sXU         = fGetUniformLocation(g_sProg, "u_X");
    g_sFU         = fGetUniformLocation(g_sProg, "u_F");
    g_sUU         = fGetUniformLocation(g_sProg, "u_U");
    g_sTopU       = fGetUniformLocation(g_sProg, "u_top");
    g_sBotU       = fGetUniformLocation(g_sProg, "u_bot");
    g_sE1U        = fGetUniformLocation(g_sProg, "u_e1");
    g_sE2U        = fGetUniformLocation(g_sProg, "u_e2");
    g_sRadU       = fGetUniformLocation(g_sProg, "u_rad");
    g_sKU         = fGetUniformLocation(g_sProg, "u_k");
    g_sStreakU    = fGetUniformLocation(g_sProg, "u_streak");
    g_sPalU       = fGetUniformLocation(g_sProg, "u_pal");
    g_sNPalU      = fGetUniformLocation(g_sProg, "u_npal");
    u9log_line("GLBEAM shrine beam ready: radius %d at the feet, %d at the top, %d above the shrine, "
               "depth %d, strength %d%%", g_sRadius, g_sTop, g_sHeight, g_sDepth, g_sStrength);
}

static float g_sDayK;
static float g_sLastPeak;
static float shrine_strength(void)
{
    U9Sun sun;
    float k = 0.0f;
    if (g_sDayStrength > 0 && u9sun_read(&sun) && !sun.underground && !sun.moonMap) {
        k = (sun.elevation + 5.0f) / 5.0f;
        k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
        k = k * k * (3.0f - 2.0f * k);
    }
    g_sDayK = k;
    {
        float day = g_sDayStrength > 0 ? (float)g_sDayStrength : (float)g_sStrength;
        float peakDay = g_sPeakDay > 0 ? (float)g_sPeakDay : (float)g_sPeak;
        float now = (float)g_sStrength + (day - (float)g_sStrength) * k;
        float peak = (float)g_sPeak + (peakDay - (float)g_sPeak) * k;
        g_sLastPeak = u9shrine_beam_peak(g_sPeakLead);
        return now + (peak - now) * g_sLastPeak;
    }
}

static int shrine_draw(const U9FrameView *v, unsigned depthTex, unsigned exactTex, int w, int h)
{
    int virtue = 0, k;
    float base[3], foot[3], level = 0.0f, top[3], bot[3], eye[3], ax[3], len, e1[3], e2[3], n;
    float baseDepth = (float)(g_depth > 1 ? g_depth : 100), baseStrength = (float)(g_strength > 0 ? g_strength : 100);
    float baseLand = (float)(g_land > 0 ? g_land : 100);
    static DWORD t0;
    DWORD now = u9time_now();
    if (!g_sOn || !g_sProg) return 0;
    u9shrine_beam_test(g_sTest);
    if (!u9shrine_beam(g_sFade, g_sDelay, &virtue, base, foot, &level) || level <= 0.0f || virtue < 1 || virtue > 8 ||
        !g_nPal[virtue])
        return 0;
    if (!t0) t0 = now;
    g_sLastLevel = level;
    g_sLastVirtue = virtue;
    eye[0] = v->m[3]; eye[1] = v->m[7]; eye[2] = v->m[11];
    for (k = 0; k < 3; k++) { top[k] = base[k] - eye[k]; bot[k] = foot[k] - eye[k]; }
    top[2] += (float)g_sHeight;
    bot[2] -= 60.0f;
    for (k = 0; k < 3; k++) ax[k] = bot[k] - top[k];
    len = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
    if (len < 1.0f) return 0;
    for (k = 0; k < 3; k++) ax[k] /= len;
    e1[0] = 0.0f; e1[1] = -ax[2]; e1[2] = ax[1];
    if (fabsf(ax[0]) > 0.9f) { e1[0] = ax[2]; e1[1] = 0.0f; e1[2] = -ax[0]; }
    n = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
    for (k = 0; k < 3; k++) e1[k] /= n;
    e2[0] = ax[1] * e1[2] - ax[2] * e1[1];
    e2[1] = ax[2] * e1[0] - ax[0] * e1[2];
    e2[2] = ax[0] * e1[1] - ax[1] * e1[0];

    fUseProgram(g_sProg);
    fUniform1i(g_sDepthU, 1);
    fUniform1i(g_sExactU, 2);
    fUniform1i(g_sHaveExactU, exactTex ? 1 : 0);
    fUniform1i(g_sDepthModeU, v->depthMode);
    fUniform2f(g_sZU, v->zRef, v->zFar);
    fUniform4f(g_sProjU, v->sx, v->sy, v->cx, v->cy);
    fUniform2f(g_sSizeU, (float)w, (float)h);
    fUniform3f(g_sXU, v->m[0], v->m[4], v->m[8]);
    fUniform3f(g_sFU, v->m[1], v->m[5], v->m[9]);
    fUniform3f(g_sUU, v->m[2], v->m[6], v->m[10]);
    fUniform3f(g_sTopU, top[0], top[1], top[2]);
    fUniform3f(g_sBotU, bot[0], bot[1], bot[2]);
    fUniform3f(g_sE1U, e1[0], e1[1], e1[2]);
    fUniform3f(g_sE2U, e2[0], e2[1], e2[2]);
    fUniform2f(g_sRadU, (float)g_sTop, (float)g_sRadius);
    fUniform4f(g_sKU, baseDepth / (float)(g_sDepth > 1 ? g_sDepth : 1),
               level * shrine_strength() / baseStrength, level * (float)g_sLand / baseLand, 0.0f);
    fUniform2f(g_sStreakU, (float)(g_sStreaks > 0 ? g_sStreaks : 1),
               (float)((now - t0) % 3600000u) * 0.001f * (float)g_sTurn * 0.0174533f / 6.2831853f *
                   (float)(g_sStreaks > 0 ? g_sStreaks : 1));
    fUniform3fv(g_sPalU, SHRINE_PAL, &g_pal[virtue][0][0]);
    fUniform1i(g_sNPalU, g_nPal[virtue]);
    (void)depthTex;
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(v->engineW, 0.0f);
    glVertex2f(v->engineW, v->engineH);
    glVertex2f(0.0f, v->engineH);
    glEnd();
    g_sFrames++;
    return 1;
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
    fUniform2f              = (Uniform2fFn)gl_proc("glUniform2f");
    fUniform4f              = (Uniform4fFn)gl_proc("glUniform4f");
    fUniform3f              = (Uniform3fFn)gl_proc("glUniform3f");
    fUniform3fv             = (Uniform3fvFn)gl_proc("glUniform3fv");
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    fBlendEquation          = (BlendEquationFn)gl_proc("glBlendEquation");
    fDrawBuffers            = (DrawBuffersFn)gl_proc("glDrawBuffers");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D ||
        !fCheckFramebufferStatus || !fCreateShader || !fShaderSource || !fCompileShader ||
        !fGetShaderiv || !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog ||
        !fCreateProgram || !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram ||
        !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform2f || !fUniform4f ||
        !fActiveTexture || !fBlendEquation || !fDrawBuffers || !fUniform3f || !fUniform3fv) {
        fail("missing GL entry points");
        return 0;
    }
    vs = compile(GL_VERTEX_SHADER, k_vs, "vertex");
    fs = vs ? compile(GL_FRAGMENT_SHADER, k_fs, "fragment") : 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        fail("the beam shaders did not build");
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
        u9log_line("GLBEAM program did not link: %s", log);
        fDeleteProgram(g_prog);
        g_prog = 0;
        fail("the beam program did not link");
        return 0;
    }
    g_uTex       = fGetUniformLocation(g_prog, "u_tex");
    g_uDepth     = fGetUniformLocation(g_prog, "u_depth");
    g_uExact     = fGetUniformLocation(g_prog, "u_exact");
    g_uHaveExact = fGetUniformLocation(g_prog, "u_haveExact");
    g_uDepthMode = fGetUniformLocation(g_prog, "u_depthMode");
    g_uZ         = fGetUniformLocation(g_prog, "u_z");
    g_uProj      = fGetUniformLocation(g_prog, "u_proj");
    g_uSize      = fGetUniformLocation(g_prog, "u_size");
    g_uCentre    = fGetUniformLocation(g_prog, "u_centre");
    shrine_load();
    u9log_line("GLBEAM ready: textures %d%s, depth %d, strength %d%%", g_nTex ? g_tex[0] : 0,
               g_nTex > 1 ? " and more" : "", g_depth, g_strength);
    return 1;
}

static void delete_target(void)
{
    if (g_fbo) { fDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_target) {
        glDeleteTextures(1, &g_target);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 8));
        g_target = 0;
    }
    if (g_inside) {
        glDeleteTextures(1, &g_inside);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 8));
        g_inside = 0;
    }
    g_w = g_h = 0;
}

static GLuint make_texture(int w, int h)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    u9metrics_tex_created((unsigned)(w * h * 8));
    return t;
}

static int make_target(int w, int h)
{
    GLenum status;
    delete_target();
    glPushAttrib(GL_TEXTURE_BIT);
    g_target = make_texture(w, h);
    g_inside = make_texture(w, h);
    glPopAttrib();
    g_w = w; g_h = h;
    fGenFramebuffers(1, &g_fbo);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_target, 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1, GL_TEXTURE_2D, g_inside, 0);
    {
        static const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT0 + 1 };
        fDrawBuffers(2, bufs);
    }
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        u9log_line("GLBEAM target %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
        delete_target();
        fail("its target was incomplete");
        return 0;
    }
    u9log_line("GLBEAM target %dx%d, RGBA16F", w, h);
    return 1;
}

static void beam_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    const U9FrameView *v = (const U9FrameView *)ctx;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    double c[3] = { 0.0, 0.0, 0.0 };
    unsigned t, n = 0;
    (void)cutout;
    for (t = first; t < first + count; t++) {
        const float *p = (const float *)(base + (size_t)t * (size_t)stride);
        float w;
        if (p[6] <= 0.0f) continue;
        w = 1.0f / p[6];
        c[0] += (p[0] - v->cx) * w / v->sx;
        c[1] += w;
        c[2] += (v->cy - p[1]) * w / v->sy;
        n++;
    }
    if (!n) return;
    fUniform4f(g_uCentre, (float)(c[0] / n), (float)(c[1] / n), (float)(c[2] / n), 0.0f);
    glBindTexture(GL_TEXTURE_2D, glTex);
    glDrawArrays(GL_TRIANGLES, (GLint)first, (GLsizei)count);
    g_runs++;
    g_tris += count / 3;
}

unsigned u9beam_build(const U9FrameView *v, unsigned depthTex, unsigned exactTex, int w, int h,
                      unsigned *inside, float k[4])
{
    int stride, i, shrine = 0;
    GLint prevFbo = 0;
    const char *base;
    k[0] = k[1] = k[2] = k[3] = 0.0f;
    *inside = 0;
    if (!(g_on && g_nTex) && !g_sOn) return 0;
    if (g_failed || !v || !v->valid || w < 2 || h < 2 || !load()) return 0;
    g_frames++;
    g_runs = g_tris = 0;
    if ((w != g_w || h != g_h) && !make_target(w, h)) return 0;
    glGetIntegerv(0x8CA6 , &prevFbo);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(v->vx, v->vy, v->vw, v->vh);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)v->engineW, (double)v->engineH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    fBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    fUseProgram(g_prog);
    fActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)depthTex);
    fActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, (GLuint)exactTex);
    fActiveTexture(GL_TEXTURE0);
    fUniform1i(g_uTex, 0);
    fUniform1i(g_uDepth, 1);
    fUniform1i(g_uExact, 2);
    fUniform1i(g_uHaveExact, exactTex ? 1 : 0);
    fUniform1i(g_uDepthMode, v->depthMode);
    fUniform2f(g_uZ, v->zRef, v->zFar);
    fUniform4f(g_uProj, v->sx, v->sy, v->cx, v->cy);
    fUniform2f(g_uSize, (float)w, (float)h);
    base = (const char *)u9gl_caster_vertices(&stride);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, base);
    glTexCoordPointer(4, GL_FLOAT, stride, base + 12);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, base + 32);
    if (g_on && g_strength > 0)
        for (i = 0; i < g_nTex; i++)
            u9gl_runs_with_texture(g_tex[i], beam_run, (void *)v);
    shrine = shrine_draw(v, depthTex, exactTex, w, h);
    fUseProgram(0);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    fBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glPopClientAttrib();
    glPopAttrib();
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!g_runs && !shrine) return 0;
    if (g_runs) g_framesDrawn++;
    k[0] = 1.0f / (float)(g_depth > 1 ? g_depth : 100);
    k[1] = (float)(g_strength > 0 ? g_strength : 100) / 100.0f;
    k[2] = (float)(g_land > 0 ? g_land : 100) / 100.0f;
    k[3] = g_view ? 1.0f : 0.0f;
    *inside = g_inside;
    return g_target;
}

void u9beam_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strncmp(v, "shrine", 6) && (v[6] == 0 || v[6] == ' ')) {
        static const struct { const char *word; int *val; } NUM[] = {
            { "radius ", &g_sRadius }, { "top ", &g_sTop }, { "height ", &g_sHeight }, { "depth ", &g_sDepth },
            { "strength ", &g_sStrength }, { "day ", &g_sDayStrength }, { "land ", &g_sLand }, { "streaks ", &g_sStreaks },
            { "turn ", &g_sTurn }, { "fade ", &g_sFade }, { "delay ", &g_sDelay },
            { "peak ", &g_sPeak }, { "peakday ", &g_sPeakDay }, { "lead ", &g_sPeakLead },
        };
        const char *s = v + 6;
        int i, done = !*s;
        while (*s == ' ') s++;
        if (!strcmp(s, "on")) { g_sOn = 1; done = 1; }
        else if (!strcmp(s, "off")) { g_sOn = 0; done = 1; }
        else if (!strcmp(s, "test off")) { g_sTest = 0; done = 1; }
        for (i = 0; !done && i < (int)(sizeof NUM / sizeof NUM[0]); i++)
            if (!strncmp(s, NUM[i].word, strlen(NUM[i].word))) { *NUM[i].val = atoi(s + strlen(NUM[i].word)); done = 1; }
        for (i = 1; !done && i <= 8; i++)
            if (!strcmp(s, VIRTUE_KEY[i])) { g_sTest = i; done = 1; }
        if (!done) {
            out("beam shrine | on|off | <virtue> (a test beam over the Avatar) | test off | radius <units at the feet>"
                " | top <units> | height <above the shrine> | depth <units for 63%%> | strength <percent>"
                " | day <percent with the sun up, 0 none>"
                " | land <percent> | streaks <n> | turn <degrees a second> | fade <ms> | delay <ms after the lift starts>"
                " | peak <percent at the strike> | peakday <the same, sun up> | lead <ms before the strike>");
            return;
        }
        if (*s) u9log_line("BEAM shrine '%s' - %s, test %s, radius %d, top %d, height %d, depth %d, strength %d%%, land %d%%,"
                           " streaks %d, turn %d, fade %d", s, g_sOn ? "on" : "off", g_sTest ? VIRTUE_KEY[g_sTest] : "off",
                           g_sRadius, g_sTop, g_sHeight, g_sDepth, g_sStrength, g_sLand, g_sStreaks, g_sTurn, g_sFade);
        out("shrine beam %s%s%s: radius %d at the feet, %d at the top, %d above the shrine, depth %d, strength %d%%, land %d%%,"
            " %d streaks turning %d deg/s, fade %d ms, on %d ms after the lift starts", g_sOn ? "on" : "off (beam shrine on)",
            g_sTest ? ", TEST " : "", g_sTest ? VIRTUE_KEY[g_sTest] : "", g_sRadius, g_sTop, g_sHeight, g_sDepth,
            g_sStrength, g_sLand, g_sStreaks, g_sTurn, g_sFade, g_sDelay);
        out("day strength %d%%%s; now %.0f%% (%.0f%% of the way to it, by the sun's height)", g_sDayStrength,
            g_sDayStrength > 0 ? "" : " (off)", (double)shrine_strength(), (double)(g_sDayK * 100.0f));
        out("at the strike %d%%, %d%% with the sun up, from %d ms before it; now %.0f%% of the way",
            g_sPeak, g_sPeakDay, g_sPeakLead, (double)(g_sLastPeak * 100.0f));
        out("drawn in %u frames%s; last: %s at level %.2f", g_sFrames, g_sProg ? "" : " - its program did not build, see the log",
            g_sLastVirtue ? VIRTUE_KEY[g_sLastVirtue] : "none", (double)g_sLastLevel);
        return;
    }
    if (!strcmp(v, "on")) g_on = 1;
    else if (!strcmp(v, "off")) g_on = 0;
    else if (!strncmp(v, "depth ", 6)) g_depth = atoi(v + 6);
    else if (!strncmp(v, "strength ", 9)) g_strength = atoi(v + 9);
    else if (!strncmp(v, "land ", 5)) g_land = atoi(v + 5);
    else if (!strcmp(v, "view")) g_view = !g_view;
    else if (!strncmp(v, "soft ", 5)) g_soft = atoi(v + 5);
    else if (!strncmp(v, "textures ", 9)) list_read(v + 9);
    else if (*v) {
        out("beam | beam on|off | depth <units> | strength <percent> | land <percent> | soft <pixels> | view | textures <ids ...>"
            " | shrine ... (the shrine beam) (post beam: the shaft's runs, as sent)");
        return;
    }
    if (*v) u9log_line("BEAM '%s' - %s, depth %d, strength %d%%, land %d%%, soft %d", v, g_on ? "on" : "off", g_depth,
                       g_strength, g_land, g_soft);
    out("beam volume %s%s: depth %d (units for 63%% of the light), strength %d%%, land %d%%, soft %d px, textures %d%s",
        g_on ? "on" : "off (beam on)", g_failed ? " - FAILED, see the log" : !u9glpost_running() ? " - needs the post pass" : "",
        g_depth, g_strength, g_land, g_soft, g_nTex ? g_tex[0] : 0, g_nTex > 1 ? " and more" : "");
    out("last frame: %u runs, %u triangles; drawn in %u of %u frames", g_runs, g_tris, g_framesDrawn, g_frames);
}

void u9beam_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_target();
        if (g_prog) fDeleteProgram(g_prog);
        if (g_sProg) fDeleteProgram(g_sProg);
    }
    g_sProg = 0;
    g_fbo = g_target = g_inside = 0;
    g_prog = 0;
    g_w = g_h = 0;
    g_loaded = 0;
    g_failed = 0;
}

int u9beam_soft(void)
{
    return g_soft > 0 ? g_soft : 0;
}
