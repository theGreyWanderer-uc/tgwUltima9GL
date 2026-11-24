#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "metrics.h"
#include "glpost.h"
#include "idlist.h"
#include "../common/lifecycle.h"
#include "../fix/lightwatch.h"
#include "glspaces.h"
#include "glshadow.h"
#include "glbackface.h"
#include "glanimdraw.h"
void u9gl_back_face_stats(unsigned *tris, unsigned *runs, unsigned *dropped, unsigned *polys);
void u9gl_sort_layers_command(const char *v, void (*out)(const char *fmt, ...));
void u9gl_sort_effects_command(const char *v, void (*out)(const char *fmt, ...));
void u9gl_water_cover_command(const char *v, void (*out)(const char *fmt, ...));
int  u9gl_screen_washes(float (*out)[8], int max);
extern int g_guardPct;
void u9gl_guard_stats(unsigned *tris);
#include "glbloom.h"
#include "glbeam.h"
#include "glrays.h"
#include "glao.h"
#include "glreflect.h"
#include "gleffects.h"
#include "glsun.h"
#include "glfog.h"
#include "glunder.h"
#include "glaa.h"
#include "glterrain.h"
#include "../fix/gold.h"
#include "regions.h"
#include "../common/u9time.h"

float u9glslots_gamma(void);

#define GL_FRAMEBUFFER              0x8D40
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5
#define GL_DEPTH24_STENCIL8         0x88F0
#define GL_DEPTH_STENCIL            0x84F9
#define GL_UNSIGNED_INT_24_8        0x84FA
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_VERTEX_SHADER            0x8B31
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE1                 0x84C1
#define GL_TEXTURE2                 0x84C2
#define GL_TEXTURE3                 0x84C3
#define GL_TEXTURE4                 0x84C4
#define GL_TEXTURE_CUBE_MAP_        0x8513
#define GL_RGBA32F                  0x8814
#define GL_COLOR_ATTACHMENT1        0x8CE1
#define GL_COLOR_ATTACHMENT2        0x8CE2
#define GL_TEXTURE12                0x84CC
#define GL_TEXTURE13                0x84CD
#define GL_TEXTURE14                0x84CE
#define GL_TEXTURE15                0x84CF
#define GL_COLOR_ATTACHMENT3        0x8CE3
#define GL_MAX_DRAW_BUFFERS         0x8824
#define SUN_ROOMS (g_sunRange < 4000.0f ? g_sunRange : 4000.0f)
#define MAX_POOL_LIGHTS            128

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
typedef void   (APIENTRY *Uniform3fFn)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *Uniform4fFn)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *ActiveTextureFn)(GLenum);
typedef void   (APIENTRY *DrawBuffersFn)(GLsizei, const GLenum *);
typedef void   (APIENTRY *Uniform4fvFn)(GLint, GLsizei, const GLfloat *);
typedef void   (APIENTRY *Uniform1fvFn)(GLint, GLsizei, const GLfloat *);
typedef void   (APIENTRY *Uniform2fvFn)(GLint, GLsizei, const GLfloat *);

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
static Uniform3fFn              fUniform3f;
static Uniform4fFn              fUniform4f;
static ActiveTextureFn          fActiveTexture;
static DrawBuffersFn            fDrawBuffers;
static Uniform4fvFn             fUniform4fv;
static Uniform1fvFn             fUniform1fv;
static Uniform2fvFn             fUniform2fv;

static int    g_want;
static int    g_wantGamma;
static int    g_failed;
static int    g_loaded;
static GLuint g_fbo, g_colour, g_depth, g_albedo, g_glow, g_bloomSrc, g_prog;
static GLuint g_exact;
static GLuint g_sky;
static GLuint g_waterTex;
static int    g_waterOn = 1;
static int    g_waterStrength = 100;
static int    g_waterDist = 8000;
static int    g_waterSteps = 32;
static int    g_waterView;
static int    g_waterFloor = 25;
static int    g_waterMarked;
static unsigned g_waterFrames;
typedef void (APIENTRY *ColorMaskiFn)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
static ColorMaskiFn fColorMaski;
static GLint  g_uExact, g_uHaveExact, g_uDepthView;
static int    g_depthView;
static GLint  g_uNrmAcross;
static int    g_nrmAcross = 1;
static GLint  g_uSeams;
static int    g_seams = 0;
static GLint  g_uAa;
static GLint  g_uFrame, g_uInvGamma, g_uDepth, g_uGrid, g_uDepthMode, g_uVp, g_uEngine,
              g_uProj, g_uZ, g_uM0, g_uM1, g_uM2;
static int    g_grid;
static int    g_pools;
static int    g_replace = 1;
static int    g_march;
static float  g_strength = 1.0f;
static int    g_replaced;
static int    g_relightWanted;
static int    g_saveSafe;
static unsigned g_savedSwitch = 1u;
static unsigned g_poolFrames, g_poolLightsMax;
static GLint  g_uAlbedo, g_uPools, g_uMarch, g_uStrength, g_uNl, g_uLp, g_uLc, g_uSurf,
              g_uPoolDebug;
static int    g_poolDebug;
static int    g_contain = 1;
static float  g_warmth = 1.0f;
static int    g_soft;
static GLint  g_uWarmth, g_uSoft;
static int    g_keepOut = 1;
static int    g_tail = 2;
static float  g_tailLevel = 0.25f;
static float  g_tailReach = 1.75f;
static GLint  g_uTail;
static int    g_flickerRange = 5000;
static int    g_flickerSmooth = 150;
static int    g_rangeHeld, g_rangeEngine = 500, g_rangeWritten;
static float  g_shadowBias = 6.0f;
static float  g_shadowStrength = 1.0f;
static float  g_shadowSoft = 0.55f;
static float  g_shadowSlope = 0.01f;
static GLint  g_uSh[U9SHADOW_MAX], g_uLsh, g_uShadowK;
static unsigned g_shadowRec, g_shadowChanges;
static struct { unsigned rec; float w; int held; } g_sh[U9SHADOW_MAX];
static unsigned g_shadowHeld;
static int    g_night = 100;
static int    g_nightDark = 35;
static int    g_nightCool = 50;
static int    g_nightGrey = 20;
static int    g_nightKeep = 50;
static char   g_nightGradedList[256];
static int    g_dungeon = 1;
static int    g_dungeonDark = 80;
static int    g_dungeonCool = 200;
static int    g_dungeonGrey = 0;
static int    g_dungeonNow = -1;
static float  g_nightNow = -1.0f;
static GLint  g_uNight, g_uNightKeep, g_uGlow, g_uGlowView, g_uBloom, g_uBloomK;
static GLint  g_uBeam, g_uBeamIn, g_uBeamK, g_uBeamS;
static GLint  g_uRays, g_uRaysK, g_uRaysAir;
static GLint  g_uAo, g_uAoK;
static GLint  g_uWater, g_uWaterK;
static GLint  g_uRefl, g_uReflOn;
static GLint  g_uWaterFloor;
static GLint  g_uWash[4];
static GLint  g_uFogK, g_uFogBox, g_uFogC, g_uFogN;
static GLint  g_uUnderK, g_uUnderA, g_uUnderC, g_uUnderW, g_uUnderB, g_uUnderS, g_uUnderL, g_uUnderG;

static int    g_dome = 1;
static int    g_domeBright = 250;
static int    g_domeReach = 3000;
static int    g_domeTex[8] = { 6486, 6487 };
static int    g_domeMap = 9, g_domeRadius = 800;
static float  g_domeAt[3] = { 36111.0f, 73962.0f, 962.0f };
static volatile unsigned g_loads;
static int    g_nDomeTex = 2;
static int    g_bloom = 1;
static int    g_bloomLava = 1;
static int    g_bloomFire = 100;
static int    g_bloomAdditive = 100;
static int    g_bloomGlow = 100;
static int    g_bloomRunes = 300;
static int    g_bloomGlyphs = 300;
static char   g_bloomGlowList[256];
static int    g_bloomSurface = 100;
static char   g_bloomSurfaceList[512];
static char   g_guardianList[256];
static int    g_guardianGlow = 150;
static int    g_guardianBright = 175;
static int    g_moonBright = 100;
static int    g_moonBloom = 100;
static int    g_moonHalo = 100;
static int    g_moonHaloBloom = 100;
static int    moon_percent(int v);
static int    g_moonPhase = 1;
static int    g_moonPhasePct[5] = { 0, 25, 50, 75, 100 };
static void   moon_phase_list(const char *s);
static void   map_list(const char *s, char *out, size_t cap);
int           u9lava_is(int texId);
static int    map_listed(const char *list, int map);
int u9fx_is_flame(int texId);
static int    g_bloomStrength = 200;
static int    g_bloomView;
static unsigned g_bloomFrames;
static int    g_effectFade = 0;
static int    g_effectDepthWrite = 1;
static int    g_effectFadeDebug;
static int    g_effectFullSize;
static int    g_depthGlide = 1;
static int    g_depthSlack = 0;
void u9gl_fade_report(void (*out)(const char *fmt, ...));
static GLint  g_uSun, g_uSunR, g_uSunU, g_uSunL, g_uSunC, g_uSunK, g_uSunMode;
static GLint  g_uSunPR, g_uSunPU, g_uSunPL;
static GLint  g_uSun2, g_uSun2C, g_uSun2K;
static GLint  g_uSunX, g_uSunO, g_uSunO2, g_uSunOR, g_uSunOU, g_uSunOL;
static GLint  g_uSunOPR, g_uSunOPU, g_uSunOPL, g_uSunOC, g_uSunO2C, g_uSunOK, g_uSunO2K;
static GLint  g_uSunFade, g_uSunBias, g_uSunSlope, g_uSunSmooth, g_uSunWalls, g_uSunChar, g_uSunCharBlend;
static unsigned g_sunFrames;
static int    g_glowView;
static void   night_grade(void);
static int    g_maxLights = 128;
static int    g_rank = 1;
static int    g_fadeMs = 250;
static unsigned g_capWait, g_fadeIns, g_fadeOuts;
static GLint  g_uContain, g_uKeepOut, g_uSpt, g_uNsp, g_uLsp;
static GLuint g_spaceTex;
static unsigned g_roomLit, g_roomLightsMax, g_outerMax;
static unsigned g_capLights, g_capRoom, g_capOuter, g_lightsSeenMax;
static int    g_roomsWanted;
static void (*g_roomsOut)(const char *fmt, ...);
static int    g_probeWanted;
static int    g_poolProbeWanted;
static void (*g_poolProbeOut)(const char *fmt, ...);
static int    g_ppX = -1, g_ppY;
static int    g_ppSlot[MAX_POOL_LIGHTS], g_ppFill[MAX_POOL_LIGHTS], g_ppN;
static float  g_ppAtX = -1.0f, g_ppAtY;
static float  g_ppW[MAX_POOL_LIGHTS];
static unsigned char g_ppFrame[4], g_ppAlb[4];
static int    g_lastRooms[MAX_POOL_LIGHTS][4];
static void (*g_probeOut)(const char *fmt, ...);
static int    g_pickWanted;
static void (*g_pickOut)(const char *fmt, ...);
static int    g_beamWanted;
static void (*g_beamOut)(const char *fmt, ...);
static int    g_w, g_h;
static int    g_scale = 1;
static int    g_bound;
static unsigned g_frames, g_resizes;
static int      g_checkWanted;
static unsigned g_checks, g_checkFails;
static int      g_paused;

static const char *const k_vertex =
    "#version 120\n"
    "void main() {\n"
    "    gl_Position    = gl_Vertex;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "}\n";

static const char *const k_fragment =
    "#version 120\n"
    "uniform sampler2D u_frame;\n"
    "uniform sampler2D u_depth;\n"
    "uniform sampler2D u_albedo;\n"
    "uniform float u_invGamma;\n"
    "uniform int   u_grid;\n"
    "uniform int   u_pools;\n"
    "uniform int   u_march;\n"
    "uniform float u_strength;\n"
    "uniform float u_warmth;\n"
    "uniform int   u_soft;\n"
    "uniform int   u_nl;\n"
    "uniform int   u_poolDebug;\n"
    "uniform int   u_contain;\n"
    "uniform int   u_keepOut;\n"
    "uniform sampler2D u_spt;\n"
    "uniform int   u_nsp;\n"
    "uniform vec3  u_tail;\n"
    "uniform vec4  u_lsp[128];\n"
    "uniform vec4  u_lp[128];\n"
    "uniform vec4  u_lc[128];\n"
    "uniform samplerCube u_sh0, u_sh1, u_sh2, u_sh3, u_sh4, u_sh5, u_sh6, u_sh7;\n"
    "uniform vec2  u_lsh[128];\n"
    "uniform vec4  u_night;\n"
    "uniform sampler2D u_glow;\n"
    "uniform float u_nightKeep;\n"
    "uniform int   u_glowView;\n"
    "uniform sampler2D u_bloom;\n"
    "uniform vec4  u_fogK;\n"
    "uniform vec4  u_fogBox;\n"
    "uniform vec4  u_fogC;\n"
    "uniform vec4  u_underK;\n"
    "uniform vec4  u_underA;\n"
    "uniform vec3  u_underC;\n"
    "uniform vec4  u_underW;\n"
    "uniform float u_underB;\n"
    "uniform vec4  u_underS;\n"
    "uniform vec3  u_underL;\n"
    "uniform vec3  u_underG;\n"
    "uniform vec4  u_fogN;\n"
    "uniform vec2  u_bloomK;\n"
    "uniform sampler2D u_beam;\n"
    "uniform sampler2D u_beamIn;\n"
    "uniform vec4  u_beamK;\n"
    "uniform vec2  u_beamS;\n"
    "uniform sampler2D u_rays;\n"
    "uniform vec4  u_raysK;\n"
    "uniform float u_raysAir;\n"
    "uniform sampler2D u_ao;\n"
    "uniform vec4  u_aoK;\n"
    "vec2 ao_at(vec2 uv, float w) {\n"
    "    vec2 tc = uv / u_aoK.zw - 0.5, b = floor(tc), f = tc - b;\n"
    "    vec2 sum = vec2(0.0);\n"
    "    float ws = 0.0, tol = 0.03 * w + 2.0;\n"
    "    for (int j = 0; j < 2; j++)\n"
    "        for (int i = 0; i < 2; i++) {\n"
    "            vec3 s = texture2D(u_ao, (b + vec2(float(i), float(j)) + 0.5) * u_aoK.zw).rgb;\n"
    "            float k = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y) * max(0.0, 1.0 - abs(s.g - w) / tol);\n"
    "            sum += s.rb * k;\n"
    "            ws += k;\n"
    "        }\n"
    "    return ws > 1e-3 ? sum / ws : vec2(1.0);\n"
    "}\n"
    "uniform vec4  u_shadowK;\n"
    "uniform sampler2D u_sun;\n"
    "uniform vec3  u_sunR, u_sunU, u_sunL, u_sunC;\n"
    "uniform vec3  u_sunPR, u_sunPU, u_sunPL;\n"
    "uniform vec4  u_sunK;\n"
    "uniform sampler2D u_sun2;\n"
    "uniform vec3  u_sun2C;\n"
    "uniform vec4  u_sun2K;\n"
    "uniform float u_sunX;\n"
    "uniform sampler2D u_sunO, u_sunO2;\n"
    "uniform vec3  u_sunOR, u_sunOU, u_sunOL, u_sunOPR, u_sunOPU, u_sunOPL, u_sunOC, u_sunO2C;\n"
    "uniform vec4  u_sunOK, u_sunO2K;\n"
    "uniform vec3  u_sunMode;\n"
    "uniform vec2  u_sunFade;\n"
    "uniform float u_sunBias;\n"
    "uniform int   u_sunSlope;\n"
    "uniform int   u_sunWalls;\n"
    "uniform int   u_sunSmooth;\n"
    "uniform int   u_depthMode;\n"
    "uniform int   u_aa;\n"
    "uniform vec4  u_vp;\n"
    "uniform vec2  u_surf;\n"
    "uniform vec2  u_engine;\n"
    "uniform vec4  u_proj;\n"
    "uniform vec2  u_z;\n"
    "uniform vec4  u_m0, u_m1, u_m2;\n"
    "vec3 hue(float h) {\n"
    "    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);\n"
    "}\n"
    "float eye_of(float d) {\n"
    "    return (u_depthMode == 0) ? u_z.x / max(1.0 - d, 1e-6) : u_z.x + d * (u_z.y - u_z.x);\n"
    "}\n"
    "uniform sampler2D u_exact;\n"
    "uniform int   u_haveExact;\n"
    "uniform int   u_depthView;\n"
    "uniform int   u_seams;\n"
    "uniform int   u_nrmAcross;\n"
    "uniform sampler2D u_water;\n"
    "uniform vec4  u_waterK;\n"
    "uniform sampler2D u_refl;\n"
    "uniform float u_reflOn;\n"
    "uniform float u_waterFloor;\n"
    "uniform vec4 u_wash0, u_washA0, u_wash1, u_washA1;\n"
    "vec3 wash_one(vec3 c, vec2 q, vec4 w, vec4 a) {\n"
    "    if (w.w < 0.5) return c;\n"
    "    vec2 C = clamp(a.zw, vec2(0.001), vec2(0.999));\n"
    "    vec2 lo = q / C, hi = (1.0 - q) / (1.0 - C);\n"
    "    float lc = clamp(min(min(lo.x, lo.y), min(hi.x, hi.y)), 0.0, 1.0);\n"
    "    return mix(c, w.rgb, clamp(mix(a.x, a.y, lc), 0.0, 1.0));\n"
    "}\n"
    "vec3 wash_tint(vec3 c, vec2 q) { return wash_one(wash_one(c, q, u_wash0, u_washA0), q, u_wash1, u_washA1); }\n"
    "vec2 wr_px(vec3 P, vec3 R, float t, out float fwd) {\n"
    "    vec3 d = P + R * t - vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    vec3 q = vec3(dot(vec3(u_m0.x, u_m1.x, u_m2.x), d), dot(vec3(u_m0.y, u_m1.y, u_m2.y), d),\n"
    "                  dot(vec3(u_m0.z, u_m1.z, u_m2.z), d));\n"
    "    fwd = q.y;\n"
    "    if (q.y <= 16.0) return vec2(-1.0e6);\n"
    "    float ex = u_proj.z + q.x * u_proj.x / q.y, ey = u_proj.w - q.z * u_proj.y / q.y;\n"
    "    return vec2(ex * u_vp.z / u_engine.x, u_vp.w - ey * u_vp.w / u_engine.y);\n"
    "}\n"
    "bool wr_on(vec2 p) { return p.x >= 0.0 && p.y >= 0.0 && p.x < u_vp.z && p.y < u_vp.w; }\n"
    "bool wr_world(vec2 uv) {\n"
    "    return texture2D(u_water, uv).r < 0.5 && texture2D(u_exact, uv).r > -1.0;\n"
    "}\n"
    "float wr_trust(vec3 R) {\n"
    "    vec3 r = vec3(dot(vec3(u_m0.x, u_m1.x, u_m2.x), R), dot(vec3(u_m0.y, u_m1.y, u_m2.y), R),\n"
    "                  dot(vec3(u_m0.z, u_m1.z, u_m2.z), R));\n"
    "    if (r.y <= 0.05) return 0.0;\n"
    "    vec2 v = vec2((u_proj.z + r.x * u_proj.x / r.y) * u_vp.z / u_engine.x,\n"
    "                  u_vp.w - (u_proj.w - r.z * u_proj.y / r.y) * u_vp.w / u_engine.y);\n"
    "    vec2 out1 = max(max(-v, v - u_vp.zw), vec2(0.0));\n"
    "    return 1.0 - smoothstep(0.0, 0.5, length(out1) / u_vp.w);\n"
    "}\n"
    "vec4 water_reflect(vec3 P, vec3 V) {\n"
    "    vec3 R = vec3(V.x, V.y, -V.z);\n"
    "    float trust = wr_trust(R);\n"
    "    float tPrev = 0.0, fwd;\n"
    "    vec2 lastUV = vec2(-1.0);\n"
    "    vec2 p0 = wr_px(P, R, 0.0, fwd);\n"
    "    for (int i = 0; i < 48; i++) {\n"
    "        if (float(i) >= u_waterK.z) break;\n"
    "        float f = (float(i) + 1.0) / u_waterK.z;\n"
    "        float t = u_waterK.y * f * f;\n"
    "        vec2 p = wr_px(P, R, t, fwd);\n"
    "        if (fwd <= 16.0) break;\n"
    "        if (!wr_on(p)) {\n"
    "            float a = tPrev, b = t, fa;\n"
    "            vec2 pe = vec2(-1.0);\n"
    "            for (int k = 0; k < 6; k++) {\n"
    "                float m = 0.5 * (a + b);\n"
    "                vec2 pm = wr_px(P, R, m, fa);\n"
    "                if (fa > 16.0 && wr_on(pm)) { a = m; pe = pm; } else b = m;\n"
    "            }\n"
    "            if (pe.x < 0.0) break;\n"
    "            vec2 back = normalize(p0 - pe + vec2(1e-3));\n"
    "            for (int k = 0; k < 12; k++) {\n"
    "                vec2 ue = (pe + back * (float(k) * 6.0) + u_vp.xy) / u_surf;\n"
    "                if (wr_world(ue)) return vec4(texture2D(u_frame, ue).rgb, -trust);\n"
    "            }\n"
    "            break;\n"
    "        }\n"
    "        vec2 uv = (p + u_vp.xy) / u_surf;\n"
    "        float e = texture2D(u_exact, uv).r;\n"
    "        float qy = fwd;\n"
    "        if (e > 16.0 && qy > e + 1.0 && qy - e < (t - tPrev) + 0.02 * qy + 8.0 && texture2D(u_water, uv).r < 0.5) {\n"
    "            float a = tPrev, b = t, fm;\n"
    "            for (int k = 0; k < 4; k++) {\n"
    "                float m = 0.5 * (a + b);\n"
    "                vec2 pm = wr_px(P, R, m, fm);\n"
    "                vec2 um = (pm + u_vp.xy) / u_surf;\n"
    "                float em = texture2D(u_exact, um).r;\n"
    "                if (wr_on(pm) && em > 16.0 && fm > em) { b = m; uv = um; p = pm; } else a = m;\n"
    "            }\n"
    "            vec2 edge = min(p, u_vp.zw - p) / u_vp.zw;\n"
    "            float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - smoothstep(0.5, 1.0, b / u_waterK.y));\n"
    "            vec3 hit = texture2D(u_frame, uv).rgb;\n"
    "            if (lastUV.x < 0.0) return vec4(hit, fade);\n"
    "            return vec4(mix(texture2D(u_frame, lastUV).rgb, hit, fade), mix(trust, 1.0, fade));\n"
    "        }\n"
    "        if (wr_world(uv)) lastUV = uv;\n"
    "        tPrev = t;\n"
    "    }\n"
    "    return lastUV.x < 0.0 ? vec4(0.0) : vec4(texture2D(u_frame, lastUV).rgb, -trust);\n"
    "}\n"
    "float eye_px(vec2 uvq, float d) {\n"
    "    if (u_haveExact != 0 && d < 1.0) {\n"
    "        float e = texture2D(u_exact, uvq).r;\n"
    "        if (e > 0.0) return e;\n"
    "    }\n"
    "    return eye_of(d);\n"
    "}\n"
    "vec3 rel_at(vec2 uvq, vec2 pq) {\n"
    "    float wq = eye_px(uvq, texture2D(u_depth, uvq).r);\n"
    "    float ex = pq.x * u_engine.x / u_vp.z;\n"
    "    float ey = (u_vp.w - pq.y) * u_engine.y / u_vp.w;\n"
    "    vec3 cs = vec3((ex - u_proj.z) * wq / u_proj.x, wq, (u_proj.w - ey) * wq / u_proj.y);\n"
    "    return vec3(dot(u_m0.xyz, cs), dot(u_m1.xyz, cs), dot(u_m2.xyz, cs));\n"
    "}\n"
    "vec3 world_at(vec2 uvq, vec2 pq) {\n"
    "    float wq = eye_px(uvq, texture2D(u_depth, uvq).r);\n"
    "    float ex = pq.x * u_engine.x / u_vp.z;\n"
    "    float ey = (u_vp.w - pq.y) * u_engine.y / u_vp.w;\n"
    "    vec4 cs = vec4((ex - u_proj.z) * wq / u_proj.x, wq, (u_proj.w - ey) * wq / u_proj.y, 1.0);\n"
    "    return vec3(dot(u_m0, cs), dot(u_m1, cs), dot(u_m2, cs));\n"
    "}\n"
    "uniform float u_sunChar;\n"
    "uniform float u_sunCharBlend;\n"
    "vec3 char_normal(vec2 uvq, vec2 pq, vec3 relq, vec3 Nq, float upp) {\n"
    "    vec3 acc = 0.5 * Nq;\n"
    "    float rmax = clamp(u_sunChar / max(upp, 1e-3), 1.5, 32.0);\n"
    "    float lim = 1.5 * rmax * 0.25 * upp + 1.5;\n"
    "    bool aR = true, aL = true, aU = true, aD = true;\n"
    "    vec3 pR = relq, pL = relq, pU = relq, pD = relq;\n"
    "    for (int k = 1; k <= 4; k++) {\n"
    "        float r = rmax * float(k) * 0.25;\n"
    "        vec2 dx = vec2(r, 0.0), dy = vec2(0.0, r);\n"
    "        vec2 tx = vec2(r / u_surf.x, 0.0), ty = vec2(0.0, r / u_surf.y);\n"
    "        vec3 qR = rel_at(uvq + tx, pq + dx), qL = rel_at(uvq - tx, pq - dx);\n"
    "        vec3 qU = rel_at(uvq + ty, pq + dy), qD = rel_at(uvq - ty, pq - dy);\n"
    "        aR = aR && texture2D(u_exact, uvq + tx).g > 0.5 && length(qR - pR) < lim;\n"
    "        aL = aL && texture2D(u_exact, uvq - tx).g > 0.5 && length(qL - pL) < lim;\n"
    "        aU = aU && texture2D(u_exact, uvq + ty).g > 0.5 && length(qU - pU) < lim;\n"
    "        aD = aD && texture2D(u_exact, uvq - ty).g > 0.5 && length(qD - pD) < lim;\n"
    "        if (aR) pR = qR;\n"
    "        if (aL) pL = qL;\n"
    "        if (aU) pU = qU;\n"
    "        if (aD) pD = qD;\n"
    "        bool okR = aR, okL = aL, okU = aU, okD = aD;\n"
    "        if (!(okR || okL) || !(okU || okD)) break;\n"
    "        vec3 sx = okR && okL ? qR - qL : okR ? qR - relq : relq - qL;\n"
    "        vec3 sy = okU && okD ? qU - qD : okU ? qU - relq : relq - qD;\n"
    "        vec3 n = cross(sx, sy);\n"
    "        float l = length(n);\n"
    "        if (l < 1e-6) continue;\n"
    "        n /= l;\n"
    "        if (dot(n, Nq) < 0.0) n = -n;\n"
    "        if (dot(n, Nq) < 0.2) continue;\n"
    "        acc += n;\n"
    "    }\n"
    "    return normalize(acc);\n"
    "}\n"
    "vec3 cam_of(vec3 X) {\n"
    "    vec3 d = X - vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    return vec3(dot(vec3(u_m0.x, u_m1.x, u_m2.x), d), dot(vec3(u_m0.y, u_m1.y, u_m2.y), d),\n"
    "                dot(vec3(u_m0.z, u_m1.z, u_m2.z), d));\n"
    "}\n"
    "vec2 uv_of(vec3 cs) {\n"
    "    float ex = u_proj.z + cs.x * u_proj.x / cs.y;\n"
    "    float ey = u_proj.w - cs.z * u_proj.y / cs.y;\n"
    "    return vec2(u_vp.x + ex * u_vp.z / u_engine.x, u_vp.y + u_vp.w - ey * u_vp.w / u_engine.y) / u_surf;\n"
    "}\n"
    "bool in_tri(vec3 X, vec3 a, vec3 b, vec3 c, vec3 n) {\n"
    "    return dot(cross(b - a, X - a), n) >= 0.0 && dot(cross(c - b, X - b), n) >= 0.0 &&\n"
    "           dot(cross(a - c, X - c), n) >= 0.0;\n"
    "}\n"
    "bool through(vec3 A, vec3 B, vec3 c0, vec3 c1, vec3 c2, vec3 c3) {\n"
    "    vec3 n = cross(c1 - c0, c3 - c0);\n"
    "    float den = dot(n, B - A);\n"
    "    if (abs(den) < 1e-6) return false;\n"
    "    float t = dot(n, c0 - A) / den;\n"
    "    if (t < 0.0 || t > 1.0) return false;\n"
    "    vec3 X = A + t * (B - A);\n"
    "    return in_tri(X, c0, c1, c2, n) || in_tri(X, c0, c2, c3, n);\n"
    "}\n"
    "vec4 spt(float i) {\n"
    "    return texture2D(u_spt, vec2((mod(i, 1024.0) + 0.5) / 1024.0, (floor(i / 1024.0) + 0.5) / 16.0));\n"
    "}\n"
    "bool in_doorway(vec4 sp, vec3 P) {\n"
    "    for (int k = 0; k < 32; k++) {\n"
    "        if (float(k) >= sp.w) break;\n"
    "        float b = sp.z + float(k) * 4.0;\n"
    "        vec3 c0 = spt(b).xyz, c1 = spt(b + 1.0).xyz, c2 = spt(b + 2.0).xyz, c3 = spt(b + 3.0).xyz;\n"
    "        vec3 n = cross(c1 - c0, c3 - c0);\n"
    "        float ln = length(n);\n"
    "        if (ln < 1e-3) continue;\n"
    "        n /= ln;\n"
    "        float e = dot(n, P - c0);\n"
    "        if (abs(e) > 24.0) continue;\n"
    "        vec3 X = P - e * n;\n"
    "        if (in_tri(X, c0, c1, c2, n) || in_tri(X, c0, c2, c3, n)) return true;\n"
    "    }\n"
    "    return false;\n"
    "}\n"
    "#define U9OUTER 32.0\n"
    "bool indoors(vec4 sp, vec3 P, vec3 N) {\n"
    "    for (int k = 0; k < 64; k++) {\n"
    "        if (float(k) >= sp.y) break;\n"
    "        vec4 pl = spt(sp.x + float(k));\n"
    "        float e = dot(pl.xyz, P) + pl.w;\n"
    "        if (e < -8.0) return false;\n"
    "        if (e < U9OUTER && dot(N, pl.xyz) < -0.5) return false;\n"
    "    }\n"
    "    return true;\n"
    "}\n"
    "float room_of(vec3 P, vec3 N) {\n"
    "    float best = -1.0, bestPri = 0.0;\n"
    "    for (int k = 0; k < 256; k++) {\n"
    "        if (k >= u_nsp) break;\n"
    "        float pri = spt(float(k) * 2.0 + 1.0).x;\n"
    "        if (best >= 0.0 && pri <= bestPri) continue;\n"
    "        if (indoors(spt(float(k) * 2.0), P, N)) { best = float(k); bestPri = pri; }\n"
    "    }\n"
    "    return best;\n"
    "}\n"
    "float in_room(float si, vec3 P, vec3 N) {\n"
    "    if (si < 0.0) return 0.0;\n"
    "    vec4 sp = spt(si * 2.0);\n"
    "    for (int k = 0; k < 64; k++) {\n"
    "        if (float(k) >= sp.y) break;\n"
    "        vec4 pl = spt(sp.x + float(k));\n"
    "        float e = dot(pl.xyz, P) + pl.w;\n"
    "        float f = dot(N, pl.xyz);\n"
    "        if (e < -8.0 || (e < 0.0 && f < 0.5)) return 0.0;\n"
    "        if (e < U9OUTER && f < -0.5) return 0.0;\n"
    "    }\n"
    "    return 1.0;\n"
    "}\n"
    "bool exits(float si, float prev, vec3 L, vec3 P, vec3 N, bool roomOk) {\n"
    "    vec4 sp = spt(si * 2.0);\n"
    "    for (int k = 0; k < 32; k++) {\n"
    "        if (float(k) >= sp.w) break;\n"
    "        float b = sp.z + float(k) * 4.0;\n"
    "        vec4 c0 = spt(b), c1 = spt(b + 1.0);\n"
    "        if (c0.w < 0.5 || abs(c1.w - prev) < 0.5) continue;\n"
    "        if (!through(L, P, c0.xyz, c1.xyz, spt(b + 2.0).xyz, spt(b + 3.0).xyz)) continue;\n"
    "        if (c1.w < -0.5 || (roomOk && in_room(c1.w, P, N) > 0.0)) return true;\n"
    "    }\n"
    "    return false;\n"
    "}\n"
    "bool opening(float si, vec3 L, vec3 P, vec3 N, bool roomOk) {\n"
    "    vec4 sp = spt(si * 2.0);\n"
    "    for (int k = 0; k < 32; k++) {\n"
    "        if (float(k) >= sp.w) break;\n"
    "        float b = sp.z + float(k) * 4.0;\n"
    "        vec4 c0 = spt(b), c1 = spt(b + 1.0);\n"
    "        if (c0.w < 0.5) continue;\n"
    "        if (!through(L, P, c0.xyz, c1.xyz, spt(b + 2.0).xyz, spt(b + 3.0).xyz)) continue;\n"
    "        if (c1.w < -0.5) return true;\n"
    "        if (roomOk && in_room(c1.w, P, N) > 0.0) return true;\n"
    "        if (exits(c1.w, si, L, P, N, roomOk)) return true;\n"
    "    }\n"
    "    return false;\n"
    "}\n"
    "bool door_of(float si, vec3 P) {\n"
    "    return si >= 0.0 && in_doorway(spt(si * 2.0), P);\n"
    "}\n"
    "bool out_of(float si, vec3 L, vec3 P, vec3 N) {\n"
    "    return si >= 0.0 && opening(si, L, P, N, true);\n"
    "}\n"
    "float reach(int li, vec3 P, vec3 L, vec3 N, inout float pr) {\n"
    "    if (u_contain == 0) return 1.0;\n"
    "    vec4 g = u_lsp[li];\n"
    "    if (g.x < 0.0) {\n"
    "        if (u_keepOut == 0) return 1.0;\n"
    "        if (pr < -1.5) pr = room_of(P, N);\n"
    "        if (pr < 0.0) return 1.0;\n"
    "        vec4 rp = spt(pr * 2.0);\n"
    "        if (opening(pr, L, P, N, false)) return 2.0;\n"
    "        return in_doorway(rp, P) ? 1.0 : 0.0;\n"
    "    }\n"
    "    if (in_room(g.x, P, N) + in_room(g.y, P, N) + in_room(g.z, P, N) + in_room(g.w, P, N) > 0.0) return 1.0;\n"
    "    if (out_of(g.x, L, P, N) || out_of(g.y, L, P, N) || out_of(g.z, L, P, N) || out_of(g.w, L, P, N)) return 2.0;\n"
    "    if (door_of(g.x, P) || door_of(g.y, P) || door_of(g.z, P) || door_of(g.w, P)) return 1.0;\n"
    "    return 0.0;\n"
    "}\n"
    "float seen(vec3 P, vec3 L, float wP) {\n"
    "    if (u_march == 0) return 1.0;\n"
    "    float len = length(L - P);\n"
    "    if (len < 64.0) return 1.0;\n"
    "    float t0 = min(24.0 / len, 0.45), t1 = max(1.0 - 24.0 / len, 0.55);\n"
    "    float fg = min(wP, cam_of(L).y) - 32.0;\n"
    "    int run = 0;\n"
    "    for (int k = 0; k < 16; k++) {\n"
    "        vec3 cs = cam_of(mix(P, L, mix(t0, t1, (float(k) + 0.5) / 16.0)));\n"
    "        bool hit = false;\n"
    "        if (cs.y > 16.0) {\n"
    "            vec2 uv = uv_of(cs);\n"
    "            if (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) {\n"
    "                float d = texture2D(u_depth, uv).r;\n"
    "                if (d < 1.0) {\n"
    "                    float s = eye_of(d);\n"
    "                    float behind = cs.y - s;\n"
    "                    hit = s > fg && behind > 8.0 + 0.004 * cs.y && behind < 64.0;\n"
    "                }\n"
    "            }\n"
    "        }\n"
    "        run = hit ? run + 1 : 0;\n"
    "        if (run >= 2) return 0.0;\n"
    "    }\n"
    "    return 1.0;\n"
    "}\n"
    "float cube_at(float s, vec3 r) {\n"
    "    if (s < 0.5) return textureCube(u_sh0, r).r;\n"
    "    if (s < 1.5) return textureCube(u_sh1, r).r;\n"
    "    if (s < 2.5) return textureCube(u_sh2, r).r;\n"
    "    if (s < 3.5) return textureCube(u_sh3, r).r;\n"
    "    if (s < 4.5) return textureCube(u_sh4, r).r;\n"
    "    if (s < 5.5) return textureCube(u_sh5, r).r;\n"
    "    if (s < 6.5) return textureCube(u_sh6, r).r;\n"
    "    return textureCube(u_sh7, r).r;\n"
    "}\n"
    "float shadow_of(float s, vec3 P, vec3 L, float reach) {\n"
    "    vec3 r = P - L;\n"
    "    float d = length(r);\n"
    "    float ref = (d - u_shadowK.x - u_shadowK.y * d) / reach;\n"
    "    vec3 a = normalize(cross(r, abs(r.z) < 0.9 * d ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));\n"
    "    vec3 b = cross(r / max(d, 1e-3), a);\n"
    "    float k = d * 0.006 * u_shadowK.w;\n"
    "    float lit = ref <= cube_at(s, r) ? 1.0 : 0.0;\n"
    "    lit += ref <= cube_at(s, r + (a + b) * k) ? 1.0 : 0.0;\n"
    "    lit += ref <= cube_at(s, r + (a - b) * k) ? 1.0 : 0.0;\n"
    "    lit += ref <= cube_at(s, r - (a + b) * k) ? 1.0 : 0.0;\n"
    "    lit += ref <= cube_at(s, r - (a - b) * k) ? 1.0 : 0.0;\n"
    "    return lit / 5.0;\n"
    "}\n"
    "float g_sunOcc;\n"
    "float g_sunTan;\n"
    "float sun_cmp(sampler2D m, vec2 c, vec2 st, vec2 grad, float base, vec4 K) {\n"
    "    float dl = clamp(dot(grad, c - st), -8.0 * K.z, 8.0 * K.z);\n"
    "    return 0.5 - 0.5 * (base + dl) / K.y <= texture2D(m, c).r ? 1.0 : 0.0;\n"
    "}\n"
    "vec3 sun_map(sampler2D m, vec3 C, vec4 K, vec3 P, vec3 N, float ndl, float eye,\n"
    "             vec3 R, vec3 U, vec3 L, vec3 PR, vec3 PU, vec3 PL) {\n"
    "    vec3 q = P + N * (K.z * 1.5) - C;\n"
    "    vec2 st = vec2(dot(PR, q), dot(PU, q)) / K.x * 0.5 + 0.5;\n"
    "    if (st.x <= 0.0 || st.y <= 0.0 || st.x >= 1.0 || st.y >= 1.0) return vec3(1.0, 0.0, 0.0);\n"
    "    float size = 2.0 * K.x / K.z;\n"
    "    vec2 grad = -vec2(dot(N, R), dot(N, U)) / max(dot(N, L), 0.1) * 2.0 * K.x;\n"
    "    float base = dot(PL, q) + u_sunBias + K.z * (0.5 + 0.5 * (1.0 - ndl)) + eye * 0.002;\n"
    "    float k = K.z / (2.0 * K.x) * 1.5 * u_shadowK.w;\n"
    "    float lit = 0.0, occ = 0.0;\n"
    "    for (int i = 0; i < 5; i++) {\n"
    "        vec2 o = i == 0 ? vec2(0.0) : i == 1 ? vec2(k, k) : i == 2 ? vec2(k, -k) : i == 3 ? vec2(-k, -k) : vec2(-k, k);\n"
    "        vec2 c = (floor((st + o) * size) + 0.5) / size;\n"
    "        float dl = clamp(dot(grad, c - st), -8.0 * K.z, 8.0 * K.z);\n"
    "        float at = texture2D(m, c).r;\n"
    "        float ref = 0.5 - 0.5 * (base + dl) / K.y;\n"
    "        if (i == 0) occ = max(ref - at, 0.0) * 2.0 * K.y;\n"
    "        if (i == 0 && u_sunSlope != 0) occ = max(occ - K.z * min(g_sunTan, 64.0), 0.0);\n"
    "        if (u_sunSmooth != 0) {\n"
    "            vec2 p = (st + o) * size - 0.5, f = fract(p), b0 = (floor(p) + 0.5) / size, d = vec2(1.0 / size);\n"
    "            lit += mix(mix(sun_cmp(m, b0, st, grad, base, K), sun_cmp(m, b0 + vec2(d.x, 0.0), st, grad, base, K), f.x),\n"
    "                       mix(sun_cmp(m, b0 + vec2(0.0, d.y), st, grad, base, K), sun_cmp(m, b0 + d, st, grad, base, K), f.x), f.y);\n"
    "        } else {\n"
    "            lit += ref <= at ? 1.0 : 0.0;\n"
    "        }\n"
    "    }\n"
    "    float edge = min(min(st.x, 1.0 - st.x), min(st.y, 1.0 - st.y));\n"
    "    return vec3(lit / 5.0, smoothstep(0.0, 0.08, edge), occ);\n"
    "}\n"
    "float g_sunAwayLit;\n"
    "float sun_lit(vec3 P, vec3 N) {\n"
    "    float ndl = dot(N, u_sunL);\n"
    "    g_sunTan = sqrt(max(1.0 - ndl * ndl, 0.0)) / max(abs(ndl), 0.015);\n"
    "    g_sunOcc = 0.0;\n"
    "    g_sunAwayLit = 1.0;\n"
    "    bool away = ndl <= -0.02;\n"
    "    vec3 Nq = away ? u_sunL : N;\n"
    "    float nd = max(ndl, 0.0);\n"
    "    float eye = length(P - vec3(u_m0.w, u_m1.w, u_m2.w));\n"
    "    vec3 nr = sun_map(u_sun, u_sunC, u_sunK, P, Nq, nd, eye, u_sunR, u_sunU, u_sunL, u_sunPR, u_sunPU, u_sunPL);\n"
    "    vec3 fr = u_sun2K.w > 0.5 ? sun_map(u_sun2, u_sun2C, u_sun2K, P, Nq, nd, eye, u_sunR, u_sunU, u_sunL, u_sunPR, u_sunPU, u_sunPL)\n"
    "                              : vec3(1.0, 0.0, 0.0);\n"
    "    float inside = max(nr.y, fr.y);\n"
    "    g_sunOcc = nr.y > 0.0 ? nr.z : fr.z;\n"
    "    float lit = mix(mix(1.0, fr.x, fr.y), nr.x, nr.y);\n"
    "    if (u_sunX < 1.0) {\n"
    "        vec3 on = sun_map(u_sunO, u_sunOC, u_sunOK, P, Nq, nd, eye, u_sunOR, u_sunOU, u_sunOL, u_sunOPR, u_sunOPU, u_sunOPL);\n"
    "        vec3 of = u_sunO2K.w > 0.5\n"
    "            ? sun_map(u_sunO2, u_sunO2C, u_sunO2K, P, Nq, nd, eye, u_sunOR, u_sunOU, u_sunOL, u_sunOPR, u_sunOPU, u_sunOPL)\n"
    "            : vec3(1.0, 0.0, 0.0);\n"
    "        lit = mix(mix(mix(1.0, of.x, of.y), on.x, on.y), lit, u_sunX);\n"
    "        g_sunOcc = mix(on.y > 0.0 ? on.z : of.z, g_sunOcc, u_sunX);\n"
    "        inside = max(inside, max(on.y, of.y));\n"
    "    }\n"
    "    if (inside <= 0.0) return away ? -2.0 : -1.0;\n"
    "    if (away) { g_sunAwayLit = lit; return -2.0; }\n"
    "    return lit;\n"
    "}\n"
    "float fhash(vec3 p) {\n"
    "    p = fract(p * 0.3183099 + 0.1);\n"
    "    p *= 17.0;\n"
    "    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));\n"
    "}\n"
    "float fnoise(vec3 x) {\n"
    "    vec3 i = floor(x);\n"
    "    vec3 f = fract(x);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(mix(fhash(i), fhash(i + vec3(1.0, 0.0, 0.0)), f.x),\n"
    "                   mix(fhash(i + vec3(0.0, 1.0, 0.0)), fhash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),\n"
    "               mix(mix(fhash(i + vec3(0.0, 0.0, 1.0)), fhash(i + vec3(1.0, 0.0, 1.0)), f.x),\n"
    "                   mix(fhash(i + vec3(0.0, 1.0, 1.0)), fhash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);\n"
    "}\n"
    "float fog_density(vec3 s, float near) {\n"
    "    float h = smoothstep(u_fogK.y, u_fogK.y - u_fogK.w, s.z);\n"
    "    float fe = u_fogC.w;\n"
    "    float e = smoothstep(u_fogBox.x, u_fogBox.x + fe, s.x) * (1.0 - smoothstep(u_fogBox.z - fe, u_fogBox.z, s.x))\n"
    "            * smoothstep(u_fogBox.y, u_fogBox.y + fe, s.y) * (1.0 - smoothstep(u_fogBox.w - fe, u_fogBox.w, s.y));\n"
    "    if (h * e <= 0.0) return 0.0;\n"
    "    vec3 q = s * u_fogN.y + vec3(u_fogN.z * 0.05, u_fogN.z * 0.03, u_fogN.z * 0.01);\n"
    "    float n = fnoise(q);\n"
    "    if (near > 0.0) n = mix(n, n * 0.65 + fnoise(q * 2.07 + 11.0) * 0.35, near);\n"
    "    return u_fogK.z * h * e * mix(1.0, n * 2.0, u_fogN.x);\n"
    "}\n"
    "vec3 fog_apply(vec3 c, vec3 world, bool sky) {\n"
    "    vec3 C = vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    vec3 ray = world - C;\n"
    "    float L = length(ray);\n"
    "    if (L < 1.0) return c;\n"
    "    vec3 dir = ray / L;\n"
    "    if (sky || L > u_fogN.w) L = u_fogN.w;\n"
    "    float t0 = 0.0, t1 = L;\n"
    "    if (abs(dir.z) > 1e-4) {\n"
    "        float th = (u_fogK.y - C.z) / dir.z;\n"
    "        if (dir.z > 0.0) t1 = min(t1, th); else t0 = max(t0, th);\n"
    "    } else if (C.z > u_fogK.y) return c;\n"
    "    if (t1 <= t0) return c;\n"
    "    float seg = t1 - t0;\n"
    "    float acc = 0.0;\n"
    "    for (int i = 0; i < 6; i++) {\n"
    "        float t = t0 + (float(i) + 0.5) * seg / 6.0;\n"
    "        acc += fog_density(C + dir * t, 1.0 - smoothstep(400.0, 1200.0, t));\n"
    "    }\n"
    "    float tau = acc / 6.0 * seg;\n"
    "    float T = exp(-tau);\n"
    "    vec3 fc = u_fogC.rgb;\n"
    "    if (u_night.w >= 0.0) fc = mix(fc, vec3(dot(fc, vec3(0.299, 0.587, 0.114))), u_night.w) * u_night.xyz;\n"
    "    return c * T + fc * (1.0 - T);\n"
    "}\n"
    "float caus_cell(vec2 q, float t, float seed) {\n"
    "    vec2 i = floor(q), f = fract(q);\n"
    "    float f1 = 8.0, f2 = 8.0;\n"
    "    for (int y = -1; y <= 1; y++)\n"
    "        for (int x = -1; x <= 1; x++) {\n"
    "            vec2 g = vec2(float(x), float(y));\n"
    "            vec2 h = vec2(fhash(vec3(i + g, seed)), fhash(vec3(i + g, seed + 7.3)));\n"
    "            vec2 o = 0.5 + 0.38 * sin(t * (0.6 + 0.5 * h.yx) + 6.2831 * h);\n"
    "            float e = length(g + o - f);\n"
    "            if (e < f1) { f2 = f1; f1 = e; } else if (e < f2) f2 = e;\n"
    "        }\n"
    "    return f2 - f1;\n"
    "}\n"
    "float caustics(vec3 P, float pd) {\n"
    "    float t = u_underW.w;\n"
    "    vec2 q = P.xy * u_underW.y;\n"
    "    q += (vec2(fnoise(vec3(q * 0.35, t * 0.15)), fnoise(vec3(q * 0.35 + 17.0, t * 0.15))) - 0.5) * 0.9;\n"
    "    float e1 = caus_cell(q + vec2(t * 0.05, t * 0.03), t, 1.7);\n"
    "    float e2 = caus_cell(q * 1.41 + vec2(5.2 - t * 0.04, 1.3 + t * 0.02), t * 1.2, 9.1);\n"
    "    float wd = 0.07 + pd / (u_underW.z * 4.0);\n"
    "    float sb = u_underB * 0.12 * (1.0 + 2.0 * pd / u_underW.z);\n"
    "    float a1 = 1.0 - smoothstep(0.0, wd + sb + fwidth(e1), e1);\n"
    "    float a2 = 1.0 - smoothstep(0.0, wd + sb + fwidth(e2), e2);\n"
    "    float pk = mix(1.0, wd / (wd + sb), 0.6);\n"
    "    return (a1 * a1 + 0.7 * a2 * a2) * pk;\n"
    "}\n"
    "float shaft_at(vec2 q, float t) {\n"
    "    float n = 0.6 * fnoise(vec3(q, t * 0.12)) + 0.4 * fnoise(vec3(q * 2.3 + 5.0, t * 0.2));\n"
    "    return smoothstep(0.5, 0.85, n);\n"
    "}\n"
    "vec3 under_shafts(vec3 C, vec3 dir, float L) {\n"
    "    float Ls = min(L, u_underS.z);\n"
    "    if (Ls < 1.0) return vec3(0.0);\n"
    "    float seg = Ls / 16.0;\n"
    "    float sigma = u_underK.z > 0.0 ? u_underK.z : 0.0008;\n"
    "    float dark = u_underK.w > 0.0 ? u_underK.w : sigma;\n"
    "    vec2 slope = u_underL.xy / max(u_underL.z, 0.2);\n"
    "    vec3 acc = vec3(0.0);\n"
    "    for (int i = 0; i < 16; i++) {\n"
    "        float t = (float(i) + 0.5) * seg;\n"
    "        vec3 S = C + dir * t;\n"
    "        float ds = max(u_underK.y - S.z, 0.0);\n"
    "        float pat = shaft_at((S.xy + slope * ds) * u_underS.y, u_underS.w);\n"
    "        acc += pat * exp(-u_underA.rgb * (dark * ds + sigma * t));\n"
    "    }\n"
    "    float g = max(dot(dir, u_underL), 0.0);\n"
    "    return acc * (seg * sigma) * (0.35 + 1.65 * g * g * g) * u_underS.x;\n"
    "}\n"
    "vec3 under_apply(vec3 c, vec3 eg, vec3 ep, vec3 world, bool sky, vec3 N) {\n"
    "    vec3 C = vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    vec3 ray = world - C;\n"
    "    float L = length(ray);\n"
    "    if (L < 1.0) return c;\n"
    "    vec3 dir = ray / L;\n"
    "    if (sky || L > u_underA.w) L = u_underA.w;\n"
    "    if (dir.z > 1e-4) L = min(L, max(u_underK.y - C.z, 0.0) / dir.z);\n"
    "    vec3 T = exp(-u_underA.rgb * (u_underK.z * L));\n"
    "    float pd = max(u_underK.y - (C.z + dir.z * L), 0.0);\n"
    "    float cd = max(u_underK.y - C.z, 0.0);\n"
    "    vec3 lit = exp(-u_underA.rgb * (u_underK.w * pd));\n"
    "    if (u_underW.x > 0.0 && !sky && pd > 0.0) {\n"
    "        vec3 Nc = dot(N, C - world) < 0.0 ? -N : N;\n"
    "        float k = u_underW.x * smoothstep(-0.3, 0.8, Nc.z) * smoothstep(0.0, 10.0, pd)\n"
    "                * exp(-pd * 1.0986 / u_underW.z) * (1.0 - smoothstep(900.0, 2200.0, L));\n"
    "        if (u_night.w >= 0.0) k *= 1.0 - 0.8 * u_night.w;\n"
    "        if (k > 0.002) lit *= 1.0 + k * caustics(world, pd);\n"
    "    }\n"
    "    vec3 fc = u_underC * exp(-u_underA.rgb * (u_underK.w * cd));\n"
    "    if (u_night.w >= 0.0) fc = mix(fc, vec3(dot(fc, vec3(0.299, 0.587, 0.114))), u_night.w) * u_night.xyz;\n"
    "    float Lg = min(L, u_underG.z);\n"
    "    if (u_pools != 0 && dot(eg, vec3(1.0)) > 0.002) {\n"
    "        for (int i = 0; i < 128; i++) {\n"
    "            if (i >= u_nl) break;\n"
    "            if (u_lc[i].w > 0.5 || u_lc[i].w < -0.5) continue;\n"
    "            vec3 q = u_lp[i].xyz - C;\n"
    "            float t = dot(q, dir);\n"
    "            if (t > 0.0 && t < Lg && length(q - dir * t) < 192.0) Lg = t;\n"
    "        }\n"
    "    }\n"
    "    vec3 Tg = exp(-u_underA.rgb * (u_underK.z * Lg * u_underG.y));\n"
    "    vec3 o = ((c - eg - ep) * lit + ep) * T + eg * Tg + fc * (1.0 - T);\n"
    "    if (u_underS.x > 0.0) o += under_shafts(C, dir, L);\n"
    "    return o;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st;\n"
    "    vec3 c = texture2D(u_frame, uv).rgb;\n"
    "    float d = texture2D(u_depth, uv).r;\n"
    "    vec2 p = gl_FragCoord.xy - u_vp.xy;\n"
    "    float w = eye_px(uv, d);\n"
    "    float ex = p.x * u_engine.x / u_vp.z;\n"
    "    float ey = (u_vp.w - p.y) * u_engine.y / u_vp.w;\n"
    "    vec4 cs = vec4((ex - u_proj.z) * w / u_proj.x, w, (u_proj.w - ey) * w / u_proj.y, 1.0);\n"
    "    vec3 eyeP = vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "    vec3 rel = rel_at(uv, p);\n"
    "    vec3 world = eyeP + rel;\n"
    "    vec2 tx = vec2(1.0 / u_surf.x, 0.0), ty = vec2(0.0, 1.0 / u_surf.y);\n"
    "    vec3 wR = rel_at(uv + tx, p + vec2(1.0, 0.0)), wL = rel_at(uv - tx, p - vec2(1.0, 0.0));\n"
    "    vec3 wU = rel_at(uv + ty, p + vec2(0.0, 1.0)), wD = rel_at(uv - ty, p - vec2(0.0, 1.0));\n"
    "    if (u_seams != 0 && d < 1.0) {\n"
    "        float e0 = length(rel), tol = max(4.0, e0 * 0.01);\n"
    "        float eR = length(wR), eL = length(wL), eU = length(wU), eD = length(wD);\n"
    "        if (e0 - max(eR, eL) > tol && abs(eR - eL) < tol) rel = 0.5 * (wR + wL);\n"
    "        else if (e0 - max(eU, eD) > tol && abs(eU - eD) < tol) rel = 0.5 * (wU + wD);\n"
    "        world = eyeP + rel;\n"
    "    }\n"
    "    vec3 ax = wR - rel, bx = rel - wL, ay = wU - rel, by = rel - wD;\n"
    "    vec3 sx = dot(ax, ax) < dot(bx, bx) ? ax : bx, sy = dot(ay, ay) < dot(by, by) ? ay : by;\n"
    "    if (u_nrmAcross != 0) {\n"
    "        vec3 cx = 0.5 * (wR - wL), cy = 0.5 * (wU - wD);\n"
    "        if (dot(cx, cx) < dot(sx, sx)) sx = cx;\n"
    "        if (dot(cy, cy) < dot(sy, sy)) sy = cy;\n"
    "    }\n"
    "    vec3 N = normalize(cross(sx, sy));\n"
    "    vec3 Nsun = N;\n"
    "    bool charPx = u_sunChar > 0.0 && u_haveExact != 0 && d < 1.0 && texture2D(u_exact, uv).g > 0.5;\n"
    "    if (charPx) Nsun = char_normal(uv, p, rel, N, abs(w * u_engine.x / (u_vp.z * u_proj.x)));\n"
    "    float charShade = -1.0;\n"
    "    bool solid = d < 1.0 && w > 16.0 && p.x >= 0.0 && p.y >= 0.0 && p.x < u_vp.z && p.y < u_vp.w;\n"
    "    if (u_night.w >= 0.0 && solid) {\n"
    "        float keep = 0.0;\n"
    "        if (u_pools != 0 && u_nightKeep > 0.0) {\n"
    "            for (int i = 0; i < 128; i++) {\n"
    "                if (i >= u_nl) break;\n"
    "                if (u_lc[i].w > 0.5 || u_lc[i].w < -0.5) continue;\n"
    "                float k = 1.0 - smoothstep(0.5 * u_nightKeep, u_nightKeep, length(u_lp[i].xyz - world));\n"
    "                keep = max(keep, k * clamp(max(u_lc[i].r, max(u_lc[i].g, u_lc[i].b)) * 4.0, 0.0, 1.0));\n"
    "            }\n"
    "        }\n"
    "        vec3 e = min(texture2D(u_glow, uv).rgb, c);\n"
    "        vec3 b = c - e;\n"
    "        vec3 g = mix(b, vec3(dot(b, vec3(0.299, 0.587, 0.114))), u_night.w) * u_night.xyz;\n"
    "        c = mix(g, b, keep) + e;\n"
    "    }\n"
    "    float aoV = 1.0, ctV = 1.0;\n"
    "    if (u_aoK.x > 0.0 && solid) {\n"
    "        vec2 ac = ao_at(uv, w);\n"
    "        aoV = ac.x;\n"
    "        ctV = ac.y;\n"
    "        vec3 e = min(texture2D(u_glow, uv).rgb, c);\n"
    "        c = (c - e) * aoV + e;\n"
    "    }\n"
    "    float sunV = -3.0;\n"
    "    float sunAway = 0.0;\n"
    "    float sunCast = 0.0;\n"
    "    bool sunFar = u_sunFade.y > u_sunFade.x && length(world - eyeP) >= u_sunFade.y;\n"
    "    if ((u_sunMode.x > 0.5 || u_sunMode.y > 0.5) && solid && !sunFar) {\n"
    "        vec3 Ns = Nsun;\n"
    "        if (dot(Ns, vec3(u_m0.w, u_m1.w, u_m2.w) - world) < 0.0) Ns = -Ns;\n"
    "        vec3 Ps = world + Ns * 4.0;\n"
    "        sunV = sun_lit(Ps, Ns);\n"
    "        bool roomShade = false;\n"
    "        if (u_sunMode.z > 0.5 && sunV > -2.5) {\n"
    "            float rs = room_of(world, Ns);\n"
    "            if (rs >= 0.0 && !opening(rs, Ps, Ps + u_sunL * 32768.0, Ns, false)){ sunV = 0.0; roomShade = true; }\n"
    "        }\n"
    "        if (ctV < 1.0 && sunV > -1.5) sunV = sunV >= 0.0 ? min(sunV, ctV) : ctV;\n"
    "        if (u_sunMode.y > 0.5) {\n"
    "            float ndl = dot(Ns, u_sunL);\n"
    "            bool wall = !charPx && u_sunWalls != 0 && abs(Ns.z) < 0.5;\n"
    "            float face = roomShade ? 1.0 : wall ? smoothstep(0.0, 0.25, ndl) : mix(smoothstep(0.0, 0.25, ndl), 1.0, smoothstep(24.0, 96.0, g_sunOcc));\n"
    "            float near = u_sunFade.y > u_sunFade.x\n"
    "                ? 1.0 - smoothstep(u_sunFade.x, u_sunFade.y, length(world - vec3(u_m0.w, u_m1.w, u_m2.w))) : 1.0;\n"
    "            if (sunV >= 0.0) sunV = 1.0 - (1.0 - sunV) * face * near;\n"
    "            if (sunV < -1.5) {\n"
    "                float away = wall ? smoothstep(0.02, 0.08, -ndl) : smoothstep(0.05, 0.35, -ndl);\n"
    "                sunAway = away * near;\n"
    "                if (!roomShade && !wall) sunCast = (1.0 - g_sunAwayLit) * smoothstep(24.0, 96.0, g_sunOcc) * near;\n"
    "            }\n"
    "            float amount = sunV >= 0.0 ? 1.0 - sunV : max(sunAway, sunCast);\n"
    "            if (charPx && u_sunCharBlend > 0.0 && !roomShade) {\n"
    "                float soft = smoothstep(-u_sunCharBlend * 0.33, u_sunCharBlend, -ndl) * near;\n"
    "                amount = sunV >= 0.0 ? max(amount, soft) : max(soft, sunCast);\n"
    "                charShade = soft;\n"
    "            }\n"
    "            vec3 e = min(texture2D(u_glow, uv).rgb, c);\n"
    "            c = (c - e) * (1.0 - u_sunK.w * amount) + e;\n"
    "        }\n"
    "    }\n"
    "    vec3 poolLit = vec3(0.0);\n"
    "    if (u_pools != 0 && solid) {\n"
    "        vec3 a = texture2D(u_albedo, uv).rgb;\n"
    "        if (a.r + a.g + a.b > 0.0) {\n"
    "            vec3 camPos = vec3(u_m0.w, u_m1.w, u_m2.w);\n"
    "            if (dot(N, camPos - world) < 0.0) N = -N;\n"
    "            vec3 add = vec3(0.0);\n"
    "            float dark = 0.0;\n"
    "            float nLit = 0.0, nBlocked = 0.0, nAway = 0.0;\n"
    "            float pr = -2.0;\n"
    "            float shDbg = -1.0;\n"
    "            for (int i = 0; i < 128; i++) {\n"
    "                if (i >= u_nl) break;\n"
    "                vec3 toL = u_lp[i].xyz - world;\n"
    "                float dist = length(toL);\n"
    "                float reachEnd = u_tail.x > 0.5 ? max(u_lp[i].w, u_lp[i].w * u_tail.z) : u_lp[i].w;\n"
    "                if (dist >= reachEnd) continue;\n"
    "                bool soft = u_lc[i].w < -0.5;\n"
    "                float nd = dot(N, toL / max(dist, 1e-3));\n"
    "                float ndl = soft ? clamp(0.5 + 0.5 * nd, 0.0, 1.0) : max(nd, 0.0);\n"
    "                if (ndl <= 0.0) { nAway += 1.0; continue; }\n"
    "                float fade = max(1.0 - dist / u_lp[i].w, 0.0);\n"
    "                float att = soft ? fade * fade\n"
    "                                 : clamp((u_lp[i].w - dist) / max(u_lp[i].w - 50.0, 1.0), 0.0, 1.0);\n"
    "                float how = reach(i, world, u_lp[i].xyz, N, pr);\n"
    "                if (how <= 0.0) { nBlocked += 1.0; continue; }\n"
    "                if (u_tail.x > 1.5 || (u_tail.x > 0.5 && how > 1.5)) {\n"
    "                    float t = u_tail.y * max(1.0 - dist / reachEnd, 0.0);\n"
    "                    float a2 = att * att, t2 = t * t;\n"
    "                    att = pow(a2 * a2 * a2 + t2 * t2 * t2, 1.0 / 6.0);\n"
    "                }\n"
    "                if (att <= 0.0) continue;\n"
    "                if (seen(world + N * 12.0, u_lp[i].xyz, w) <= 0.0) { nBlocked += 1.0; continue; }\n"
    "                if (u_lsh[i].x >= 0.0) {\n"
    "                    float sh = shadow_of(u_lsh[i].x, world + N * 4.0, u_lp[i].xyz, reachEnd);\n"
    "                    shDbg = shDbg < 0.0 ? sh : min(shDbg, sh);\n"
    "                    att *= mix(1.0, sh, u_shadowK.z * u_lsh[i].y);\n"
    "                }\n"
    "                nLit += 1.0;\n"
    "                if (u_lc[i].w > 0.5) dark += att * ndl * dot(u_lc[i].rgb, vec3(0.3333));\n"
    "                else add += mix(u_lc[i].rgb, u_lc[i].rgb * vec3(1.12, 0.96, 0.72), u_warmth) * att * ndl;\n"
    "            }\n"
    "            vec3 pool = a * add * u_strength;\n"
    "            c += pool;\n"
    "            poolLit = pool;\n"
    "            if (u_soft != 0) {\n"
    "                float m = max(c.r, max(c.g, c.b));\n"
    "                if (m > 0.8) {\n"
    "                    float f = 0.8 + 0.2 * (1.0 - exp(-(m - 0.8) / 0.2));\n"
    "                    c = mix(c, c * (f / m), clamp(dot(pool, vec3(0.3333)) * 4.0, 0.0, 1.0));\n"
    "                }\n"
    "            }\n"
    "            c *= 1.0 - clamp(dark * u_strength * 0.5, 0.0, 0.6);\n"
    "            if (u_poolDebug == 1)\n"
    "                c = vec3(nBlocked > 0.0 ? 0.8 : 0.0, nLit > 0.0 ? 0.8 : 0.0, nAway > 0.0 ? 0.8 : 0.0)\n"
    "                    * (0.4 + 0.6 * clamp(dot(add, vec3(0.3333)) * 2.0, 0.0, 1.0));\n"
    "            else if (u_poolDebug == 2) c = a;\n"
    "            else if (u_poolDebug == 3)\n"
    "                c = shDbg < 0.0 ? c * 0.3 : mix(vec3(0.9, 0.1, 0.1), vec3(0.1, 0.9, 0.1), shDbg);\n"
    "        } else if (u_poolDebug == 2) {\n"
    "            c = vec3(0.5, 0.0, 0.5);\n"
    "        }\n"
    "    }\n"
    "    if (u_waterK.x > 0.0 && solid && texture2D(u_water, uv).r > 0.5) {\n"
    "        vec3 V = normalize(rel);\n"
    "        float ct = clamp(-V.z, 0.0, 1.0);\n"
    "        float fr = clamp((0.02 + 0.98 * pow(1.0 - ct, 5.0)) * u_waterK.x, 0.0, 1.0);\n"
    "        float pa = 0.0;\n"
    "        vec4 rf;\n"
    "        if (u_reflOn > 0.5) {\n"
    "            vec4 pl = texture2D(u_refl, p / u_vp.zw);\n"
    "            pa = pl.a;\n"
    "            rf = vec4(pl.rgb + gl_Fog.color.rgb * (1.0 - pa), pa > 0.5 ? 1.0 : -1.0);\n"
    "        } else {\n"
    "            rf = V.z < 0.0 ? water_reflect(world, V) : vec4(0.0);\n"
    "        }\n"
    "        vec3 rc = rf.rgb;\n"
    "        if (u_night.w >= 0.0) rc = mix(rc, vec3(dot(rc, vec3(0.299, 0.587, 0.114))), u_night.w) * u_night.xyz;\n"
    "        rc = wash_tint(rc, p / u_vp.zw);\n"
    "        if (u_waterK.w > 0.5) c = pa > 0.5 ? rc : rf.a > 0.0 ? mix(vec3(0.5, 0.0, 0.5), rc, rf.a)\n"
    "                                : rf.a < 0.0 ? mix(rc, vec3(0.6, 0.0, 0.6), 0.35) : vec3(0.15, 0.0, 0.15);\n"
    "        else c = mix(c, rc, clamp(max(fr, u_waterFloor * u_waterK.x), 0.0, 1.0) * abs(rf.a));\n"
    "    } else if (u_waterK.w > 0.5 && u_waterK.x > 0.0) {\n"
    "        c *= 0.3;\n"
    "    }\n"
    "    if (u_pools != 0 && u_poolDebug != 0 && !solid) c = vec3(0.0);\n"
    "    if (u_sunMode.x > 0.5 && sunV > -2.5)\n"
    "        c = sunV < -1.5 ? mix(vec3(0.1, 0.25, 0.9) * (1.0 - 0.7 * sunAway), vec3(0.9, 0.1, 0.1), sunCast) : sunV < -0.5 ? c * 0.3\n"
    "          : mix(vec3(0.9, 0.1, 0.1), vec3(0.1, 0.9, 0.1), sunV);\n"
    "    if (u_sunMode.x > 0.5 && charShade >= 0.0 && !(sunV >= 0.0 && sunV < 0.99))\n"
    "        c = mix(vec3(0.1, 0.9, 0.1), vec3(0.05, 0.12, 0.6), charShade);\n"
    "    if (u_grid != 0 && solid) {\n"
    "        vec2 g = world.xy / 256.0;\n"
    "        vec2 f = fwidth(g);\n"
    "        vec2 dist = abs(fract(g - 0.5) - 0.5);\n"
    "        float line = (dist.x < f.x * 1.5 || dist.y < f.y * 1.5) ? 1.0 : 0.0;\n"
    "        float hz = world.z / 16.0;\n"
    "        float contour = (abs(fract(hz - 0.5) - 0.5) < fwidth(hz) * 1.0) ? 1.0 : 0.0;\n"
    "        c = mix(c, hue(world.z / 512.0), 0.35);\n"
    "        c = mix(c, vec3(0.0), contour * 0.6);\n"
    "        c = mix(c, vec3(1.0), line * 0.85);\n"
    "    }\n"
    "    bool inView = (d >= 1.0 || w > 16.0) && p.x >= 0.0 && p.y >= 0.0 && p.x < u_vp.z && p.y < u_vp.w;\n"
    "    bool underNow = u_underK.x > 0.5 && inView;\n"
    "    if (u_fogK.x > 0.5 && inView && !underNow)\n"
    "        c = fog_apply(c, world, d >= 1.0);\n"
    "    if (underNow)\n"
    "        { vec3 eg = min(texture2D(u_glow, uv).rgb, c) * u_underG.x;\n"
    "          c = under_apply(c, eg, min(poolLit, c - eg) * u_underG.x, world, d >= 1.0, N); }\n"
    "    if (u_fogK.x > 0.5 && underNow)\n"
    "        c = fog_apply(c, world, d >= 1.0);\n"
    "    if (u_beamK.y > 0.0) {\n"
    "        vec4 bm = texture2D(u_beam, uv);\n"
    "        float inside = clamp(texture2D(u_beamIn, uv).r, 0.0, 4.0);\n"
    "        if (u_beamS.x > 0.0) {\n"
    "            vec4 sb = bm;\n"
    "            float si = inside;\n"
    "            for (int i = 0; i < 12; i++) {\n"
    "                float a = float(i) * 0.5236 + (i < 6 ? 0.0 : 0.2618);\n"
    "                vec2 o = vec2(cos(a), sin(a)) * u_beamS * (i < 6 ? 0.5 : 1.0);\n"
    "                sb += texture2D(u_beam, uv + o);\n"
    "                si += clamp(texture2D(u_beamIn, uv + o).r, 0.0, 4.0);\n"
    "            }\n"
    "            bm = sb / 13.0;\n"
    "            inside = si / 13.0;\n"
    "        }\n"
    "        vec3 lit = bm.gba * (1.0 - exp(-abs(bm.r) * u_beamK.x)) * u_beamK.y;\n"
    "        lit += c * bm.gba * inside * u_beamK.z;\n"
    "        if (u_beamK.w > 0.5) { c = vec3(inside, clamp(abs(bm.r) / 300.0, 0.0, 1.0), 0.0); lit = vec3(0.0); }\n"
    "        c += lit * (1.0 - clamp(c, 0.0, 1.0));\n"
    "    }\n"
    "    bool uiPx = false;\n"
    "    if (u_haveExact != 0) {\n"
    "        vec2 t1 = vec2(abs(dFdx(uv.x)), abs(dFdy(uv.y)));\n"
    "        uiPx = min(min(texture2D(u_exact, uv).r, min(texture2D(u_exact, uv + vec2(t1.x, 0.0)).r, texture2D(u_exact, uv - vec2(t1.x, 0.0)).r)),\n"
    "                   min(texture2D(u_exact, uv + vec2(0.0, t1.y)).r, texture2D(u_exact, uv - vec2(0.0, t1.y)).r)) < -1.0;\n"
    "    }\n"
    "    if (u_raysK.w > 0.5) c = texture2D(u_rays, uv).rgb;\n"
    "    else if (u_raysK.x + u_raysK.y + u_raysK.z > 0.0 && (d >= 1.0 || w > 16.0) && p.x >= 0.0 && p.y >= 0.0 && p.x < u_vp.z && p.y < u_vp.w\n"
    "             && !uiPx)\n"
    "        c += texture2D(u_rays, uv).rgb * u_raysK.rgb * (1.0 - clamp(c, 0.0, 1.0))\n"
    "             * (d >= 1.0 || u_raysAir <= 0.0 ? 1.0 : 1.0 - exp(-w / u_raysAir));\n"
    "    if (u_bloomK.x > 0.0 && (!uiPx || u_bloomK.y > 0.5)) {\n"
    "        vec3 bl = texture2D(u_bloom, uv).rgb * u_bloomK.x;\n"
    "        c = u_bloomK.y > 0.5 ? bl : c + bl * (1.0 - clamp(c, 0.0, 1.0));\n"
    "    }\n"
    "    if (u_glowView != 0) c = texture2D(u_glow, uv).rgb;\n"
    "    if (u_aoK.x > 0.0 && u_aoK.y > 0.5) c = vec3(u_aoK.y > 1.5 ? ctV : aoV);\n"
    "    if (u_waterK.w > 1.5 && u_reflOn > 0.5) {\n"
    "        vec4 mm = texture2D(u_refl, p / u_vp.zw);\n"
    "        c = mm.a > 0.0 ? mm.rgb : vec3(0.3, 0.0, 0.3);\n"
    "    }\n"
    "    if (u_depthView == 1) c = d >= 1.0 ? vec3(0.0) : texture2D(u_exact, uv).r > 0.0 ? c * 0.4 : vec3(1.0, 0.0, 1.0);\n"
    "    if (u_depthView == 2) c = d >= 1.0 ? vec3(0.0) : N * 0.5 + 0.5;\n"
    "    if (u_invGamma != 1.0) c = pow(clamp(c, 0.0, 1.0), vec3(u_invGamma));\n"
    "    float aa = (u_aa == 1 && (d >= 1.0 || w > 16.0) && p.x >= 0.0 && p.y >= 0.0 && p.x < u_vp.z && p.y < u_vp.w)\n"
    "             ? 1.0 : 0.0;\n"
    "    gl_FragColor = vec4(c, u_aa == 0 ? 1.0 : aa);\n"
    "}\n";

#include "gpuprof.h"
#include "metrics.h"
#include "../telemetry/publisher.h"
#include "../../include/u9telemetry.h"
#include "../common/perfclock.h"
#include "shadowcost.h"
#define GPU_SAMPLE_MS 250u
static int g_gpuScene = -1, g_gpuBeam = -1, g_gpuRays = -1, g_gpuBloom = -1, g_gpuAo = -1,
           g_gpuPoolShadows = -1, g_gpuSunShadows = -1, g_gpuWater = -1, g_gpuComposite = -1;
static uint64_t g_gpuNext;

static void gpu_frame_begin(void)
{
    uint64_t now = u9perf_now();
    int sample = ((u9telemetry_requested_groups() & U9TM_GROUP_BIT(U9TM_GROUP_GPU)) || g_scostOn) && now >= g_gpuNext;
    if (sample) g_gpuNext = now + u9perf_frequency() * GPU_SAMPLE_MS / 1000u;
    u9gpu_frame_begin(u9metrics_current()->frame_no, sample);
    u9gpu_begin(g_gpuScene);
}

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed)
        u9log_line("GLPOST off for this session: %s - the frame draws to the window as before", why);
    g_failed = 1;
}

static int load_entry_points(void)
{
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
    fUniform3f              = (Uniform3fFn)gl_proc("glUniform3f");
    fUniform4f              = (Uniform4fFn)gl_proc("glUniform4f");
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    fDrawBuffers            = (DrawBuffersFn)gl_proc("glDrawBuffers");
    fUniform4fv             = (Uniform4fvFn)gl_proc("glUniform4fv");
    fUniform1fv             = (Uniform1fvFn)gl_proc("glUniform1fv");
    fUniform2fv             = (Uniform2fvFn)gl_proc("glUniform2fv");
    fColorMaski             = (ColorMaskiFn)gl_proc("glColorMaski");
    if (!fColorMaski) fColorMaski = (ColorMaskiFn)gl_proc("glColorMaskIndexedEXT");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer ||
        !fFramebufferTexture2D || !fCheckFramebufferStatus) {
        fail("no framebuffer objects (GL 3.0 or ARB_framebuffer_object)");
        return 0;
    }
    if (!fCreateShader || !fShaderSource || !fCompileShader || !fGetShaderiv ||
        !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog || !fCreateProgram ||
        !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram || !fDeleteShader ||
        !fGetUniformLocation || !fUniform1i || !fUniform1f || !fUniform2f || !fUniform3f || !fUniform4f ||
        !fActiveTexture || !fDrawBuffers || !fUniform4fv || !fUniform1fv || !fUniform2fv) {
        fail("no GLSL entry points");
        return 0;
    }
    return 1;
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
        u9log_line("GLPOST %s shader did not compile: %s", what, log);
        fDeleteShader(s);
        return 0;
    }
    return s;
}

static int make_program(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, k_vertex, "vertex");
    GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, k_fragment, "fragment") : 0;
    GLint  ok = 0;
    if (!vs || !fs) {
        if (vs) fDeleteShader(vs);
        fail("the post shader did not compile");
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
        u9log_line("GLPOST program did not link: %s", log);
        fDeleteProgram(g_prog);
        g_prog = 0;
        fail("the post program did not link");
        return 0;
    }
    g_uFrame    = fGetUniformLocation(g_prog, "u_frame");
    g_uInvGamma = fGetUniformLocation(g_prog, "u_invGamma");
    g_uDepth     = fGetUniformLocation(g_prog, "u_depth");
    g_uGrid      = fGetUniformLocation(g_prog, "u_grid");
    g_uDepthMode = fGetUniformLocation(g_prog, "u_depthMode");
    g_uVp        = fGetUniformLocation(g_prog, "u_vp");
    g_uEngine    = fGetUniformLocation(g_prog, "u_engine");
    g_uProj      = fGetUniformLocation(g_prog, "u_proj");
    g_uZ         = fGetUniformLocation(g_prog, "u_z");
    g_uM0        = fGetUniformLocation(g_prog, "u_m0");
    g_uM1        = fGetUniformLocation(g_prog, "u_m1");
    g_uM2        = fGetUniformLocation(g_prog, "u_m2");
    g_uAlbedo    = fGetUniformLocation(g_prog, "u_albedo");
    g_uPools     = fGetUniformLocation(g_prog, "u_pools");
    g_uMarch     = fGetUniformLocation(g_prog, "u_march");
    g_uStrength  = fGetUniformLocation(g_prog, "u_strength");
    g_uWarmth    = fGetUniformLocation(g_prog, "u_warmth");
    g_uSoft      = fGetUniformLocation(g_prog, "u_soft");
    g_uNl        = fGetUniformLocation(g_prog, "u_nl");
    g_uLp        = fGetUniformLocation(g_prog, "u_lp");
    g_uLc        = fGetUniformLocation(g_prog, "u_lc");
    g_uSurf      = fGetUniformLocation(g_prog, "u_surf");
    g_uPoolDebug = fGetUniformLocation(g_prog, "u_poolDebug");
    g_uContain   = fGetUniformLocation(g_prog, "u_contain");
    g_uKeepOut   = fGetUniformLocation(g_prog, "u_keepOut");
    g_uSpt       = fGetUniformLocation(g_prog, "u_spt");
    g_uNsp       = fGetUniformLocation(g_prog, "u_nsp");
    g_uTail      = fGetUniformLocation(g_prog, "u_tail");
    g_uLsp       = fGetUniformLocation(g_prog, "u_lsp");
    {
        int s;
        char name[8] = "u_sh0";
        for (s = 0; s < U9SHADOW_MAX; s++) {
            name[4] = (char)('0' + s);
            g_uSh[s] = fGetUniformLocation(g_prog, name);
        }
    }
    g_uLsh       = fGetUniformLocation(g_prog, "u_lsh");
    g_uNight     = fGetUniformLocation(g_prog, "u_night");
    g_uNightKeep = fGetUniformLocation(g_prog, "u_nightKeep");
    g_uGlow      = fGetUniformLocation(g_prog, "u_glow");
    g_uGlowView  = fGetUniformLocation(g_prog, "u_glowView");
    g_uBloom     = fGetUniformLocation(g_prog, "u_bloom");
    g_uBloomK    = fGetUniformLocation(g_prog, "u_bloomK");
    g_uBeam      = fGetUniformLocation(g_prog, "u_beam");
    g_uBeamK     = fGetUniformLocation(g_prog, "u_beamK");
    g_uBeamIn    = fGetUniformLocation(g_prog, "u_beamIn");
    g_uBeamS     = fGetUniformLocation(g_prog, "u_beamS");
    g_uRays      = fGetUniformLocation(g_prog, "u_rays");
    g_uRaysK     = fGetUniformLocation(g_prog, "u_raysK");
    g_uRaysAir   = fGetUniformLocation(g_prog, "u_raysAir");
    g_uAo        = fGetUniformLocation(g_prog, "u_ao");
    g_uAoK       = fGetUniformLocation(g_prog, "u_aoK");
    g_uWater     = fGetUniformLocation(g_prog, "u_water");
    g_uWaterK    = fGetUniformLocation(g_prog, "u_waterK");
    g_uRefl      = fGetUniformLocation(g_prog, "u_refl");
    g_uReflOn    = fGetUniformLocation(g_prog, "u_reflOn");
    g_uWaterFloor = fGetUniformLocation(g_prog, "u_waterFloor");
    g_uWash[0]   = fGetUniformLocation(g_prog, "u_wash0");
    g_uWash[1]   = fGetUniformLocation(g_prog, "u_washA0");
    g_uWash[2]   = fGetUniformLocation(g_prog, "u_wash1");
    g_uWash[3]   = fGetUniformLocation(g_prog, "u_washA1");
    g_uExact     = fGetUniformLocation(g_prog, "u_exact");
    g_uHaveExact = fGetUniformLocation(g_prog, "u_haveExact");
    g_uDepthView = fGetUniformLocation(g_prog, "u_depthView");
    g_uNrmAcross = fGetUniformLocation(g_prog, "u_nrmAcross");
    g_uSeams     = fGetUniformLocation(g_prog, "u_seams");
    g_uAa        = fGetUniformLocation(g_prog, "u_aa");
    g_uFogK     = fGetUniformLocation(g_prog, "u_fogK");
    g_uFogBox    = fGetUniformLocation(g_prog, "u_fogBox");
    g_uFogC      = fGetUniformLocation(g_prog, "u_fogC");
    g_uUnderK    = fGetUniformLocation(g_prog, "u_underK");
    g_uUnderA    = fGetUniformLocation(g_prog, "u_underA");
    g_uUnderC    = fGetUniformLocation(g_prog, "u_underC");
    g_uUnderW    = fGetUniformLocation(g_prog, "u_underW");
    g_uUnderB    = fGetUniformLocation(g_prog, "u_underB");
    g_uUnderS    = fGetUniformLocation(g_prog, "u_underS");
    g_uUnderL    = fGetUniformLocation(g_prog, "u_underL");
    g_uUnderG    = fGetUniformLocation(g_prog, "u_underG");
    g_uFogN      = fGetUniformLocation(g_prog, "u_fogN");
    g_uShadowK   = fGetUniformLocation(g_prog, "u_shadowK");
    g_uSun       = fGetUniformLocation(g_prog, "u_sun");
    g_uSunR      = fGetUniformLocation(g_prog, "u_sunR");
    g_uSunU      = fGetUniformLocation(g_prog, "u_sunU");
    g_uSunL      = fGetUniformLocation(g_prog, "u_sunL");
    g_uSunPR     = fGetUniformLocation(g_prog, "u_sunPR");
    g_uSunPU     = fGetUniformLocation(g_prog, "u_sunPU");
    g_uSunPL     = fGetUniformLocation(g_prog, "u_sunPL");
    g_uSunC      = fGetUniformLocation(g_prog, "u_sunC");
    g_uSunK      = fGetUniformLocation(g_prog, "u_sunK");
    g_uSunMode   = fGetUniformLocation(g_prog, "u_sunMode");
    g_uSun2      = fGetUniformLocation(g_prog, "u_sun2");
    g_uSun2C     = fGetUniformLocation(g_prog, "u_sun2C");
    g_uSun2K     = fGetUniformLocation(g_prog, "u_sun2K");
    g_uSunX      = fGetUniformLocation(g_prog, "u_sunX");
    g_uSunO      = fGetUniformLocation(g_prog, "u_sunO");
    g_uSunO2     = fGetUniformLocation(g_prog, "u_sunO2");
    g_uSunOR     = fGetUniformLocation(g_prog, "u_sunOR");
    g_uSunOU     = fGetUniformLocation(g_prog, "u_sunOU");
    g_uSunOL     = fGetUniformLocation(g_prog, "u_sunOL");
    g_uSunOPR    = fGetUniformLocation(g_prog, "u_sunOPR");
    g_uSunOPU    = fGetUniformLocation(g_prog, "u_sunOPU");
    g_uSunOPL    = fGetUniformLocation(g_prog, "u_sunOPL");
    g_uSunOC     = fGetUniformLocation(g_prog, "u_sunOC");
    g_uSunO2C    = fGetUniformLocation(g_prog, "u_sunO2C");
    g_uSunOK     = fGetUniformLocation(g_prog, "u_sunOK");
    g_uSunO2K    = fGetUniformLocation(g_prog, "u_sunO2K");
    g_uSunFade   = fGetUniformLocation(g_prog, "u_sunFade");
    g_uSunBias   = fGetUniformLocation(g_prog, "u_sunBias");
    g_uSunSlope  = fGetUniformLocation(g_prog, "u_sunSlope");
    g_uSunWalls  = fGetUniformLocation(g_prog, "u_sunWalls");
    g_uSunChar   = fGetUniformLocation(g_prog, "u_sunChar");
    g_uSunCharBlend = fGetUniformLocation(g_prog, "u_sunCharBlend");
    g_uSunSmooth = fGetUniformLocation(g_prog, "u_sunSmooth");
    return 1;
}

static void spaces_upload(const U9SpaceSet *set)
{
    fActiveTexture(GL_TEXTURE3);
    if (!g_spaceTex) {
        glGenTextures(1, &g_spaceTex);
        glBindTexture(GL_TEXTURE_2D, g_spaceTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, U9SP_TEX_W, U9SP_TEX_H, 0, GL_RGBA, GL_FLOAT, NULL);
        u9metrics_tex_created((unsigned)(U9SP_TEX_W * U9SP_TEX_H * 16));
    } else {
        glBindTexture(GL_TEXTURE_2D, g_spaceTex);
    }
    if (set->nSpaces)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, U9SP_TEX_W, U9SP_TEX_H, GL_RGBA, GL_FLOAT, set->tex);
    fActiveTexture(GL_TEXTURE0);
    fUniform1i(g_uSpt, 3);
    fUniform1i(g_uNsp, set->nSpaces);
}

static void delete_frame(void)
{
    if (g_fbo) { fDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_colour) {
        glDeleteTextures(1, &g_colour);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_colour = 0;
    }
    if (g_depth) {
        glDeleteTextures(1, &g_depth);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_depth = 0;
    }
    if (g_albedo) {
        glDeleteTextures(1, &g_albedo);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_albedo = 0;
    }
    if (g_glow) {
        glDeleteTextures(1, &g_glow);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_glow = 0;
    }
    if (g_bloomSrc) {
        glDeleteTextures(1, &g_bloomSrc);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_bloomSrc = 0;
    }
    if (g_exact) {
        glDeleteTextures(1, &g_exact);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_exact = 0;
    }
    if (g_sky) {
        glDeleteTextures(1, &g_sky);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_sky = 0;
    }
    if (g_waterTex) {
        glDeleteTextures(1, &g_waterTex);
        u9metrics_tex_destroyed((unsigned)(g_w * g_h * 4));
        g_waterTex = 0;
    }
    g_w = g_h = 0;
}

static GLuint make_texture(GLint internal, GLenum format, GLenum type, int w, int h)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
    u9metrics_tex_created((unsigned)(w * h * 4));
    return t;
}

static int make_frame(int w, int h)
{
    GLenum status;
    delete_frame();
    glPushAttrib(GL_TEXTURE_BIT);
    g_colour = make_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
    g_depth  = make_texture(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, w, h);
    g_albedo = make_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
    g_glow   = make_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
    {
        GLint maxBufs = 0;
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxBufs);
        if (maxBufs >= 4) {
            g_bloomSrc = make_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            u9log_line("GLPOST no bloom: %d draw buffers, 4 needed", (int)maxBufs);
        }
        if (maxBufs >= 5 && fColorMaski && g_bloomSrc) {
            g_exact = make_texture(0x8230 , 0x8227 , GL_FLOAT, w, h);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            u9log_line("GLPOST no exact depth target: %d draw buffers, %s - positions from the depth buffer",
                       (int)maxBufs, fColorMaski ? "glColorMaski present" : "no glColorMaski");
        }
        if (maxBufs >= 6 && g_exact) {
            g_sky = make_texture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            u9log_line("GLPOST no sky copy: %d draw buffers - the sun rays fill the interface from round it", (int)maxBufs);
        }
        if (maxBufs >= 7 && g_exact) {
            g_waterTex = make_texture(0x8229 , GL_RED, GL_UNSIGNED_BYTE, w, h);
        } else {
            u9log_line("GLPOST no water mask: %d draw buffers - no water reflections", (int)maxBufs);
        }
    }
    glPopAttrib();
    g_w = w; g_h = h;
    fGenFramebuffers(1, &g_fbo);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_colour, 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_depth, 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, g_albedo, 0);
    fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, g_glow, 0);
    if (g_bloomSrc)
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, g_bloomSrc, 0);
    if (g_exact)
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3 + 1, GL_TEXTURE_2D, g_exact, 0);
    if (g_sky)
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3 + 2, GL_TEXTURE_2D, g_sky, 0);
    if (g_waterTex)
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3 + 3, GL_TEXTURE_2D, g_waterTex, 0);
    status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() == GL_OUT_OF_MEMORY) {
        u9log_line("GLPOST offscreen frame %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
        delete_frame();
        return 0;
    }
    g_resizes++;
    u9log_line("GLPOST offscreen frame %dx%d: RGBA8 colour, 24/8 depth-stencil, RGBA8 albedo and glow%s%s (frame %u)",
               w, h, g_bloomSrc ? ", RGBA8 bloom source" : "", g_exact ? ", RG32F exact depth and character mark" : "", g_frames);
    if (g_sky) u9log_line("GLPOST sky copy RGBA8 (the sun rays behind the interface)");
    if (g_waterTex) u9log_line("GLPOST water mask R8 (the water reflections)");
    return 1;
}

static void replace_apply(void)
{
    int want = g_pools && g_replace && g_saveSafe && !g_failed && !g_paused;
    unsigned now = want ? u9light_ranged_switch() : 0u;
    if (want && now) {
        int first = !g_replaced;
        g_savedSwitch = now;
        u9light_set_ranged_switch(0u);
        g_replaced = 1;
        g_relightWanted = 1;
        u9log_line("GLPOST pools: the engine's point lights switched off (light system +0x64 was %u%s)",
                   now, first ? "" : ", set again by the engine");
    } else if (!want && g_replaced) {
        u9light_set_ranged_switch(g_savedSwitch);
        g_replaced = 0;
        g_relightWanted = 1;
        u9log_line("GLPOST pools: the engine's point lights back on (+0x64 = %u)", g_savedSwitch);
    }
}

static void replace_restore(void)
{
    if (g_replaced) {
        u9light_set_ranged_switch(g_savedSwitch);
        g_replaced = 0;
    }
}

static void flicker_range_apply(void)
{
    int want = g_pools && g_flickerRange > 0 && g_saveSafe && !g_failed && !g_paused;
    int now = u9light_flicker_range();
    if (want && now != g_flickerRange) {
        int engine = !g_rangeHeld || now != g_rangeWritten;
        if (engine) g_rangeEngine = now;
        u9light_set_flicker_range(g_flickerRange);
        u9log_line("GLPOST pools: flicker range %d -> %d%s", now, g_flickerRange,
                   g_rangeHeld && engine ? " (set again by the engine)" : "");
        g_rangeHeld = 1;
        g_rangeWritten = g_flickerRange;
    } else if (!want && g_rangeHeld) {
        if (now == g_rangeWritten) u9light_set_flicker_range(g_rangeEngine);
        g_rangeHeld = 0;
        u9log_line("GLPOST pools: flicker range back to %d", g_rangeEngine);
    }
}

static void flicker_range_restore(void)
{
    if (g_rangeHeld && u9light_flicker_range() == g_rangeWritten) u9light_set_flicker_range(g_rangeEngine);
    g_rangeHeld = 0;
}

static struct {
    unsigned rec;
    float    rgb[3];
    float    w;
    int      in;
    int      present;
    DWORD    seen;
} g_lt[128];

static int lt_slot(unsigned rec, DWORD now)
{
    int i, oldest = -1;
    for (i = 0; i < 128; i++) {
        if (g_lt[i].rec == rec) return i;
        if (g_lt[i].present) continue;
        if (oldest < 0 || now - g_lt[i].seen > now - g_lt[oldest].seen) oldest = i;
    }
    if (oldest < 0) oldest = 0;
    memset(&g_lt[oldest], 0, sizeof g_lt[oldest]);
    g_lt[oldest].rec = rec;
    g_lt[oldest].seen = now - 1000u;
    return oldest;
}

static void pool_colour(const U9LightSnap *l, int slot, float dt, DWORD now, float out[3])
{
    float target[3];
    int k;
    if (!(l->flags & 0x01u)) {
        out[0] = l->rgb[0]; out[1] = l->rgb[1]; out[2] = l->rgb[2];
        return;
    }
    for (k = 0; k < 3; k++)
        target[k] = l->stepped ? l->rgb[k] : l->baseRgb[k] * (1.0f - 0.5f * l->flickerDepth);
    if (now - g_lt[slot].seen > 500u) {
        for (k = 0; k < 3; k++) g_lt[slot].rgb[k] = target[k];
    } else {
        float a = g_flickerSmooth > 0 ? 1.0f - expf(-dt * 1000.0f / (float)g_flickerSmooth) : 1.0f;
        for (k = 0; k < 3; k++) g_lt[slot].rgb[k] += (target[k] - g_lt[slot].rgb[k]) * a;
    }
    for (k = 0; k < 3; k++) out[k] = g_lt[slot].rgb[k];
}

static float pool_score(const U9LightSnap *l, const U9FrameView *v, float reach, int *inView)
{
    float d[3], cs[3], dist, lum, t, q;
    const float *rgb = (l->baseRgb[0] + l->baseRgb[1] + l->baseRgb[2] > 0.0f) ? l->baseRgb : l->rgb;
    int i;
    for (i = 0; i < 3; i++) d[i] = l->pos[i] - v->m[i * 4 + 3];
    for (i = 0; i < 3; i++)
        cs[i] = v->m[i] * d[0] + v->m[4 + i] * d[1] + v->m[8 + i] * d[2];
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *inView = 0;
    if (cs[1] < -reach) return 0.0f;
    if (v->sx > 0.0f && v->sy > 0.0f) {
        t = (v->engineW - v->cx) / v->sx; q = sqrtf(1.0f + t * t);
        if ((cs[0] - cs[1] * t) / q > reach) return 0.0f;
        t = v->cx / v->sx; q = sqrtf(1.0f + t * t);
        if ((-cs[0] - cs[1] * t) / q > reach) return 0.0f;
        t = v->cy / v->sy; q = sqrtf(1.0f + t * t);
        if ((cs[2] - cs[1] * t) / q > reach) return 0.0f;
        t = (v->engineH - v->cy) / v->sy; q = sqrtf(1.0f + t * t);
        if ((-cs[2] - cs[1] * t) / q > reach) return 0.0f;
    }
    *inView = 1;
    lum = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
    if (rgb[2] > lum) lum = rgb[2];
    if (lum < 0.05f) lum = 0.05f;
    return lum * reach / (dist > 0.25f * reach ? dist : 0.25f * reach);
}

static int pool_select(const U9LightSnap *all, int n, const U9FrameView *v, float dt, DWORD now,
                       int *idx, float *w, int *slot)
{
    static float key[128];
    static int   sl[128], vis[128], order[128];
    int k, j, m, count = 0, nWant, waited = 0;
    float reachMul = (g_tail && g_tailReach > 1.0f) ? g_tailReach : 1.0f;
    float step = g_fadeMs > 0 ? dt * 1000.0f / (float)g_fadeMs : 1.0f;
    for (k = 0; k < 128; k++) g_lt[k].present = 0;
    for (k = 0; k < n; k++) {
        float reach = all[k].range * reachMul, sc;
        sl[k] = lt_slot(all[k].rec, now);
        g_lt[sl[k]].present = 1;
        sc = pool_score(&all[k], v, reach, &vis[k]);
        if (!g_rank) {
            float dx = all[k].pos[0] - v->m[3], dy = all[k].pos[1] - v->m[7], dz = all[k].pos[2] - v->m[11];
            sc = -(sqrtf(dx * dx + dy * dy + dz * dz) - reach);
        } else if (g_lt[sl[k]].in) {
            sc *= 1.25f;
        }
        key[k] = sc;
        for (j = k; j > 0 && key[order[j - 1]] < sc; j--) order[j] = order[j - 1];
        order[j] = k;
    }
    for (k = 0; k < 128; k++)
        if (g_lt[k].in && !g_lt[k].present) { g_lt[k].in = 0; g_lt[k].w = 0.0f; }
    nWant = n < g_maxLights ? n : g_maxLights;
    for (j = 0; j < n; j++) {
        int i = order[j], e = sl[i];
        if (!g_lt[e].in) continue;
        if (!vis[i] && j >= nWant) { g_lt[e].in = 0; g_lt[e].w = 0.0f; continue; }
        if (j < nWant) {
            g_lt[e].w += step;
            if (g_lt[e].w > 1.0f) g_lt[e].w = 1.0f;
        } else {
            if (g_lt[e].w >= 1.0f) g_fadeOuts++;
            g_lt[e].w -= step;
            if (g_lt[e].w <= 0.0f) { g_lt[e].in = 0; g_lt[e].w = 0.0f; continue; }
        }
        count++;
    }
    for (j = 0; j < nWant; j++) {
        int i = order[j], e = sl[i];
        if (g_lt[e].in) continue;
        if (count >= g_maxLights) { waited = 1; continue; }
        g_lt[e].in = 1;
        g_lt[e].w = vis[i] ? (g_fadeMs > 0 ? step : 1.0f) : 1.0f;
        if (vis[i] && g_fadeMs > 0) g_fadeIns++;
        count++;
    }
    if (waited) g_capWait++;
    for (j = 0, m = 0; j < n && m < MAX_POOL_LIGHTS; j++) {
        int i = order[j];
        if (!g_lt[sl[i]].in) continue;
        idx[m] = i;
        w[m] = g_lt[sl[i]].w;
        slot[m] = sl[i];
        m++;
    }
    return m;
}

#ifndef U9CALL
#define U9CALL __attribute__((thiscall))
#endif
typedef int (U9CALL *LightsFileFn)(void *ls, int file);
static LightsFileFn o_lightsSave, o_lightsLoad;
static unsigned     g_saveSwaps, g_loadRepairs;

static int U9CALL h_lights_save(void *ls, int file)
{
    int r, held = g_replaced, range = g_rangeHeld && u9light_flicker_range() == g_rangeWritten;
    if (held) {
        u9light_set_ranged_switch(g_savedSwitch);
        g_saveSwaps++;
    }
    if (range) u9light_set_flicker_range(g_rangeEngine);
    r = o_lightsSave(ls, file);
    if (held) u9light_set_ranged_switch(0u);
    if (range) u9light_set_flicker_range(g_rangeWritten);
    return r;
}

static int U9CALL h_lights_load(void *ls, int file)
{
    int r = o_lightsLoad(ls, file);
    g_loads++;
    if (r && u9light_ranged_switch() == 0u) {
        u9light_set_ranged_switch(1u);
        g_loadRepairs++;
        u9log_line("GLPOST the save loaded had the engine's point lights switched off (+0x64 = 0, left"
                   " by the light pools' replace mode in an earlier build) - put back to 1");
    }
    g_replaced = 0;
    return r;
}

typedef void (U9CALL *MarkAllDirtyFn)(void *ls, int force, unsigned flags);
static unsigned g_relights;

static void relight_all(void)
{
    MarkAllDirtyFn fn;
    if (!g_relightWanted) return;
    g_relightWanted = 0;
    fn = (MarkAllDirtyFn)u9sym(U9SYM_LIGHTSYS_MARK_ALL_DIRTY);
    if (!fn) return;
    fn((void *)0x008515F0u, 1, 2u);
    g_relights++;
}

void u9glpost_install_hooks(void)
{
    static const uint8_t expSave[5] = { 0xE8, 0x78, 0xD6, 0xFB, 0xFF };
    static const uint8_t expLoad[5] = { 0xE8, 0x92, 0xAC, 0xFB, 0xFF };
    static const U9PatchSpec specSave = {
        "LIGHTS_SAVE_SWITCH", U9PATCH_CALLSITE, U9SYM_LIGHTS_SAVE_CALL, 5, expSave, 5, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specLoad = {
        "LIGHTS_LOAD_SWITCH", U9PATCH_CALLSITE, U9SYM_LIGHTS_LOAD_CALL, 5, expLoad, 5, U9PF_EXCLUSIVE
    };
    o_lightsSave = (LightsFileFn)u9patch_install(&specSave, (void *)h_lights_save);
    o_lightsLoad = (LightsFileFn)u9patch_install(&specLoad, (void *)h_lights_load);
    u9log_line("GLPOST light switch in saves: Lights_Save %s, Lights_Load %s", o_lightsSave ? "hooked" : "REFUSED",
               o_lightsLoad ? "hooked (a switched-off save is repaired)" : "REFUSED");
    g_saveSafe = o_lightsSave != NULL;
}

void u9glpost_init(void)
{
    u9scost_init();
    g_gpuScene = u9gpu_region("scene");
    g_gpuBeam = u9gpu_region("beam");
    g_gpuRays = u9gpu_region("rays");
    g_gpuBloom = u9gpu_region("bloom");
    g_gpuAo = u9gpu_region("ao");
    g_gpuPoolShadows = u9gpu_region("pool_shadows");
    g_gpuSunShadows = u9gpu_region("sun_shadows");
    g_gpuWater = u9gpu_region("water");
    g_gpuComposite = u9gpu_region("composite");
    g_want      = u9cfg_int("gl_post", 1);
    g_wantGamma = u9cfg_int("gl_post_gamma", 1);
    g_pools     = u9cfg_int("gl_light_pools", 1);
    g_replace   = u9cfg_int("gl_light_replace", 1);
    g_march     = u9cfg_int("gl_light_march", 0);
    g_contain   = u9cfg_int("gl_light_contain", 1);
    g_keepOut   = u9cfg_int("gl_light_keep_out", 1);
    g_tail      = u9cfg_int("gl_light_tail", 2);
    if (g_tail < 0 || g_tail > 2) g_tail = 2;
    g_tailLevel = (float)u9cfg_int("gl_light_tail_level", 25) / 100.0f;
    g_tailReach = (float)u9cfg_int("gl_light_tail_reach", 175) / 100.0f;
    g_strength  = (float)u9cfg_int("gl_light_strength", 100) / 100.0f;
    g_warmth    = (float)u9cfg_int("gl_light_warmth", 100) / 100.0f;
    if (g_warmth < 0.0f) g_warmth = 0.0f;
    if (g_warmth > 1.0f) g_warmth = 1.0f;
    g_soft      = u9cfg_int("gl_light_soft", 0);
    g_flickerRange  = u9cfg_int("gl_light_flicker_range", 5000);
    g_flickerSmooth = u9cfg_int("gl_light_flicker_smooth", 150);
    if (g_flickerSmooth < 0) g_flickerSmooth = 0;
    g_rank      = u9cfg_int("gl_light_rank", 1) != 0;
    g_night     = u9cfg_int("gl_night_grade", 100);
    g_nightDark = u9cfg_int("gl_night_dark", 35);
    g_nightCool = u9cfg_int("gl_night_cool", 50);
    g_nightGrey = u9cfg_int("gl_night_grey", 20);
    g_nightKeep = u9cfg_int("gl_night_keep", 50);
    map_list(u9cfg_str("gl_night_graded_textures", "4949"), g_nightGradedList, sizeof g_nightGradedList);
    g_dungeon     = u9cfg_int("gl_dungeon_grade", 1);
    g_dungeonDark = u9cfg_int("gl_dungeon_dark", 80);
    g_dungeonCool = u9cfg_int("gl_dungeon_cool", 200);
    g_dungeonGrey = u9cfg_int("gl_dungeon_grey", 0);
    g_effectFade    = u9cfg_int("gl_effect_fade", 0);
    g_effectDepthWrite = u9cfg_int("gl_effect_depth_write", 1);
    g_effectFullSize   = u9cfg_int("gl_effect_full_size", 0);
    g_depthGlide       = u9cfg_int("gl_depth_glide", 1);
    g_depthSlack       = u9cfg_int("gl_depth_glide_slack", 0);
    if (g_depthSlack < 0) g_depthSlack = 0;
    if (g_depthSlack > 64) g_depthSlack = 64;
    if (g_effectFade < 0) g_effectFade = 0;
    if (g_effectFade > 50) g_effectFade = 50;
    g_bloom         = u9cfg_int("gl_bloom", 1);
    g_bloomLava     = u9cfg_int("gl_bloom_lava", 1);
    g_bloomFire     = u9cfg_int("gl_bloom_fire", 100);
    if (g_bloomFire < 0) g_bloomFire = 0;
    g_bloomAdditive = u9cfg_int("gl_bloom_additive", 100);
    if (g_bloomAdditive < 0) g_bloomAdditive = 0;
    g_bloomGlow     = u9cfg_int("gl_bloom_glow", 100);
    g_bloomRunes    = u9cfg_int("gl_bloom_runes", 300);
    if (g_bloomRunes < 0) g_bloomRunes = 0;
    if (g_bloomGlow < 0) g_bloomGlow = 0;
    map_list(u9cfg_str("gl_bloom_glow_textures", "6429,6412,6143,6500,6501,4664,6415,4328,4329,4330,4761,4762"), g_bloomGlowList, sizeof g_bloomGlowList);
    g_bloomGlyphs   = u9cfg_int("gl_bloom_glyphs", 300);
    if (g_bloomGlyphs < 0) g_bloomGlyphs = 0;
    g_bloomSurface  = u9cfg_int("gl_bloom_surface", 100);
    if (g_bloomSurface < 0) g_bloomSurface = 0;
    map_list(u9cfg_str("gl_bloom_surface_textures", "3160,2197,6552,6553,1082,6006,6007,6008,6009,6010,6011,6012,6013,5831,3184,3319,6511,6512,6513,6514,6515,6516,6517,6518,7336,6843,5895,5896,6383,6973,6966,3703,6953,6961"), g_bloomSurfaceList, sizeof g_bloomSurfaceList);
    map_list(u9cfg_str("gl_guardian_textures", "1405,1406,3116,3133,3134,3135,3230,3231,3232,7411"), g_guardianList, sizeof g_guardianList);
    g_guardianGlow   = u9cfg_int("gl_guardian_glow", 150);
    g_guardianBright = u9cfg_int("gl_guardian_bright", 175);
    if (g_guardianGlow < 0) g_guardianGlow = 0;
    if (g_guardianBright < 0) g_guardianBright = 0;
    g_bloomStrength = u9cfg_int("gl_bloom_strength", 200);
    if (g_bloomStrength < 0) g_bloomStrength = 0;
    g_waterOn       = u9cfg_int("gl_water_reflect", 1);
    g_waterStrength = u9cfg_int("gl_water_reflect_strength", 100);
    if (g_waterStrength < 0) g_waterStrength = 0;
    g_waterDist     = u9cfg_int("gl_water_reflect_distance", 8000);
    g_waterSteps    = u9cfg_int("gl_water_reflect_steps", 32);
    g_waterFloor    = u9cfg_int("gl_water_reflect_floor", 25);
    g_dome          = u9cfg_int("gl_dome_light", 1);
    g_domeBright    = u9cfg_int("gl_dome_light_bright", 250);
    if (g_domeBright < 0) g_domeBright = 0;
    if (g_domeBright > 1000) g_domeBright = 1000;
    g_domeReach     = u9cfg_int("gl_dome_light_reach", 3000);
    if (g_domeReach < 50) g_domeReach = 50;
    {
        const char *at = u9cfg_str("gl_dome_light_at", "9:36111,73962,962");
        int mp; float x, y, z;
        if (at && sscanf(at, "%d:%f,%f,%f", &mp, &x, &y, &z) == 4) {
            g_domeMap = mp; g_domeAt[0] = x; g_domeAt[1] = y; g_domeAt[2] = z;
        }
        g_domeRadius = u9cfg_int("gl_dome_light_radius", 800);
        if (g_domeRadius < 50) g_domeRadius = 50;
    }
    {
        const char *s = u9cfg_str("gl_dome_light_textures", "6486,6487");
        g_nDomeTex = 0;
        while (s && *s && g_nDomeTex < 8) {
            int id = atoi(s);
            if (id > 0) g_domeTex[g_nDomeTex++] = id;
            while (*s && *s != ',') s++;
            if (*s == ',') s++;
        }
    }
    g_moonBright    = moon_percent(u9cfg_int("gl_moon_bright", 100));
    g_moonBloom     = moon_percent(u9cfg_int("gl_moon_bloom", 100));
    g_moonHalo      = moon_percent(u9cfg_int("gl_moon_halo", 100));
    g_moonHaloBloom = moon_percent(u9cfg_int("gl_moon_halo_bloom", 100));
    g_moonPhase     = u9cfg_int("gl_moon_phase", 1);
    moon_phase_list(u9cfg_str("gl_moon_phase_percent", "0,25,50,75,100"));
    u9shadow_init();
    u9moon_init();
    u9fog_init();
    u9under_init();
    u9beam_init();
    u9rays_init();
    u9ao_init();
    u9reflect_init();
    u9aa_init();
    g_maxLights = u9cfg_int("gl_light_max", 128);
    if (g_maxLights < 1) g_maxLights = 1;
    if (g_maxLights > MAX_POOL_LIGHTS) g_maxLights = MAX_POOL_LIGHTS;
    g_fadeMs    = u9cfg_int("gl_light_fade", 250);
    if (g_fadeMs < 0) g_fadeMs = 0;
    u9life_push("light pools replace mode", replace_restore);
    u9life_push("light pools flicker range", flicker_range_restore);
    if (g_pools && g_want)
        u9log_line("GLPOST light pools on: up to %d engine lights per frame, per pixel, rooms %s,"
                   " wall test %s, strength %.2f, the engine's point lights %s", g_maxLights,
                   g_contain ? "on" : "off", g_march ? "on" : "off", (double)g_strength,
                   g_replace ? "replaced" : "kept");
    else if (g_pools)
        u9log_line("GLPOST light pools need gl_post = 1 - off");
    if (g_want)
        u9log_line("GLPOST on: the frame draws offscreen and is copied to the window by the post"
                   " pass%s", g_wantGamma ? ", which applies the engine's gamma" : "");
}

unsigned u9glpost_exact_tex(void)
{
    return g_exact;
}

int u9glpost_running(void)
{
    return g_want && !g_paused && !g_failed && g_fbo;
}

int u9glpost_frame(int *w, int *h)
{
    *w = g_w;
    *h = g_h;
    return g_bound && g_fbo;
}

int u9glpost_owns_gamma(void)
{
    return g_want && g_wantGamma && !g_failed;
}

void u9glpost_begin(int surfW, int surfH, int *frameW, int *frameH)
{
    int s;
    *frameW = surfW;
    *frameH = surfH;
    if (!g_want || g_paused || g_failed || surfW <= 0 || surfH <= 0 || !wglGetCurrentContext()) return;
    if (!g_loaded) {
        if (!load_entry_points() || !make_program()) return;
        g_loaded = 1;
    }
    s = u9aa_scale();
    if (s > 1) {
        GLint maxTex = 0, maxVp[2] = { 0, 0 };
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxVp);
        if (surfW * s > maxTex || surfH * s > maxTex || surfW * s > maxVp[0] || surfH * s > maxVp[1]) {
            u9log_line("GLPOST %dx%d at %dx is over the GL limits: texture %d, viewport %dx%d", surfW, surfH, s,
                       (int)maxTex, (int)maxVp[0], (int)maxVp[1]);
            u9aa_scale_refused("over the GL size limits");
            s = 1;
        }
    }
    if (surfW * s != g_w || surfH * s != g_h) {
        if (!make_frame(surfW * s, surfH * s)) {
            if (s == 1) { fail("the offscreen frame was incomplete"); return; }
            u9aa_scale_refused("the larger frame could not be made");
            s = 1;
            if (!make_frame(surfW, surfH)) { fail("the offscreen frame was incomplete"); return; }
        }
    }
    g_scale = s;
    *frameW = g_w;
    *frameH = g_h;
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g_bound = 1;
    gpu_frame_begin();
    g_waterMarked = 0;
    if (g_albedo) {
        GLfloat cc[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
        glDisable(GL_SCISSOR_TEST);
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawBuffer(GL_COLOR_ATTACHMENT2);
        glClear(GL_COLOR_BUFFER_BIT);
        if (g_bloomSrc) {
            glDrawBuffer(GL_COLOR_ATTACHMENT3);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (g_exact) {
            glDrawBuffer(GL_COLOR_ATTACHMENT3 + 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (g_sky && u9rays_sun_on()) {
            glDrawBuffer(GL_COLOR_ATTACHMENT3 + 2);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (g_waterTex && g_waterOn) {
            glDrawBuffer(GL_COLOR_ATTACHMENT3 + 3);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glClearColor(cc[0], cc[1], cc[2], cc[3]);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    replace_apply();
    flicker_range_apply();
    relight_all();
}

void u9glpost_albedo_targets(int on)
{
    GLenum bufs[6];
    int bloom = g_bloom && g_bloomSrc;
    int sky = g_sky && g_exact && u9rays_sun_on();
    if (!g_bound || !g_albedo || (!g_pools && !g_night && !g_dungeon && !bloom && !g_exact)) return;
    bufs[0] = GL_COLOR_ATTACHMENT0;
    bufs[1] = g_pools ? GL_COLOR_ATTACHMENT1 : GL_NONE;
    bufs[2] = (g_night || g_dungeon) ? GL_COLOR_ATTACHMENT2 : GL_NONE;
    bufs[3] = bloom ? GL_COLOR_ATTACHMENT3 : GL_NONE;
    bufs[4] = GL_COLOR_ATTACHMENT3 + 1;
    bufs[5] = GL_COLOR_ATTACHMENT3 + 2;
    fDrawBuffers(!on ? 1 : sky ? 6 : g_exact ? 5 : bloom ? 4 : (g_night || g_dungeon) ? 3 : 2, bufs);
}

static GLint g_waterBufs[7];
int u9glpost_water_mark_begin(void)
{
    GLenum one = GL_COLOR_ATTACHMENT3 + 3;
    int i;
    if (!g_waterTex || !g_waterOn || !g_bound || !g_albedo) return 0;
    for (i = 0; i < 7; i++) {
        g_waterBufs[i] = GL_NONE;
        glGetIntegerv(0x8825  + i, &g_waterBufs[i]);
    }
    fDrawBuffers(1, &one);
    g_waterMarked = 1;
    return 1;
}

void u9glpost_water_mark_end(void)
{
    GLenum bufs[7];
    int i;
    for (i = 0; i < 7; i++) bufs[i] = (GLenum)g_waterBufs[i];
    fDrawBuffers(7, bufs);
}

void u9glpost_exact_mask(int on)
{
    GLboolean m = on ? GL_TRUE : GL_FALSE;
    if (g_exact && g_bound && fColorMaski) fColorMaski(4, m, m, m, m);
}

void u9glpost_bloom_mask(int on)
{
    GLboolean m = on ? GL_TRUE : GL_FALSE;
    if (g_bloomSrc && g_bound && fColorMaski) fColorMaski(3, m, m, m, m);
    if (g_sky && g_bound && fColorMaski) fColorMaski(5, m, m, m, m);
}

float u9glpost_effect_fade(void)
{
    float w = (float)(g_effectFade ? g_effectFade : 12) / 100.0f;
    if (!g_bound) return 0.0f;
    if (g_effectFadeDebug) return -w;
    return g_effectFade ? w : 0.0f;
}

float u9glpost_effect_lod_bias(int effect)
{
    return (effect && g_effectFullSize) ? -16.0f : 0.0f;
}

int u9glpost_depth_slack(void)
{
    return g_depthSlack;
}

int u9glpost_depth_glide(void)
{
    return g_depthGlide;
}

int u9glpost_effect_depth_write(void)
{
    return g_effectDepthWrite;
}

static const char *g_bloomWhy = "";
const char *u9glpost_bloom_why(void) { return g_bloomWhy; }

float u9glpost_bloom_share(int texId, int effect, int additive)
{
    if (!g_bloom || !g_bloomSrc || !g_bound) {
        g_bloomWhy = !g_bloom ? "bloom off" : !g_bloomSrc ? "no bloom target" : "frame not bound";
        return 0.0f;
    }
    if (u9lava_is(texId)) { g_bloomWhy = "lava"; return g_bloomLava ? 1.0f : 0.0f; }
    if (u9fx_is_rune(texId)) { g_bloomWhy = "rune"; return (float)g_bloomRunes / 100.0f; }
    if (u9fx_is_pillar(texId)) { g_bloomWhy = "small columns"; return u9fx_pillar_glow(); }
    if (u9fx_is_glyph(texId)) { g_bloomWhy = "glyphs"; return (float)g_bloomGlyphs / 100.0f; }
    if (map_listed(g_guardianList, texId)) { g_bloomWhy = "guardian"; return (float)g_guardianGlow / 100.0f; }
    if (map_listed(g_bloomSurfaceList, texId)) { g_bloomWhy = "surface list"; return (float)g_bloomSurface / 100.0f; }
    g_bloomWhy = "not listed";
    if (!effect) return 0.0f;
    if (u9fx_is_flame(texId)) { g_bloomWhy = "flame"; return (float)g_bloomFire / 100.0f; }
    if (map_listed(g_bloomGlowList, texId)) { g_bloomWhy = "glow list"; return (float)g_bloomGlow / 100.0f; }
    g_bloomWhy = additive ? "additive" : "effect, not listed";
    if (additive) return (float)g_bloomAdditive / 100.0f;
    return 0.0f;
}

static int moon_percent(int v)
{
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

float u9glpost_moon_bright(void) { return (float)g_moonBright / 100.0f; }

float u9glpost_guardian_bright(int texId)
{
    return map_listed(g_guardianList, texId) ? (float)g_guardianBright / 100.0f : 1.0f;
}

int u9glpost_dome_tex(int texId)
{
    int k;
    for (k = 0; k < g_nDomeTex; k++) if (g_domeTex[k] == texId) return 1;
    return 0;
}
float u9glpost_dome_strength(void) { return g_dome ? (float)g_domeBright / 100.0f : 0.0f; }
int u9glpost_dome_here(int map, const float p[3])
{
    float dx = p[0] - g_domeAt[0], dy = p[1] - g_domeAt[1], dz = p[2] - g_domeAt[2];
    float r = (float)g_domeRadius;
    return map == g_domeMap && dx * dx + dy * dy + dz * dz < r * r;
}
unsigned u9glpost_loads(void) { return g_loads; }
float u9glpost_dome_range(void)    { return (float)g_domeReach; }
float u9glpost_moon_halo(void)   { return (float)g_moonHalo / 100.0f; }

static void moon_phase_list(const char *s)
{
    int k;
    for (k = 0; k < 5 && s && *s; k++) {
        while (*s == ' ' || *s == ',') s++;
        if (*s < '0' || *s > '9') break;
        g_moonPhasePct[k] = moon_percent(atoi(s));
        while (*s >= '0' && *s <= '9') s++;
    }
}

float u9glpost_moon_phase(int cls)
{
    if (!g_moonPhase || cls < 0 || cls > 4) return 1.0f;
    return (float)g_moonPhasePct[cls] / 100.0f;
}

float u9glpost_moon_bloom_setting(int halo)
{
    if (!g_bloom) return 0.0f;
    return (float)(halo ? g_moonHaloBloom : g_moonBloom) / 100.0f;
}

float u9glpost_moon_bloom(int halo)
{
    if (!g_bloom || !g_bloomSrc || !g_bound) return 0.0f;
    return (float)(halo ? g_moonHaloBloom : g_moonBloom) / 100.0f;
}

int u9glpost_bloom_lava(void)
{
    return g_bloom && g_bloomLava && g_bloomSrc && g_bound;
}

static void check_copy(int w, int h, float inv, int pools)
{
    SIZE_T bytes = (SIZE_T)w * (SIZE_T)h * 4u, i;
    unsigned char *a = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, bytes);
    unsigned char *b = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, bytes);
    unsigned differ = 0, maxd = 0;
    if (!a || !b) {
        u9log_line("GLPOST check skipped: no memory for %dx%d", w, h);
    } else {
        GLint pack = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, a);
        fBindFramebuffer(GL_FRAMEBUFFER, (GLuint)u9aa_fbo());
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, b);
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, pack);
        for (i = 0; i < bytes; i += 4) {
            unsigned k, px = 0;
            for (k = 0; k < 3; k++) {
                unsigned d = a[i + k] > b[i + k] ? a[i + k] - b[i + k] : b[i + k] - a[i + k];
                if (d) px = 1;
                if (d > maxd) maxd = d;
            }
            differ += px;
        }
        g_checks++;
        if (inv == 1.0f && !pools && differ) g_checkFails++;
        u9log_line("GLPOST check at post frame %u: %dx%d, %u pixels differ (largest %u of 255)%s",
                   g_frames, w, h, differ, maxd,
                   pools ? " - the light pools are drawn, so they should" :
                   inv != 1.0f ? " - gamma is applied, so they should" :
                   differ ? " - NOT AN EXACT COPY" : " - exact copy");
    }
    if (a) HeapFree(GetProcessHeap(), 0, a);
    if (b) HeapFree(GetProcessHeap(), 0, b);
}

static float eye_depth(const U9FrameView *v, float d)
{
    return (v->depthMode == 0) ? v->zRef / (1.0f - d > 1e-6f ? 1.0f - d : 1e-6f)
                               : v->zRef + d * (v->zFar - v->zRef);
}

static void pixel_to_world(const U9FrameView *v, int px, int py, float w, float world[3])
{
    float ex = ((float)(px - v->vx) + 0.5f) * v->engineW / (float)v->vw;
    float ey = ((float)(v->vh - (py - v->vy)) - 0.5f) * v->engineH / (float)v->vh;
    float cs[3];
    int k;
    cs[0] = (ex - v->cx) * w / v->sx;
    cs[1] = w;
    cs[2] = (v->cy - ey) * w / v->sy;
    for (k = 0; k < 3; k++)
        world[k] = v->m[k * 4] * cs[0] + v->m[k * 4 + 1] * cs[1] + v->m[k * 4 + 2] * cs[2] + v->m[k * 4 + 3];
}

static int world_to_pixel(const U9FrameView *v, const float p[3], int *px, int *py)
{
    float d[3], cs[3], ex, ey;
    int k;
    for (k = 0; k < 3; k++) d[k] = p[k] - v->m[k * 4 + 3];
    for (k = 0; k < 3; k++) cs[k] = v->m[k] * d[0] + v->m[4 + k] * d[1] + v->m[8 + k] * d[2];
    if (cs[1] <= 1.0f) return 0;
    ex = v->cx + cs[0] * v->sx / cs[1];
    ey = v->cy - cs[2] * v->sy / cs[1];
    *px = v->vx + (int)(ex * (float)v->vw / v->engineW);
    *py = v->vy + v->vh - 1 - (int)(ey * (float)v->vh / v->engineH);
    return *px >= v->vx && *py >= v->vy && *px < v->vx + v->vw && *py < v->vy + v->vh;
}

int u9glpost_point_covered(const U9FrameView *v, const float p[3], float margin)
{
    float depth, ew = 0.0f;
    int px, py, j;
    if (!g_fbo || !v->valid || !world_to_pixel(v, p, &px, &py)) return -1;
    for (j = 0; j < 3; j++) ew += v->m[j * 4 + 1] * (p[j] - v->m[j * 4 + 3]);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (depth >= 1.0f) return 0;
    return eye_depth(v, depth) < ew - margin;
}

static int find_depth(const U9FrameView *v, int px, int py, int r, float nearW,
                      int *ox, int *oy, float *ow)
{
    static float block[65 * 65];
    int n = 2 * r + 1, x, y, best = -1;
    float bestD2 = 1e30f;
    if (r > 32) r = 32, n = 65;
    if (g_w < n || g_h < n) return 0;
    if (px < r) px = r;
    if (py < r) py = r;
    if (px > g_w - 1 - r) px = g_w - 1 - r;
    if (py > g_h - 1 - r) py = g_h - 1 - r;
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(px - r, py - r, n, n, GL_DEPTH_COMPONENT, GL_FLOAT, block);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++) {
            float d = block[y * n + x], d2;
            if (d >= 1.0f || eye_depth(v, d) <= nearW) continue;
            d2 = (float)((x - r) * (x - r) + (y - r) * (y - r));
            if (d2 < bestD2) { bestD2 = d2; best = y * n + x; }
        }
    if (best < 0) return 0;
    *ox = px - r + best % n;
    *oy = py - r + best / n;
    *ow = eye_depth(v, block[best]);
    return 1;
}

int u9avatar_position(int *x, int *y, int *z);

static void probe(const U9FrameView *v)
{
    void (*out)(const char *fmt, ...) = g_probeOut;
    int px, py, qx, qy, ax, ay, az;
    float w, world[3];
    px = v->vx + (int)(v->cx * (float)v->vw / v->engineW);
    py = v->vy + v->vh - 1 - (int)(v->cy * (float)v->vh / v->engineH);
    if (find_depth(v, px, py, 32, 16.0f, &qx, &qy, &w)) {
        pixel_to_world(v, qx, qy, w, world);
        u9log_line("GLPOST probe centre: pixel %d,%d (centre %d,%d) eye %.1f -> world %.0f,%.0f,%.0f;"
                   " camera at %.0f,%.0f,%.0f, mode %d zFar %.0f", qx, qy, px, py, w, world[0],
                   world[1], world[2], v->m[3], v->m[7], v->m[11], v->depthMode, v->zFar);
        if (out) out("centre of the view: world %.0f,%.0f,%.0f, %.0f ahead", world[0], world[1],
                     world[2], w);
    } else if (out) {
        out("nothing near the centre of the view - the sky?");
    }
    if (u9avatar_position(&ax, &ay, &az)) {
        float a[3], dx, dy, dz;
        a[0] = (float)ax; a[1] = (float)ay; a[2] = (float)az + 8.0f;
        if (!world_to_pixel(v, a, &px, &py)) {
            if (out) out("the Avatar is not on screen");
            return;
        }
        if (!find_depth(v, px, py, 6, 16.0f, &qx, &qy, &w)) {
            if (out) out("no depth at the Avatar's feet");
            return;
        }
        pixel_to_world(v, qx, qy, w, world);
        dx = world[0] - a[0]; dy = world[1] - a[1]; dz = world[2] - (float)az;
        u9log_line("GLPOST probe Avatar: at %d,%d,%d -> pixel %d,%d, depth found at %d,%d eye %.1f"
                   " -> world %.0f,%.0f,%.0f; off by %.0f,%.0f,%.0f (%.0f across)", ax, ay, az, px, py,
                   qx, qy, w, world[0], world[1], world[2], dx, dy, dz, sqrtf(dx * dx + dy * dy));
        if (out) out("Avatar at %d,%d,%d; from the screen %.0f,%.0f,%.0f - off by %.0f across, %.0f up",
                     ax, ay, az, world[0], world[1], world[2], sqrtf(dx * dx + dy * dy), dz);
    }
}

static float smooth_join(float a, float b)
{
    float a2 = a * a, b2 = b * b;
    if (b <= 0.0f) return a;
    return powf(a2 * a2 * a2 + b2 * b2 * b2, 1.0f / 6.0f);
}

static void mouse_pixel(const U9FrameView *v, int *px, int *py)
{
    HWND hw = WindowFromDC(wglGetCurrentDC());
    POINT pt;
    RECT rc;
    *px = v->vx + (int)(v->cx * (float)v->vw / v->engineW);
    *py = v->vy + v->vh - 1 - (int)(v->cy * (float)v->vh / v->engineH);
    if (hw && GetCursorPos(&pt) && ScreenToClient(hw, &pt) && GetClientRect(hw, &rc) &&
        rc.right > 0 && rc.bottom > 0 && pt.x >= 0 && pt.y >= 0 && pt.x < rc.right && pt.y < rc.bottom) {
        *px = pt.x * g_w / rc.right;
        *py = g_h - 1 - pt.y * g_h / rc.bottom;
    }
}

static void reticle_pixel(const U9FrameView *v, int *px, int *py)
{
    float ex, ey;
    if (u9gl_reticle_centre(&ex, &ey)) {
        *px = v->vx + (int)(ex * (float)v->vw / v->engineW);
        *py = v->vy + v->vh - 1 - (int)(ey * (float)v->vh / v->engineH);
    } else {
        mouse_pixel(v, px, py);
    }
}

static int    g_markX = -1, g_markY;
static DWORD  g_markUntil;

static void mark_pixel(int x, int y)
{
    g_markX = x;
    g_markY = y;
    g_markUntil = u9time_now() + 3000;
}

static void pool_probe(const U9FrameView *v, const GLfloat *lp, const GLfloat *lc, int m)
{
    void (*out)(const char *fmt, ...) = g_poolProbeOut;
    int px, py, qx, qy, ax, ay, bx, by, i, shown = 0;
    float w, wa, wb, P[3], A[3], B[3], N[3], cam[3], len, tailR;
    g_ppX = -1;
    if (g_ppAtX >= 0.0f) {
        px = v->vx + (int)(g_ppAtX * (float)(v->vw - 1) / 100.0f);
        py = v->vy + v->vh - 1 - (int)(g_ppAtY * (float)(v->vh - 1) / 100.0f);
    } else {
        reticle_pixel(v, &px, &py);
    }
    if (!find_depth(v, px, py, 4, 16.0f, &qx, &qy, &w)) { if (out) out("nothing under the reticle"); return; }
    mark_pixel(qx, qy);
    pixel_to_world(v, qx, qy, w, P);
    {
        static const int off[6] = { 2, -2, 4, -4, 8, -8 };
        int oa = -1, ob = -1, k;
        for (k = 0; k < 6 && oa < 0; k++) if (find_depth(v, qx + off[k], qy, 0, 16.0f, &ax, &ay, &wa)) oa = k;
        for (k = 0; k < 6 && ob < 0; k++) if (find_depth(v, qx, qy + off[k], 0, 16.0f, &bx, &by, &wb)) ob = k;
        if (oa >= 0 && ob >= 0) {
            pixel_to_world(v, ax, ay, wa, A);
            pixel_to_world(v, bx, by, wb, B);
            for (i = 0; i < 3; i++) { A[i] -= P[i]; B[i] -= P[i]; }
            N[0] = A[1] * B[2] - A[2] * B[1];
            N[1] = A[2] * B[0] - A[0] * B[2];
            N[2] = A[0] * B[1] - A[1] * B[0];
            len = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
            if (len > 1e-6f) for (i = 0; i < 3; i++) N[i] /= len;
        } else {
            N[0] = N[1] = 0.0f; N[2] = 1.0f;
            if (out) out("no depth beside the point: taken as level ground");
        }
    }
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
    if (N[0] * (cam[0] - P[0]) + N[1] * (cam[1] - P[1]) + N[2] * (cam[2] - P[2]) < 0.0f)
        for (i = 0; i < 3; i++) N[i] = -N[i];
    {
        GLint pack = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(qx, qy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, g_ppFrame);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(qx, qy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, g_ppAlb);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, pack);
    }
    u9log_line("GLPOST pool probe: pixel %d,%d world %.0f,%.0f,%.0f normal %.2f,%.2f,%.2f; frame %u,%u,%u"
               " albedo %u,%u,%u (alpha %u)", qx, qy, P[0], P[1], P[2], N[0], N[1], N[2], g_ppFrame[0],
               g_ppFrame[1], g_ppFrame[2], g_ppAlb[0], g_ppAlb[1], g_ppAlb[2], g_ppAlb[3]);
    if (out) {
        out("at %.0f,%.0f,%.0f, facing %.2f,%.2f,%.2f", P[0], P[1], P[2], N[0], N[1], N[2]);
        out("game's colour %u,%u,%u; surface colour %u,%u,%u%s", g_ppFrame[0], g_ppFrame[1], g_ppFrame[2],
            g_ppAlb[0], g_ppAlb[1], g_ppAlb[2],
            (g_ppAlb[0] | g_ppAlb[1] | g_ppAlb[2]) ? "" : " - NONE: the pools cannot light this pixel");
    }
    tailR = (g_tail && g_tailReach > 1.0f) ? g_tailReach : 1.0f;
    for (i = 0; i < m; i++) {
        float d[3], dist, R = lp[i * 4 + 3], reachEnd = R * tailR, att, ndl;
        int k;
        for (k = 0; k < 3; k++) d[k] = lp[i * 4 + k] - P[k];
        dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist >= reachEnd) continue;
        att = (R - dist) / (R - 50.0f > 1.0f ? R - 50.0f : 1.0f);
        if (att < 0.0f) att = 0.0f;
        if (att > 1.0f) att = 1.0f;
        if (g_tail == 2) att = smooth_join(att, g_tailLevel * (1.0f - dist / reachEnd));
        ndl = (N[0] * d[0] + N[1] * d[1] + N[2] * d[2]) / (dist > 1e-3f ? dist : 1e-3f);
        u9log_line("GLPOST pool probe: light %d at %.0f,%.0f,%.0f range %.0f colour %.2f,%.2f,%.2f%s:"
                   " %.0f away, falloff %.2f, facing %.2f, room %d (+%d,%d,%d)", i + 1, lp[i * 4], lp[i * 4 + 1],
                   lp[i * 4 + 2], R, lc[i * 4], lc[i * 4 + 1], lc[i * 4 + 2], lc[i * 4 + 3] > 0.5f ? " NEGATIVE" : "",
                   dist, att, ndl, g_lastRooms[i][0], g_lastRooms[i][1], g_lastRooms[i][2], g_lastRooms[i][3]);
        if (out && shown < 6) {
            out("light %d: %.0f away, falloff %.2f, facing %.2f, colour %.2f,%.2f,%.2f%s", i + 1, dist, att, ndl,
                lc[i * 4], lc[i * 4 + 1], lc[i * 4 + 2], lc[i * 4 + 3] > 0.5f ? " (negative)" : "");
            shown++;
        }
        if (dist < 600.0f && lc[i * 4 + 3] <= 0.5f) u9shadow_report_near(&lp[i * 4], 400.0f, u9log_line);
        if (lc[i * 4 + 3] <= 0.5f) {
            int slot = i < g_ppN ? g_ppSlot[i] : -1;
            u9shadow_probe_ray(v, &lp[i * 4], P, reachEnd, slot, i < g_ppN && slot >= 0 ? g_ppW[i] : 0.0f,
                               !(i < g_ppN && g_ppFill[i]), g_shadowBias, g_shadowSlope, u9log_line);
        }
    }
    if (out && !shown) out("no light reaches this point by distance");
    g_ppX = qx;
    g_ppY = qy;
}

static int    g_sunProbeWanted;
static void (*g_sunProbeOut)(const char *fmt, ...);
int u9console_current_map(void);
static int  g_fillCut = -1;
static char g_fillKeep[256];

static int fill_cut_here(void)
{
    int map;
    if (g_fillCut < 0) {
        const char *s = u9cfg_str("gl_fill_lights_keep", "");
        size_t i, n = 1;
        g_fillCut = u9cfg_int("gl_fill_lights_cut", 1);
        g_fillKeep[0] = ',';
        for (i = 0; s[i] && n < sizeof g_fillKeep - 2; i++)
            if (s[i] != ' ') g_fillKeep[n++] = s[i];
        g_fillKeep[n++] = ',';
        g_fillKeep[n] = 0;
    }
    if (!g_fillCut || (map = u9console_current_map()) < 0) return 0;
    return !u9_id_listed(g_fillKeep, map);
}

static int drop_fill(U9LightSnap *all, int n)
{
    int k, m = 0;
    if (n <= 0 || !fill_cut_here()) return n;
    for (k = 0; k < n; k++)
        if (all[k].type != U9TYPE_FILL_LIGHT) all[m++] = all[k];
    return m;
}

static int  g_torchBoost = -1, g_torchReach;
static char g_torchTypes[128];

static char g_dungeonSkip[256];
static int  g_dungeonListsRead;

static void map_list(const char *s, char *out, size_t cap)
{
    size_t i, n = 1;
    out[0] = ',';
    for (i = 0; s[i] && n < cap - 2; i++)
        if (s[i] != ' ') out[n++] = s[i];
    if (n > 1) out[n++] = ',';
    out[n] = 0;
}

static int map_listed(const char *list, int map)
{
    return list[1] && u9_id_listed(list, map);
}

int u9glpost_night_graded(int texId)
{
    return map_listed(g_nightGradedList, texId);
}

static int dungeon_here(int *flagged)
{
    U9Sun s;
    int map, under;
    if (!g_dungeonListsRead) {
        g_dungeonListsRead = 1;
        map_list(u9cfg_str("gl_dungeon_skip", "9,14,56,39"), g_dungeonSkip, sizeof g_dungeonSkip);
    }
    under = u9sun_read(&s) && s.underground;
    if (flagged) *flagged = under;
    if (!g_dungeon) return 0;
    map = u9console_current_map();
    if (map < 0) return under;
    return !map_listed(g_dungeonSkip, map);
}

static int boost_torches(U9LightSnap *all, int n)
{
    float b, r;
    int k;
    if (g_torchBoost < 0) {
        const char *t = u9cfg_str("gl_dungeon_torch_types", "0");
        size_t i, j = 1;
        g_torchBoost = u9cfg_int("gl_dungeon_torch", 150);
        g_torchReach = u9cfg_int("gl_dungeon_torch_reach", 150);
        g_torchTypes[0] = ',';
        for (i = 0; t[i] && j < sizeof g_torchTypes - 2; i++)
            if (t[i] != ' ') g_torchTypes[j++] = t[i];
        g_torchTypes[j++] = ',';
        g_torchTypes[j] = 0;
    }
    if (n <= 0 || (g_torchBoost == 100 && g_torchReach == 100) || !dungeon_here(NULL)) return n;
    b = (float)g_torchBoost / 100.0f;
    r = (float)g_torchReach / 100.0f;
    for (k = 0; k < n; k++) {
        int c;
        if (all[k].type < 0 || (all[k].flags & 0x02u)) continue;
        if (!u9_id_listed(g_torchTypes, all[k].type)) continue;
        for (c = 0; c < 3; c++) { all[k].rgb[c] *= b; all[k].baseRgb[c] *= b; }
        all[k].range *= r;
    }
    return n;
}

#define LAVA_CELL    320.0f
#define LAVA_MAX     4096
#define LAVA_LIGHTS  40
#define LAVA_TESTS   32
#define U9TYPE_LAVA_LIGHT (-43)
typedef struct { int cx, cy, used; float z; } LavaCell;
static LavaCell g_lava[LAVA_MAX];
static int      g_lavaN, g_lavaMap = -2, g_lavaTests;
static int      g_lavaOn = -1, g_lavaGlow = 1, g_lavaStrength = 100, g_lavaRange = 500;
static const U9FrameView *g_lavaView;
static char     g_lavaList[128];

static void lava_settings(void)
{
    if (g_lavaOn >= 0) return;
    g_lavaOn       = u9cfg_int("gl_lava_light", 1);
    g_lavaGlow     = u9cfg_int("gl_lava_glow", 1);
    g_lavaStrength = u9cfg_int("gl_lava_strength", 100);
    g_lavaRange    = u9cfg_int("gl_lava_range", 500);
    map_list(u9cfg_str("gl_lava_textures", "4759"), g_lavaList, sizeof g_lavaList);
}

int u9lava_glows(void) { lava_settings(); return g_lavaGlow; }

int u9lava_is(int texId)
{
    lava_settings();
    return texId == U9TEX_LAVA || map_listed(g_lavaList, texId);
}

static LavaCell *lava_cell(int cx, int cy, int add)
{
    unsigned h = ((unsigned)cx * 73856093u ^ (unsigned)cy * 19349663u) & (LAVA_MAX - 1), k;
    for (k = 0; k < LAVA_MAX; k++, h = (h + 1) & (LAVA_MAX - 1)) {
        LavaCell *c = &g_lava[h];
        if (!c->used) {
            if (!add || g_lavaN >= LAVA_MAX * 3 / 4) return NULL;
            c->used = 1; c->cx = cx; c->cy = cy; c->z = -1e30f;
            g_lavaN++;
            return c;
        }
        if (c->cx == cx && c->cy == cy) return c;
    }
    return NULL;
}

static int lava_world(const U9FrameView *v, const float *f, float out[3])
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

static void lava_run(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout)
{
    const U9FrameView *v = g_lavaView;
    int stride;
    const char *base = (const char *)u9gl_caster_vertices(&stride);
    unsigned t;
    (void)ctx; (void)glTex; (void)cutout;
    for (t = first; t + 2 < first + count && g_lavaTests < LAVA_TESTS; t += 3) {
        float a[3], b[3], c[3], x0, x1, y0, y1, d;
        int gx, gy;
        if (!lava_world(v, (const float *)(base + (size_t)t * (size_t)stride), a) ||
            !lava_world(v, (const float *)(base + (size_t)(t + 1) * (size_t)stride), b) ||
            !lava_world(v, (const float *)(base + (size_t)(t + 2) * (size_t)stride), c)) continue;
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
        if (d > -1e-3f && d < 1e-3f) continue;
        x0 = a[0] < b[0] ? (a[0] < c[0] ? a[0] : c[0]) : (b[0] < c[0] ? b[0] : c[0]);
        x1 = a[0] > b[0] ? (a[0] > c[0] ? a[0] : c[0]) : (b[0] > c[0] ? b[0] : c[0]);
        y0 = a[1] < b[1] ? (a[1] < c[1] ? a[1] : c[1]) : (b[1] < c[1] ? b[1] : c[1]);
        y1 = a[1] > b[1] ? (a[1] > c[1] ? a[1] : c[1]) : (b[1] > c[1] ? b[1] : c[1]);
        for (gy = (int)floorf(y0 / LAVA_CELL); gy <= (int)floorf(y1 / LAVA_CELL) && g_lavaTests < LAVA_TESTS; gy++)
            for (gx = (int)floorf(x0 / LAVA_CELL); gx <= (int)floorf(x1 / LAVA_CELL) && g_lavaTests < LAVA_TESTS; gx++) {
                float p[3], wa, wb, wc, dpt, ew, depth;
                int px, py, j;
                LavaCell *cell = lava_cell(gx, gy, 0);
                if (cell && cell->z > -1e29f) continue;
                p[0] = ((float)gx + 0.5f) * LAVA_CELL;
                p[1] = ((float)gy + 0.5f) * LAVA_CELL;
                wa = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / d;
                wb = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / d;
                wc = 1.0f - wa - wb;
                if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                p[2] = wa * a[2] + wb * b[2] + wc * c[2];
                if (!world_to_pixel(v, p, &px, &py)) continue;
                for (j = 0, ew = 0.0f; j < 3; j++) ew += v->m[j * 4 + 1] * (p[j] - v->m[j * 4 + 3]);
                g_lavaTests++;
                fBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
                glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                fBindFramebuffer(GL_FRAMEBUFFER, 0);
                dpt = eye_depth(v, depth);
                if (fabsf(dpt - ew) > 8.0f + 0.02f * ew) continue;
                if ((cell = lava_cell(gx, gy, 1)) != NULL && p[2] > cell->z) cell->z = p[2];
            }
    }
}

static void lava_learn(const U9FrameView *v)
{
    int map;
    lava_settings();
    if (!g_lavaOn) return;
    map = u9console_current_map();
    if (map != g_lavaMap) {
        const int *cells;
        int nc = u9regions_lava(map, &cells), i;
        if (g_lavaN) u9log_line("GLPOST lava: %d cells known on map %d, forgotten", g_lavaN, g_lavaMap);
        memset(g_lava, 0, sizeof g_lava);
        g_lavaN = 0;
        g_lavaMap = map;
        for (i = 0; i < nc; i++) {
            LavaCell *c = lava_cell(cells[3 * i], cells[3 * i + 1], 1);
            if (c && (float)cells[3 * i + 2] > c->z) c->z = (float)cells[3 * i + 2];
        }
        if (nc) u9log_line("GLPOST lava: map %d, %d cells of lava ground from its terrain", map, g_lavaN);
    }
    g_lavaView = v;
    g_lavaTests = 0;
    {
        int before = g_lavaN;
        u9gl_runs_with_texture(U9TEX_LAVA, lava_run, NULL);
        if (g_lavaN != before && (g_lavaN / 16 != before / 16 || before == 0))
            u9log_line("GLPOST lava: %d cells known on map %d", g_lavaN, map);
    }
}

static int lava_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v)
{
    int best[LAVA_LIGHTS], nb = 0, k, j;
    float bd[LAVA_LIGHTS], t = (float)(u9time_now() % 100000u) / 1000.0f, s, ahead[3];
    if (!g_lavaOn || !g_lavaN) return n;
    for (k = 0; k < 3; k++) ahead[k] = v->m[k * 4 + 3] + v->m[k * 4 + 1] * 600.0f;
    for (k = 0; k < LAVA_MAX; k++) {
        const LavaCell *c = &g_lava[k];
        float dx, dy, dz, dd;
        if (!c->used || c->z < -1e29f) continue;
        dx = ((float)c->cx + 0.5f) * LAVA_CELL - ahead[0];
        dy = ((float)c->cy + 0.5f) * LAVA_CELL - ahead[1];
        dz = c->z - ahead[2];
        dd = dx * dx + dy * dy + dz * dz;
        if (dd > 4000.0f * 4000.0f || (nb == LAVA_LIGHTS && dd >= bd[nb - 1])) continue;
        j = nb < LAVA_LIGHTS ? nb++ : LAVA_LIGHTS - 1;
        while (j > 0 && bd[j - 1] > dd) { bd[j] = bd[j - 1]; best[j] = best[j - 1]; j--; }
        bd[j] = dd; best[j] = k;
    }
    s = (float)g_lavaStrength / 100.0f;
    for (k = 0; k < nb && n < cap; k++) {
        const LavaCell *c = &g_lava[best[k]];
        U9LightSnap *l = &all[n++];
        float pulse = 0.88f + 0.12f * sinf(t * 0.9f + (float)(best[k] % 97) * 0.37f);
        memset(l, 0, sizeof *l);
        l->pos[0] = l->base[0] = ((float)c->cx + 0.5f) * LAVA_CELL;
        l->pos[1] = l->base[1] = ((float)c->cy + 0.5f) * LAVA_CELL;
        l->pos[2] = l->base[2] = c->z + 40.0f;
        l->rgb[0] = 1.0f * s * pulse; l->rgb[1] = 0.42f * s * pulse; l->rgb[2] = 0.08f * s * pulse;
        memcpy(l->baseRgb, l->rgb, sizeof l->rgb);
        l->range = (float)g_lavaRange;
        l->rec = 0xE0000000u + (unsigned)best[k];
        l->type = U9TYPE_LAVA_LIGHT;
    }
    return n;
}

int u9gl_label_mode(void);

static void light_marks(void)
{
    static U9LightSnap all[128];
    U9FrameView v;
    GLfloat cc[4];
    GLboolean sc;
    GLint box[4];
    int n, k, px, py, h;
    if (u9gl_label_mode() != 2 || !u9gl_frame_view(&v)) return;
    n = u9light_snapshot(all, 128);
    if (n <= 0) return;
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    sc = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, box);
    glEnable(GL_SCISSOR_TEST);
    for (k = 0; k < n; k++) {
        float d[3], depth;
        int j;
        if (!world_to_pixel(&v, all[k].pos, &px, &py)) continue;
        px /= g_scale;
        py /= g_scale;
        for (j = 0; j < 3; j++) d[j] = all[k].pos[j] - v.m[j * 4 + 3];
        depth = v.m[1] * d[0] + v.m[5] * d[1] + v.m[9] * d[2];
        h = depth > 1.0f ? (int)(12.0f * v.sy / depth * (float)v.vh / v.engineH / (float)g_scale) : 3;
        if (h < 3) h = 3;
        if (h > 60) h = 60;
        if (all[k].type == U9TYPE_FILL_LIGHT && fill_cut_here())
                                  glClearColor(0.45f, 0.45f, 0.45f, 1.0f);
        else if (all[k].flags & 0x02u) glClearColor(0.8f, 0.35f, 0.0f, 1.0f);
        else                      glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
        glScissor(px - h, py - h, 2 * h + 1, 1);   glClear(GL_COLOR_BUFFER_BIT);
        glScissor(px - h, py + h, 2 * h + 1, 1);   glClear(GL_COLOR_BUFFER_BIT);
        glScissor(px - h, py - h, 1, 2 * h + 1);   glClear(GL_COLOR_BUFFER_BIT);
        glScissor(px + h, py - h, 1, 2 * h + 1);   glClear(GL_COLOR_BUFFER_BIT);
        glScissor(px, py, 1, 1);                   glClear(GL_COLOR_BUFFER_BIT);
    }
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glScissor(box[0], box[1], box[2], box[3]);
    if (!sc) glDisable(GL_SCISSOR_TEST);
}

static void probe_mark(void)
{
    GLfloat cc[4];
    GLboolean sc;
    GLint box[4];
    int mx = g_markX / g_scale, my = g_markY / g_scale;
    if (g_markX < 0) return;
    if ((LONG)(u9time_now() - g_markUntil) >= 0) { g_markX = -1; return; }
    glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    sc = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, box);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    glScissor(mx - 8, my, 6, 1);  glClear(GL_COLOR_BUFFER_BIT);
    glScissor(mx + 3, my, 6, 1);  glClear(GL_COLOR_BUFFER_BIT);
    glScissor(mx, my - 8, 1, 6);  glClear(GL_COLOR_BUFFER_BIT);
    glScissor(mx, my + 3, 1, 6);  glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    glScissor(box[0], box[1], box[2], box[3]);
    if (!sc) glDisable(GL_SCISSOR_TEST);
}

static int world_px(const U9FrameView *v, int x, int y, float out[3])
{
    int ox, oy;
    float w;
    if (!find_depth(v, x, y, 0, 16.0f, &ox, &oy, &w)) return 0;
    pixel_to_world(v, ox, oy, w, out);
    return 1;
}

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int sun_sunward(const U9SunMap *sm, const float *map, const float X[3], float *d, int *ix, int *iy)
{
    float q[3], s, t;
    int k;
    for (k = 0; k < 3; k++) q[k] = X[k] - sm->c[k];
    s = dot3(sm->pr, q) / sm->half * 0.5f + 0.5f;
    t = dot3(sm->pu, q) / sm->half * 0.5f + 0.5f;
    if (s <= 0.0f || t <= 0.0f || s >= 1.0f || t >= 1.0f) return 0;
    *ix = (int)(s * (float)sm->size);
    *iy = (int)(t * (float)sm->size);
    *d = (0.5f - map[*iy * sm->size + *ix]) * 2.0f * sm->dz - dot3(sm->pl, q);
    return 1;
}

static void sun_probe(const U9FrameView *v, const U9SunMap *sm)
{
    void (*out)(const char *fmt, ...) = g_sunProbeOut;
    int px, py, qx, qy, ix, iy, i, k, n;
    float w, P[3], N[3], A[2][3], B[2][3], ea[2], eb[2], cam[3], Ps[3], Q[3], X[3], Lh[3];
    float len, ndl, eye, bias, slope, d, occ, *map;
    char line[400];
    GLint bound = 0;
    if (!sm->size || !sm->tex) return;
    reticle_pixel(v, &px, &py);
    if (!find_depth(v, px, py, 4, 16.0f, &qx, &qy, &w)) { if (out) out("sun probe: nothing under the reticle"); return; }
    mark_pixel(qx, qy);
    pixel_to_world(v, qx, qy, w, P);
    ea[0] = ea[1] = eb[0] = eb[1] = 1e30f;
    if (world_px(v, qx + 1, qy, A[0])) { for (k = 0; k < 3; k++) A[0][k] -= P[k]; ea[0] = dot3(A[0], A[0]); }
    if (world_px(v, qx - 1, qy, A[1])) { for (k = 0; k < 3; k++) A[1][k] = P[k] - A[1][k]; ea[1] = dot3(A[1], A[1]); }
    if (world_px(v, qx, qy + 1, B[0])) { for (k = 0; k < 3; k++) B[0][k] -= P[k]; eb[0] = dot3(B[0], B[0]); }
    if (world_px(v, qx, qy - 1, B[1])) { for (k = 0; k < 3; k++) B[1][k] = P[k] - B[1][k]; eb[1] = dot3(B[1], B[1]); }
    if ((ea[0] > 1e29f && ea[1] > 1e29f) || (eb[0] > 1e29f && eb[1] > 1e29f)) { if (out) out("sun probe: no depth beside the mouse"); return; }
    {
        const float *a = ea[0] < ea[1] ? A[0] : A[1], *b = eb[0] < eb[1] ? B[0] : B[1];
        N[0] = a[1] * b[2] - a[2] * b[1];
        N[1] = a[2] * b[0] - a[0] * b[2];
        N[2] = a[0] * b[1] - a[1] * b[0];
    }
    len = sqrtf(dot3(N, N));
    if (len > 1e-6f) for (k = 0; k < 3; k++) N[k] /= len;
    cam[0] = v->m[3]; cam[1] = v->m[7]; cam[2] = v->m[11];
    for (k = 0; k < 3; k++) X[k] = cam[k] - P[k];
    if (dot3(N, X) < 0.0f) for (k = 0; k < 3; k++) N[k] = -N[k];
    map = (float *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sm->size * (SIZE_T)sm->size * sizeof(float));
    if (!map) return;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glBindTexture(GL_TEXTURE_2D, sm->tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, map);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
    ndl = dot3(N, sm->l);
    for (k = 0; k < 3; k++) Ps[k] = P[k] + N[k] * 4.0f;
    for (k = 0; k < 3; k++) Q[k] = Ps[k] + (ndl > -0.02f ? N[k] : sm->l[k]) * sm->texel * 1.5f;
    for (k = 0; k < 3; k++) X[k] = Ps[k] - cam[k];
    eye = sqrtf(dot3(X, X));
    slope = sm->texel * (0.5f + 0.5f * (1.0f - (ndl > 0.0f ? ndl : 0.0f)));
    bias = g_sunBias + slope + eye * 0.002f;
    if (!sun_sunward(sm, map, Q, &d, &ix, &iy)) {
        HeapFree(GetProcessHeap(), 0, map);
        if (out) out("sun probe: outside the near map");
        return;
    }
    {
        const float *Nq = ndl > -0.02f ? N : sm->l;
        float q[3], nl = dot3(Nq, sm->l), cs, ct, dl, lim = 8.0f * sm->texel;
        for (k = 0; k < 3; k++) q[k] = Q[k] - sm->c[k];
        if (nl < 0.1f) nl = 0.1f;
        cs = ((float)ix + 0.5f) / (float)sm->size - (dot3(sm->r, q) / sm->half * 0.5f + 0.5f);
        ct = ((float)iy + 0.5f) / (float)sm->size - (dot3(sm->u, q) / sm->half * 0.5f + 0.5f);
        dl = -(dot3(Nq, sm->r) * cs + dot3(Nq, sm->u) * ct) / nl * 2.0f * sm->half;
        if (dl > lim) dl = lim;
        if (dl < -lim) dl = -lim;
        d -= dl;
    }
    occ = d - bias;
    u9log_line("GLSUN probe: pixel %d,%d world %.1f,%.1f,%.1f normal %.2f,%.2f,%.2f, toward the sun %.2f; near map texel %d,%d"
               " (%.2f units); its surface %.1f sunward of the lookup point, bias %.1f (sun %.1f + texel %.1f + distance %.1f)"
               " -> %s", qx, qy, P[0], P[1], P[2], N[0], N[1], N[2], ndl, ix, iy, sm->texel, d, bias, g_sunBias, slope,
               eye * 0.002f, occ > 0.0f ? "SHADOW" : "LIT");
    if (out) {
        out("sun probe at %.0f,%.0f,%.0f, facing %.2f,%.2f,%.2f (%.2f toward the sun)", P[0], P[1], P[2],
            N[0], N[1], N[2], ndl);
        out("the map's surface is %.1f sunward of the lookup; bias %.1f (sun %.1f, texel %.1f, distance %.1f): %s",
            d, bias, g_sunBias, slope, eye * 0.002f, occ > 0.0f ? "in shadow" : "LIT");
    }
    Lh[0] = sm->l[0]; Lh[1] = sm->l[1]; Lh[2] = 0.0f;
    len = sqrtf(dot3(Lh, Lh));
    if (len > 1e-6f) for (k = 0; k < 3; k++) Lh[k] /= len;
    n = sprintf(line, "toward the sun, units along the ground: sunward surface");
    for (i = 0; i <= 48; i += 4) {
        for (k = 0; k < 3; k++) X[k] = P[k] + Lh[k] * (float)i;
        if (!sun_sunward(sm, map, X, &d, &ix, &iy)) break;
        n += sprintf(line + n, " %d:%.1f", i, d);
    }
    u9log_line("GLSUN probe: %s", line);
    if (out) out("%s", line);
    HeapFree(GetProcessHeap(), 0, map);
    u9shadow_sun_probe_ray(v, P, N, sm->l, out);
}

void u9glpost_sun_probe(void (*out)(const char *fmt, ...))
{
    g_sunProbeWanted = 1;
    g_sunProbeOut = out;
}

static void pool_probe_after(void)
{
    unsigned char fin[4];
    GLint pack = 4;
    int k, add[3];
    if (g_ppX < 0) return;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(g_ppX / g_scale, g_ppY / g_scale, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fin);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    for (k = 0; k < 3; k++) add[k] = (int)fin[k] - (int)g_ppFrame[k];
    u9log_line("GLPOST pool probe: on screen %u,%u,%u - the pools added %d,%d,%d (gamma included)",
               fin[0], fin[1], fin[2], add[0], add[1], add[2]);
    if (g_poolProbeOut)
        g_poolProbeOut("on screen %u,%u,%u: the pools added %d,%d,%d", fin[0], fin[1], fin[2],
                       add[0], add[1], add[2]);
    g_ppX = -1;
}

void u9glpost_end(int surfW, int surfH)
{
    float gamma, inv;
    int viewNow = 0, aaOn;
    if (!g_bound) return;
    g_bound = 0;
    u9gpu_end(g_gpuScene);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);

    gamma = g_wantGamma ? u9glslots_gamma() : 1.0f;
    inv = (gamma > 0.01f) ? 1.0f / gamma : 1.0f;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glViewport(0, 0, g_w, g_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    {
        U9FrameView bv;
        float bk[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        unsigned bin = 0;
        u9gpu_begin(g_gpuBeam);
        GLuint bt = u9gl_frame_view(&bv) ? (GLuint)u9beam_build(&bv, g_depth, g_exact, g_w, g_h, &bin, bk) : 0;
        u9gpu_end(g_gpuBeam);
        fActiveTexture(GL_TEXTURE0 + 19);
        glBindTexture(GL_TEXTURE_2D, bt);
        fActiveTexture(GL_TEXTURE0 + 20);
        glBindTexture(GL_TEXTURE_2D, (GLuint)bin);
        fActiveTexture(GL_TEXTURE0);
        fUseProgram(g_prog);
        fUniform1i(g_uBeam, 19);
        fUniform1i(g_uBeamIn, 20);
        fUniform4f(g_uBeamK, bk[0], bt ? bk[1] : 0.0f, bt ? bk[2] : 0.0f, bt ? bk[3] : 0.0f);
        {
            float px = (float)u9beam_soft() * (float)g_h / 1080.0f;
            fUniform2f(g_uBeamS, bt ? px / (float)g_w : 0.0f, bt ? px / (float)g_h : 0.0f);
        }
    }
    {
        U9FrameView rv;
        float rk[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        u9gpu_begin(g_gpuRays);
        GLuint rt = u9gl_frame_view(&rv) ? (GLuint)u9rays_build(&rv, g_colour, g_depth, g_exact, g_bloom ? g_bloomSrc : 0, g_sky && g_exact && u9rays_sun_on() ? g_sky : 0, g_w, g_h, g_scale, rk) : 0;
        u9gpu_end(g_gpuRays);
        glViewport(0, 0, g_w, g_h);
        fActiveTexture(GL_TEXTURE0 + 21);
        glBindTexture(GL_TEXTURE_2D, rt);
        fActiveTexture(GL_TEXTURE0);
        fUseProgram(g_prog);
        fUniform1i(g_uRays, 21);
        fUniform4f(g_uRaysK, rt ? rk[0] : 0.0f, rt ? rk[1] : 0.0f, rt ? rk[2] : 0.0f, rt ? rk[3] : 0.0f);
        fUniform1f(g_uRaysAir, (float)u9rays_air());
    }
    {
        u9gpu_begin(g_gpuBloom);
        GLuint bl = (g_bloom && g_bloomSrc) ? (GLuint)u9bloom_run(g_bloomSrc, g_w, g_h, g_scale) : 0;
        u9gpu_end(g_gpuBloom);
        glViewport(0, 0, g_w, g_h);
        fActiveTexture(GL_TEXTURE15);
        glBindTexture(GL_TEXTURE_2D, bl);
        fActiveTexture(GL_TEXTURE0);
        fUseProgram(g_prog);
        fUniform1i(g_uBloom, 15);
        fUniform2f(g_uBloomK, bl ? (float)g_bloomStrength / 100.0f / (float)U9BLOOM_LEVELS : 0.0f,
                   g_bloomView ? 1.0f : 0.0f);
        if (bl) g_bloomFrames++;
    }
    {
        U9FrameView fv;
        if (u9gl_frame_view(&fv)) u9fx_frame(&fv);
    }
    fActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_depth);
    fActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_colour);

    fUseProgram(g_prog);
    fUniform1i(g_uFrame, 0);
    fUniform1i(g_uDepth, 1);
    fUniform1f(g_uInvGamma, inv);
    fActiveTexture(GL_TEXTURE0 + 16);
    glBindTexture(GL_TEXTURE_2D, g_exact);
    fActiveTexture(GL_TEXTURE0);
    fUniform1i(g_uExact, 16);
    fUniform1i(g_uHaveExact, g_exact ? 1 : 0);
    fUniform1i(g_uDepthView, g_depthView);
    fUniform1i(g_uNrmAcross, g_nrmAcross);
    fUniform1i(g_uSeams, g_seams);
    {
        U9FrameView v;
        int sunWanted = g_sunDebug || g_sunOn || g_sunView;
        int waterNow = g_waterOn && g_waterTex && g_waterMarked && g_exact;
        int have = (g_grid || g_probeWanted || g_pools || sunWanted || g_moonShadowOn || waterNow) && u9gl_frame_view(&v);
        U9Sun sun;
        int sunLive = have && sunWanted && v.depthMode == 2 && u9sun_read(&sun) && !sun.underground &&
                      !sun.moonMap && sun.elevation >= g_sunMinElevation;
        U9Moons moons;
        float moonK = 0.0f;
        int moonCaster = -1;
        int moonLive = have && !sunLive && g_moonShadowOn && v.depthMode == 2 && u9moon_read(&moons) &&
                       (moonCaster = u9moon_caster(&moons, &moonK)) >= 0;
        int castLive = sunLive || moonLive;
        const float *castDir = sunLive ? sun.dir : moonLive ? moons.m[moonCaster].dir : NULL;
        int spacesUp = 0;
        viewNow = have;
        {
            U9FrameView av;
            float ak[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            u9gpu_begin(g_gpuAo);
            GLuint at = u9gl_frame_view(&av) ? (GLuint)u9ao_build(&av, g_exact, g_w, g_h, g_scale, castDir, ak) : 0;
            u9gpu_end(g_gpuAo);
            glViewport(0, 0, g_w, g_h);
            fActiveTexture(GL_TEXTURE0 + 22);
            glBindTexture(GL_TEXTURE_2D, at);
            fActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_colour);
            fUseProgram(g_prog);
            fUniform1i(g_uAo, 22);
            fUniform4f(g_uAoK, at ? ak[0] : 0.0f, at ? ak[1] : 0.0f, ak[2], ak[3]);
        }
        fUniform1i(g_uGrid, g_grid && have);
        night_grade();
        {
            U9FogParams fp;
            u9fog_frame(&fp);
            fUniform4f(g_uFogK, fp.on && have && v.depthMode == 2 && !u9light_book_open() ? 1.0f : 0.0f,
                       fp.top, fp.density, fp.soft);
            fUniform4f(g_uFogBox, fp.box[0], fp.box[1], fp.box[2], fp.box[3]);
            fUniform4f(g_uFogC, fp.rgb[0], fp.rgb[1], fp.rgb[2], fp.edge);
            fUniform4f(g_uFogN, fp.noise, fp.scale, fp.time, fp.reach);
        }
        {
            U9UnderFog uf;
            u9under_fog(&uf);
            fUniform4f(g_uUnderK, uf.on && have && v.depthMode == 2 && !u9light_book_open() ? 1.0f : 0.0f,
                       uf.surface, uf.density, uf.dark);
            fUniform4f(g_uUnderA, uf.absorb[0], uf.absorb[1], uf.absorb[2], uf.reach);
            fUniform3f(g_uUnderC, uf.rgb[0], uf.rgb[1], uf.rgb[2]);
            fUniform4f(g_uUnderW, uf.caustic, uf.cscale, uf.cdepth, uf.time);
            fUniform1f(g_uUnderB, uf.cblur);
            fUniform4f(g_uUnderS, uf.shaft, uf.sscale, uf.sreach, uf.time);
            fUniform3f(g_uUnderL, uf.sdir[0], uf.sdir[1], uf.sdir[2]);
            fUniform3f(g_uUnderG, uf.glowFree, uf.glowHaze, uf.glowReach);
        }
        fActiveTexture(GL_TEXTURE12);
        glBindTexture(GL_TEXTURE_2D, g_glow);
        fActiveTexture(GL_TEXTURE0);
        fUniform1i(g_uGlow, 12);
        fUniform1i(g_uGlowView, g_glowView);
        {
            int pools = g_pools && have && v.depthMode == 2;
            fUniform1i(g_uPools, pools);
            {
                int s;
                for (s = 0; s < U9SHADOW_MAX; s++) fUniform1i(g_uSh[s], 4 + s);
            }
            if (!pools) {
                int s;
                for (s = 0; s < U9SHADOW_MAX; s++) g_sh[s].held = 0;
            }
            if (pools) {
                static U9LightSnap all[128 + LAVA_LIGHTS + U9FX_LIGHTS + U9FX_RUNE_LIGHTS + U9FX_PILLAR_LIGHTS + 1];
                static GLfloat lp[MAX_POOL_LIGHTS * 4], lc[MAX_POOL_LIGHTS * 4];
                float cam[3], dt, fw[MAX_POOL_LIGHTS], av[3];
                int lsp[MAX_POOL_LIGHTS + 1][U9SP_LINKED], haveAv = 0, roomsKnown = 0, r2;
                int n = boost_torches(all, drop_fill(all, u9light_snapshot(all, 128))), k, m, idx[MAX_POOL_LIGHTS],
                    slot[MAX_POOL_LIGHTS];
                DWORD now = u9time_now();
                cam[0] = v.m[3]; cam[1] = v.m[7]; cam[2] = v.m[11];
                lava_learn(&v);
                n = lava_lights(all, n, 128 + LAVA_LIGHTS, &v);
                n = u9fx_fire_lights(all, n, 128 + LAVA_LIGHTS + U9FX_LIGHTS, &v, dungeon_here(NULL));
                n = u9fx_rune_lights(all, n, 128 + LAVA_LIGHTS + U9FX_LIGHTS + U9FX_RUNE_LIGHTS, &v);
                n = u9fx_pillar_lights(all, n, 128 + LAVA_LIGHTS + U9FX_LIGHTS + U9FX_RUNE_LIGHTS + U9FX_PILLAR_LIGHTS,
                                       &v);
                n = u9fx_gate_lights(all, n, 128 + LAVA_LIGHTS + U9FX_LIGHTS + U9FX_RUNE_LIGHTS + U9FX_PILLAR_LIGHTS + 1);
                if (g_scostOn) u9scost_lights(cam, all[0].pos, (int)(sizeof all[0] / sizeof(float)), n);
                {
                    int ax, ay, az;
                    if (g_shadowOn && u9avatar_position(&ax, &ay, &az)) {
                        av[0] = (float)ax; av[1] = (float)ay; av[2] = (float)az + 90.0f;
                        haveAv = 1;
                    }
                }
                {
                    static LARGE_INTEGER last, freq;
                    LARGE_INTEGER t;
                    QueryPerformanceCounter(&t);
                    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
                    dt = last.QuadPart ? (float)((double)(t.QuadPart - last.QuadPart) / (double)freq.QuadPart)
                                       : 0.0f;
                    if (dt < 0.0f || dt > 0.25f) dt = 0.25f;
                    last = t;
                }
                m = pool_select(all, n, &v, dt, now, idx, fw, slot);
                for (k = 0; k < m; k++) {
                    const U9LightSnap *l = &all[idx[k]];
                    float col[3];
                    lp[k * 4] = l->pos[0]; lp[k * 4 + 1] = l->pos[1]; lp[k * 4 + 2] = l->pos[2];
                    lp[k * 4 + 3] = l->range;
                    pool_colour(l, slot[k], dt, now, col);
                    g_lt[slot[k]].seen = now;
                    lc[k * 4] = col[0] * fw[k]; lc[k * 4 + 1] = col[1] * fw[k]; lc[k * 4 + 2] = col[2] * fw[k];
                    lc[k * 4 + 3] = (l->flags & 0x02u) ? 1.0f
                                  : (l->type == U9TYPE_LAVA_LIGHT || (l->flags & U9LIGHT_SOFT)) ? -1.0f : 0.0f;
                }
                fActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, g_albedo);
                fActiveTexture(GL_TEXTURE0);
                fUniform1i(g_uAlbedo, 2);
                fUniform1i(g_uMarch, g_march);
                fUniform1i(g_uPoolDebug, g_poolDebug);
                fUniform1f(g_uStrength, g_strength);
                fUniform1f(g_uWarmth, g_warmth);
                fUniform1i(g_uSoft, g_soft);
                fUniform1i(g_uNl, m);
                if (m) {
                    fUniform4fv(g_uLp, m, lp);
                    fUniform4fv(g_uLc, m, lc);
                }
                fUniform1i(g_uContain, g_contain);
                fUniform3f(g_uTail, (float)g_tail, g_tailLevel, g_tailReach);
                if (g_contain && m) {
                    static U9SpaceSet set;
                    static float pos[MAX_POOL_LIGHTS + 1][3], base[MAX_POOL_LIGHTS + 1][3], rch[MAX_POOL_LIGHTS + 1];
                    int inRoom = 0, r;
                    GLfloat lspf[MAX_POOL_LIGHTS * U9SP_LINKED];
                    for (k = 0; k < m; k++) {
                        const U9LightSnap *l = &all[idx[k]];
                        pos[k][0] = l->pos[0];  pos[k][1] = l->pos[1];  pos[k][2] = l->pos[2];
                        base[k][0] = l->base[0]; base[k][1] = l->base[1]; base[k][2] = l->base[2];
                        rch[k] = l->range * (g_tail && g_tailReach > 1.0f ? g_tailReach : 1.0f);
                    }
                    if (haveAv) {
                        for (r = 0; r < 3; r++) pos[m][r] = base[m][r] = av[r];
                        rch[m] = 0.0f;
                    }
                    u9spaces_for_lights((const float (*)[3])base, (const float (*)[3])pos, rch, m + haveAv,
                                        g_keepOut, cam, lsp, &set, castLive ? SUN_ROOMS : 0.0f);
                    for (k = 0; k < m; k++) {
                        if (haveAv && all[idx[k]].type == 0)
                            for (r = 0; r < U9SP_LINKED; r++) lsp[k][r] = lsp[m][r];
                        for (r = 0; r < U9SP_LINKED; r++) {
                            lspf[k * U9SP_LINKED + r] = (float)lsp[k][r];
                            g_lastRooms[k][r] = lsp[k][r];
                        }
                        if (lsp[k][0] >= 0) inRoom++;
                    }
                    fUniform4fv(g_uLsp, m, lspf);
                    spaces_upload(&set);
                    spacesUp = set.nSpaces > 0;
                    fUniform1i(g_uKeepOut, g_keepOut);
                    if ((unsigned)set.nSpaces > g_outerMax) g_outerMax = (unsigned)set.nSpaces;
                    if (set.roomFull) g_capRoom++;
                    if (set.nearFull) g_capOuter++;
                    if ((unsigned)inRoom > g_roomLightsMax) g_roomLightsMax = (unsigned)inRoom;
                    if (g_roomsWanted) {
                        g_roomsWanted = 0;
                        for (k = 0; k < m && k < 10; k++) {
                            const char *room = lsp[k][0] >= 0 ? set.name[lsp[k][0]] : "(no room - not contained)";
                            const char *link = lsp[k][1] >= 0 ? set.name[lsp[k][1]] : "";
                            u9log_line("GLPOST rooms: light %d at %.0f,%.0f,%.0f, object %.0f,%.0f,%.0f type %d,"
                                       " range %.0f -> %s%s%s", k + 1, pos[k][0], pos[k][1], pos[k][2],
                                       base[k][0], base[k][1], base[k][2], all[idx[k]].type, lp[k * 4 + 3], room,
                                       *link ? " + linked " : "", link);
                            if (g_roomsOut)
                                g_roomsOut("%2d  light %.0f,%.0f,%.0f  object z %.0f type %d  range %.0f  %s%s%s", k + 1,
                                           pos[k][0], pos[k][1], pos[k][2], base[k][2], all[idx[k]].type, lp[k * 4 + 3],
                                           room, *link ? " + " : "", link);
                        }
                    }
                    g_roomLit++;
                    roomsKnown = 1;
                }
                {
                    int want[U9SHADOW_MAX], nWant = 0, pick = -1, held = -1, k2, s;
                    float best = 0.0f, heldAt = 0.0f, step = g_fadeMs > 0 ? dt * 1000.0f / (float)g_fadeMs : 1.0f;
                    GLfloat lsh[MAX_POOL_LIGHTS * 2];
                    int count = g_shadowCount < 1 ? 1 : g_shadowCount > U9SHADOW_MAX ? U9SHADOW_MAX : g_shadowCount;
                    for (k = 0; k < m && g_shadowOn && haveAv; k++) {
                        const U9LightSnap *l = &all[idx[k]];
                        float d[3], dist, R = l->range, reach, at, lum;
                        int share = 1;
                        if ((l->flags & 0x02u) || fw[k] < 0.99f || l->type == U9TYPE_LAVA_LIGHT ||
                            l->type == U9TYPE_FIRE_LIGHT || l->type == U9TYPE_RUNE_LIGHT || l->type == U9TYPE_PILLAR_LIGHT || l->type == U9TYPE_GATE_LIGHT) continue;
                        if (roomsKnown && l->type != 0) {
                            share = 0;
                            if (lsp[k][0] < 0 && lsp[m][0] < 0) share = 1;
                            for (r2 = 0; r2 < U9SP_LINKED && !share; r2++)
                                for (k2 = 0; k2 < U9SP_LINKED && !share; k2++)
                                    if (lsp[k][r2] >= 0 && lsp[k][r2] == lsp[m][k2]) share = 1;
                        }
                        if (!share) continue;
                        for (r2 = 0; r2 < 3; r2++) d[r2] = l->pos[r2] - av[r2];
                        dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        reach = R * (g_tail && g_tailReach > 1.0f ? g_tailReach : 1.0f);
                        if (dist >= reach) continue;
                        at = (R - dist) / (R - 50.0f > 1.0f ? R - 50.0f : 1.0f);
                        if (at < 0.0f) at = 0.0f;
                        if (at > 1.0f) at = 1.0f;
                        if (g_tail) at = smooth_join(at, g_tailLevel * (1.0f - dist / reach));
                        lum = l->rgb[0] > l->rgb[1] ? l->rgb[0] : l->rgb[1];
                        if (l->rgb[2] > lum) lum = l->rgb[2];
                        at *= lum;
                        if (l->rec == g_shadowRec) { held = k; heldAt = at; }
                        if (at > best) { best = at; pick = k; }
                    }
                    if (held >= 0 && best < heldAt * 1.3f) pick = held;
                    if (pick >= 0) {
                        const U9LightSnap *l = &all[idx[pick]];
                        want[nWant++] = pick;
                        if (l->rec != g_shadowRec) {
                            float dx = l->pos[0] - av[0], dy = l->pos[1] - av[1], dz = l->pos[2] - av[2];
                            u9log_line("GLPOST shadow light: %.0f,%.0f,%.0f range %.0f colour %.2f,%.2f,%.2f,"
                                       " %.0f from the Avatar%s", l->pos[0], l->pos[1], l->pos[2], l->range,
                                       l->rgb[0], l->rgb[1], l->rgb[2], sqrtf(dx * dx + dy * dy + dz * dz),
                                       !roomsKnown ? "" : lsp[m][0] >= 0 ? ", in its room" : ", both outdoors");
                            g_shadowChanges++;
                            g_shadowRec = l->rec;
                        }
                    }
                    for (k = 0; k < m && g_shadowOn && nWant < count; k++) {
                        if (k == pick || (all[idx[k]].flags & 0x02u) || fw[k] < 0.99f ||
                            all[idx[k]].type == U9TYPE_LAVA_LIGHT ||
                            (all[idx[k]].type == U9TYPE_FIRE_LIGHT && !g_shadowFire) ||
                            all[idx[k]].type == U9TYPE_RUNE_LIGHT || all[idx[k]].type == U9TYPE_PILLAR_LIGHT || all[idx[k]].type == U9TYPE_GATE_LIGHT) continue;
                        want[nWant++] = k;
                    }
                    for (k = 0; k < m; k++) { lsh[k * 2] = -1.0f; lsh[k * 2 + 1] = 0.0f; }
                    for (s = 0; s < U9SHADOW_MAX; s++) {
                        int at = -1, w;
                        if (!g_sh[s].held) continue;
                        for (k = 0; k < m; k++) if (all[idx[k]].rec == g_sh[s].rec) { at = k; break; }
                        if (at < 0 || !g_shadowOn) { g_sh[s].held = 0; continue; }
                        for (w = 0; w < nWant; w++) if (want[w] == at) break;
                        if (w < nWant && s < count) {
                            g_sh[s].w += step;
                            if (g_sh[s].w > 1.0f) g_sh[s].w = 1.0f;
                        } else {
                            g_sh[s].w -= step;
                            if (g_sh[s].w <= 0.0f) { g_sh[s].held = 0; continue; }
                        }
                    }
                    for (k2 = 0; k2 < nWant; k2++) {
                        unsigned rec = all[idx[want[k2]]].rec;
                        for (s = 0; s < U9SHADOW_MAX; s++) if (g_sh[s].held && g_sh[s].rec == rec) break;
                        if (s < U9SHADOW_MAX) continue;
                        for (s = 0; s < count; s++) if (!g_sh[s].held) break;
                        if (s >= count) break;
                        g_sh[s].held = 1;
                        g_sh[s].rec = rec;
                        g_sh[s].w = g_fadeMs > 0 ? step : 1.0f;
                    }
                    uint64_t cubesT0 = u9perf_now();
                    g_shadowHeld = 0;
                    u9gpu_begin(g_gpuPoolShadows);
                    u9shadow_new_frame(&v);
                    for (s = 0; s < U9SHADOW_MAX; s++) {
                        int at = -1;
                        GLuint cube;
                        if (!g_sh[s].held) continue;
                        for (k = 0; k < m; k++) if (all[idx[k]].rec == g_sh[s].rec) { at = k; break; }
                        if (at < 0) continue;
                        {
                            const U9LightSnap *l = &all[idx[at]];
                            float reach = l->range * (g_tail && g_tailReach > 1.0f ? g_tailReach : 1.0f);
                            uint64_t cubeT0 = u9perf_now();
                            unsigned memBefore = g_shadowMemTris;
                            cube = u9shadow_build(&v, l->pos, reach, s, l->type != U9TYPE_FILL_LIGHT);
                            if (g_scostOn && cube)
                                u9scost_cube(s, l->pos, reach, l->type, g_shadowTris, g_shadowMemTris - memBefore,
                                             u9perf_now() - cubeT0);
                        }
                        if (!cube) continue;
                        fActiveTexture(GL_TEXTURE4 + s);
                        glBindTexture(GL_TEXTURE_CUBE_MAP_, cube);
                        lsh[at * 2] = (float)s;
                        lsh[at * 2 + 1] = g_sh[s].w;
                        g_shadowHeld++;
                    }
                    if (g_scostOn) u9scost_add(U9SC_CUBES, u9perf_now() - cubesT0);
                    fActiveTexture(GL_TEXTURE0);
                    fUseProgram(g_prog);
                    fUniform4f(g_uShadowK, g_shadowBias, g_shadowSlope, g_shadowStrength, g_shadowSoft);
                    if (m) fUniform2fv(g_uLsh, m, lsh);
                    for (k = 0; k < m && k < MAX_POOL_LIGHTS; k++) {
                        g_ppSlot[k] = (int)lsh[k * 2];
                        g_ppW[k] = lsh[k * 2 + 1];
                        g_ppFill[k] = all[idx[k]].type == U9TYPE_FILL_LIGHT;
                    }
                    g_ppN = m;
                }
                if ((unsigned)m > g_poolLightsMax) g_poolLightsMax = (unsigned)m;
                if (n > g_maxLights) g_capLights++;
                if ((unsigned)n > g_lightsSeenMax) g_lightsSeenMax = (unsigned)n;
                if (g_poolProbeWanted) {
                    g_poolProbeWanted = 0;
                    pool_probe(&v, lp, lc, m);
                }
                g_poolFrames++;
            }
        }
        {
            U9SunMap sm, sm2;
            int on = 0, maps = 0;
            float fade = 0.0f;
            if (castLive && !spacesUp) {
                static U9SpaceSet sset;
                int none[1][U9SP_LINKED];
                float cam[3];
                cam[0] = v.m[3]; cam[1] = v.m[7]; cam[2] = v.m[11];
                if (u9spaces_for_lights(NULL, NULL, NULL, 0, 0, cam, none, &sset, SUN_ROOMS)) {
                    spaces_upload(&sset);
                    spacesUp = sset.nSpaces > 0;
                }
            }
            if (sunLive) {
                fade = (sun.elevation - g_sunMinElevation) / 10.0f;
                if (fade > 1.0f) fade = 1.0f;
                if (fade < 0.0f) fade = 0.0f;
                fade *= 1.0f - 4.0f * sun.storm > 0.0f ? 1.0f - 4.0f * sun.storm : 0.0f;
            }
            u9gpu_end(g_gpuPoolShadows);
            u9gpu_begin(g_gpuSunShadows);
            {
                uint64_t sunT0 = u9perf_now();
                maps = castLive ? u9shadow_build_sun(&v, castDir, &sm, &sm2) : 0;
                if (g_scostOn) u9scost_add(U9SC_SUN, u9perf_now() - sunT0);
            }
            u9gpu_end(g_gpuSunShadows);
            if (castLive && maps > 0) {
                on = 1;
                g_sunFrames++;
                fActiveTexture(GL_TEXTURE13);
                glBindTexture(GL_TEXTURE_2D, sm.tex);
                fActiveTexture(GL_TEXTURE0);
                fUseProgram(g_prog);
                fUniform1i(g_uSun, 13);
                fUniform3f(g_uSunR, sm.r[0], sm.r[1], sm.r[2]);
                fUniform3f(g_uSunU, sm.u[0], sm.u[1], sm.u[2]);
                fUniform3f(g_uSunL, sm.l[0], sm.l[1], sm.l[2]);
                fUniform3f(g_uSunPR, sm.pr[0], sm.pr[1], sm.pr[2]);
                fUniform3f(g_uSunPU, sm.pu[0], sm.pu[1], sm.pu[2]);
                fUniform3f(g_uSunPL, sm.pl[0], sm.pl[1], sm.pl[2]);
                fUniform3f(g_uSunC, sm.c[0], sm.c[1], sm.c[2]);
                fUniform4f(g_uSunK, sm.half, sm.dz, sm.texel, moonLive ? moonK : g_sunStrength * fade);
                fUniform1i(g_uSun2, 14);
                if (maps == 2) {
                    fActiveTexture(GL_TEXTURE14);
                    glBindTexture(GL_TEXTURE_2D, sm2.tex);
                    fActiveTexture(GL_TEXTURE0);
                    fUniform3f(g_uSun2C, sm2.c[0], sm2.c[1], sm2.c[2]);
                    fUniform4f(g_uSun2K, sm2.half, sm2.dz, sm2.texel, 1.0f);
                } else {
                    fUniform4f(g_uSun2K, 1.0f, 1.0f, 1.0f, 0.0f);
                }
                fUniform4f(g_uShadowK, g_shadowBias, g_shadowSlope, g_shadowStrength, g_shadowSoft);
                fUniform2f(g_uSunFade, g_sunFadeFrom, g_sunFadeTo);
                fUniform1f(g_uSunBias, g_sunBias);
                fUniform1i(g_uSunSlope, g_sunSlopeOcc);
                fUniform1i(g_uSunWalls, g_sunWalls);
                fUniform1f(g_uSunChar, g_sunChar);
                fUniform1f(g_uSunCharBlend, g_sunChar > 0.0f ? g_sunCharBlend : 0.0f);
                fUniform1i(g_uSunSmooth, g_sunSmooth);
                {
                    U9SunMap om, om2;
                    float x = 1.0f;
                    int old = u9shadow_sun_fading(&om, &om2, &x);
                    fUniform1i(g_uSunO, 17);
                    fUniform1i(g_uSunO2, 18);
                    if (old > 0) {
                        fActiveTexture(GL_TEXTURE0 + 17);
                        glBindTexture(GL_TEXTURE_2D, om.tex);
                        if (old == 2) {
                            fActiveTexture(GL_TEXTURE0 + 18);
                            glBindTexture(GL_TEXTURE_2D, om2.tex);
                        }
                        fActiveTexture(GL_TEXTURE0);
                        fUniform3f(g_uSunOR, om.r[0], om.r[1], om.r[2]);
                        fUniform3f(g_uSunOU, om.u[0], om.u[1], om.u[2]);
                        fUniform3f(g_uSunOL, om.l[0], om.l[1], om.l[2]);
                        fUniform3f(g_uSunOPR, om.pr[0], om.pr[1], om.pr[2]);
                        fUniform3f(g_uSunOPU, om.pu[0], om.pu[1], om.pu[2]);
                        fUniform3f(g_uSunOPL, om.pl[0], om.pl[1], om.pl[2]);
                        fUniform3f(g_uSunOC, om.c[0], om.c[1], om.c[2]);
                        fUniform4f(g_uSunOK, om.half, om.dz, om.texel, 1.0f);
                        if (old == 2) {
                            fUniform3f(g_uSunO2C, om2.c[0], om2.c[1], om2.c[2]);
                            fUniform4f(g_uSunO2K, om2.half, om2.dz, om2.texel, 1.0f);
                        } else {
                            fUniform4f(g_uSunO2K, 1.0f, 1.0f, 1.0f, 0.0f);
                        }
                    }
                    fUniform1f(g_uSunX, old > 0 ? x : 1.0f);
                }
                if (g_sunProbeWanted) { g_sunProbeWanted = 0; sun_probe(&v, &sm); }
            }
            fUniform3f(g_uSunMode, on && g_sunDebug ? 1.0f : 0.0f, on && (moonLive || g_sunOn) ? 1.0f : 0.0f,
                       on && spacesUp ? 1.0f : 0.0f);
        }
        if (have) {
            fUniform1i(g_uDepthMode, v.depthMode);
            fUniform4f(g_uVp, (float)v.vx, (float)v.vy, (float)v.vw, (float)v.vh);
            fUniform2f(g_uEngine, v.engineW, v.engineH);
            fUniform2f(g_uSurf, (float)g_w, (float)g_h);
            fUniform4f(g_uProj, v.sx, v.sy, v.cx, v.cy);
            fUniform2f(g_uZ, v.zRef, v.zFar);
            fUniform4f(g_uM0, v.m[0], v.m[1], v.m[2],  v.m[3]);
            fUniform4f(g_uM1, v.m[4], v.m[5], v.m[6],  v.m[7]);
            fUniform4f(g_uM2, v.m[8], v.m[9], v.m[10], v.m[11]);
        }
        u9under_frame();
        {
            int on = have && waterNow && v.depthMode == 2 && !u9under_state(NULL);
            int steps = g_waterSteps < 4 ? 4 : g_waterSteps > 48 ? 48 : g_waterSteps;
            fActiveTexture(GL_TEXTURE0 + 23);
            glBindTexture(GL_TEXTURE_2D, on ? g_waterTex : 0);
            fActiveTexture(GL_TEXTURE0);
            fUniform1i(g_uWater, 23);
            fUniform4f(g_uWaterK, on ? (float)g_waterStrength / 100.0f : 0.0f,
                       (float)(g_waterDist > 100 ? g_waterDist : 100), (float)steps, (float)g_waterView);
            if (on) g_waterFrames++;
            {
                u9gpu_begin(g_gpuWater);
                GLuint rt = on ? (GLuint)u9reflect_build(&v, g_w, g_h, g_scale) : 0;
                u9gpu_end(g_gpuWater);
                glViewport(0, 0, g_w, g_h);
                fActiveTexture(GL_TEXTURE0 + 24);
                glBindTexture(GL_TEXTURE_2D, rt);
                fActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, g_colour);
                fUseProgram(g_prog);
                fUniform1i(g_uRefl, 24);
                fUniform1f(g_uReflOn, rt ? 1.0f : 0.0f);
                fUniform1f(g_uWaterFloor, (float)(g_waterFloor < 0 ? 0 : g_waterFloor > 100 ? 100 : g_waterFloor) / 100.0f);
            }
            {
                float w[2][8];
                int n = on ? u9gl_screen_washes(w, 2) : 0, k;
                for (k = 0; k < 2; k++) {
                    if (k < n) {
                        fUniform4f(g_uWash[k * 2], w[k][0], w[k][1], w[k][2], 1.0f);
                        fUniform4f(g_uWash[k * 2 + 1], w[k][3], w[k][4], w[k][5], w[k][6]);
                    } else {
                        fUniform4f(g_uWash[k * 2], 0.0f, 0.0f, 0.0f, 0.0f);
                        fUniform4f(g_uWash[k * 2 + 1], 0.0f, 0.0f, 0.5f, 0.5f);
                    }
                }
            }
        }
        if (g_probeWanted) {
            g_probeWanted = 0;
            if (have) probe(&v);
            else if (g_probeOut) g_probeOut("no view this frame - is a world loaded?");
        }
        if (g_pickWanted) {
            int ties = g_pickWanted == 2;
            g_pickWanted = 0;
            if (g_pickOut) { if (ties) u9gl_pick_ties(g_pickOut); else u9gl_pick_report(g_pickOut); }
        }
        if (g_beamWanted) {
            int tex = g_beamWanted;
            g_beamWanted = 0;
            if (g_beamOut) u9gl_beam_report(tex, g_beamOut);
        }
    }
    u9gpu_begin(g_gpuComposite);
    aaOn = u9aa_begin(g_w, g_h, g_scale);
    if (!aaOn && g_scale > 1) {
        u9aa_scale_refused("the output target could not be made");
        glViewport(0, 0, surfW, surfH);
    }
    fUniform1i(g_uAa, !aaOn ? 0 : viewNow ? 1 : 2);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    fUseProgram(0);
    if (aaOn) u9aa_end(surfW, surfH);
    u9gpu_end(g_gpuComposite);
    if (g_shadowView) {
        U9FrameView sv;
        if (u9gl_frame_view(&sv)) u9shadow_draw_view(&sv, surfW, surfH);
    }
    u9shadow_draw_sun_view(surfW, surfH);
    pool_probe_after();
    probe_mark();
    light_marks();
    if (u9gl_label_mode() == 3) {
        U9FrameView wv;
        if (u9gl_frame_view(&wv)) u9wz_draw(&wv, surfW, surfH, g_scale);
    }
    if (g_checkWanted || g_frames == 300) {
        g_checkWanted = 0;
        check_copy(g_w, g_h, inv, g_pools);
    }
    glPopAttrib();
    g_frames++;
    u9gpu_frame_end();
    u9scost_frame_end();
}

static float night_amount(int amb[3])
{
    float n;
    if (!u9light_ambient(amb)) return -1.0f;
    if (amb[0] < 4 && amb[1] < 4 && amb[2] < 4) return -1.0f;
    n = (40.0f - (float)amb[0]) / 20.0f;
    return n < 0.0f ? 0.0f : n > 1.0f ? 1.0f : n;
}

static int dungeon_grade(void)
{
    float dark, cool, grey;
    int flagged, under = dungeon_here(&flagged);
    if (under != g_dungeonNow) {
        u9log_line("GLPOST dungeon grade %s on map %d (%s underground)", under ? "on" : "off",
                   u9console_current_map(), flagged ? "flagged" : "not flagged");
        g_dungeonNow = under;
    }
    if (!under) return 0;
    dark = 1.0f - (float)g_dungeonDark / 100.0f;
    cool = (float)g_dungeonCool / 100.0f;
    grey = (float)g_dungeonGrey / 100.0f;
    fUniform4f(g_uNight, dark * (1.0f - 0.25f * cool), dark * (1.0f - 0.12f * cool), dark, grey);
    fUniform1f(g_uNightKeep, (float)g_nightKeep);
    return 1;
}

static void night_grade(void)
{
    int amb[3];
    float n, s, dark, cool, grey;
    if (dungeon_grade()) return;
    n = night_amount(amb);
    if (!g_night || n <= 0.0f) {
        fUniform4f(g_uNight, 1.0f, 1.0f, 1.0f, -1.0f);
        if (n >= 0.0f && g_nightNow > 0.05f) {
            u9log_line("GLPOST night grade: ambient %d,%d,%d - day", amb[0], amb[1], amb[2]);
            g_nightNow = 0.0f;
        }
        return;
    }
    if (g_nightNow < 0.0f || fabsf(n - g_nightNow) > 0.1f) {
        u9log_line("GLPOST night grade: ambient %d,%d,%d - night %.2f", amb[0], amb[1], amb[2], (double)n);
        g_nightNow = n;
    }
    s    = n * (float)g_night / 100.0f;
    dark = 1.0f - s * (float)g_nightDark / 100.0f;
    cool = s * (float)g_nightCool / 100.0f;
    grey = s * (float)g_nightGrey / 100.0f;
    fUniform4f(g_uNight, dark * (1.0f - 0.25f * cool), dark * (1.0f - 0.12f * cool), dark, grey);
    fUniform1f(g_uNightKeep, (float)g_nightKeep);
}

void u9glpost_command(const char *arg, void (*out)(const char *fmt, ...))
{
    if (arg && !strncmp(arg, "terrain", 7) && (arg[7] == 0 || arg[7] == ' ')) {
        u9gl_terrain_command(arg + 7, out);
        return;
    }
    if (arg && !strncmp(arg, "dome", 4) && (arg[4] == 0 || arg[4] == ' ')) {
        const char *v = arg + 4;
        while (*v == ' ') v++;
        if (!strcmp(v, "on"))                    g_dome = 1;
        else if (!strcmp(v, "off"))              g_dome = 0;
        else if (!strncmp(v, "bright ", 7))      g_domeBright = atoi(v + 7);
        else if (!strncmp(v, "reach ", 6))       g_domeReach = atoi(v + 6);
        else if (*v) {
            out("post dome [on|off | bright <percent> | reach <units>]   the light cast by the glow over the"
                " column in Sir William Lett's tomb");
            return;
        }
        if (g_domeBright < 0) g_domeBright = 0;
        if (g_domeBright > 1000) g_domeBright = 1000;
        if (g_domeReach < 50) g_domeReach = 50;
        u9log_line("GLPOST dome light %s: strength %d%%, reach %d", g_dome ? "on" : "off",
                   g_domeBright, g_domeReach);
        out("dome light %s: strength %d%%, reach %d units; it lights what is round it when the light pools"
            " are on (pool on)", g_dome ? "on" : "off", g_domeBright, g_domeReach);
        return;
    }
    if (arg && !strncmp(arg, "dungeon", 7)) {
        const char *v = arg + 7;
        int flagged, graded, map = u9console_current_map();
        while (*v == ' ') v++;
        dungeon_here(NULL);
        if (!strncmp(v, "dark ", 5))      g_dungeonDark = atoi(v + 5);
        else if (!strncmp(v, "cool ", 5)) g_dungeonCool = atoi(v + 5);
        else if (!strncmp(v, "grey ", 5)) g_dungeonGrey = atoi(v + 5);
        else if (!strncmp(v, "skip", 4)) map_list(v + 4, g_dungeonSkip, sizeof g_dungeonSkip);
        else if (!strcmp(v, "off"))       g_dungeon = 0;
        else if (!strcmp(v, "on"))        g_dungeon = 1;
        else if (*v) {
            out("post dungeon on|off | dark|cool|grey <percent> | skip <maps> (e.g. skip 9,14; skip with nothing: none)");
            return;
        }
        if (g_dungeonDark < 0) g_dungeonDark = 0;
        if (g_dungeonDark > 95) g_dungeonDark = 95;
        if (g_dungeonCool < 0) g_dungeonCool = 0;
        if (g_dungeonCool > 200) g_dungeonCool = 200;
        if (g_dungeonGrey < 0) g_dungeonGrey = 0;
        if (g_dungeonGrey > 100) g_dungeonGrey = 100;
        graded = dungeon_here(&flagged);
        if (*v) u9log_line("GLPOST dungeon grade: '%s' - %s, dark %d%%, cool %d%%, grey %d%%, skip '%s'", arg,
                           g_dungeon ? "on" : "off", g_dungeonDark, g_dungeonCool, g_dungeonGrey, g_dungeonSkip);
        out("dungeon grade %s (dark %d%%, cool %d%%, grey %d%%; lamps' own %d units kept); every map but%s",
            g_dungeon ? "on" : "off", g_dungeonDark, g_dungeonCool, g_dungeonGrey, g_nightKeep,
            g_dungeonSkip[1] ? g_dungeonSkip : " none");
        out("map %d is %s and %s", map, flagged ? "flagged underground" : "not flagged underground",
            graded ? "graded" : "not graded");
        if (!g_want) out("the dungeon grade needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "night", 5)) {
        const char *v = arg + 5;
        int amb[3];
        float n;
        while (*v == ' ') v++;
        if (!strncmp(v, "dark ", 5))      g_nightDark = atoi(v + 5);
        else if (!strncmp(v, "cool ", 5)) g_nightCool = atoi(v + 5);
        else if (!strncmp(v, "grey ", 5)) g_nightGrey = atoi(v + 5);
        else if (!strncmp(v, "keep ", 5)) g_nightKeep = atoi(v + 5);
        else if (!strcmp(v, "glow")) {
            g_glowView = !g_glowView;
            u9log_line("GLPOST night grade: glow view %s", g_glowView ? "on" : "off");
            out(g_glowView ? "glow view on: black is graded, colour is left out (additive effects)"
                           : "glow view off");
            return;
        }
        else if (!strncmp(v, "graded", 6)) {
            const char *l = v + 6;
            while (*l == ' ') l++;
            if (!strcmp(l, "none")) g_nightGradedList[0] = g_nightGradedList[1] = 0;
            else if (*l) map_list(l, g_nightGradedList, sizeof g_nightGradedList);
            if (*l) u9log_line("GLPOST night grade: graded effects %s", g_nightGradedList[1] ? g_nightGradedList : "none");
            out("effects graded as world at night: %s (post night graded <textures ...>|none)",
                g_nightGradedList[1] ? g_nightGradedList : "none");
            return;
        }
        else if (!strcmp(v, "off"))       g_night = 0;
        else if (!strcmp(v, "on"))        g_night = 100;
        else if (*v >= '0' && *v <= '9')  g_night = atoi(v);
        else if (*v) {
            out("post night on|off|<percent> | post night dark <percent> | cool <percent> | grey <percent> | keep <units> | glow"
                " | graded <textures ...>|none");
            return;
        }
        if (g_night < 0) g_night = 0;
        if (g_nightDark < 0) g_nightDark = 0;
        if (g_nightDark > 90) g_nightDark = 90;
        if (g_nightCool < 0) g_nightCool = 0;
        if (g_nightCool > 200) g_nightCool = 200;
        if (g_nightGrey < 0) g_nightGrey = 0;
        if (g_nightGrey > 100) g_nightGrey = 100;
        if (g_nightKeep < 0) g_nightKeep = 0;
        if (g_nightKeep > 512) g_nightKeep = 512;
        n = night_amount(amb);
        if (*v) u9log_line("GLPOST night grade: '%s' - %d%%, dark %d%%, cool %d%%, grey %d%%, keep %d", arg,
                           g_night, g_nightDark, g_nightCool, g_nightGrey, g_nightKeep);
        out("night grade %d%% (dark %d%%, cool %d%%, grey %d%% at full night; lamps' own %d units kept); "
            "ambient %d,%d,%d -> %s",
            g_night, g_nightDark, g_nightCool, g_nightGrey, g_nightKeep, n < 0.0f ? 0 : amb[0], n < 0.0f ? 0 : amb[1],
            n < 0.0f ? 0 : amb[2], n < 0.0f ? "not graded (unreadable, or the moon map)" :
            n <= 0.0f ? "day, not graded" : n >= 1.0f ? "full night" : "dusk or dawn, part graded");
        if (!g_want) out("the night grade needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "effects full", 12)) {
        const char *v = arg + 12;
        while (*v == ' ') v++;
        if (!strcmp(v, "on")) g_effectFullSize = 1;
        else if (!strcmp(v, "off")) g_effectFullSize = 0;
        else if (*v) { out("post effects full on|off"); return; }
        if (*v) u9log_line("GLPOST effect layers sample the full-size texture: %s", g_effectFullSize ? "on" : "off");
        out(g_effectFullSize ? "effect layers sample the full-size texture, never a smaller mip"
                             : "effect layers sample mips as usual");
        return;
    }
    if (arg && !strncmp(arg, "sort effects", 12)) {
        const char *v = arg + 12;
        while (*v == ' ') v++;
        u9gl_sort_effects_command(v, out);
        return;
    }
    if (arg && !strncmp(arg, "sort layers", 11)) {
        const char *v = arg + 11;
        while (*v == ' ') v++;
        u9gl_sort_layers_command(v, out);
        return;
    }
    if (arg && !strncmp(arg, "effects depth", 13)) {
        const char *v = arg + 13;
        while (*v == ' ') v++;
        if (!strcmp(v, "on")) g_effectDepthWrite = 1;
        else if (!strcmp(v, "off")) g_effectDepthWrite = 0;
        else if (*v) { out("post effects depth on|off"); return; }
        if (*v) u9log_line("GLPOST effect layers write depth: %s", g_effectDepthWrite ? "on" : "off");
        out(g_effectDepthWrite ? "alpha-only effect layers write depth, as the game sends them"
                               : "alpha-only effect layers do not write depth (still hidden by walls)");
        return;
    }
    if (arg && !strncmp(arg, "effects", 7)) {
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        u9fx_command(arg + 7, out);
        return;
    }
    if (arg && !strncmp(arg, "depth", 5) && (arg[5] == ' ' || !arg[5])) {
        const char *v = arg + 5;
        while (*v == ' ') v++;
        if (!strcmp(v, "glide on")) g_depthGlide = 1;
        else if (!strcmp(v, "glide off")) g_depthGlide = 0;
        else if (!strncmp(v, "view", 4)) {
            g_depthView = atoi(v + 4);
            if (g_depthView < 0 || g_depthView > 2) g_depthView = 0;
            out(g_depthView == 1 ? "depth view: magenta where the position comes from the rounded depth (no exact distance)"
                : g_depthView == 2 ? "depth view: the rebuilt surface direction as colour" : "depth view off");
            return;
        }
        else if (!strncmp(v, "slack ", 6)) {
            g_depthSlack = atoi(v + 6);
            if (g_depthSlack < 0) g_depthSlack = 0;
            if (g_depthSlack > 64) g_depthSlack = 64;
        }
        else if (*v) { out("post depth glide on|off | post depth slack <steps, 0 off>"); return; }
        if (*v) u9log_line("GLPOST depth rounded to Glide's W-buffer: %s, slack %d steps", g_depthGlide ? "on" : "off", g_depthSlack);
        out(g_depthGlide ? "depth rounded to Glide's W-buffer precision (w/4096): nearly coplanar surfaces tie as on a Voodoo;"
                           " runs writing no depth test %d steps nearer"
                         : "depth at full precision (slack %d steps, used only with glide on)", g_depthSlack);
        return;
    }
    if (arg && !strncmp(arg, "normals", 7) && (arg[7] == ' ' || !arg[7])) {
        const char *v = arg + 7;
        while (*v == ' ') v++;
        if (!strcmp(v, "across")) g_nrmAcross = 1;
        else if (!strcmp(v, "nearer")) g_nrmAcross = 0;
        else if (*v) { out("post normals across|nearer"); return; }
        if (*v) u9log_line("GLPOST surface direction: %s", g_nrmAcross ? "across the pixel allowed" : "nearer neighbour only");
        out(g_nrmAcross ? "surface direction: the shortest of the nearer neighbour and the step across the pixel"
                        : "surface direction: the nearer neighbour only (as before 2026-10-27)");
        return;
    }
    if (arg && !strncmp(arg, "seams", 5) && (arg[5] == ' ' || !arg[5])) {
        const char *v = arg + 5;
        while (*v == ' ') v++;
        if (!strcmp(v, "on")) g_seams = 1;
        else if (!strcmp(v, "off")) g_seams = 0;
        else if (*v) { out("post seams on|off"); return; }
        if (*v) u9log_line("GLPOST seams: %s", g_seams ? "a crack takes its neighbours' surface" : "off");
        out(g_seams ? "seams on: a pixel seen through a crack in a seam is lit as the surface round it"
                    : "seams off: a crack's pixel lit where its depth puts it (before 2026-10-30)");
        return;
    }
    if (arg && !strncmp(arg, "hide", 4) && (arg[4] == ' ' || !arg[4])) {
        void u9gl_hide_command(const char *arg, void (*out)(const char *fmt, ...));
        u9gl_hide_command(arg + 4, out);
        return;
    }
    if (arg && !strncmp(arg, "fade", 4)) {
        const char *v = arg + 4;
        while (*v == ' ') v++;
        if (*v >= '0' && *v <= '9') g_effectFade = atoi(v);
        else if (!strcmp(v, "off")) g_effectFade = 0;
        else if (!strcmp(v, "debug")) g_effectFadeDebug = !g_effectFadeDebug;
        else if (*v) { out("post fade <percent of the card, 0 off> | post fade debug"); return; }
        if (g_effectFade > 50) g_effectFade = 50;
        if (*v) u9log_line("GLPOST effect fade: %d%%%s", g_effectFade, g_effectFadeDebug ? ", debug view" : "");
        out(g_effectFade ? "effect layers fade over the outer %d%% of their cards" : "effect fade off", g_effectFade);
        if (g_effectFadeDebug) out("debug view on: magenta where it fades, cyan on effects whose coordinates leave 0..1");
        u9gl_fade_report(out);
        return;
    }
    if (arg && !strncmp(arg, "water", 5) && (arg[5] == 0 || arg[5] == ' ')) {
        const char *v = arg + 5;
        while (*v == ' ') v++;
        if (!strncmp(v, "cover", 5) && (v[5] == ' ' || !v[5])) {
            const char *w = v + 5;
            while (*w == ' ') w++;
            u9gl_water_cover_command(w, out);
            return;
        }
        if (u9reflect_command(v, out)) {}
        else if (!strcmp(v, "on")) g_waterOn = 1;
        else if (!strcmp(v, "off")) g_waterOn = 0;
        else if (!strcmp(v, "view")) g_waterView = g_waterView == 1 ? 0 : 1;
        else if (!strcmp(v, "mirror")) g_waterView = g_waterView == 2 ? 0 : 2;
        else if (!strncmp(v, "strength ", 9)) g_waterStrength = atoi(v + 9);
        else if (!strncmp(v, "distance ", 9)) g_waterDist = atoi(v + 9);
        else if (!strncmp(v, "steps ", 6)) g_waterSteps = atoi(v + 6);
        else if (!strncmp(v, "floor ", 6)) g_waterFloor = atoi(v + 6);
        else if (*v) {
            out("post water on|off | view (the reflection alone on the water, magenta a miss) | mirror (the mirror from memory, whole) | strength <percent>"
                " | distance <units marched> | steps <4-48> | floor <percent> | memory on|off|forget | keep <seconds>"
                " | reach <units>");
            return;
        }
        if (g_waterStrength < 0) g_waterStrength = 0;
        if (g_waterStrength > 1000) g_waterStrength = 1000;
        if (*v) u9log_line("GLPOST water: '%s' - %s, strength %d%%, distance %d, %d steps, view %s", arg,
                           g_waterOn ? "on" : "off", g_waterStrength, g_waterDist, g_waterSteps,
                           g_waterView ? "on" : "off");
        out("water reflections %s%s: strength %d%%, floor %d%%, marched %d in %d steps; %u frames reflected%s",
            g_waterOn ? "on" : "off (post water on)", g_waterView ? ", showing the reflection alone" : "",
            g_waterStrength, g_waterFloor, g_waterDist, g_waterSteps, g_waterFrames,
            !g_waterTex ? " - NO water mask (needs 7 draw buffers and the exact depth)"
            : !g_waterMarked ? " - no water marked last frame (none in view, or gl_water_depth off)" : "");
        u9reflect_report(out);
        return;
    }
    if (arg && !strncmp(arg, "bloom", 5)) {
        const char *v = arg + 5;
        while (*v == ' ') v++;
        if (!strncmp(v, "strength ", 9))  g_bloomStrength = atoi(v + 9);
        else if (!strncmp(v, "fire ", 5)) g_bloomFire = atoi(v + 5);
        else if (!strncmp(v, "additive ", 9)) g_bloomAdditive = atoi(v + 9);
        else if (!strncmp(v, "glow ", 5)) g_bloomGlow = atoi(v + 5);
        else if (!strncmp(v, "surface ", 8)) g_bloomSurface = atoi(v + 8);
        else if (!strncmp(v, "runes ", 6)) g_bloomRunes = atoi(v + 6);
        else if (!strncmp(v, "glyphs ", 7)) g_bloomGlyphs = atoi(v + 7);
        else if (!strcmp(v, "lava on"))   g_bloomLava = 1;
        else if (!strcmp(v, "lava off"))  g_bloomLava = 0;
        else if (!strcmp(v, "view"))      g_bloomView = !g_bloomView;
        else if (!strcmp(v, "off"))       g_bloom = 0;
        else if (!strcmp(v, "on"))        g_bloom = 1;
        else if (*v) {
            out("post bloom on|off | strength <percent> | lava on|off | fire|additive|glow|surface|runes|glyphs <percent, 0 off> | view");
            return;
        }
        if (g_bloomStrength < 0) g_bloomStrength = 0;
        if (g_bloomStrength > 1000) g_bloomStrength = 1000;
        if (g_bloomFire < 0) g_bloomFire = 0;
        if (g_bloomFire > 1000) g_bloomFire = 1000;
        if (g_bloomAdditive < 0) g_bloomAdditive = 0;
        if (g_bloomAdditive > 1000) g_bloomAdditive = 1000;
        if (g_bloomGlow < 0) g_bloomGlow = 0;
        if (g_bloomGlow > 1000) g_bloomGlow = 1000;
        if (g_bloomSurface < 0) g_bloomSurface = 0;
        if (g_bloomSurface > 1000) g_bloomSurface = 1000;
        if (g_bloomRunes < 0) g_bloomRunes = 0;
        if (g_bloomRunes > 1000) g_bloomRunes = 1000;
        if (g_bloomGlyphs < 0) g_bloomGlyphs = 0;
        if (g_bloomGlyphs > 1000) g_bloomGlyphs = 1000;
        if (*v) u9log_line("GLPOST bloom: '%s' - %s, strength %d%%, lava %s, view %s", arg,
                           g_bloom ? "on" : "off", g_bloomStrength, g_bloomLava ? "on" : "off",
                           g_bloomView ? "on" : "off");
        out("bloom %s, strength %d%%; sources: lava %s, flames %d%%, additive effects %d%%, glows %d%% (%s),"
            " surfaces %d%% (%s), runes %d%%, glyphs %d%%; %u frames bloomed%s%s", g_bloom ? "on" : "off",
            g_bloomStrength, g_bloomLava ? "on" : "off", g_bloomFire, g_bloomAdditive, g_bloomGlow,
            g_bloomGlowList[1] ? g_bloomGlowList : "none", g_bloomSurface,
            g_bloomSurfaceList[1] ? g_bloomSurfaceList : "none", g_bloomRunes, g_bloomGlyphs, g_bloomFrames,
            g_bloomView ? "; view on: the bloom alone" : "",
            !g_want ? " - needs gl_post = 1 (restart)" : g_fbo && !g_bloomSrc ? " - NOT AVAILABLE (see log)" : "");
        return;
    }
    if (arg && !strncmp(arg, "blackgate", 9)) {
        u9fx_gate_command(arg + 9, out);
        return;
    }
    if (arg && !strncmp(arg, "pillar", 6)) {
        u9fx_pillar_command(arg + 6, out);
        return;
    }
    if (arg && !strncmp(arg, "guardian", 8)) {
        const char *v = arg + 8;
        while (*v == ' ') v++;
        if (!strncmp(v, "glow ", 5))        g_guardianGlow = atoi(v + 5);
        else if (!strncmp(v, "bright ", 7)) g_guardianBright = atoi(v + 7);
        else if (*v) {
            out("post guardian glow <percent, 0 off> | bright <percent, 100 as drawn>");
            return;
        }
        if (g_guardianGlow < 0) g_guardianGlow = 0;
        if (g_guardianGlow > 1000) g_guardianGlow = 1000;
        if (g_guardianBright < 0) g_guardianBright = 0;
        if (g_guardianBright > 1000) g_guardianBright = 1000;
        if (*v) u9log_line("GLPOST guardian: '%s' - glow %d%%, bright %d%%", arg, g_guardianGlow, g_guardianBright);
        out("guardian: glow %d%% of the bloom, colour %d%%; textures %s%s", g_guardianGlow, g_guardianBright,
            g_guardianList[1] ? g_guardianList : "none",
            !g_bloom ? " - bloom is off (post bloom on)" : "");
        return;
    }
    if (arg && !strncmp(arg, "moon", 4)) {
        const char *v = arg + 4;
        while (*v == ' ') v++;
        if (!strcmp(v, "phase on"))             g_moonPhase = 1;
        else if (!strcmp(v, "phase off"))       g_moonPhase = 0;
        else if (!strncmp(v, "phase ", 6))      { moon_phase_list(v + 6); g_moonPhase = 1; }
        else if (!strncmp(v, "bright ", 7))     g_moonBright = moon_percent(atoi(v + 7));
        else if (!strncmp(v, "bloom ", 6))      g_moonBloom = moon_percent(atoi(v + 6));
        else if (!strncmp(v, "halo bloom ", 11)) g_moonHaloBloom = moon_percent(atoi(v + 11));
        else if (!strncmp(v, "halo ", 5))       g_moonHalo = moon_percent(atoi(v + 5));
        else if (*v) {
            out("post moon bright|bloom|halo|halo bloom <percent> - both moons; 100 as drawn |"
                " post moon phase on|off | post moon phase <new> <crescent> <half> <gibbous> <full>");
            return;
        }
        if (*v) u9log_line("GLPOST moon: '%s' - bright %d%%, bloom %d%%, halo %d%%, halo bloom %d%%, "
                           "phase %s %d,%d,%d,%d,%d", arg, g_moonBright, g_moonBloom, g_moonHalo,
                           g_moonHaloBloom, g_moonPhase ? "on" : "off", g_moonPhasePct[0],
                           g_moonPhasePct[1], g_moonPhasePct[2], g_moonPhasePct[3], g_moonPhasePct[4]);
        out("moons: disc bright %d%%, disc bloom %d%%, halo %d%%, halo bloom %d%%; phase %s"
            " (new %d%%, crescent %d%%, half %d%%, gibbous %d%%, full %d%%)%s", g_moonBright,
            g_moonBloom, g_moonHalo, g_moonHaloBloom, g_moonPhase ? "on" : "off",
            g_moonPhasePct[0], g_moonPhasePct[1], g_moonPhasePct[2], g_moonPhasePct[3],
            g_moonPhasePct[4],
            u9cfg_int("gl_moon_glow", 1) ? "" : " - the disc bloom needs gl_moon_glow = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "ss", 2) && (arg[2] == 0 || arg[2] == ' ')) {
        u9aa_ss_command(arg + 2, out);
        if (!g_want) out("supersampling needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "contact", 7) && (arg[7] == 0 || arg[7] == ' ')) {
        u9contact_command(arg + 7, out);
        return;
    }
    if (arg && !strncmp(arg, "ao", 2) && (arg[2] == 0 || arg[2] == ' ')) {
        u9ao_command(arg + 2, out);
        if (!g_want) out("AO needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "taa", 3) && (arg[3] == 0 || arg[3] == ' ')) {
        u9aa_taa_command(arg + 3, out);
        if (!g_want) out("TAA needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strncmp(arg, "aa", 2) && (arg[2] == 0 || arg[2] == ' ')) {
        u9aa_command(arg + 2, out);
        if (!g_want) out("anti-aliasing needs gl_post = 1 (restart)");
        return;
    }
    if (arg && !strcmp(arg, "check")) {
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        g_checkWanted = 1;
        out("the next frame is compared with its offscreen copy - see GLPOST check in the log");
        return;
    }
    if (arg && !strcmp(arg, "grid")) {
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        g_grid = !g_grid;
        out(g_grid ? "world grid on: lines every 256, colour by height, dark contours every 16 up -"
                     " they should stay put as you turn and walk" : "world grid off");
        u9log_line("GLPOST world grid %s", g_grid ? "on" : "off");
        return;
    }
    if (arg && !strcmp(arg, "probe")) {
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        g_probeWanted = 1;
        g_probeOut = out;
        return;
    }
    if (arg && (!strcmp(arg, "pick") || !strcmp(arg, "pick ties"))) {
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        g_pickWanted = arg[4] ? 2 : 1;
        g_pickOut = out;
        return;
    }
    if (arg && !strncmp(arg, "beam", 4) && (arg[4] == 0 || arg[4] == ' ')) {
        int tex = arg[4] ? atoi(arg + 5) : 1760;
        if (!g_want || g_paused || g_failed || !g_fbo) { out("the post pass is not running"); return; }
        if (tex <= 0) { out("post beam [texture] - 1760 if none given"); return; }
        g_beamWanted = tex;
        g_beamOut = out;
        return;
    }
    if (arg && !strcmp(arg, "off")) { g_paused = 1; out("post pass off: drawing to the window"); return; }
    if (arg && !strcmp(arg, "on")) {
        if (!g_want) { out("gl_post is 0 in the ini - restart with gl_post = 1"); return; }
        g_paused = 0; out("post pass on"); return;
    }
    out("post pass %s%s; offscreen %dx%d; %u frames; gamma %s; %u checks, %u not exact",
        !g_want ? "not enabled (gl_post = 0)" : g_failed ? "FAILED (see log)" : g_paused ? "paused" : "on",
        "", g_w, g_h, g_frames, u9glpost_owns_gamma() ? "applied here" : "not applied here",
        g_checks, g_checkFails);
    out("post check | post grid | post probe | post pick [ties] | post beam [texture] | post aa ... | post ao ... | post contact ... | post water ... | post taa ... | post ss 1|2|3 | post night ... | post dungeon ... | post bloom ... | post moon ... | post guardian ... | post pillar ... | post blackgate ... | post fade <percent> | post hide <texture ...>|off | post depth glide on|off | post normals across|nearer | post effects [census|depth on|off|full on|off] | post off | post on");
}

void u9glpost_pool_command(const char *arg, void (*out)(const char *fmt, ...))
{
    if (!arg) arg = "";
    if (!strcmp(arg, "on"))  g_pools = 1;
    else if (!strcmp(arg, "off")) g_pools = 0;
    else if (!strncmp(arg, "lava", 4)) {
        const char *v = arg + 4;
        lava_settings();
        while (*v == ' ') v++;
        if (!strcmp(v, "on")) g_lavaOn = 1;
        else if (!strcmp(v, "off")) g_lavaOn = 0;
        else if (!strncmp(v, "strength ", 9)) g_lavaStrength = atoi(v + 9);
        else if (!strncmp(v, "range ", 6)) g_lavaRange = atoi(v + 6);
        else if (!strcmp(v, "forget")) { memset(g_lava, 0, sizeof g_lava); g_lavaN = 0; }
        else if (*v) { out("pool lava on|off | strength <percent> | range <units> | forget"); return; }
        if (g_lavaStrength < 0) g_lavaStrength = 0;
        if (g_lavaRange < 50) g_lavaRange = 50;
        out("lava lights %s: %d cells known on map %d; strength %d%%, range %d; the lava itself %s",
            g_lavaOn ? "on" : "off", g_lavaN, g_lavaMap, g_lavaStrength, g_lavaRange,
            g_lavaGlow ? "left as drawn" : "graded like any surface");
        return;
    }
    else if (!strncmp(arg, "fire", 4)) { u9fx_fire_command(arg + 4, out); return; }
    else if (!strncmp(arg, "glow", 4)) { u9fx_glow_command(arg + 4, out); return; }
    else if (!strncmp(arg, "rune", 4)) { u9fx_rune_command(arg + 4 + (arg[4] == 's'), out); return; }
    else if (!strcmp(arg, "fill cut") || !strcmp(arg, "fill keep")) {
        fill_cut_here();
        g_fillCut = arg[5] == 'c';
        if (!g_fillCut) out("fill lights drawn on every map");
        else if (!strcmp(g_fillKeep, ",,")) out("fill lights (type 1510) left out on every map");
        else out("fill lights (type 1510) left out, except on maps%s", g_fillKeep);
        return;
    }
    else if (!strcmp(arg, "replace on"))  g_replace = 1;
    else if (!strcmp(arg, "replace off")) g_replace = 0;
    else if (!strcmp(arg, "probe") || !strncmp(arg, "probe ", 6)) {
        float ax = -1.0f, ay = -1.0f;
        if (!g_pools || !g_want) { out("needs the pools on (pool on)"); return; }
        if (arg[5] && sscanf(arg + 6, "%f %f", &ax, &ay) != 2) {
            out("pool probe [<across> <down>]: a point on screen in percent, 0 0 the top left; bare: the reticle");
            return;
        }
        if (arg[5] && (ax < 0.0f || ax > 100.0f || ay < 0.0f || ay > 100.0f)) { out("percent, 0 to 100"); return; }
        g_ppAtX = ax;
        g_ppAtY = ay;
        g_poolProbeWanted = 1;
        g_poolProbeOut = out;
        if (ax >= 0.0f) out("the pools at %.0f%% across, %.0f%% down (also in the log; marked yellow for 3 s):", ax, ay);
        else out("the pools at the reticle (also in the log):");
        return;
    }
    else if (!strcmp(arg, "rooms")) {
        if (!g_pools || !g_contain) { out("needs the pools and rooms on (pool on, pool contain on)"); return; }
        g_roomsWanted = 1;
        g_roomsOut = out;
        out("the nearest lights and the room each is placed in (also in the log):");
        return;
    }
    else if (!strncmp(arg, "tail level ", 11)) {
        int pc = atoi(arg + 11);
        if (pc < 0) pc = 0;
        if (pc > 100) pc = 100;
        g_tailLevel = (float)pc / 100.0f;
    } else if (!strncmp(arg, "tail reach ", 11)) {
        int pc = atoi(arg + 11);
        if (pc < 100) pc = 100;
        if (pc > 500) pc = 500;
        g_tailReach = (float)pc / 100.0f;
    } else if (!strncmp(arg, "tail ", 5)) {
        const char *v = arg + 5;
        g_tail = !strcmp(v, "off") || !strcmp(v, "0") ? 0 :
                 !strcmp(v, "spill") || !strcmp(v, "1") ? 1 :
                 !strcmp(v, "all") || !strcmp(v, "2") ? 2 : g_tail;
    }
    else if (!strncmp(arg, "max ", 4)) {
        g_maxLights = atoi(arg + 4);
        if (g_maxLights < 1) g_maxLights = 1;
        if (g_maxLights > MAX_POOL_LIGHTS) {
            out("pool max: %d at most - the shader has room for %d", MAX_POOL_LIGHTS, MAX_POOL_LIGHTS);
            g_maxLights = MAX_POOL_LIGHTS;
        }
    }
    else if (!strncmp(arg, "shadow", 6)) {
        const char *v = arg + 6;
        while (*v == ' ') v++;
        if (!strcmp(v, "on"))  g_shadowOn = 1;
        else if (!strcmp(v, "off")) g_shadowOn = 0;
        else if (!strcmp(v, "view")) {
            g_shadowView = !g_shadowView;
            out(g_shadowView ? "shadow view on: this frame's casters from above, 2048 across, cut 128 over"
                               " the camera - walls, the Avatar and furniture should sit where they stand"
                             : "shadow view off");
        } else if (!strcmp(v, "debug")) {
            g_poolDebug = g_poolDebug == 3 ? 0 : 3;
            out(g_poolDebug == 3 ? "shadow debug: green lit by the shadow light, red in its shadow, dim elsewhere"
                                 : "shadow debug off");
        } else if (!strncmp(v, "memory", 6) && (v[6] == ' ' || !v[6])) {
            const char *w = v + 6;
            while (*w == ' ') w++;
            if (!strcmp(w, "on")) g_shadowMem = 1;
            else if (!strcmp(w, "off")) g_shadowMem = 0;
            else if (!strcmp(w, "animated on")) g_shadowMemAnimated = 1;
            else if (!strcmp(w, "animated off")) g_shadowMemAnimated = 0;
            else if (!strcmp(w, "faces on") || !strcmp(w, "faces off")) {
                g_lmFaces = w[7] == 'n';
                u9log_line("GLPOST shadow memory by face: %s", g_lmFaces ? "on" : "off");
                out("lamp memory by face %s: %s", g_lmFaces ? "ON" : "off", g_lmFaces ?
                    "each cube face draws only the remembered triangles that can reach it" : "every face draws the lamp's whole share");
                return;
            }
            else if (!strcmp(w, "grid on") || !strcmp(w, "grid off")) {
                g_lmGrid = w[6] == 'n';
                u9log_line("GLPOST shadow memory by place: %s", g_lmGrid ? "on" : "off");
                out("lamp memory by place %s: %s", g_lmGrid ? "ON" : "off", g_lmGrid ?
                    "each lamp tests only the remembered triangles near it" : "each lamp tests every remembered triangle");
                return;
            }
            else if (!strcmp(w, "runbox on") || !strcmp(w, "runbox off")) {
                g_lmRunBox = w[8] == 'n';
                u9log_line("GLPOST shadow memory by run: %s", g_lmRunBox ? "on" : "off");
                out("lamp memory by run %s: %s", g_lmRunBox ? "ON" : "off", g_lmRunBox ?
                    "a lamp skips the remembered runs whose box lies beyond its reach" : "each lamp tests every remembered triangle");
                return;
            }
            else if (!strcmp(w, "keep on") || !strcmp(w, "keep off")) {
                g_lmKeep = w[6] == 'n';
                u9log_line("GLPOST shadow memory keep: %s", g_lmKeep ? "on" : "off");
                out("lamp choices kept %s: %s", g_lmKeep ? "ON" : "off", g_lmKeep ?
                    "a lamp keeps its share of the memory while the memory is the same" : "every pass makes every choice again");
                return;
            }
            else if (*w) { out("pool shadow memory on|off | animated on|off | faces on|off | grid on|off | runbox on|off | keep on|off"); return; }
            if (*w) u9log_line("GLPOST shadow memory: %s, animated objects %s", g_shadowMem ? "on" : "off",
                               g_shadowMemAnimated ? "kept" : "left out");
            out("the memory keeps animated objects (characters...): %s%s", g_shadowMemAnimated ? "on" : "OFF - they cast only while drawn",
                u9gl_anim_draw_installed() ? "" : " (marking not installed: nothing is told apart)");
            out("lamp shadows from memory %s: walls and floors that left the screen still cast; this frame %u"
                " triangles from memory, %u left out as fixtures, %u us choosing them", g_shadowMem ? "on" : "off",
                g_shadowMemTris, g_shadowMemFix, g_shadowMemUs);
            return;
        } else if (!strncmp(v, "backfaces", 9) && (v[9] == ' ' || !v[9])) {
            const char *w = v + 9;
            unsigned tris, runs, dropped, polys;
            while (*w == ' ') w++;
            if (!strcmp(w, "on")) g_bfOn = 1;
            else if (!strcmp(w, "off")) g_bfOn = 0;
            else if (*w) { out("pool shadow backfaces on|off"); return; }
            if (*w) u9log_line("GLPOST back faces as casters: %s", g_bfOn ? "on" : "off");
            u9gl_back_face_stats(&tris, &runs, &dropped, &polys);
            out("back faces as shadow casters %s%s: last frame %u triangles in %u runs; %u sent in all, %u left out"
                " (translucent, second colour, not bound), %u lost for room, %u as polygons (left out)",
                g_bfOn ? "ON" : "off", u9gl_back_face_installed() ? "" : " - NOT INSTALLED (see the log)",
                tris, runs, g_bfSent, g_bfSkipped, dropped, polys);
            out("they cast for lamps and the sun alike, and the memory keeps them; never drawn on screen");
            u9gl_back_face_held_report(out);
            return;
        } else if (!strncmp(v, "guard", 5) && (v[5] == ' ' || !v[5])) {
            unsigned gt;
            if (v[5]) {
                g_guardPct = atoi(v + 6);
                if (g_guardPct < 0) g_guardPct = 0;
                if (g_guardPct > 200) g_guardPct = 200;
                u9log_line("GLPOST shadow guard band: %d%%", g_guardPct);
            }
            u9gl_guard_stats(&gt);
            if (g_guardPct > 0)
                out("guard band %d%% of the view beyond each edge: what lies there casts shadows (sun and lamps),"
                    " never drawn; last frame %u triangles", g_guardPct, gt);
            else out("guard band off: only what is on screen casts (pool shadow guard <percent> to try, e.g. 50)");
            return;
        } else if (!strncmp(v, "cost", 4) && (v[4] == ' ' || !v[4])) {
            const char *w = v + 4;
            while (*w == ' ') w++;
            if (!strcmp(w, "on")) g_scostOn = 1;
            else if (!strcmp(w, "off")) g_scostOn = 0;
            else if (*w) { out("pool shadow cost on|off"); return; }
            if (*w) u9log_line("GLPOST shadow cost timing: %s", g_scostOn ? "on" : "off");
            u9scost_report(out);
            return;
        } else if (!strcmp(v, "fire on") || !strcmp(v, "fire off")) {
            g_shadowFire = v[6] == 'n';
            u9log_line("GLPOST fire lights cast shadows: %s", g_shadowFire ? "on" : "off");
            out(g_shadowFire ? "fire lights (learnt from flames) take shadow cubes by the ranking, like lamps"
                             : "fire lights cast no shadows: they light through anything (as before)");
            return;
        } else if (!strcmp(v, "blob on"))  g_shadowHideBlob = 1;
        else if (!strcmp(v, "blob off")) g_shadowHideBlob = 0;
        else if (!strcmp(v, "cutouts on"))  g_shadowCutouts = 1;
        else if (!strcmp(v, "cutouts off")) g_shadowCutouts = 0;
        else if (!strncmp(v, "size ", 5))  g_shadowSize = atoi(v + 5);
        else if (!strncmp(v, "count ", 6)) {
            g_shadowCount = atoi(v + 6);
            if (g_shadowCount < 1) g_shadowCount = 1;
            if (g_shadowCount > U9SHADOW_MAX) {
                out("pool shadow count: %d at most", U9SHADOW_MAX);
                g_shadowCount = U9SHADOW_MAX;
            }
        }
        else if (!strncmp(v, "near ", 5)) {
            g_shadowNear = (float)atof(v + 5);
            if (g_shadowNear < 0.0f) g_shadowNear = 0.0f;
        } else if (!strncmp(v, "fixture ", 8)) {
            g_shadowFixture = (float)atof(v + 8);
            if (g_shadowFixture < 0.0f) g_shadowFixture = 0.0f;
        } else if (!strncmp(v, "part ", 5)) {
            g_shadowPart = (float)atof(v + 5);
            if (g_shadowPart < 0.0f) g_shadowPart = 0.0f;
        } else if (!strncmp(v, "column ", 7)) {
            g_shadowColumn = (float)atof(v + 7);
            if (g_shadowColumn < 0.0f) g_shadowColumn = 0.0f;
        }
        else if (!strncmp(v, "bias ", 5)) {
            g_shadowBias = (float)atof(v + 5);
            if (g_shadowBias < 0.0f) g_shadowBias = 0.0f;
        } else if (!strncmp(v, "soft ", 5)) {
            int pc = atoi(v + 5);
            if (pc < 0) pc = 0;
            if (pc > 1000) pc = 1000;
            g_shadowSoft = (float)pc / 100.0f;
        } else if (!strncmp(v, "slope ", 6)) {
            g_shadowSlope = (float)atof(v + 6) / 100.0f;
            if (g_shadowSlope < 0.0f) g_shadowSlope = 0.0f;
        } else if (!strncmp(v, "strength ", 9)) {
            int pc = atoi(v + 9);
            if (pc < 0) pc = 0;
            if (pc > 100) pc = 100;
            g_shadowStrength = (float)pc / 100.0f;
        } else if (*v) {
            out("pool shadow on|off | view | debug | cutouts on|off | size <texels> | bias <units> |"
                " strength <percent> | soft <percent> | slope <percent> | near <units> | fixture <units> | part <units> | column <units> | count <1-8> | blob on|off (hide the game's blob shadow) |"
                " memory on|off | backfaces on|off | fire on|off | guard <percent, 0 off>");
            return;
        }
    }
    else if (!strcmp(arg, "rank screen")) g_rank = 1;
    else if (!strcmp(arg, "rank near"))   g_rank = 0;
    else if (!strncmp(arg, "fade ", 5)) {
        g_fadeMs = atoi(arg + 5);
        if (g_fadeMs < 0) g_fadeMs = 0;
        if (g_fadeMs > 5000) g_fadeMs = 5000;
    }
    else if (!strncmp(arg, "flicker smooth ", 15)) {
        g_flickerSmooth = atoi(arg + 15);
        if (g_flickerSmooth < 0) g_flickerSmooth = 0;
        if (g_flickerSmooth > 1000) g_flickerSmooth = 1000;
    } else if (!strncmp(arg, "flicker range ", 14)) {
        g_flickerRange = atoi(arg + 14);
        if (g_flickerRange < 0) g_flickerRange = 0;
        if (g_flickerRange > 100000) g_flickerRange = 100000;
        flicker_range_apply();
    }
    else if (!strcmp(arg, "contain on"))  g_contain = 1;
    else if (!strcmp(arg, "contain off")) g_contain = 0;
    else if (!strcmp(arg, "keepout on"))  g_keepOut = 1;
    else if (!strcmp(arg, "soft on"))  g_soft = 1;
    else if (!strcmp(arg, "soft off")) g_soft = 0;
    else if (!strncmp(arg, "warmth ", 7)) {
        int pc = atoi(arg + 7);
        if (pc < 0) pc = 0;
        if (pc > 100) pc = 100;
        g_warmth = (float)pc / 100.0f;
    }
    else if (!strcmp(arg, "keepout off")) g_keepOut = 0;
    else if (!strcmp(arg, "march on"))  g_march = 1;
    else if (!strcmp(arg, "march off")) g_march = 0;
    else if (!strncmp(arg, "debug", 5)) {
        g_poolDebug = atoi(arg + 5);
        if (g_poolDebug < 0 || g_poolDebug > 2) g_poolDebug = 0;
        out(g_poolDebug == 1 ? "pool debug: green lit, red a light blocked by a wall, blue facing away,"
                               " dark none in range; brighter = more light" :
            g_poolDebug == 2 ? "pool debug: the albedo - purple where nothing can be lit" :
                               "pool debug off");
    }
    else if (!strncmp(arg, "strength ", 9)) {
        int pc = atoi(arg + 9);
        if (pc < 0) pc = 0;
        if (pc > 1000) pc = 1000;
        g_strength = (float)pc / 100.0f;
    } else if (*arg) {
        out("pool on|off | pool contain on|off | pool keepout on|off | pool march on|off | pool replace on|off | pool fill cut|keep | pool lava ... | pool fire ... | pool glow ... | pool rune ... |"
            " pool strength <percent> | pool warmth <percent> | pool soft on|off | pool debug 0|1|2 | pool rooms |"
            " pool probe | pool tail off|spill|all | pool tail level <percent> | pool tail reach <percent> |"
            " pool flicker smooth <ms> | pool flicker range <units, 0 the game's> | pool rank screen|near |"
            " pool fade <ms> | pool max <lights, 1-64> | pool shadow ...");
        return;
    }
    if (*arg) {
        u9log_line("GLPOST pools: '%s' - pools %s, rooms %s, wall test %s, replace %s, strength %.2f",
                   arg, g_pools ? "on" : "off", g_contain ? "on" : "off", g_march ? "on" : "off",
                   g_replace ? "on" : "off", (double)g_strength);
        if (!g_pools) { replace_apply(); flicker_range_apply(); }
    }
    if (!g_want) out("the pools need gl_post = 1 (restart)");
    out("pools %s; rooms %s; wall test %s; engine point lights %s; strength %d%%; up to %u lights"
        " so far, %u of them in rooms", g_pools ? "on" : "off", g_contain ? "on" : "off",
        g_march ? "on" : "off", g_replaced ? "OFF (replaced)" : "on",
        (int)(g_strength * 100.0f + 0.5f), g_poolLightsMax, g_roomLightsMax);
    out("warmth %d%%; soft bright pools %s", (int)(g_warmth * 100.0f + 0.5f), g_soft ? "on" : "off");
    out("outdoor lights %s rooms (up to %u rooms in a frame so far)",
        g_keepOut ? "kept out of" : "free to light", g_outerMax);
    out("caps, frames cut of %u: lights over %d %u (most seen %u); a light's room left out %u;"
        " a room nearby left out %u", g_poolFrames, g_maxLights, g_capLights, g_lightsSeenMax,
        g_capRoom, g_capOuter);
    out("tail %s: %d%% of full, out to %d%% of each light's range",
        g_tail == 0 ? "off" : g_tail == 1 ? "on spill only (light leaving through an open door or window)"
                                          : "everywhere", (int)(g_tailLevel * 100.0f + 0.5f),
        (int)(g_tailReach * 100.0f + 0.5f));
    out("flicker: eased over %d ms%s; range %s %d (the game's own %d)", g_flickerSmooth,
        g_flickerSmooth ? "" : " (off: the game's steps as they come)", g_rangeHeld ? "held at" : "not held,",
        u9light_flicker_range(), g_rangeEngine);
    out("which %d: %s; fade in and out over %d ms; %u fades in, %u out; a light waited for a slot in %u"
        " frames", g_maxLights, g_rank ? "by what each lights on screen" : "nearest reach to the camera",
        g_fadeMs, g_fadeIns, g_fadeOuts, g_capWait);
    out("shadows %s: up to %d lamps (%u now), cube %d, cutouts %s, bias %.1f + %.1f%% of the distance,"
        " strength %d%%, soft %d%%%s", g_shadowOn ? "on" : "off", g_shadowCount, g_shadowHeld, g_shadowSize,
        g_shadowCutouts ? "on" : "off", (double)g_shadowBias, (double)(g_shadowSlope * 100.0f),
        (int)(g_shadowStrength * 100.0f + 0.5f), (int)(g_shadowSoft * 100.0f + 0.5f),
        g_shadowView ? "; view on" : "");
    out("shadow fixture: in a run whose box holds the light within %.0f, the triangles within %.0f of it across"
        " with no edge over %.0f cast nothing; in any run, those wholly within %.0f across at any height"
        " (a chain, a post, a lantern)", (double)g_shadowNear, (double)(g_shadowFixture * 0.5f), (double)g_shadowPart,
        (double)g_shadowColumn);
    out("shadow cubes drawn %u; the last: %u triangles, %u fixture triangles, %u runs drawn, %u culled;"
        " the Avatar's lamp changed %u times", g_shadowBuilds, g_shadowTris, g_shadowFixtures,
        g_shadowRunsDrawn, g_shadowRunsCulled, g_shadowChanges);
    out("shadows from memory %s: %u triangles from it last frame, %u left as fixtures, %u us choosing",
        g_shadowMem ? "on" : "off", g_shadowMemTris, g_shadowMemFix, g_shadowMemUs);
    {
        unsigned tris, runs, dropped, polys;
        u9gl_back_face_stats(&tris, &runs, &dropped, &polys);
        unsigned gt;
        u9gl_guard_stats(&gt);
        out("back faces as casters %s%s: %u triangles last frame; fire lights cast %s; guard band %d%% (%u triangles)",
            g_bfOn ? "ON" : "off", u9gl_back_face_installed() ? "" : " (not installed)", tris,
            g_shadowFire ? "ON" : "off", g_guardPct > 0 ? g_guardPct : 0, gt);
    }
    out("the game's blob shadow under characters (texture %d) %s", U9TEX_BLOB_SHADOW,
        u9shadow_hide_blob() ? "hidden" : g_shadowHideBlob ? "drawn (shadows off)" : "drawn");
}

void u9glpost_release(void)
{
    if (g_loaded && wglGetCurrentContext()) {
        if (g_bound) fBindFramebuffer(GL_FRAMEBUFFER, 0);
        delete_frame();
        if (g_prog) fDeleteProgram(g_prog);
        u9bloom_release();
        u9beam_release();
        u9rays_release();
        u9ao_release();
        u9reflect_release();
        u9aa_release();
        if (g_spaceTex) {
            glDeleteTextures(1, &g_spaceTex);
            u9metrics_tex_destroyed((unsigned)(U9SP_TEX_W * U9SP_TEX_H * 16));
        }
    }
    g_fbo = g_colour = g_depth = g_albedo = g_glow = g_bloomSrc = g_prog = g_spaceTex = 0;
    g_sky = 0;
    g_waterTex = 0;
    g_waterMarked = 0;
    g_w = g_h = 0;
    g_bound = 0;
    g_loaded = 0;
}

void u9glpost_report(void)
{
    if (!g_want) return;
    u9log_line("GLPOST totals: %u frames through the post pass, offscreen frame made %u times,"
               " %u copy checks (%u not exact)%s", g_frames, g_resizes, g_checks, g_checkFails,
               g_failed ? " - turned itself OFF (see the GLPOST line above)" : "");
    u9fx_report();
    u9aa_report();
    if (g_bloom)
        u9log_line("GLPOST bloom: %u frames bloomed, strength %d%%, lava %s", g_bloomFrames,
                   g_bloomStrength, g_bloomLava ? "on" : "off");
    if (g_poolFrames)
        u9log_line("GLPOST pools: %u frames lit, at most %u lights in a frame, at most %u of them"
                   " in rooms (%u frames with rooms)", g_poolFrames, g_poolLightsMax, g_roomLightsMax,
                   g_roomLit);
    if (o_lightsSave || o_lightsLoad)
        u9log_line("GLPOST light switch: %u saves written with the engine's own value, %u loads repaired,"
                   " %u relights after a change", g_saveSwaps, g_loadRepairs, g_relights);
    if (g_poolFrames)
        u9log_line("GLPOST pools caps: %u frames had over %d lights (most %u); a light's room left out"
                   " in %u, a room nearby in %u; up to %u rooms in a frame", g_capLights, g_maxLights,
                   g_lightsSeenMax, g_capRoom, g_capOuter, g_outerMax);
}
