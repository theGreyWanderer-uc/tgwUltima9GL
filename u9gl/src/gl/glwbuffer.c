#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include "../diag/log.h"
#include "glpost.h"

float u9glpost_effect_lod_bias(int effect);
int   u9glpost_depth_glide(void);
int   u9glpost_depth_slack(void);
int   u9gl_effect_run_now(void);

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

typedef GLuint (APIENTRY *CreateShaderFn)(GLenum);
typedef void   (APIENTRY *ShaderSourceFn)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void   (APIENTRY *CompileShaderFn)(GLuint);
typedef void   (APIENTRY *GetivFn)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *GetInfoLogFn)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *CreateProgramFn)(void);
typedef void   (APIENTRY *AttachShaderFn)(GLuint, GLuint);
typedef void   (APIENTRY *LinkProgramFn)(GLuint);
typedef void   (APIENTRY *UseProgramFn)(GLuint);
typedef GLint  (APIENTRY *GetUniformLocationFn)(GLuint, const char *);
typedef void   (APIENTRY *Uniform1iFn)(GLint, GLint);
typedef void   (APIENTRY *ActiveTextureFn)(GLenum);
typedef void   (APIENTRY *Uniform3fFn)(GLint, GLfloat, GLfloat, GLfloat);
static Uniform3fFn fUniform3f;
typedef void   (APIENTRY *Uniform2fFn)(GLint, GLfloat, GLfloat);
static Uniform2fFn fUniform2f;
static ActiveTextureFn fActiveTexture;
typedef void   (APIENTRY *Uniform1fFn)(GLint, GLfloat);

static CreateShaderFn       fCreateShader;
static ShaderSourceFn       fShaderSource;
static CompileShaderFn      fCompileShader;
static GetivFn              fGetShaderiv, fGetProgramiv;
static GetInfoLogFn         fGetShaderInfoLog, fGetProgramInfoLog;
static CreateProgramFn      fCreateProgram;
static AttachShaderFn       fAttachShader;
static LinkProgramFn        fLinkProgram;
static UseProgramFn         fUseProgram;
static GetUniformLocationFn fGetUniformLocation;
static Uniform1iFn          fUniform1i;
static Uniform1fFn          fUniform1f;

static int    g_tried;
static GLuint g_prog;
static GLint  g_uTex, g_uTextured, g_uAlphaTest, g_uAlphaRef, g_uFog,
              g_uZRef, g_uZFar, g_uLightable, g_uGlow, g_uBloom, g_uFade, g_uLodBias, g_uAlphaScale,
              g_uRgbScale, g_uSelfLit, g_uUi, g_uMark,
              g_uDepthGlide, g_uSlack, g_uRune, g_uRuneTex, g_uRuneTint;
static int    g_lastTextured = -1, g_lastAlphaTest = -1, g_lastLightable = -1, g_lastGlow = -1;
static float  g_lastBloom = -1.0f, g_lastAlphaScale = -1.0f, g_lastRgbScale = -1.0f;
static int    g_lastSelfLit = -1, g_lastUi = -1, g_lastSlack = -1;
static float  g_lastFade = -1.0f, g_lastLodBias = -1.0f;
static int    g_lastRune = -1;
static float  g_lastAlphaRef = -1.0f;
static GLint  g_uJitter = -1;
static float  g_jit[2];
static int    g_lastJit = -1;

static const char *const k_vertex =
    "#version 120\n"
    "uniform vec2 u_jitter;\n"
    "void main() {\n"
    "    gl_Position    = ftransform();\n"
    "    gl_Position.xy += u_jitter * gl_Position.w;\n"
    "    gl_FrontColor  = gl_Color;\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "    gl_FogFragCoord = gl_FogCoord;\n"
    "}\n";

static const char *const k_fragment =
    "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "uniform int   u_textured;\n"
    "uniform int   u_alphaTest;\n"
    "uniform float u_alphaRef;\n"
    "uniform int   u_fog;\n"
    "uniform float u_zRef;\n"
    "uniform float u_zFar;\n"
    "uniform int   u_depthGlide;\n"
    "uniform float u_slack;\n"
    "uniform int   u_rune;\n"
    "uniform sampler2D u_runeTex;\n"
    "uniform vec3  u_runeTint;\n"
    "uniform int   u_lightable;\n"
    "uniform int   u_glow;\n"
    "uniform float u_bloom;\n"
    "uniform float u_alphaScale;\n"
    "uniform float u_rgbScale;\n"
    "uniform int   u_selfLit;\n"
    "uniform int   u_ui;\n"
    "uniform int   u_mark;\n"
    "uniform float u_fade;\n"
    "uniform float u_lodBias;\n"
    "void main() {\n"
    "    vec4 c = gl_Color;\n"
    "    vec4 t = vec4(gl_Color.rgb, 1.0);\n"
    "    if (u_textured != 0) { t = texture2DProj(u_tex, gl_TexCoord[0], u_lodBias); c *= t; }\n"
    "    if (u_selfLit != 0 && u_textured != 0) c.rgb = t.rgb;\n"
    "    if (u_alphaTest != 0 && c.a <= u_alphaRef) discard;\n"
    "    if (u_fade != 0.0 && u_textured != 0) {\n"
    "        vec2 st = gl_TexCoord[0].st / gl_TexCoord[0].q;\n"
    "        float wf = abs(u_fade);\n"
    "        if (st.x >= -0.01 && st.y >= -0.01 && st.x <= 1.01 && st.y <= 1.01) {\n"
    "            vec2 e = min(st, 1.0 - st);\n"
    "            float f = smoothstep(0.0, wf, min(e.x, e.y));\n"
    "            if (u_fade < 0.0) c = vec4(mix(vec3(1.0, 0.0, 1.0), c.rgb, f), max(c.a, 0.6));\n"
    "            else c.a *= f;\n"
    "        } else if (u_fade < 0.0) c = vec4(0.0, 1.0, 1.0, max(c.a, 0.6));\n"
    "    }\n"
    "    c.rgb *= u_rgbScale;\n"
    "    c.a *= u_alphaScale;\n"
    "    if (u_fog != 0) {\n"
    "        float f = clamp(1.0 - gl_FogFragCoord, 0.0, 1.0);\n"
    "        c.rgb = mix(gl_Fog.color.rgb, c.rgb, f);\n"
    "    }\n"
    "    float w = 1.0 / gl_TexCoord[0].q;\n"
    "    float wx = w;\n"
    "    if (u_depthGlide != 0) {\n"
    "        float st = exp2(floor(log2(max(w, 1.0))) - 12.0);\n"
    "        w = floor(w / st + 0.5) * st;\n"
    "        w -= u_slack * st;\n"
    "    }\n"
    "    gl_FragDepth = clamp((w - u_zRef) / (u_zFar - u_zRef), 0.0, 1.0);\n"
    "    gl_FragData[0] = c;\n"
    "    gl_FragData[1] = (u_lightable != 0) ? vec4(t.rgb, c.a) : vec4(0.0, 0.0, 0.0, c.a);\n"
    "    gl_FragData[2] = (u_glow != 0) ? c : vec4(0.0, 0.0, 0.0, c.a);\n"
    "    float rm = (u_rune != 0) ? texture2DProj(u_runeTex, gl_TexCoord[0]).a : 1.0;\n"
    "    vec3 bc = (u_rune != 0) ? dot(c.rgb, vec3(0.299, 0.587, 0.114)) * u_runeTint : c.rgb;\n"
    "    gl_FragData[3] = (u_bloom > 0.0) ? vec4(bc * u_bloom * rm, c.a) : vec4(0.0, 0.0, 0.0, c.a);\n"
    "    float ua = step(0.1, c.a);\n"
    "    gl_FragData[5] = (u_lightable != 0 && gl_FragDepth < 1.0) ? vec4(0.0, 0.0, 0.0, c.a) : c;\n"
    "    gl_FragData[4] = (u_ui != 0) ? vec4(-1.0e6 * float(u_ui) * ua, 0.0, 0.0, ua)\n"
    "                   : vec4(wx, 0.0, 0.0, 1.0);\n"
    "    if (u_mark == 1) gl_FragData[0] = vec4(1.0);\n"
    "    else if (u_mark == 2) gl_FragData[0] = vec4(0.0, 0.0, 0.0, c.a);\n"
    "}\n";

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    if (p >= -1 && p <= 3) return NULL;
    return (void *)p;
}

static int load_entry_points(void)
{
    fCreateShader       = (CreateShaderFn)gl_proc("glCreateShader");
    fShaderSource       = (ShaderSourceFn)gl_proc("glShaderSource");
    fCompileShader      = (CompileShaderFn)gl_proc("glCompileShader");
    fGetShaderiv        = (GetivFn)gl_proc("glGetShaderiv");
    fGetProgramiv       = (GetivFn)gl_proc("glGetProgramiv");
    fGetShaderInfoLog   = (GetInfoLogFn)gl_proc("glGetShaderInfoLog");
    fGetProgramInfoLog  = (GetInfoLogFn)gl_proc("glGetProgramInfoLog");
    fCreateProgram      = (CreateProgramFn)gl_proc("glCreateProgram");
    fAttachShader       = (AttachShaderFn)gl_proc("glAttachShader");
    fLinkProgram        = (LinkProgramFn)gl_proc("glLinkProgram");
    fUseProgram         = (UseProgramFn)gl_proc("glUseProgram");
    fGetUniformLocation = (GetUniformLocationFn)gl_proc("glGetUniformLocation");
    fUniform1i          = (Uniform1iFn)gl_proc("glUniform1i");
    fUniform1f          = (Uniform1fFn)gl_proc("glUniform1f");
    fActiveTexture      = (ActiveTextureFn)gl_proc("glActiveTexture");
    fUniform3f          = (Uniform3fFn)gl_proc("glUniform3f");
    fUniform2f          = (Uniform2fFn)gl_proc("glUniform2f");
    return fCreateShader && fShaderSource && fCompileShader && fGetShaderiv &&
           fGetProgramiv && fGetShaderInfoLog && fGetProgramInfoLog &&
           fCreateProgram && fAttachShader && fLinkProgram && fUseProgram &&
           fGetUniformLocation && fUniform1i && fUniform1f && fActiveTexture && fUniform3f && fUniform2f;
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
        char buf[512];
        buf[0] = 0;
        fGetShaderInfoLog(s, (GLsizei)sizeof buf, NULL, buf);
        u9log_line("GLWBUF %s shader did not compile: %s", what, buf);
        return 0;
    }
    return s;
}

static void build(void)
{
    GLuint vs, fs, prog;
    GLint  ok = 0;

    if (!load_entry_points()) {
        u9log_line("GLWBUF shader entry points missing - depth_mode 2 falls "
                   "back to 1");
        return;
    }
    vs = compile(GL_VERTEX_SHADER,   k_vertex,   "vertex");
    fs = compile(GL_FRAGMENT_SHADER, k_fragment, "fragment");
    if (!vs || !fs) return;
    prog = fCreateProgram();
    if (!prog) return;
    fAttachShader(prog, vs);
    fAttachShader(prog, fs);
    fLinkProgram(prog);
    fGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        buf[0] = 0;
        fGetProgramInfoLog(prog, (GLsizei)sizeof buf, NULL, buf);
        u9log_line("GLWBUF program did not link: %s", buf);
        return;
    }
    g_uTex       = fGetUniformLocation(prog, "u_tex");
    g_uTextured  = fGetUniformLocation(prog, "u_textured");
    g_uAlphaTest = fGetUniformLocation(prog, "u_alphaTest");
    g_uAlphaRef  = fGetUniformLocation(prog, "u_alphaRef");
    g_uFog       = fGetUniformLocation(prog, "u_fog");
    g_uZRef      = fGetUniformLocation(prog, "u_zRef");
    g_uZFar      = fGetUniformLocation(prog, "u_zFar");
    g_uDepthGlide = fGetUniformLocation(prog, "u_depthGlide");
    g_uSlack      = fGetUniformLocation(prog, "u_slack");
    g_uRune      = fGetUniformLocation(prog, "u_rune");
    g_uRuneTex   = fGetUniformLocation(prog, "u_runeTex");
    g_uRuneTint  = fGetUniformLocation(prog, "u_runeTint");
    g_uLightable = fGetUniformLocation(prog, "u_lightable");
    g_uGlow      = fGetUniformLocation(prog, "u_glow");
    g_uBloom     = fGetUniformLocation(prog, "u_bloom");
    g_uAlphaScale = fGetUniformLocation(prog, "u_alphaScale");
    g_uRgbScale   = fGetUniformLocation(prog, "u_rgbScale");
    g_uSelfLit    = fGetUniformLocation(prog, "u_selfLit");
    g_uUi         = fGetUniformLocation(prog, "u_ui");
    g_uMark       = fGetUniformLocation(prog, "u_mark");
    g_uFade      = fGetUniformLocation(prog, "u_fade");
    g_uLodBias   = fGetUniformLocation(prog, "u_lodBias");
    g_uJitter    = fGetUniformLocation(prog, "u_jitter");
    g_prog = prog;
    u9log_line("GLWBUF per-pixel W-buffer depth ready (program %u)",
               (unsigned)prog);
}

int u9gl_wbuf_begin(float zRef, float zFar, int fog)
{
    if (!g_tried) { g_tried = 1; build(); }
    if (!g_prog) return 0;
    fUseProgram(g_prog);
    fUniform1i(g_uTex, 0);
    fUniform1f(g_uZRef, zRef);
    fUniform1f(g_uZFar, zFar);
    fUniform1i(g_uDepthGlide, u9glpost_depth_glide());
    fUniform1i(g_uRuneTex, 1);
    fUniform1i(g_uRune, 0);
    g_lastRune = 0;
    fUniform1i(g_uFog, fog ? 1 : 0);
    g_lastTextured = g_lastAlphaTest = g_lastLightable = g_lastGlow = -1;
    g_lastBloom = g_lastAlphaScale = -1.0f;
    fUniform1f(g_uRgbScale, 1.0f);
    g_lastRgbScale = 1.0f;
    fUniform1i(g_uSelfLit, 0);
    g_lastSelfLit = 0;
    fUniform1f(g_uSlack, 0.0f);
    g_lastSlack = 0;
    fUniform1i(g_uUi, 0);
    g_lastUi = 0;
    fUniform1i(g_uMark, 0);
    fUniform2f(g_uJitter, 0.0f, 0.0f);
    g_lastJit = 0;
    g_lastFade = g_lastLodBias = -1.0f;
    g_lastAlphaRef = -1.0f;
    u9glpost_albedo_targets(1);
    return 1;
}

void u9gl_wbuf_run(int textured, int alphaTest, float alphaRef, int lightable, int glow, float bloom,
                   float fade)
{
    if (fade != g_lastFade) {
        fUniform1f(g_uFade, fade);
        g_lastFade = fade;
    }
    {
        float bias = u9glpost_effect_lod_bias(fade != 0.0f || u9gl_effect_run_now());
        if (bias != g_lastLodBias) {
            fUniform1f(g_uLodBias, bias);
            g_lastLodBias = bias;
        }
    }
    if (bloom != g_lastBloom) {
        fUniform1f(g_uBloom, bloom);
        g_lastBloom = bloom;
    }
    glow = glow ? 1 : 0;
    if (glow != g_lastGlow) {
        fUniform1i(g_uGlow, glow);
        g_lastGlow = glow;
    }
    lightable = lightable ? 1 : 0;
    if (lightable != g_lastLightable) {
        fUniform1i(g_uLightable, lightable);
        g_lastLightable = lightable;
    }
    textured  = textured  ? 1 : 0;
    alphaTest = alphaTest ? 1 : 0;
    if (textured != g_lastTextured) {
        fUniform1i(g_uTextured, textured);
        g_lastTextured = textured;
    }
    if (alphaTest != g_lastAlphaTest) {
        fUniform1i(g_uAlphaTest, alphaTest);
        g_lastAlphaTest = alphaTest;
    }
    if (alphaRef != g_lastAlphaRef) {
        fUniform1f(g_uAlphaRef, alphaRef);
        g_lastAlphaRef = alphaRef;
    }
}

void u9gl_wbuf_alpha(float scale)
{
    if (!g_prog) return;
    if (scale != g_lastAlphaScale) {
        fUniform1f(g_uAlphaScale, scale);
        g_lastAlphaScale = scale;
    }
}

void u9gl_wbuf_rgb(float scale)
{
    if (!g_prog) return;
    if (scale != g_lastRgbScale) {
        fUniform1f(g_uRgbScale, scale);
        g_lastRgbScale = scale;
    }
}

void u9gl_wbuf_selflit(int on)
{
    on = on ? 1 : 0;
    if (!g_prog || on == g_lastSelfLit) return;
    fUniform1i(g_uSelfLit, on);
    g_lastSelfLit = on;
}

void u9gl_wbuf_slack(int on)
{
    on = on ? 1 : 0;
    if (on) on = u9glpost_depth_slack();
    if (!g_prog || on == g_lastSlack) return;
    fUniform1f(g_uSlack, (float)on);
    g_lastSlack = on;
}

void u9gl_wbuf_ui(int on)
{
    on = on < 0 ? 0 : on > 2 ? 2 : on;
    if (!g_prog || on == g_lastUi) return;
    fUniform1i(g_uUi, on);
    g_lastUi = on;
}

void u9gl_wbuf_mark(int on)
{
    if (!g_prog) return;
    fUniform1i(g_uMark, on);
}

void u9gl_wbuf_set_jitter(float x, float y)
{
    g_jit[0] = x;
    g_jit[1] = y;
    g_lastJit = -1;
}

void u9gl_wbuf_jitter(int on)
{
    on = on && (g_jit[0] != 0.0f || g_jit[1] != 0.0f);
    if (!g_prog || on == g_lastJit) return;
    fUniform2f(g_uJitter, on ? g_jit[0] : 0.0f, on ? g_jit[1] : 0.0f);
    g_lastJit = on;
}

void u9gl_wbuf_rune(unsigned mask, const float tint[3])
{
    int on = mask != 0;
    if (!g_prog) return;
    if (on) {
        fActiveTexture(0x84C1);
        glBindTexture(GL_TEXTURE_2D, mask);
        fActiveTexture(0x84C0);
        fUniform3f(g_uRuneTint, tint[0], tint[1], tint[2]);
    }
    if (on != g_lastRune) {
        fUniform1i(g_uRune, on);
        g_lastRune = on;
    }
}

void u9gl_wbuf_end(void)
{
    u9glpost_albedo_targets(0);
    if (g_prog) fUseProgram(0);
}
