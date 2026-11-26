#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../injection/config.h"
#include "../common/buildid.h"
#include "../common/lifecycle.h"
#include "fixmem.h"
#include "mousefix.h"

void u9log_line(const char *fmt, ...);

#define MOUSE_SENS        0x0078730Cu
#define MOUSE_INVERT      0x00787318u
#define GAME_GLOBALS      0x00850228u
#define CAM_SCALE_X       0x10C
#define STOCK_SENS_BITS   0x3AA3D70Au
#define STOCK_SENS        0.00125f

static int   g_on;
static int   g_follow;
static int   g_pct = 100;
static float g_scale = 1.0f;
static unsigned g_rescales;

static volatile float *sens(void) { return (volatile float *)(uintptr_t)MOUSE_SENS; }

static uintptr_t rd_ptr(uintptr_t p) { return readable4(p) ? rd32(p) : 0; }

static void mouse_remove(void)
{
    *sens() = STOCK_SENS;
    *(volatile uint32_t *)(uintptr_t)MOUSE_INVERT = 1;
    g_on = 0;
}

void u9mouse_install(void)
{
    uint32_t bits, inv;
    int invert;

    g_pct    = u9cfg_int("mouse_sensitivity", 100);
    g_follow = u9cfg_int("mouse_sensitivity_resolution", 0) != 0;
    invert   = u9cfg_int("mouse_invert", 0) != 0;
    if (g_pct < 10) g_pct = 10;
    if (g_pct > 1000) g_pct = 1000;
    if (g_pct == 100 && !g_follow && !invert) return;

    if (!u9build_is_exact()) {
        u9log_line("MOUSE settings REFUSED: build %s not recognised - mouse-look as stock",
                   u9build_name());
        return;
    }
    memcpy(&bits, (const void *)(uintptr_t)MOUSE_SENS, 4);
    inv = rd32(MOUSE_INVERT);
    if (bits != STOCK_SENS_BITS || inv != 1) {
        u9log_line("MOUSE settings REFUSED: sensitivity bits %08X, invert %u, expected %08X and 1"
                   " - mouse-look as stock", bits, inv, STOCK_SENS_BITS);
        return;
    }

    *sens() = STOCK_SENS * (float)g_pct / 100.0f;
    if (invert) *(volatile uint32_t *)(uintptr_t)MOUSE_INVERT = 0;
    g_scale = 1.0f;
    g_on = 1;
    u9life_push("mouse settings", mouse_remove);
    u9log_line("MOUSE settings: sensitivity %d%% of stock (%.6f)%s%s", g_pct, (double)*sens(),
               g_follow ? ", times the screen scale once the camera is up" : "",
               invert ? ", mouse-y inverted (Ctrl+I flips it back)" : "");
}

void u9mouse_tick(void)
{
    uintptr_t g, cam;
    float s;

    if (!g_on || !g_follow) return;
    g = rd_ptr(GAME_GLOBALS);
    cam = g ? rd_ptr(g + 0xBC) : 0;
    if (!cam || !readable4(cam + CAM_SCALE_X)) return;
    s = *(volatile float *)(cam + CAM_SCALE_X);
    if (!(s >= 0.25f && s <= 16.0f) || s == g_scale) return;
    *sens() = *sens() * s / g_scale;
    if (g_rescales < 8)
        u9log_line("MOUSE screen scale %.3f -> %.3f: sensitivity now %.6f",
                   (double)g_scale, (double)s, (double)*sens());
    g_scale = s;
    g_rescales++;
}

void u9mouse_report(void)
{
    if (g_on)
        u9log_line("MOUSE sensitivity at exit %.6f (%.0f%% of stock at this screen scale), invert flag %u",
                   (double)*sens(), (double)(*sens() / (STOCK_SENS * g_scale) * 100.0f),
                   rd32(MOUSE_INVERT));
}
