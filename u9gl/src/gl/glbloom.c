#include <windows.h>
#include <GL/gl.h>
#include "../diag/log.h"
#include "metrics.h"
#include "glbloom.h"

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
static ActiveTextureFn          fActiveTexture;

static int    g_loaded, g_failed;
static GLuint g_down, g_up;
static GLint  g_downTex, g_downTexel, g_upTex, g_upTexel;
static GLuint g_tex[U9BLOOM_LEVELS + 1], g_fbo[U9BLOOM_LEVELS + 1];
static int    g_lw[U9BLOOM_LEVELS + 1], g_lh[U9BLOOM_LEVELS + 1], g_levels;
static int    g_skip;
static int    g_w, g_h;

static const char *const k_vs =
    "#version 120\n"
    "void main() {\n"
    "    gl_Position    = gl_Vertex;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "}\n";

static const char *const k_down =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st, h = u_texel;\n"
    "    vec4 s = texture2D(u_tex, uv) * 4.0;\n"
    "    s += texture2D(u_tex, uv - h);\n"
    "    s += texture2D(u_tex, uv + h);\n"
    "    s += texture2D(u_tex, uv + vec2(h.x, -h.y));\n"
    "    s += texture2D(u_tex, uv - vec2(h.x, -h.y));\n"
    "    gl_FragColor = s / 8.0;\n"
    "}\n";

static const char *const k_up =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "void main() {\n"
    "    vec2 uv = gl_TexCoord[0].st, h = u_texel * 0.5;\n"
    "    vec4 s = texture2D(u_tex, uv + vec2(-2.0 * h.x, 0.0));\n"
    "    s += texture2D(u_tex, uv + vec2(-h.x, h.y)) * 2.0;\n"
    "    s += texture2D(u_tex, uv + vec2(0.0, 2.0 * h.y));\n"
    "    s += texture2D(u_tex, uv + vec2(h.x, h.y)) * 2.0;\n"
    "    s += texture2D(u_tex, uv + vec2(2.0 * h.x, 0.0));\n"
    "    s += texture2D(u_tex, uv + vec2(h.x, -h.y)) * 2.0;\n"
    "    s += texture2D(u_tex, uv + vec2(0.0, -2.0 * h.y));\n"
    "    s += texture2D(u_tex, uv + vec2(-h.x, -h.y)) * 2.0;\n"
    "    gl_FragColor = s / 12.0;\n"
    "}\n";

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static void fail(const char *why)
{
    if (!g_failed) u9log_line("GLBLOOM off for this session: %s", why);
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
        u9log_line("GLBLOOM %s shader did not compile: %s", what, log);
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
        u9log_line("GLBLOOM %s program did not link: %s", what, log);
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
    fActiveTexture          = (ActiveTextureFn)gl_proc("glActiveTexture");
    if (!fGenFramebuffers || !fDeleteFramebuffers || !fBindFramebuffer || !fFramebufferTexture2D ||
        !fCheckFramebufferStatus || !fCreateShader || !fShaderSource || !fCompileShader ||
        !fGetShaderiv || !fGetProgramiv || !fGetShaderInfoLog || !fGetProgramInfoLog ||
        !fCreateProgram || !fAttachShader || !fLinkProgram || !fUseProgram || !fDeleteProgram ||
        !fDeleteShader || !fGetUniformLocation || !fUniform1i || !fUniform2f || !fActiveTexture) {
        fail("missing GL entry points");
        return 0;
    }
    g_down = build_program(k_down, "down");
    g_up   = g_down ? build_program(k_up, "up") : 0;
    if (!g_down || !g_up) { fail("the blur shaders did not build"); return 0; }
    g_downTex   = fGetUniformLocation(g_down, "u_tex");
    g_downTexel = fGetUniformLocation(g_down, "u_texel");
    g_upTex     = fGetUniformLocation(g_up, "u_tex");
    g_upTexel   = fGetUniformLocation(g_up, "u_texel");
    u9log_line("GLBLOOM blur chain ready: %d levels, half size down", U9BLOOM_LEVELS);
    return 1;
}

static void delete_levels(void)
{
    int i;
    for (i = 0; i < g_levels; i++) {
        if (g_fbo[i]) { fDeleteFramebuffers(1, &g_fbo[i]); g_fbo[i] = 0; }
        if (g_tex[i]) {
            glDeleteTextures(1, &g_tex[i]);
            u9metrics_tex_destroyed((unsigned)(g_lw[i] * g_lh[i] * 8));
            g_tex[i] = 0;
        }
    }
    g_levels = 0;
    g_w = g_h = 0;
    g_skip = 0;
}

static int make_levels(int w, int h, int skip)
{
    int i, lw = w, lh = h, n = U9BLOOM_LEVELS + skip;
    delete_levels();
    glPushAttrib(GL_TEXTURE_BIT);
    for (i = 0; i < n; i++) {
        GLenum status;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        g_lw[i] = lw; g_lh[i] = lh;
        glGenTextures(1, &g_tex[i]);
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, lw, lh, 0, GL_RGBA, GL_FLOAT, NULL);
        u9metrics_tex_created((unsigned)(lw * lh * 8));
        fGenFramebuffers(1, &g_fbo[i]);
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        fFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex[i], 0);
        status = fCheckFramebufferStatus(GL_FRAMEBUFFER);
        fBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_levels = i + 1;
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glPopAttrib();
            u9log_line("GLBLOOM level %d (%dx%d) incomplete: status 0x%04X", i, lw, lh, (unsigned)status);
            delete_levels();
            fail("a blur level was incomplete");
            return 0;
        }
    }
    glPopAttrib();
    g_w = w; g_h = h;
    g_skip = skip;
    u9log_line("GLBLOOM levels for %dx%d: %dx%d down to %dx%d, RGBA16F%s", w, h, g_lw[skip], g_lh[skip],
               g_lw[n - 1], g_lh[n - 1], skip ? " (supersampled: one halving first)" : "");
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

unsigned u9bloom_run(unsigned src, int w, int h, int scale)
{
    int i, skip = scale > 1 ? 1 : 0, n = U9BLOOM_LEVELS + skip;
    if (g_failed || !src || w < 2 || h < 2 || !load()) return 0;
    if ((w != g_w || h != g_h || skip != g_skip) && !make_levels(w, h, skip)) return 0;
    fActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    fUseProgram(g_down);
    fUniform1i(g_downTex, 0);
    for (i = 0; i < n; i++) {
        int sw = i ? g_lw[i - 1] : w, sh = i ? g_lh[i - 1] : h;
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        glViewport(0, 0, g_lw[i], g_lh[i]);
        glBindTexture(GL_TEXTURE_2D, i ? g_tex[i - 1] : (GLuint)src);
        fUniform2f(g_downTexel, 1.0f / (float)sw, 1.0f / (float)sh);
        quad();
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    fUseProgram(g_up);
    fUniform1i(g_upTex, 0);
    for (i = n - 2; i >= skip; i--) {
        fBindFramebuffer(GL_FRAMEBUFFER, g_fbo[i]);
        glViewport(0, 0, g_lw[i], g_lh[i]);
        glBindTexture(GL_TEXTURE_2D, g_tex[i + 1]);
        fUniform2f(g_upTexel, 1.0f / (float)g_lw[i + 1], 1.0f / (float)g_lh[i + 1]);
        quad();
    }
    glDisable(GL_BLEND);
    fUseProgram(0);
    fBindFramebuffer(GL_FRAMEBUFFER, 0);
    return g_tex[skip];
}

void u9bloom_release(void)
{
    if (g_loaded && !g_failed && wglGetCurrentContext()) {
        delete_levels();
        if (g_down) fDeleteProgram(g_down);
        if (g_up) fDeleteProgram(g_up);
    }
    {
        int i;
        for (i = 0; i <= U9BLOOM_LEVELS; i++) g_tex[i] = g_fbo[i] = 0;
    }
    g_down = g_up = 0;
    g_levels = 0;
    g_w = g_h = 0;
    g_loaded = 0;
    g_failed = 0;
}
