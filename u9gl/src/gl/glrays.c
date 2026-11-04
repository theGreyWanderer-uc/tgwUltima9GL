#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "../fix/lightwatch.h"
#include "metrics.h"
#include "glsun.h"
#include "glrays.h"

int u9glpost_running(void);

#define GL_FRAMEBUFFER               0x8D40
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_VERTEX_SHADER             0x8B31
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_TEXTURE0                  0x84C0
#define GL_CLAMP_TO_BORDER           0x812D
#define GL_RGBA16F                   0x881A

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
static ActiveTextureFn          fActiveTexture;

#define RAYS_MAX_SAMPLES 128

static int    g_on = 1;
static int    g_strength = 200;
static int    g_decay = 97;
static int    g_length = 150;
static int    g_samples = 64;
static int    g_threshold = 30;
static int    g_radius = 20;
static int    g_air = 1000;
static int    g_moon = 100;
static int    g_moonRadius = 25;
static int    g_glareOn = 1;
static int    g_glare = 30;
static int    g_view;

static int    g_loaded, g_failed;
static GLuint g_mask, g_blur;
static GLint  g_mFrame, g_mDepth, g_mExact, g_mHaveExact, g_mDepthMode, g_mZ, g_mLight, g_mVp, g_mK;
static GLint  g_mBloom, g_mSrc, g_mTexel, g_mSky, g_mHaveSky;
static GLint  g_bMask, g_bLight, g_bK, g_bN, g_bScale;
static float  g_aspect;
static GLuint g_tex[2], g_fbo[2];
static int    g_w, g_h;

static unsigned g_frames, g_framesDrawn;
static float  g_lastFade, g_lastUv[2], g_lastElev;
static const char *g_lastWhy = "not run yet";
static float  g_moonFade[2], g_moonUv[2][2], g_moonElev[2];
static const char *g_moonWhy[2] = { "not run yet", "not run yet" };

static const char *const k_vs =
    "#version 120\n"
    "void main() {\n"
    "    gl_Position    = gl_Vertex;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "}\n";

static const char *const k_mask =
    "#version 120\n"
    "uniform sampler2D u_frame, u_depth, u_exact, u_bloom, u_sky;\n"
    "uniform int  u_haveExact, u_depthMode, u_haveSky;\n"
    "uniform int  u_src;\n"
    "uniform vec2 u_z;\n"
    "uniform vec4 u_light;\n"
    "uniform vec4 u_vp;\n"
    "uniform vec4 u_k;\n"
    "uniform vec2 u_texel;\n"
    "bool ui_at(vec2 q, float d) {\n"
    "    if (u_haveExact != 0) {\n"
    "        vec2 h = u_texel * 0.4;\n"
    "        if (texture2D(u_exact, q + vec2(-h.x, -h.y)).r < -1.0 || texture2D(u_exact, q + vec2(h.x, -h.y)).r < -1.0 ||\n"
    "            texture2D(u_exact, q + vec2(-h.x, h.y)).r < -1.0 || texture2D(u_exact, q + vec2(h.x, h.y)).r < -1.0) return true;\n"
    "        float e = texture2D(u_exact, q).r; if (e > 0.0) return e <= 16.0;\n"
    "    }\n"
    "    if (d >= 1.0) return false;\n"
    "    return ((u_depthMode == 0) ? u_z.x / max(1.0 - d, 1e-6) : u_z.x + d * (u_z.y - u_z.x)) <= 16.0;\n"
    "}\n"
    "bool solid_ui(vec2 q) {\n"
    "    vec2 h = u_texel * 0.4;\n"
    "    return texture2D(u_exact, q + vec2(-h.x, -h.y)).r < -1.5e6 || texture2D(u_exact, q + vec2(h.x, -h.y)).r < -1.5e6 ||\n"
    "           texture2D(u_exact, q + vec2(-h.x, h.y)).r < -1.5e6 || texture2D(u_exact, q + vec2(h.x, h.y)).r < -1.5e6;\n"
    "}\n"
    "vec3 over(vec3 c) {\n"
    "    float lum = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    return c * (max(lum - u_k.x, 0.0) / max(1.0 - u_k.x, 0.01));\n"
    "}\n"
    "vec3 sky_at(vec2 q, float d) {\n"
    "    if (d < 1.0) return vec3(0.0);\n"
    "    if (u_src == 1) return texture2D(u_bloom, q).rgb;\n"
    "    return over(texture2D(u_frame, q).rgb);\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st;\n"
    "    if (uv.x < u_vp.x || uv.y < u_vp.y || uv.x > u_vp.z || uv.y > u_vp.w) { gl_FragColor = vec4(0.0); return; }\n"
    "    if (u_k.w > 1.5) { gl_FragColor = vec4(texture2D(u_sky, uv).rgb, 1.0); return; }\n"
    "    float d = texture2D(u_depth, uv).r;\n"
    "    vec2 o = (uv - u_light.xy) * vec2(u_light.z, 1.0);\n"
    "    float fall = 1.0 - smoothstep(0.0, u_k.y, length(o));\n"
    "    vec3 m;\n"
    "    if (u_src == 1 && ui_at(uv, d)) m = u_k.w > 0.5 ? vec3(1.0, 0.0, 0.0) : texture2D(u_bloom, uv).rgb;\n"
    "    else if (u_haveSky != 0 && ui_at(uv, d) && !solid_ui(uv))\n"
    "        m = u_k.w > 0.5 ? vec3(1.0, 0.0, 0.0) : over(texture2D(u_sky, uv).rgb);\n"
    "    else if (ui_at(uv, d)) {\n"
    "        vec3 sum = vec3(0.0);\n"
    "        float n = 0.0;\n"
    "        for (int i = 0; i < 12; i++) {\n"
    "            float a = float(i) * 1.0472 + (i < 6 ? 0.0 : 0.5236);\n"
    "            vec2 q = uv + vec2(cos(a) / u_light.z, sin(a)) * u_k.z * (i < 6 ? 1.0 : 2.0);\n"
    "            float dq = texture2D(u_depth, q).r;\n"
    "            if (q.x < u_vp.x || q.y < u_vp.y || q.x > u_vp.z || q.y > u_vp.w || ui_at(q, dq)) continue;\n"
    "            sum += sky_at(q, dq);\n"
    "            n += 1.0;\n"
    "        }\n"
    "        m = n > 0.0 ? sum / n : vec3(0.0);\n"
    "        if (u_k.w > 0.5) { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); return; }\n"
    "    } else m = sky_at(uv, d);\n"
    "    gl_FragColor = vec4(u_k.w > 0.5 && m == vec3(1.0, 0.0, 0.0) ? m : m * fall, 1.0);\n"
    "}\n";

static const char *const k_blur =
    "#version 120\n"
    "uniform sampler2D u_mask;\n"
    "uniform vec2 u_light;\n"
    "uniform vec2 u_k;\n"
    "uniform int  u_n;\n"
    "uniform vec3 u_scale;\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st;\n"
    "    vec2 step = (uv - u_light) * u_k.x / float(u_n);\n"
    "    vec3 sum = vec3(0.0);\n"
    "    float w = 1.0, wsum = 0.0;\n"
    "    for (int i = 0; i < 128; i++) {\n"
    "        if (i >= u_n) break;\n"
    "        sum += texture2D(u_mask, uv).rgb * w;\n"
    "        wsum += w;\n"
    "        w *= u_k.y;\n"
    "        uv -= step;\n"
    "    }\n"
    "    gl_FragColor = vec4(sum / max(wsum, 1e-6) * u_scale, 1.0);\n"
    "}\n";

void u9rays_init(void)
{
    g_on        = u9cfg_int("gl_sun_rays", 1);
    g_strength  = u9cfg_int("gl_sun_rays_strength", 200);
    g_decay     = u9cfg_int("gl_sun_rays_decay", 97);
    g_length    = u9cfg_int("gl_sun_rays_length", 150);
    g_samples   = u9cfg_int("gl_sun_rays_samples", 64);
    g_threshold = u9cfg_int("gl_sun_rays_threshold", 30);
    g_radius    = u9cfg_int("gl_sun_rays_radius", 20);
    g_air       = u9cfg_int("gl_sun_rays_air", 1000);
    g_moon      = u9cfg_int("gl_moon_rays", 100);
    g_moonRadius = u9cfg_int("gl_moon_rays_radius", 25);
    g_glareOn   = u9cfg_int("gl_sun_glare", 1);
    g_glare     = u9cfg_int("gl_sun_glare_scale", 30);
}

int u9rays_glare(void)
{
    if (!g_glareOn) return 0;
    return g_glare < 0 ? 0 : g_glare > 400 ? 400 : g_glare;
}

int u9rays_sun_on(void)
{
    return g_on && !g_failed;
}

int u9rays_air(void)
{
    return g_air > 0 ? g_air : 0;
}

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLRAYS off for this session: %s", why);
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
        u9log_line("GLRAYS %s shader did not compile: %s", what, log);
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
    GLint ok = 0;
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
        u9log_line("GLRAYS %s program did not link: %s", what, log);
        fDeleteProgram(p);
        return 0;
    }
    return p;
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
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D ||
        !fCheckFramebufferStatus || !fCreateShader || !fShaderSource || !fCompileShader ||
        !fGetShaderiv || !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog ||
        !fCreateProgram || !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram ||
        !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform2f || !fUniform3f || !fUniform4f ||
        !fActiveTexture) {
        fail("missing GL entry points");
        return 0;
    }
    g_mask = build_program(k_mask, "mask");
    g_blur = g_mask ? build_program(k_blur, "blur") : 0;
    if (!g_mask || !g_blur) { fail("the ray shaders did not build"); return 0; }
    g_mFrame     = fGetUniformLocation(g_mask, "u_frame");
    g_mDepth     = fGetUniformLocation(g_mask, "u_depth");
    g_mExact     = fGetUniformLocation(g_mask, "u_exact");
    g_mHaveExact = fGetUniformLocation(g_mask, "u_haveExact");
    g_mDepthMode = fGetUniformLocation(g_mask, "u_depthMode");
    g_mZ         = fGetUniformLocation(g_mask, "u_z");
    g_mLight     = fGetUniformLocation(g_mask, "u_light");
    g_mVp        = fGetUniformLocation(g_mask, "u_vp");
    g_mK         = fGetUniformLocation(g_mask, "u_k");
    g_mBloom     = fGetUniformLocation(g_mask, "u_bloom");
    g_mSrc       = fGetUniformLocation(g_mask, "u_src");
    g_mTexel     = fGetUniformLocation(g_mask, "u_texel");
    g_mSky       = fGetUniformLocation(g_mask, "u_sky");
    g_mHaveSky   = fGetUniformLocation(g_mask, "u_haveSky");
    g_bMask      = fGetUniformLocation(g_blur, "u_mask");
    g_bLight     = fGetUniformLocation(g_blur, "u_light");
    g_bK         = fGetUniformLocation(g_blur, "u_k");
    g_bN         = fGetUniformLocation(g_blur, "u_n");
    g_bScale     = fGetUniformLocation(g_blur, "u_scale");
    u9log_line("GLRAYS ready: strength %d%%, decay %d%%, length %d%%, %d samples", g_strength, g_decay,
               g_length, g_samples);
    return 1;
}

static void delete_targets(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (g_fbo[i]) { fDeleteFramebuffers(1, &g_fbo[i]); g_fbo[i] = 0; }
        if (g_tex[i]) {
            glDeleteTextures(1, &g_tex[i]);
            u9metrics_tex_destroyed((unsigned)(g_w * g_h * 8));
            g_tex[i] = 0;
        }
    }
    g_w = g_h = 0;
}

static int make_targets(int w, int h)
{
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int i;
    delete_targets();
    glPushAttrib(GL_TEXTURE_BIT);
    for (i = 0; i < 2; i++) {
        GLenum status;
        glGenTextures(1, &g_tex[i]);
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, black);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        u9metrics_tex_created((unsigned)(w * h * 8));
        fGenFramebuffers(1, &g_fbo[i]);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex[i], 0);
        status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_w = w; g_h = h;
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glPopAttrib();
            u9log_line("GLRAYS target %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
            delete_targets();
            fail("a target was incomplete");
            return 0;
        }
    }
    glPopAttrib();
    u9log_line("GLRAYS targets %dx%d, RGBA16F", w, h);
    return 1;
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

static float smooth01(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

static int project(const U9FrameView *v, int w, int h, const float dir[3], float uv[2], float *edge)
{
    float cx = 0.0f, fwd = 0.0f, up = 0.0f, ex, ey, out;
    int i;
    for (i = 0; i < 3; i++) {
        cx  += v->m[4 * i + 0] * dir[i];
        fwd += v->m[4 * i + 1] * dir[i];
        up  += v->m[4 * i + 2] * dir[i];
    }
    if (fwd < 0.05f) return 0;
    ex = v->cx + cx * v->sx / fwd;
    ey = v->cy - up * v->sy / fwd;
    uv[0] = ((float)v->vx + ex * (float)v->vw / v->engineW) / (float)w;
    uv[1] = ((float)v->vy + (float)v->vh - ey * (float)v->vh / v->engineH) / (float)h;
    out = 0.0f;
    if (uv[0] < 0.0f) out = -uv[0] * (float)w / (float)h;
    if (uv[0] > 1.0f) out = (uv[0] - 1.0f) * (float)w / (float)h;
    if (uv[1] < 0.0f && -uv[1] > out) out = -uv[1];
    if (uv[1] > 1.0f && uv[1] - 1.0f > out) out = uv[1] - 1.0f;
    *edge = 1.0f - smooth01(out / 0.5f);
    return 1;
}

static int sun_on_screen(const U9FrameView *v, int w, int h, float uv[2], float tint[3], float *fade)
{
    U9Sun sun;
    float k;
    *fade = 0.0f;
    if (!u9sun_read(&sun)) { g_lastWhy = "no sky state"; return 0; }
    g_lastElev = sun.elevation;
    if (sun.underground) { g_lastWhy = "underground"; return 0; }
    if (sun.moonMap) { g_lastWhy = "the moon map"; return 0; }
    if (sun.time < 19800 || sun.time > 66600) { g_lastWhy = "the sun is not drawn (05:30-18:30 only)"; return 0; }
    if (!project(v, w, h, sun.dir, uv, fade)) { g_lastWhy = "the sun is behind the camera"; return 0; }
    g_lastUv[0] = uv[0]; g_lastUv[1] = uv[1];
    *fade *= smooth01((sun.elevation + 5.0f) / 5.0f);
    *fade *= 1.0f - 4.0f * sun.storm > 0.0f ? 1.0f - 4.0f * sun.storm : 0.0f;
    if (*fade <= 0.001f) { g_lastWhy = "faded out (edge, height or storm)"; return 0; }
    k = 2.0f - 10.0f * sun.dir[U9SUN_UP];
    if (k < 1.0f) k = 1.0f;
    if (k > 2.0f) k = 2.0f;
    tint[0] = 1.0f;
    tint[1] = tint[2] = 0.5f + 0.5f / k;
    g_lastWhy = "on";
    return 1;
}

static int moon_on_screen(const U9FrameView *v, int w, int h, const U9Moons *ms, int m, float uv[2], float *fade)
{
    const U9MoonOne *mo = &ms->m[m];
    *fade = 0.0f;
    g_moonElev[m] = mo->elevation;
    if (!ms->shown) { g_moonWhy[m] = "the moons are not drawn"; return 0; }
    if (!project(v, w, h, mo->dir, uv, fade)) { g_moonWhy[m] = "behind the camera"; return 0; }
    g_moonUv[m][0] = uv[0]; g_moonUv[m][1] = uv[1];
    *fade *= smooth01((mo->elevation + 5.0f) / 5.0f);
    if (*fade <= 0.001f) { g_moonWhy[m] = "faded out (edge or height)"; return 0; }
    g_moonWhy[m] = "on";
    return 1;
}

static void light_pass(int src, const float uv[2], const float rgb[3], float threshold, float radius, int n)
{
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[0]);
    glDisable(GL_BLEND);
    fUseProgram(g_mask);
    fUniform1i(g_mSrc, src);
    fUniform4f(g_mLight, uv[0], uv[1], g_aspect, 0.0f);
    fUniform4f(g_mK, threshold, radius, 0.04f, g_view == 4 ? 2.0f : g_view == 3 ? 1.0f : 0.0f);
    quad();

    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[1]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    fUseProgram(g_blur);
    fActiveTexture(GL_TEXTURE0 + 4);
    glBindTexture(GL_TEXTURE_2D, g_tex[0]);
    fActiveTexture(GL_TEXTURE0);
    fUniform1i(g_bMask, 4);
    fUniform2f(g_bLight, uv[0], uv[1]);
    fUniform2f(g_bK, (float)g_length / 100.0f, (float)g_decay / 100.0f);
    fUniform1i(g_bN, n);
    fUniform3f(g_bScale, rgb[0], rgb[1], rgb[2]);
    quad();
    glDisable(GL_BLEND);
}

unsigned u9rays_build(const U9FrameView *v, unsigned colourTex, unsigned depthTex, unsigned exactTex,
                      unsigned bloomTex, unsigned skyTex, int w, int h, int scale, float k[4])
{
    float uv[2], tint[3], fade, rgb[3];
    int rw, rh, n, m, lights = 0, sunOn, wantMoons;
    U9Moons ms;
    k[0] = k[1] = k[2] = k[3] = 0.0f;
    g_lastFade = g_moonFade[0] = g_moonFade[1] = 0.0f;
    g_moonWhy[0] = g_moonWhy[1] = g_moon > 0 ? "not run" : "off (rays moon <percent>)";
    if (!g_on && !g_view) { g_lastWhy = "off (rays on)"; return 0; }
    if (g_failed || !v || !v->valid || w < 4 || h < 4 || !load()) {
        g_lastWhy = g_failed ? "FAILED, see the log" : "no frame view";
        return 0;
    }
    g_frames++;
    if (u9light_book_open()) { g_lastWhy = g_moonWhy[0] = g_moonWhy[1] = "a book is open"; return 0; }
    sunOn = sun_on_screen(v, w, h, uv, tint, &fade);
    wantMoons = g_moon > 0;
    if (wantMoons && !bloomTex) { g_moonWhy[0] = g_moonWhy[1] = "needs the bloom (post bloom on)"; wantMoons = 0; }
    if (wantMoons && !u9moon_read(&ms)) { g_moonWhy[0] = g_moonWhy[1] = "no clock"; wantMoons = 0; }
    if (!sunOn && !wantMoons) return 0;
    if (scale < 1) scale = 1;
    rw = w / (2 * scale); rh = h / (2 * scale);
    if (rw < 2) rw = 2;
    if (rh < 2) rh = 2;
    if ((rw != g_w || rh != g_h) && !make_targets(rw, rh)) return 0;
    n = g_samples < 1 ? 1 : g_samples > RAYS_MAX_SAMPLES ? RAYS_MAX_SAMPLES : g_samples;
    g_aspect = (float)w / (float)h;

    glDisable(GL_BLEND);
    glViewport(0, 0, rw, rh);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[1]);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fUseProgram(g_mask);
    fActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, (GLuint)depthTex);
    fActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, (GLuint)exactTex);
    fActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, (GLuint)bloomTex);
    fActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, (GLuint)skyTex);
    fActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)colourTex);
    fUniform1i(g_mFrame, 0);
    fUniform1i(g_mDepth, 1);
    fUniform1i(g_mExact, 2);
    fUniform1i(g_mBloom, 3);
    fUniform1i(g_mSky, 5);
    fUniform1i(g_mHaveSky, skyTex ? 1 : 0);
    fUniform1i(g_mHaveExact, exactTex ? 1 : 0);
    fUniform1i(g_mDepthMode, v->depthMode);
    fUniform2f(g_mZ, v->zRef, v->zFar);
    fUniform2f(g_mTexel, 1.0f / (float)w, 1.0f / (float)h);
    fUniform4f(g_mVp, (float)v->vx / (float)w, (float)v->vy / (float)h,
               (float)(v->vx + v->vw) / (float)w, (float)(v->vy + v->vh) / (float)h);

    if (sunOn) {
        float s = g_on ? (float)g_strength / 100.0f * fade : 0.0f;
        g_lastFade = fade;
        rgb[0] = tint[0] * s; rgb[1] = tint[1] * s; rgb[2] = tint[2] * s;
        light_pass(0, uv, rgb, (float)g_threshold / 100.0f, (float)g_radius / 100.0f, n);
        lights++;
    }
    for (m = 0; wantMoons && m < 2; m++) {
        float s;
        if (!moon_on_screen(v, w, h, &ms, m, uv, &fade)) continue;
        s = g_on ? (float)g_moon / 100.0f * fade : 0.0f;
        g_moonFade[m] = fade;
        rgb[0] = rgb[1] = rgb[2] = s;
        light_pass(1, uv, rgb, 0.0f, (float)g_moonRadius / 100.0f, n);
        lights++;
    }

    fUseProgram(0);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!lights) return 0;
    g_framesDrawn++;
    k[0] = k[1] = k[2] = g_on ? 1.0f : 0.0f;
    k[3] = (float)g_view;
    return g_view == 1 || g_view >= 3 ? g_tex[0] : g_tex[1];
}

static int num(const char *v, const char *word, int *dst)
{
    size_t n = strlen(word);
    if (strncmp(v, word, n)) return 0;
    v += n;
    while (*v == ' ') v++;
    if ((*v < '0' || *v > '9') && !(*v == '-' && v[1] >= '0' && v[1] <= '9')) return 0;
    *dst = atoi(v);
    return 1;
}

void u9gl_ui_report(void (*out)(const char *fmt, ...));

void u9rays_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    int m;
    while (*v == ' ') v++;
    if (!strcmp(v, "runs")) { u9gl_ui_report(out); return; }
    if (!strcmp(v, "on")) g_on = 1;
    else if (!strcmp(v, "off")) g_on = 0;
    else if (!strcmp(v, "view")) g_view = (g_view + 1) % 5;
    else if (!strcmp(v, "glare on")) g_glareOn = 1;
    else if (!strcmp(v, "glare off")) g_glareOn = 0;
    else if (num(v, "glare ", &g_glare)) g_glareOn = 1;
    else if (num(v, "strength ", &g_strength) || num(v, "decay ", &g_decay) || num(v, "length ", &g_length) ||
             num(v, "samples ", &g_samples) || num(v, "threshold ", &g_threshold) || num(v, "radius ", &g_radius) ||
             num(v, "air ", &g_air) || num(v, "view ", &g_view) || num(v, "moon radius ", &g_moonRadius) || num(v, "moon ", &g_moon)) {
        if (g_view < 0 || g_view > 4) g_view = 0;
    } else if (*v) {
        out("rays | rays on|off | view [1-4] (mask, blur, interface, sky copy; 0 off) | strength <percent> | decay <percent> | length <percent>"
            " | samples <n> | threshold <percent> | radius <percent of the screen's height> | air <units, 0 none>"
            " | moon <percent, 0 off> | moon radius <percent of the screen's height> | glare on|off|<percent> (the engine's own)"
            " | runs (the draws at the reticle)");
        return;
    }
    if (*v) u9log_line("RAYS '%s' - %s, strength %d%%, decay %d%%, length %d%%, samples %d, threshold %d%%, radius %d%%, air %d,"
                       " moon %d%%, moon radius %d%%, glare %s %d%%", v, g_on ? "on" : "off", g_strength, g_decay, g_length, g_samples,
                       g_threshold, g_radius, g_air, g_moon, g_moonRadius, g_glareOn ? "on" : "off", g_glare);
    out("sun rays %s%s%s: strength %d%%, decay %d%%, length %d%%, %d samples, threshold %d%%, radius %d%%, air %d",
        g_on ? "on" : "off (rays on)", g_failed ? " - FAILED, see the log" : !u9glpost_running() ? " - needs the post pass" : "",
        g_view == 1 ? ", showing the last light's mask" : g_view == 2 ? ", showing the blur" : g_view == 3 ? ", showing the interface in red" : g_view == 4 ? ", showing the sky copy" : "",
        g_strength, g_decay, g_length, g_samples, g_threshold, g_radius, g_air);
    out("moon rays %d%%, radius %d%% (from the bloom source)", g_moon, g_moonRadius);
    out("the engine's glare %s, %d%% of its alpha%s", g_glareOn ? "on" : "off (rays glare on)", g_glare,
        g_glareOn && u9rays_glare() != g_glare ? " - held to 0..400" : "");
    out("last frame: sun %s; at %.2f,%.2f on screen (0..1 is on it), %.1f deg up, fade %.2f; drawn in %u of %u frames",
        g_lastWhy, (double)g_lastUv[0], (double)g_lastUv[1], (double)g_lastElev, (double)g_lastFade,
        g_framesDrawn, g_frames);
    for (m = 0; m < 2; m++)
        out("  %s moon %s; at %.2f,%.2f, %.1f deg up, fade %.2f", m ? "orange" : "cyan", g_moonWhy[m],
            (double)g_moonUv[m][0], (double)g_moonUv[m][1], (double)g_moonElev[m], (double)g_moonFade[m]);
}

void u9rays_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_targets();
        if (g_mask) fDeleteProgram(g_mask);
        if (g_blur) fDeleteProgram(g_blur);
    }
    g_fbo[0] = g_fbo[1] = g_tex[0] = g_tex[1] = 0;
    g_mask = g_blur = 0;
    g_w = g_h = 0;
    g_loaded = 0;
    g_failed = 0;
}
