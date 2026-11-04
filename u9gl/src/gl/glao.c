#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "metrics.h"
#include "glao.h"

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

#define AO_MAX_SAMPLES 16

static int    g_on = 1;
static int    g_radius = 60;
static int    g_strength = 300;
static int    g_distance = 3000;
static int    g_samples = 12;
static int    g_blur = 1;
static int    g_view;

#define CONTACT_MAX_STEPS 24

static int    g_cOn = 1;
static int    g_cLength = 40;
static int    g_cSteps = 12;
static int    g_cThick = 30;
static int    g_cDistance = 2000;
static unsigned g_cFrames;
static const char *g_cLastWhy = "not run yet";

static int    g_loaded, g_failed;
static GLuint g_prog, g_blurProg;
static GLint  g_aExact, g_aSurf, g_aVp, g_aEngine, g_aProj, g_aK, g_aC, g_aCL;
static GLint  g_bAo, g_bDir;
static GLuint g_tex[2], g_fbo[2];
static int    g_w, g_h;
static unsigned g_frames, g_framesDrawn;
static const char *g_lastWhy = "not run yet";

static const char *const k_vs =
    "#version 120\n"
    "void main() {\n"
    "    gl_Position    = gl_Vertex;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "}\n";

static const char *const k_ao =
    "#version 120\n"
    "uniform sampler2D u_exact;\n"
    "uniform vec2 u_surf;\n"
    "uniform vec4 u_vp;\n"
    "uniform vec2 u_engine;\n"
    "uniform vec4 u_proj;\n"
    "uniform vec4 u_k;\n"
    "uniform vec4 u_c;\n"
    "uniform vec3 u_cL;\n"
    "vec3 cs_at(vec2 q, float e) {\n"
    "    vec2 p = q * u_surf - u_vp.xy;\n"
    "    float ex = p.x * u_engine.x / u_vp.z;\n"
    "    float ey = (u_vp.w - p.y) * u_engine.y / u_vp.w;\n"
    "    return vec3((ex - u_proj.z) * e / u_proj.x, e, (u_proj.w - ey) * e / u_proj.y);\n"
    "}\n"
    "vec2 px_of(vec3 Q) {\n"
    "    float ex = u_proj.z + Q.x * u_proj.x / Q.y;\n"
    "    float ey = u_proj.w - Q.z * u_proj.y / Q.y;\n"
    "    return vec2(ex * u_vp.z / u_engine.x, u_vp.w - ey * u_vp.w / u_engine.y);\n"
    "}\n"
    "float contact(vec3 P, vec3 N, float e, float ign) {\n"
    "    if (u_c.z < 1.0 || e >= u_c.y) return 1.0;\n"
    "    if (dot(N, u_cL) <= 0.02) return 1.0;\n"
    "    vec3 S = P + N * (1.0 + 0.002 * e);\n"
    "    float hit = 0.0;\n"
    "    for (int i = 0; i < 24; i++) {\n"
    "        if (float(i) >= u_c.z) break;\n"
    "        float t = u_c.x * (float(i) + ign) / u_c.z;\n"
    "        vec3 Q = S + u_cL * t;\n"
    "        if (Q.y <= 16.0) break;\n"
    "        vec2 p = px_of(Q);\n"
    "        if (p.x < 0.0 || p.y < 0.0 || p.x >= u_vp.z || p.y >= u_vp.w) break;\n"
    "        float eq = texture2D(u_exact, (p + u_vp.xy) / u_surf).r;\n"
    "        if (eq <= 16.0) continue;\n"
    "        float dz = Q.y - eq;\n"
    "        if (dz > 1.0 + 0.004 * Q.y && dz < u_c.w) { hit = 1.0 - smoothstep(0.6, 1.0, t / u_c.x); break; }\n"
    "    }\n"
    "    return mix(1.0 - hit, 1.0, smoothstep(0.6 * u_c.y, u_c.y, e));\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = (floor(gl_TexCoord[0].st * u_surf) + 0.5) / u_surf;\n"
    "    vec2 p = uv * u_surf - u_vp.xy;\n"
    "    float e = texture2D(u_exact, uv).r;\n"
    "    float far = max(u_k.w >= 1.0 ? u_k.y : 0.0, u_c.z >= 1.0 ? u_c.y : 0.0);\n"
    "    if (e <= 16.0 || p.x < 0.0 || p.y < 0.0 || p.x >= u_vp.z || p.y >= u_vp.w) {\n"
    "        gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); return;\n"
    "    }\n"
    "    if (e >= far) { gl_FragColor = vec4(1.0, e, 1.0, 1.0); return; }\n"
    "    vec3 P = cs_at(uv, e);\n"
    "    vec2 tx = vec2(1.0 / u_surf.x, 0.0), ty = vec2(0.0, 1.0 / u_surf.y);\n"
    "    float eR = texture2D(u_exact, uv + tx).r, eL = texture2D(u_exact, uv - tx).r;\n"
    "    float eU = texture2D(u_exact, uv + ty).r, eD = texture2D(u_exact, uv - ty).r;\n"
    "    if ((eR <= 0.0 && eL <= 0.0) || (eU <= 0.0 && eD <= 0.0)) { gl_FragColor = vec4(1.0, e, 1.0, 1.0); return; }\n"
    "    vec3 R = cs_at(uv + tx, eR) - P, L = P - cs_at(uv - tx, eL);\n"
    "    vec3 U = cs_at(uv + ty, eU) - P, D = P - cs_at(uv - ty, eD);\n"
    "    vec3 sx = eR <= 0.0 ? L : eL <= 0.0 ? R : dot(R, R) < dot(L, L) ? R : L;\n"
    "    vec3 sy = eU <= 0.0 ? D : eD <= 0.0 ? U : dot(U, U) < dot(D, D) ? U : D;\n"
    "    vec3 N = normalize(cross(sx, sy));\n"
    "    if (dot(N, P) > 0.0) N = -N;\n"
    "    float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "    float ao = 1.0;\n"
    "    float rad = min(u_k.x * u_proj.x / e * u_vp.z / u_engine.x, u_vp.w * 0.2);\n"
    "    if (u_k.w >= 1.0 && e < u_k.y && rad >= 1.0) {\n"
    "        float ang = 6.2831853 * ign;\n"
    "        float occ = 0.0, R2 = u_k.x * u_k.x;\n"
    "        for (int i = 0; i < 16; i++) {\n"
    "            if (float(i) >= u_k.w) break;\n"
    "            float a = ang + float(i) * 2.3999632;\n"
    "            vec2 q = uv + vec2(cos(a), sin(a)) * rad * sqrt((float(i) + 0.5) / u_k.w) / u_surf;\n"
    "            float eq = texture2D(u_exact, q).r;\n"
    "            if (eq <= 16.0) continue;\n"
    "            vec3 s = cs_at(q, eq) - P;\n"
    "            float ss = dot(s, s);\n"
    "            if (ss >= R2) continue;\n"
    "            occ += max(dot(s, N) * inversesqrt(ss + 1e-4) - 0.1, 0.0) * (1.0 - ss / R2);\n"
    "        }\n"
    "        ao = 1.0 - clamp(2.0 * occ / u_k.w * u_k.z, 0.0, 1.0);\n"
    "        ao = mix(ao, 1.0, smoothstep(0.6 * u_k.y, u_k.y, e));\n"
    "    }\n"
    "    gl_FragColor = vec4(ao, e, contact(P, N, e, ign), 1.0);\n"
    "}\n";

static const char *const k_blur =
    "#version 120\n"
    "uniform sampler2D u_ao;\n"
    "uniform vec2 u_dir;\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st;\n"
    "    vec3 c = texture2D(u_ao, uv).rgb;\n"
    "    if (c.g <= 0.0) { gl_FragColor = vec4(c, 1.0); return; }\n"
    "    vec2 sum = c.rb;\n"
    "    float ws = 1.0, tol = 0.03 * c.g + 2.0;\n"
    "    for (int i = -4; i <= 4; i++) {\n"
    "        if (i == 0) continue;\n"
    "        vec3 s = texture2D(u_ao, uv + u_dir * float(i)).rgb;\n"
    "        float w = exp(-float(i * i) / 8.0) * max(0.0, 1.0 - abs(s.g - c.g) / tol);\n"
    "        sum += s.rb * w;\n"
    "        ws += w;\n"
    "    }\n"
    "    gl_FragColor = vec4(sum.x / ws, c.g, sum.y / ws, 1.0);\n"
    "}\n";

void u9ao_init(void)
{
    g_on       = u9cfg_int("gl_ao", 1);
    g_radius   = u9cfg_int("gl_ao_radius", 60);
    g_strength = u9cfg_int("gl_ao_strength", 300);
    g_distance = u9cfg_int("gl_ao_distance", 3000);
    g_samples  = u9cfg_int("gl_ao_samples", 12);
    g_cOn       = u9cfg_int("gl_contact", 1);
    g_cLength   = u9cfg_int("gl_contact_length", 40);
    g_cSteps    = u9cfg_int("gl_contact_steps", 12);
    g_cThick    = u9cfg_int("gl_contact_thickness", 30);
    g_cDistance = u9cfg_int("gl_contact_distance", 2000);
}

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLAO off for this session: %s", why);
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
        u9log_line("GLAO %s shader did not compile: %s", what, log);
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
        u9log_line("GLAO %s program did not link: %s", what, log);
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
    g_prog     = build_program(k_ao, "estimate");
    g_blurProg = g_prog ? build_program(k_blur, "blur") : 0;
    if (!g_prog || !g_blurProg) { fail("the AO shaders did not build"); return 0; }
    g_aExact  = fGetUniformLocation(g_prog, "u_exact");
    g_aSurf   = fGetUniformLocation(g_prog, "u_surf");
    g_aVp     = fGetUniformLocation(g_prog, "u_vp");
    g_aEngine = fGetUniformLocation(g_prog, "u_engine");
    g_aProj   = fGetUniformLocation(g_prog, "u_proj");
    g_aK      = fGetUniformLocation(g_prog, "u_k");
    g_aC      = fGetUniformLocation(g_prog, "u_c");
    g_aCL     = fGetUniformLocation(g_prog, "u_cL");
    g_bAo     = fGetUniformLocation(g_blurProg, "u_ao");
    g_bDir    = fGetUniformLocation(g_blurProg, "u_dir");
    u9log_line("GLAO ready: radius %d, strength %d%%, to %d from the eye, %d samples; contact shadows %s, %d long"
               " in %d steps, %d thick, to %d", g_radius, g_strength, g_distance, g_samples, g_cOn ? "on" : "off",
               g_cLength, g_cSteps, g_cThick, g_cDistance);
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
    int i;
    delete_targets();
    glPushAttrib(GL_TEXTURE_BIT);
    for (i = 0; i < 2; i++) {
        GLenum status;
        glGenTextures(1, &g_tex[i]);
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
            u9log_line("GLAO target %dx%d incomplete: status 0x%04X", w, h, (unsigned)status);
            delete_targets();
            fail("a target was incomplete");
            return 0;
        }
    }
    glPopAttrib();
    u9log_line("GLAO targets %dx%d, RGBA16F", w, h);
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

unsigned u9ao_build(const U9FrameView *v, unsigned exactTex, int w, int h, int scale, const float *castDir,
                    float k[4])
{
    int aw, ah, n, cn = 0;
    float cl[3] = { 0.0f, 0.0f, 0.0f };
    k[0] = k[1] = k[2] = k[3] = 0.0f;
    g_cLastWhy = !g_cOn ? "off (post contact on)" : !castDir ? "no sun or moon casting" : "on";
    if (!g_on && !(g_cOn && castDir)) { g_lastWhy = "off (post ao on)"; return 0; }
    if (!exactTex) { g_lastWhy = g_cLastWhy = "no exact depth target"; return 0; }
    if (g_failed || !v || !v->valid || w < 4 || h < 4 || !load()) {
        g_lastWhy = g_cLastWhy = g_failed ? "FAILED, see the log" : "no frame view";
        return 0;
    }
    if (v->depthMode != 2) { g_lastWhy = g_cLastWhy = "not the W-buffer"; return 0; }
    if (g_cOn && castDir) {
        int i;
        float len;
        for (i = 0; i < 3; i++) cl[i] = v->m[i] * castDir[0] + v->m[4 + i] * castDir[1] + v->m[8 + i] * castDir[2];
        len = sqrtf(cl[0] * cl[0] + cl[1] * cl[1] + cl[2] * cl[2]);
        if (len > 1e-6f) {
            cl[0] /= len; cl[1] /= len; cl[2] /= len;
            cn = g_cSteps < 1 ? 1 : g_cSteps > CONTACT_MAX_STEPS ? CONTACT_MAX_STEPS : g_cSteps;
        } else {
            g_cLastWhy = "no direction";
        }
    }
    if (g_on) g_frames++;
    if (cn) g_cFrames++;
    if (scale < 1) scale = 1;
    aw = w / (2 * scale); ah = h / (2 * scale);
    if (aw < 2) aw = 2;
    if (ah < 2) ah = 2;
    if ((aw != g_w || ah != g_h) && !make_targets(aw, ah)) return 0;
    n = !g_on ? 0 : g_samples < 1 ? 1 : g_samples > AO_MAX_SAMPLES ? AO_MAX_SAMPLES : g_samples;

    glDisable(GL_BLEND);
    glViewport(0, 0, aw, ah);
    fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[0]);
    fUseProgram(g_prog);
    fActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)exactTex);
    fUniform1i(g_aExact, 0);
    fUniform2f(g_aSurf, (float)w, (float)h);
    fUniform4f(g_aVp, (float)v->vx, (float)v->vy, (float)v->vw, (float)v->vh);
    fUniform2f(g_aEngine, v->engineW, v->engineH);
    fUniform4f(g_aProj, v->sx, v->sy, v->cx, v->cy);
    fUniform4f(g_aK, (float)(g_radius > 1 ? g_radius : 1), (float)(g_distance > 100 ? g_distance : 100),
               (float)(g_strength > 0 ? g_strength : 0) / 100.0f, (float)n);
    fUniform4f(g_aC, (float)(g_cLength > 1 ? g_cLength : 1), (float)(g_cDistance > 100 ? g_cDistance : 100),
               (float)cn, (float)(g_cThick > 2 ? g_cThick : 2));
    fUniform3f(g_aCL, cl[0], cl[1], cl[2]);
    quad();

    if (g_blur) {
        fUseProgram(g_blurProg);
        fUniform1i(g_bAo, 0);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[1]);
        glBindTexture(GL_TEXTURE_2D, g_tex[0]);
        fUniform2f(g_bDir, 1.0f / (float)aw, 0.0f);
        quad();
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[0]);
        glBindTexture(GL_TEXTURE_2D, g_tex[1]);
        fUniform2f(g_bDir, 0.0f, 1.0f / (float)ah);
        quad();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    fUseProgram(0);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (g_on) g_framesDrawn++;
    g_lastWhy = g_on ? "on" : "off (post ao on)";
    k[0] = 1.0f;
    k[1] = (float)g_view;
    k[2] = 1.0f / (float)aw;
    k[3] = 1.0f / (float)ah;
    return g_tex[0];
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

void u9ao_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strcmp(v, "on")) g_on = 1;
    else if (!strcmp(v, "off")) g_on = 0;
    else if (!strcmp(v, "view")) g_view = g_view == 1 ? 0 : 1;
    else if (!strcmp(v, "blur on")) g_blur = 1;
    else if (!strcmp(v, "blur off")) g_blur = 0;
    else if (num(v, "radius ", &g_radius) || num(v, "strength ", &g_strength) || num(v, "distance ", &g_distance) ||
             num(v, "samples ", &g_samples)) {
    } else if (*v) {
        out("post ao | on|off | view (the AO alone) | radius <units> | strength <percent> | distance <units from the eye>"
            " | samples <1-%d> | blur on|off", AO_MAX_SAMPLES);
        return;
    }
    if (*v) u9log_line("GLAO '%s' - %s, radius %d, strength %d%%, distance %d, %d samples, blur %s, view %s", v,
                       g_on ? "on" : "off", g_radius, g_strength, g_distance, g_samples, g_blur ? "on" : "off",
                       g_view ? "on" : "off");
    out("ambient occlusion %s%s%s: radius %d, strength %d%%, to %d from the eye, %d samples, blur %s",
        g_on ? "on" : "off (post ao on)", g_failed ? " - FAILED, see the log" : !u9glpost_running() ? " - needs the post pass" : "",
        g_view ? ", showing the AO alone" : "", g_radius, g_strength, g_distance, g_samples, g_blur ? "on" : "off");
    out("last frame: %s; drawn in %u of %u frames, at %dx%d", g_lastWhy, g_framesDrawn, g_frames, g_w, g_h);
}

void u9contact_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    while (*v == ' ') v++;
    if (!strcmp(v, "on")) g_cOn = 1;
    else if (!strcmp(v, "off")) g_cOn = 0;
    else if (!strcmp(v, "view")) g_view = g_view == 2 ? 0 : 2;
    else if (num(v, "length ", &g_cLength) || num(v, "steps ", &g_cSteps) || num(v, "thickness ", &g_cThick) ||
             num(v, "distance ", &g_cDistance)) {
    } else if (*v) {
        out("post contact | on|off | view (the contact shadows alone) | length <units toward the sun> | steps <1-%d>"
            " | thickness <units> | distance <units from the eye>", CONTACT_MAX_STEPS);
        return;
    }
    if (*v) u9log_line("GLAO contact '%s' - %s, length %d, %d steps, thickness %d, distance %d, view %s", v,
                       g_cOn ? "on" : "off", g_cLength, g_cSteps, g_cThick, g_cDistance, g_view == 2 ? "on" : "off");
    out("contact shadows %s%s%s: %d toward the sun in %d steps, %d thick, to %d from the eye",
        g_cOn ? "on" : "off (post contact on)",
        g_failed ? " - FAILED, see the log" : !u9glpost_running() ? " - needs the post pass" : "",
        g_view == 2 ? ", showing them alone" : "", g_cLength, g_cSteps, g_cThick, g_cDistance);
    out("last frame: %s; drawn in %u frames; they need the sun shadows (gl_sun_shadows) and blur with the AO's",
        g_cLastWhy, g_cFrames);
}

void u9ao_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_targets();
        if (g_prog) fDeleteProgram(g_prog);
        if (g_blurProg) fDeleteProgram(g_blurProg);
    }
    g_fbo[0] = g_fbo[1] = g_tex[0] = g_tex[1] = 0;
    g_prog = g_blurProg = 0;
    g_w = g_h = 0;
    g_loaded = 0;
    g_failed = 0;
}
