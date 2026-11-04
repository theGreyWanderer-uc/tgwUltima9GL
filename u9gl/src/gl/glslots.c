#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include "glpost.h"
#include "glshot.h"
#include "u9renderer.h"
#include "../../addresses.h"

typedef void *(__cdecl *U9NewFn)(unsigned);

#ifndef GL_UNSIGNED_SHORT_5_6_5
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#endif

void u9log_line(const char *fmt, ...);
int  u9cfg_int(const char *key, int def);
HDC  u9gl_device_context(void);
HGLRC u9gl_render_context(void);
int  u9gl_last_viewport(int *x, int *y, int *w, int *h);
void u9gl_display_close(void);

#define GAMMA_FIELD    0x450424u
#define GAMMA_MIN      0.0f
#define GAMMA_MAX      20.0f

static const uintptr_t GAMMA_STRINGS[3] = { 0x00991704u, 0x009917B0u, 0x0099185Cu };

static float g_gamma = 1.0f;
static int   g_applyRamp;
static int   g_rampSaved;
static WORD  g_rampOrig[3][256];

static void ramp_apply(float gamma)
{
    HDC   dc = u9gl_device_context();
    WORD  ramp[3][256];
    int   i;
    double inv;

    if (!g_applyRamp || !dc) return;
    if (u9glpost_owns_gamma()) return;
    if (!g_rampSaved) {
        if (!GetDeviceGammaRamp(dc, g_rampOrig)) {
            u9log_line("GLSLOT gamma: GetDeviceGammaRamp failed, not applying");
            g_applyRamp = 0;
            return;
        }
        g_rampSaved = 1;
    }
    inv = (gamma > 0.01f) ? (1.0 / (double)gamma) : 1.0;
    for (i = 0; i < 256; i++) {
        double v = pow((double)i / 255.0, inv) * 65535.0;
        WORD   w = (WORD)(v < 0.0 ? 0.0 : (v > 65535.0 ? 65535.0 : v));
        ramp[0][i] = ramp[1][i] = ramp[2][i] = w;
    }
    SetDeviceGammaRamp(dc, ramp);
}

void u9glslots_restore_gamma(void)
{
    HDC dc = u9gl_device_context();
    if (g_rampSaved && dc) {
        SetDeviceGammaRamp(dc, g_rampOrig);
        g_rampSaved = 0;
        u9log_line("GLSLOT gamma ramp restored");
    }
}

void U9CALL u9gl_slot_set_gamma(void *self, float gamma)
{
    static int announced;
    char  text[32];
    int   i;

    if (gamma < GAMMA_MIN || gamma > GAMMA_MAX)
        return;

    g_gamma = gamma;
    *(float *)((char *)self + GAMMA_FIELD) = gamma;

    snprintf(text, sizeof text, "%7.4f", (double)gamma);
    for (i = 0; i < 3; i++)
        lstrcpynA((char *)GAMMA_STRINGS[i], text, (int)sizeof text);

    ramp_apply(gamma);

    if (!announced) {
        announced = 1;
        u9log_line("GLSLOT set_gamma is OURS now: %.4f, field and the three "
                   "display strings written, ramp %s",
                   (double)gamma, g_applyRamp ? "applied" : "tracked only");
    }
}

void U9CALL u9gl_slot_get_gamma(void *self, unsigned *r, unsigned *g, unsigned *b)
{
    unsigned v = *(unsigned *)((char *)self + GAMMA_FIELD);
    if (r) *r = v;
    if (g) *g = v;
    if (b) *b = v;
}

void u9glslots_gamma_from_options(void *self)
{
    const char *s = (const char *)GAMMA_STRINGS[0];
    char  buf[32], *end;
    float v;
    lstrcpynA(buf, s, (int)sizeof buf);
    v = (float)strtod(buf, &end);
    if (end == buf) {
        u9log_line("GLSLOT gamma: GammaRed '%s' is not a number - left at %.4f", buf, (double)g_gamma);
        return;
    }
    u9log_line("GLSLOT gamma from Options.ini: GammaRed %.4f (as Glide's CreateDevice)", (double)v);
    u9gl_slot_set_gamma(self, v);
}

float u9glslots_gamma(void)
{
    return g_gamma;
}

void u9glslots_init(void)
{
    g_applyRamp = u9cfg_int("gl_gamma_apply", 0);
}


void u9gl_set_depth_test(int enable);
void u9gl_set_depth_write(int enable);
void u9gl_set_depth_func(unsigned glideFunc);

#define DISABLED(self) (*(const volatile unsigned char *)((const char *)(self)                         + U9_OFF_DISABLED_GUARD) != 0)

void U9CALL u9gl_slot_set_depth_mode(void *self, unsigned mode)
{
    if (DISABLED(self)) return;
    if (mode > 2) return;
    u9gl_set_depth_test(mode != 0);
}

void U9CALL u9gl_slot_disable_depth(void *self)
{
    if (DISABLED(self)) return;
    u9gl_set_depth_test(0);
}

void U9CALL u9gl_slot_set_depth_write(void *self, unsigned enable)
{
    unsigned *cached;
    if (DISABLED(self)) return;
    cached = (unsigned *)((char *)self + U9_OFF_DEPTH_WRITE);
    if (*cached == enable) return;
    *cached = enable;
    u9gl_set_depth_write((int)enable);
}

void U9CALL u9gl_slot_set_depth_func(void *self, unsigned func)
{
    if (DISABLED(self)) return;
    if (func > 7) return;
    u9gl_set_depth_func(func);
}

void U9CALL u9gl_slot04(void *self, float a)
{
    *(float *)((char *)self + U9_OFF_SLOT04_A) = a;
    *(float *)((char *)self + U9_OFF_SLOT04_B) =
        a * *(const float *)(uintptr_t)U9_SLOT04_SCALE;
}

typedef unsigned (U9CALL *U9Slot13TailFn)(void *self, unsigned a, unsigned b);
unsigned U9CALL u9gl_slot13(void *self, unsigned a, unsigned b)
{
    if (DISABLED(self)) return 0;
    return ((U9Slot13TailFn)(uintptr_t)U9_SHARED_SLOT13_TAIL)(self, a, b);
}

void U9CALL u9gl_slot14(void *self, unsigned a)
{
    if (DISABLED(self)) return;
    *((unsigned char *)self + U9_OFF_SLOT14_BYTE) = (unsigned char)a;
}

void U9CALL u9gl_slot_set_mipmap_level(void *self, unsigned level)
{
    if (DISABLED(self)) return;

    if ((int)level < 0) level = 0u;
    else if (level > 3u) level = 3u;

    *(unsigned *)((char *)self + U9_OFF_MIP_LEVEL)   = level;
    *((unsigned char *)self + U9_OFF_MIP_ENABLED)    = (unsigned char)(level != 0u);
    *(unsigned *)((char *)self + U9_OFF_MIP_LODBIAS) = level ? (level - 1u) : 0u;

    *((unsigned char *)self + U9_OFF_STATE_LATCH) = 0u;
}

static void once(const char *what, int *flag)
{
    if (*flag) return;
    *flag = 1;
    u9log_line("STAGE5 %s reached - stubbed, Glide-only side effects", what);
}

void U9CALL u9gl_slot_draw_line(void *self, const void *v0, const void *v1,
                                float alpha)
{
    static int said; (void)self; (void)v0; (void)v1; (void)alpha;
    once("slot 31 DrawLine", &said);
}
void U9CALL u9gl_slot_flush_lines(void *self)
{
    static int said; (void)self; once("slot 40 FlushLines", &said);
}
void U9CALL u9gl_slot_screenshot(void *self, const char *path)
{
    static int said;
    if (DISABLED(self)) return;
    if (!u9shot_request(path)) once("slot 50 Screenshot (gl_screenshot = 0: nothing written)", &said);
}

static unsigned g_capCalls, g_capFilled;

void *U9CALL u9gl_slot_capture(void *self, unsigned toBuffer)
{
    unsigned w, h, lw = 0, lh = 0, rowBytes, pixBytes, total, r;
    unsigned short *buf;
    unsigned *rowTab;
    unsigned char *pix;
    HDC   dc;
    HGLRC rc, prevRc;
    HDC   prevDc;
    int   vx = 0, vy = 0, vw = 0, vh = 0;

    if (DISABLED(self)) return 0;

    w = *(const unsigned *)((const char *)self + U9_OFF_WIDTH);
    h = *(const unsigned *)((const char *)self + U9_OFF_HEIGHT);
    if (!w || !h || w > 4096u || h > 4096u) return 0;

    while ((1u << lw) < w) lw++;
    while ((1u << lh) < h) lh++;

    rowBytes = w * 2u;
    pixBytes = rowBytes * h;
    total    = pixBytes + 0x2Cu + h * 4u;

    buf = (unsigned short *)((U9NewFn)(uintptr_t)U9_OP_NEW)(total);
    if (!buf) return 0;
    memset(buf, 0, total);

    buf[0]  = (unsigned short)w;
    buf[2]  = (unsigned short)h;
    *(unsigned *)(buf + 4)    = 1u;
    *(unsigned *)(buf + 6)    = ((lh & 0xFu) << 4 | (lw & 0xFu)) << 12;
    *(unsigned *)(buf + 8)    = 0x18u;
    *(unsigned *)(buf + 10)   = pixBytes + 0x14u;
    *(unsigned *)(buf + 0xC)  = 0x60000400u;
    *(unsigned *)(buf + 0xE)  = w;
    *(unsigned *)(buf + 0x10) = h;
    *(unsigned *)(buf + 0x12) = 0u;
    *(unsigned *)(buf + 0x14) = 0u;

    rowTab = (unsigned *)(buf + 0x16);
    for (r = 0; r < h; r++) rowTab[r] = 0x14u + h * 4u + r * rowBytes;

    pix = (unsigned char *)buf + 0x18u + 0x14u + h * 4u;

    g_capCalls++;
    dc = u9gl_device_context();
    rc = u9gl_render_context();
    if (dc && rc) {
        prevDc = wglGetCurrentDC();
        prevRc = wglGetCurrentContext();
        if (wglMakeCurrent(dc, rc)) {
            glPixelStorei(GL_PACK_ALIGNMENT, 2);
            if (!u9gl_last_viewport(&vx, &vy, &vw, &vh)) {
                vx = 0; vy = 0; vw = (int)w; vh = (int)h;
            }
            if ((unsigned)vw == w && (unsigned)vh == h) {
                for (r = 0; r < h; r++)
                    glReadPixels(vx, vy + (GLint)(h - 1u - r), (GLsizei)w, 1,
                                 GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pix + r * rowBytes);
                g_capFilled++;
            } else {
                unsigned char *full = (unsigned char *)HeapAlloc(
                    GetProcessHeap(), 0, (SIZE_T)vw * (SIZE_T)vh * 2u);
                if (full) {
                    unsigned c;
                    glReadPixels(vx, vy, vw, vh, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                                 full);
                    for (r = 0; r < h; r++) {
                        unsigned sy = (unsigned)vh - 1u - (r * (unsigned)vh) / h;
                        const unsigned char *srow = full + sy * (unsigned)vw * 2u;
                        unsigned char *drow = pix + r * rowBytes;
                        for (c = 0; c < w; c++) {
                            unsigned sx = (c * (unsigned)vw) / w;
                            drow[c * 2u]      = srow[sx * 2u];
                            drow[c * 2u + 1u] = srow[sx * 2u + 1u];
                        }
                    }
                    HeapFree(GetProcessHeap(), 0, full);
                    g_capFilled++;
                }
            }
            wglMakeCurrent(prevDc, prevRc);
        }
    }
    u9log_line("STAGE5 CaptureScreen %ux%u from viewport %dx%d at %d,%d, %u bytes,"
               " pixels at +0x%X, call %u (%u filled)", w, h, vw, vh, vx, vy, total,
               (unsigned)(0x18u + 0x14u + h * 4u), g_capCalls, g_capFilled);
    (void)toBuffer;
    return buf;
}

typedef void (U9CALL *U9FogStepFn)(void *fog);

void U9CALL u9gl_slot_suspend(void *self)
{
    void *fog;

    if (DISABLED(self)) return;
    *(unsigned char *)((char *)self + U9_OFF_DISABLED_GUARD) = 1;
    fog = *(void **)((char *)self + U9_OFF_FOG_OBJECT);
    if (fog) ((U9FogStepFn)(*(void ***)fog)[0x30 / 4])(fog);
    u9gl_display_close();
    u9log_line("STAGE5 slot 52: renderer suspended for a cutscene (guard set,"
               " fog suspended) called from 0x%08X",
               (unsigned)(uintptr_t)__builtin_return_address(0));
}

void U9CALL u9gl_slot_restore_state(void *self)
{
    typedef void (U9CALL *U9Slot7Fn)(void *, unsigned, unsigned, unsigned);
    void *fog;
    void **vt = *(void ***)self;
    unsigned w = *(const unsigned *)((const char *)self + U9_OFF_WIDTH);
    unsigned h = *(const unsigned *)((const char *)self + U9_OFF_HEIGHT);
    unsigned b = *(const unsigned *)((const char *)self + U9_OFF_BPP);

    if (!DISABLED(self)) return;
    *(unsigned char *)((char *)self + U9_OFF_DISABLED_GUARD) = 0;
    fog = *(void **)((char *)self + U9_OFF_FOG_OBJECT);
    if (fog) ((U9FogStepFn)(*(void ***)fog)[0x34 / 4])(fog);
    ((U9Slot7Fn)vt[7])(self, w, h, b);
    u9log_line("STAGE5 slot 53: renderer restored after a cutscene (guard"
               " cleared, fog resumed, CreateDevice %ux%u bpp %u) called from"
               " 0x%08X", w, h, b, (unsigned)(uintptr_t)__builtin_return_address(0));
}
