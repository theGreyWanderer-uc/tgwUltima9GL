#include <windows.h>
#include <string.h>
#include "glpost.h"
#include "glshot.h"
#include "metrics.h"
#include "../telemetry/publisher.h"
#include "console.h"
#include "keys.h"
#include <GL/gl.h>
#include <stdint.h>

#include "../../addresses.h"
#include "../diag/log.h"
#include "../injection/config.h"
#include "../common/u9time.h"
#include "glbackground.h"

static HWND  g_wnd;
static HDC   g_dc;

HDC u9gl_device_context(void);
static HGLRC g_rc;
static ATOM  g_cls;

#define U9GL_CLASS   "u9gl_probe"
#define U9_WNDCLASS  "OFCBlackWindowClass"

static HWND g_parent;
static int  g_detached;

static int  g_frameOn;
static int  g_userPlaced;
static int  g_userX, g_userY;
static void sync_game_window(const char *why);

static BOOL CALLBACK find_game_window(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    char  cls[64] = "";
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strcmp(cls, U9_WNDCLASS) != 0) return TRUE;
    *(HWND *)lp = hwnd;
    return FALSE;
}

static const char *pf_flags(DWORD f, char *buf, size_t n)
{
    buf[0] = 0;
    if (f & PFD_DRAW_TO_WINDOW) strncat(buf, "window ",  n - strlen(buf) - 1);
    if (f & PFD_SUPPORT_OPENGL) strncat(buf, "gl ",      n - strlen(buf) - 1);
    if (f & PFD_SUPPORT_GDI)    strncat(buf, "gdi ",     n - strlen(buf) - 1);
    if (f & PFD_DOUBLEBUFFER)   strncat(buf, "double ",  n - strlen(buf) - 1);
    return buf;
}

void u9gl_probe_window(const char *when)
{
    HWND  hwnd = *(HWND *)U9_G_GLIDE_HWND;
    char  cls[64] = "";
    char  flags[64];
    RECT  r = {0, 0, 0, 0};
    DWORD pid = 0;
    HDC   dc;
    int   pf;

    if (!hwnd) {
        u9log_line("GLPROBE [%s] DAT_%08X = NULL - no window yet",
                   when, U9_G_GLIDE_HWND);
        return;
    }
    if (!IsWindow(hwnd)) {
        u9log_line("GLPROBE [%s] hwnd=0x%08X is not a window",
                   when, (unsigned)(uintptr_t)hwnd);
        return;
    }

    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);

    dc = GetDC(hwnd);
    pf = dc ? GetPixelFormat(dc) : -1;

    u9log_line("GLPROBE [%s] hwnd=0x%08X class='%s' rect=%dx%d pid=%lu%s pixelformat=%d",
               when, (unsigned)(uintptr_t)hwnd, cls,
               (int)(r.right - r.left), (int)(r.bottom - r.top),
               pid, pid == GetCurrentProcessId() ? " (ours)" : " (FOREIGN)", pf);

    if (dc && pf > 0) {
        PIXELFORMATDESCRIPTOR d;
        if (DescribePixelFormat(dc, pf, sizeof(d), &d)) {
            u9log_line("GLPROBE [%s]   pf %d: %u-bit colour, %u depth, %u stencil, flags: %s"
                       " -> window ALREADY claimed, SetPixelFormat cannot run again",
                       when, pf, d.cColorBits, d.cDepthBits, d.cStencilBits,
                       pf_flags(d.dwFlags, flags, sizeof(flags)));
        }
    } else if (dc) {
        u9log_line("GLPROBE [%s]   no pixel format set - the window is still claimable",
                   when);
    }
    if (dc) ReleaseDC(hwnd, dc);
}

int u9gl_context_create(void)
{
    PIXELFORMATDESCRIPTOR pfd;
    WNDCLASSA wc;
    HINSTANCE inst = GetModuleHandleA(NULL);
    int pf;
    const char *vendor, *renderer, *version;

    if (g_rc) return 1;

    LRESULT CALLBACK u9gl_wndproc(HWND, UINT, WPARAM, LPARAM);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = u9gl_wndproc;
    wc.hInstance     = inst;
    wc.lpszClassName = U9GL_CLASS;
    g_cls = RegisterClassA(&wc);
    if (!g_cls) {
        u9log_line("GLCTX RegisterClass failed, err=%lu", GetLastError());
        return 0;
    }

    EnumWindows(find_game_window, (LPARAM)&g_parent);

    g_detached = u9cfg_int("gl_detach", 0);
    g_wnd = CreateWindowExA(g_detached ? WS_EX_NOACTIVATE : 0,
                            U9GL_CLASS, U9GL_CLASS,
                            (g_parent && !g_detached) ? (WS_CHILD)
                                                      : (WS_POPUP | WS_BORDER),
                            0, 0, 1, 1,
                            g_detached ? NULL : g_parent, NULL, inst, NULL);
    if (!g_wnd) {
        u9log_line("GLCTX CreateWindowEx failed, err=%lu", GetLastError());
        return 0;
    }
    u9log_line("GLCTX window 0x%08X, parent %s 0x%08X",
               (unsigned)(uintptr_t)g_wnd,
               g_parent ? "'" U9_WNDCLASS "'" : "(none, top-level)",
               (unsigned)(uintptr_t)g_parent);

    g_dc = GetDC(g_wnd);
    if (!g_dc) { u9log_line("GLCTX GetDC failed"); return 0; }

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd)) {
        u9log_line("GLCTX pixel format failed (pf=%d, err=%lu)", pf, GetLastError());
        return 0;
    }

    g_rc = wglCreateContext(g_dc);
    if (!g_rc) {
        u9log_line("GLCTX wglCreateContext failed, err=%lu", GetLastError());
        return 0;
    }
    if (!wglMakeCurrent(g_dc, g_rc)) {
        u9log_line("GLCTX wglMakeCurrent failed, err=%lu", GetLastError());
        return 0;
    }

    vendor   = (const char *)glGetString(GL_VENDOR);
    renderer = (const char *)glGetString(GL_RENDERER);
    version  = (const char *)glGetString(GL_VERSION);
    u9log_line("GLCTX context 0x%08X on a 1x1 surface inside the game window, pf=%d",
               (unsigned)(uintptr_t)g_rc, pf);
    u9log_line("GLCTX   GL_VENDOR   = %s", vendor   ? vendor   : "(null)");
    u9log_line("GLCTX   GL_RENDERER = %s", renderer ? renderer : "(null)");
    u9log_line("GLCTX   GL_VERSION  = %s", version  ? version  : "(null)");

    wglMakeCurrent(NULL, NULL);
    u9log_line("GLCTX released (wglMakeCurrent NULL) - Glide continues untouched");
    return 1;
}

static void release_cursor(const char *why);

void u9gl_context_destroy(void)
{
    u9glpost_release();
    if (g_rc) { wglMakeCurrent(NULL, NULL); wglDeleteContext(g_rc); g_rc = NULL; }
    if (g_dc) { ReleaseDC(g_wnd, g_dc); g_dc = NULL; }
    release_cursor("the GL context is going");
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = NULL; }
    if (g_cls) { UnregisterClassA(U9GL_CLASS, GetModuleHandleA(NULL)); g_cls = 0; }
}

int u9gl_context_probe(void)
{
    static int answer;
    static const char CLASS[] = "u9gl_ctxprobe";
    PIXELFORMATDESCRIPTOR pfd;
    WNDCLASSA wc;
    HINSTANCE inst   = GetModuleHandleA(NULL);
    HDC       prevDc = wglGetCurrentDC();
    HGLRC     prevRc = wglGetCurrentContext();
    HWND      wnd = NULL;
    HDC       dc  = NULL;
    HGLRC     rc  = NULL;
    ATOM      cls = 0;
    DWORD     err = 0;
    int       pf, ok = 0;
    const char *failed = "";

    if (answer) return answer > 0;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = CLASS;
    cls = RegisterClassA(&wc);
    if (!cls) { failed = "RegisterClass"; err = GetLastError(); goto done; }
    wnd = CreateWindowExA(0, CLASS, CLASS, WS_POPUP, 0, 0, 1, 1, NULL, NULL, inst, NULL);
    if (!wnd) { failed = "CreateWindowEx"; err = GetLastError(); goto done; }
    dc = GetDC(wnd);
    if (!dc) { failed = "GetDC"; err = GetLastError(); goto done; }

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;
    pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { failed = "pixel format"; err = GetLastError(); goto done; }
    rc = wglCreateContext(dc);
    if (!rc) { failed = "wglCreateContext"; err = GetLastError(); goto done; }
    if (!wglMakeCurrent(dc, rc)) { failed = "wglMakeCurrent"; err = GetLastError(); goto done; }

    {
        const char *vendor   = (const char *)glGetString(GL_VENDOR);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        const char *version  = (const char *)glGetString(GL_VERSION);
        u9log_line("GLCTX probe: OpenGL available - %s / %s / %s%s",
                   vendor   ? vendor   : "(null)",
                   renderer ? renderer : "(null)",
                   version  ? version  : "(null)",
                   renderer && strstr(renderer, "GDI Generic")
                       ? "  (Microsoft's software renderer: no GPU driver answered; it will draw, slowly)"
                       : "");
        ok = 1;
    }

done:
    if (!ok)
        u9log_line("GLCTX probe: NO OpenGL context on this machine - %s failed, err=%lu",
                   failed, err);
    if (prevRc) wglMakeCurrent(prevDc, prevRc);
    else        wglMakeCurrent(NULL, NULL);
    if (rc)  wglDeleteContext(rc);
    if (dc)  ReleaseDC(wnd, dc);
    if (wnd) DestroyWindow(wnd);
    if (cls) UnregisterClassA(CLASS, inst);
    answer = ok ? 1 : -1;
    return ok;
}

static BOOL CALLBACK enum_one(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    char  cls[64] = "", title[96] = "";
    RECT  r = {0, 0, 0, 0};
    HDC   dc;
    int   pf;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId())
        return TRUE;

    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));
    GetWindowRect(hwnd, &r);
    dc = GetDC(hwnd);
    pf = dc ? GetPixelFormat(dc) : -1;
    if (dc) ReleaseDC(hwnd, dc);

    u9log_line("GLWIN [%s] hwnd=0x%08X class='%s' title='%s' %dx%d at %d,%d "
               "%s%s pixelformat=%d %s",
               (const char *)lp, (unsigned)(uintptr_t)hwnd, cls, title,
               (int)(r.right - r.left), (int)(r.bottom - r.top),
               (int)r.left, (int)r.top,
               IsWindowVisible(hwnd) ? "visible" : "hidden",
               GetParent(hwnd) ? " child" : " top-level", pf,
               pf > 0 ? "(CLAIMED)" : "(claimable)");
    return TRUE;
}

void u9gl_probe_all_windows(const char *when)
{
    u9log_line("GLWIN [%s] enumerating windows owned by pid %lu",
               when, GetCurrentProcessId());
    EnumWindows(enum_one, (LPARAM)when);
}

static void *g_boundRenderer;
static int   g_surfaceActive;
static int   g_glFrames;

static int   g_takeover = -1;
static int   g_belowForeground;
static unsigned g_placementExpectUntil;


static void resolve_takeover(void)
{
    char buf[8];
    DWORD n;
    if (g_takeover >= 0) return;
    (void)buf; (void)n;
    g_takeover = u9cfg_int("takeover", 0);
    u9log_line("GLREND takeover at startup: %s",
               g_takeover ? "ON" : "off, Glide presents");
}

void     u9gl_discard_frame(void);
void     u9gl_set_surface(int w, int h, int scale);
void     u9gl_picture_box(unsigned engineW, unsigned engineH, int surfW, int surfH,
                          int *x, int *y, int *w, int *h);
void     u9gl_set_point_mode(int m);
int      u9gl_point_mode(void);
unsigned u9gl_frame_additive_tris(void);
unsigned u9gl_frame_alphatest_tris(void);

static void poll_filter(void)
{
    if (u9keys_pressed(VK_F7)) {
        int m = (u9gl_point_mode() + 1) % 3;
        u9gl_set_point_mode(m);
        u9log_line("GLFILT F7: texture filter = %s",
                   m == 0 ? "forced LINEAR" :
                   m == 1 ? "the engine's rule (material 0x200)" : "forced POINT");
    }
}

static void poll_mark(void)
{
    if (u9keys_pressed(VK_F10)) {
        unsigned add   = u9gl_frame_additive_tris();
        unsigned atest = u9gl_frame_alphatest_tris();
        u9log_line("=== MARK (F10) frame %d: additive=%u tris, alphaTest=%u tris, "
                   "renderer=%s ===",
                   g_glFrames, add, atest, g_takeover ? "OpenGL" : "Glide");
    }
}

void u9gl_log_runs(void);
void u9gl_dump_frame(const char *why);
void u9gl_dump_poll(int frame);

const char *u9gl_cycle_mip_filter(void);

static void poll_mipfilter(void)
{
    if (u9keys_pressed(VK_F4))
        u9log_line("GLTEX F4: mip filter now %s", u9gl_cycle_mip_filter());
}

const char *u9gl_cycle_depth_mode(void);
const char *u9gl_toggle_tex_labels(void);

static void poll_texlabels(void)
{
    if (u9keys_pressed(VK_F2))
        u9log_line("GLRAST F2: debug labels %s", u9gl_toggle_tex_labels());
}

static void poll_depthmode(void)
{
    if (u9keys_pressed(VK_F5))
        u9log_line("GLREND F5: depth mapping now %s", u9gl_cycle_depth_mode());
}

static void poll_toggle(void)
{
    poll_mipfilter();
    poll_depthmode();
    poll_texlabels();
    if (u9keys_pressed(VK_F11)) {
        g_takeover = !g_takeover;
        u9log_line("GLREND F11: OpenGL takeover %s (frame %d)",
                   g_takeover ? "ON" : "OFF - Glide presents", g_glFrames);
        g_surfaceActive = 0;
    }
}

void u9gl_flush_frame(unsigned engineW, unsigned engineH);
void u9gl_raster_stats(unsigned *tris, unsigned *runs, unsigned *dropped,
                       float *zMin, float *zMax);

int u9gl_is_bound(void *self)
{
    return self && self == g_boundRenderer && g_rc != NULL;
}

void u9gl_bind_renderer(void *renderer)
{
    g_boundRenderer = renderer;
    u9log_line("GLREND bound to renderer 0x%08X", (unsigned)(uintptr_t)renderer);
}

#define HUD_W 280
#define HUD_H 26

static int g_surfW, g_surfH;
static int g_frameW, g_frameH;

static int game_monitor(RECT *out)
{
    HMONITOR    mon = MonitorFromWindow(g_parent ? g_parent : g_wnd,
                                        MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoA(mon, &mi)) return 0;
    *out = mi.rcMonitor;
    return 1;
}

static int u9gl_activate_surface(void)
{
    RECT rc = {0, 0, 0, 0};
    int  w, h;

    if (!g_rc || !g_dc || !g_wnd) return 0;

    if (g_detached) {
        w = u9cfg_int("gl_window_w", 960);
        h = u9cfg_int("gl_window_h", 720);

        {
            int  mode  = u9cfg_int("gl_window_size", 0);
            LONG style = GetWindowLongA(g_wnd, GWL_STYLE) & ~(LONG)WS_CAPTION;
            RECT mon;

            SetWindowLongA(g_wnd, GWL_STYLE, (mode == 1 || mode == 2)
                           ? style : (style | (LONG)WS_BORDER));
            g_frameOn = 0;
            if ((mode == 1 || mode == 2) && game_monitor(&mon)) {
                int mw = (int)(mon.right - mon.left), mh = (int)(mon.bottom - mon.top);
                int ew = 0, eh = 0;
                if (g_boundRenderer) {
                    ew = (int)*(const unsigned *)((const char *)g_boundRenderer + U9_OFF_WIDTH);
                    eh = (int)*(const unsigned *)((const char *)g_boundRenderer + U9_OFF_HEIGHT);
                }
                if (mode == 2) {
                    w = mw;
                    h = mh;
                } else if (ew > 0 && eh > 0) {
                    w = ew;
                    h = eh;
                    if (w > mw || h > mh) {
                        double s = (double)mw / (double)w;
                        if ((double)mh / (double)h < s) s = (double)mh / (double)h;
                        w = (int)((double)w * s + 0.5);
                        h = (int)((double)h * s + 0.5);
                    }
                }
                u9log_line("GLWIN size mode %d (%s): window %dx%d - engine %dx%d,"
                           " monitor %dx%d", mode,
                           mode == 2 ? "fill the monitor, borderless"
                                     : "engine resolution",
                           w, h, ew, eh, mw, mh);
            }

            if (u9cfg_int("gl_window_frame", 0)) {
                HMONITOR    hm = MonitorFromWindow(g_parent ? g_parent : g_wnd,
                                                   MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi;
                RECT        outer;
                LONG        framed = (GetWindowLongA(g_wnd, GWL_STYLE) & ~(LONG)WS_BORDER)
                                     | (LONG)WS_CAPTION;
                mi.cbSize = sizeof mi;
                SetRect(&outer, 0, 0, w, h);
                AdjustWindowRectEx(&outer, (DWORD)framed, FALSE,
                                   (DWORD)GetWindowLongA(g_wnd, GWL_EXSTYLE));
                if (mode == 2) {
                    u9log_line("GLWIN frame: not used - the whole-monitor window stays"
                               " borderless");
                } else if (!hm || !GetMonitorInfoA(hm, &mi)) {
                    u9log_line("GLWIN frame: not used - no monitor information");
                } else if (outer.right - outer.left > mi.rcWork.right - mi.rcWork.left ||
                           outer.bottom - outer.top > mi.rcWork.bottom - mi.rcWork.top) {
                    u9log_line("GLWIN frame: not used - %ldx%ld with its frame does not fit"
                               " the work area %ldx%ld, so it stays borderless",
                               outer.right - outer.left, outer.bottom - outer.top,
                               mi.rcWork.right - mi.rcWork.left,
                               mi.rcWork.bottom - mi.rcWork.top);
                } else {
                    SetWindowLongA(g_wnd, GWL_STYLE, framed);
                    SetWindowTextA(g_wnd, "Ultima IX");
                    g_frameOn = 1;
                    u9log_line("GLWIN frame: a title bar; client %dx%d, outer %ldx%ld",
                               w, h, outer.right - outer.left, outer.bottom - outer.top);
                }
            }
        }

        if (u9cfg_int("gl_topmost", 1) && !g_belowForeground && (u9log_frame() % 60) == 0)
            SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (g_takeover) {
        if (g_parent && GetClientRect(g_parent, &rc)) {
            w = (int)(rc.right - rc.left);
            h = (int)(rc.bottom - rc.top);
        } else {
            w = GetSystemMetrics(SM_CXSCREEN);
            h = GetSystemMetrics(SM_CYSCREEN);
        }
    } else {
        w = HUD_W;
        h = HUD_H;
    }
    if (w <= 0 || h <= 0) return 0;

    {
        int div = u9cfg_int("surface_div", 1);
        if (div > 1) { w /= div; h /= div; if (w < 16) w = 16; if (h < 16) h = 16; }
    }

    g_surfW = w;
    g_surfH = h;
    if (g_detached) {
        RECT pr = {0, 0, 0, 0};
        int x = u9cfg_int("gl_window_x", -1), y = u9cfg_int("gl_window_y", -1);
        if ((x < 0 || y < 0) && g_parent && GetWindowRect(g_parent, &pr)) {
            if (x < 0) x = (int)pr.right + 8;
            if (y < 0) y = (int)pr.top;
        }
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        {
            int anchor = u9cfg_int("gl_window_anchor", 0);
            if (anchor >= 1 && anchor <= 9) {
                static const char *const NAMES[10] = {
                    "", "bottom-left", "bottom-centre", "bottom-right",
                    "centre-left", "centre", "centre-right",
                    "top-left", "top-centre", "top-right"
                };
                HMONITOR    mon = MonitorFromWindow(g_parent ? g_parent : g_wnd,
                                                    MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi;
                mi.cbSize = sizeof mi;
                if (mon && GetMonitorInfoA(mon, &mi)) {
                    RECT area = g_frameOn ? mi.rcWork : mi.rcMonitor;
                    RECT outer;
                    int mw, mh, ow, oh;
                    int col = (anchor - 1) % 3;
                    int row = 2 - (anchor - 1) / 3;
                    SetRect(&outer, 0, 0, w, h);
                    if (g_frameOn)
                        AdjustWindowRectEx(&outer, (DWORD)GetWindowLongA(g_wnd, GWL_STYLE),
                                           FALSE, (DWORD)GetWindowLongA(g_wnd, GWL_EXSTYLE));
                    ow = (int)(outer.right - outer.left);
                    oh = (int)(outer.bottom - outer.top);
                    mw = (int)(area.right - area.left);
                    mh = (int)(area.bottom - area.top);
                    x = (int)area.left + col * (mw - ow) / 2;
                    y = (int)area.top  + row * (mh - oh) / 2;
                    u9log_line("GLWIN anchor %d (%s): monitor %dx%d at %d,%d -> window"
                               " %dx%d at %d,%d", anchor, NAMES[anchor], mw, mh,
                               (int)mi.rcMonitor.left, (int)mi.rcMonitor.top,
                               w, h, x, y);
                } else {
                    u9log_line("GLWIN anchor %d: no monitor information, using %d,%d",
                               anchor, x, y);
                }
            }
        }
        {
            int ow = w, oh = h;
            if (g_frameOn) {
                RECT outer;
                SetRect(&outer, 0, 0, w, h);
                AdjustWindowRectEx(&outer, (DWORD)GetWindowLongA(g_wnd, GWL_STYLE), FALSE,
                                   (DWORD)GetWindowLongA(g_wnd, GWL_EXSTYLE));
                ow = (int)(outer.right - outer.left);
                oh = (int)(outer.bottom - outer.top);
                if (g_userPlaced) { x = g_userX; y = g_userY; }
            }
            SetWindowPos(g_wnd,
                         u9cfg_int("gl_topmost", 1) ? HWND_TOPMOST : HWND_TOP,
                         x, y, ow, oh, SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        g_belowForeground = 0;
        if (g_frameOn) sync_game_window("placed");
    } else {
        SetWindowPos(g_wnd, HWND_TOP, 0, 0, w, h, SWP_SHOWWINDOW);
    }
    if (!wglMakeCurrent(g_dc, g_rc)) {
        u9log_line("GLREND wglMakeCurrent failed, err=%lu", GetLastError());
        return 0;
    }
    glViewport(0, 0, w, h);
    u9log_line("GLREND surface active: %dx%d, %s", w, h,
               g_detached  ? "DETACHED - own window beside the game, Glide keeps its own"
             : g_takeover  ? "OpenGL owns the frame"
                           : "indicator strip only, Glide owns the frame");
    return 1;
}

HDC u9gl_device_context(void) { return g_dc; }

HGLRC u9gl_render_context(void) { return g_rc; }

static int g_displayClosed;
static int g_placementRebaseline;

static const char *msg_name(UINT m)
{
    switch (m) {
    case WM_MOUSEACTIVATE:  return "WM_MOUSEACTIVATE";
    case WM_ACTIVATE:       return "WM_ACTIVATE";
    case WM_ACTIVATEAPP:    return "WM_ACTIVATEAPP";
    case WM_NCACTIVATE:     return "WM_NCACTIVATE";
    case WM_SETFOCUS:       return "WM_SETFOCUS";
    case WM_KILLFOCUS:      return "WM_KILLFOCUS";
    case WM_LBUTTONDOWN:    return "WM_LBUTTONDOWN";
    case WM_RBUTTONDOWN:    return "WM_RBUTTONDOWN";
    case WM_CAPTURECHANGED: return "WM_CAPTURECHANGED";
    case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
    default:                return NULL;
    }
}

static void window_label(HWND h, char *out, int n)
{
    out[0] = 0;
    if (!h) { lstrcpynA(out, "none", n); return; }
    if (h == g_wnd)    { lstrcpynA(out, "OURS", n); return; }
    if (h == g_parent) { lstrcpynA(out, "GAME", n); return; }
    GetClassNameA(h, out, n);
}

LRESULT CALLBACK u9gl_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static unsigned logged;
    const char *name = msg_name(msg);

    if (msg == WM_NCHITTEST && u9cfg_int("gl_mouse_passthrough", 1)) {
        if (g_frameOn) {
            LRESULT r = DefWindowProcA(hwnd, msg, wp, lp);
            return r == HTCLIENT ? HTTRANSPARENT : r;
        }
        return HTTRANSPARENT;
    }
    if (g_frameOn) {
        switch (msg) {
        case WM_MOVE:
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) sync_game_window("moved");
            break;
        case WM_EXITSIZEMOVE: {
            RECT wr;
            if (GetWindowRect(hwnd, &wr)) {
                g_userPlaced = 1;
                g_userX = (int)wr.left;
                g_userY = (int)wr.top;
                u9log_line("GLWIN frame: the player moved the window to %d,%d", g_userX,
                           g_userY);
            }
            g_placementRebaseline = 1;
            break;
        }
        default:
            break;
        }
    }

    if (name && msg != WM_WINDOWPOSCHANGING && logged < 80) {
        char fg[48];
        logged++;
        window_label(GetForegroundWindow(), fg, sizeof fg);
        u9log_line("GLFOCUS our window got %s wParam=0x%X lParam=0x%X; foreground %s;"
                   " frame %u", name, (unsigned)wp, (unsigned)lp, fg, u9log_frame());
        u9log_flush();
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void watch_focus(void)
{
    static HWND lastFg = (HWND)(intptr_t)-1, lastAct = (HWND)(intptr_t)-1,
                lastCap = (HWND)(intptr_t)-1;
    static unsigned logged;
    HWND fg = GetForegroundWindow(), act = GetActiveWindow(), cap = GetCapture();

    if (fg == lastFg && act == lastAct && cap == lastCap) return;
    if (logged < 80) {
        char a[48], b[48], c[48];
        logged++;
        window_label(fg, a, sizeof a);
        window_label(act, b, sizeof b);
        window_label(cap, c, sizeof c);
        u9log_line("GLFOCUS frame %u: foreground %s, active %s, capture %s%s;"
                   " threads: ours %lu, game %lu, this %lu",
                   u9log_frame(), a, b, c,
                   g_displayClosed ? " (display closed)" : "",
                   g_wnd ? GetWindowThreadProcessId(g_wnd, NULL) : 0ul,
                   g_parent ? GetWindowThreadProcessId(g_parent, NULL) : 0ul,
                   GetCurrentThreadId());
        u9log_flush();
    }
    lastFg = fg; lastAct = act; lastCap = cap;
}

static int g_displayClosed, g_placementRebaseline;

void u9gl_display_close(void)
{
    g_displayClosed = 1;
    g_surfaceActive = 0;
    if (g_wnd) ShowWindow(g_wnd, SW_HIDE);
    u9log_line("GLWIN display closed for a cutscene (slot 52, as grSstWinClose):"
               " window hidden");
}

void u9gl_display_open(void)
{
    if (!g_displayClosed) return;
    g_displayClosed = 0;
    g_surfaceActive = 0;
    g_placementRebaseline = 1;
    u9log_line("GLWIN display reopened (CreateDevice, as grSstWinOpen): the next"
               " present places the window again");
}

static void watch_placement(void *renderer)
{
    static int      seen, logged, pending;
    static int      lastSW, lastSH, lastVis, lastIcon, lastTop;
    static RECT     lastWR, lastCR;
    RECT wr = {0, 0, 0, 0}, cr = {0, 0, 0, 0};
    int  sw, sh, vis, icon, top, suspended, changed;

    if (!g_wnd || !g_detached || !g_surfaceActive) return;
    if (g_placementRebaseline) { g_placementRebaseline = 0; seen = 0; }

    sw   = GetSystemMetrics(SM_CXSCREEN);
    sh   = GetSystemMetrics(SM_CYSCREEN);
    GetWindowRect(g_wnd, &wr);
    GetClientRect(g_wnd, &cr);
    vis  = IsWindowVisible(g_wnd) != 0;
    icon = IsIconic(g_wnd) != 0;
    top  = (GetWindowLongA(g_wnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    suspended = renderer &&
        *(const unsigned char *)((const char *)renderer + U9_OFF_DISABLED_GUARD);

    changed = !seen || sw != lastSW || sh != lastSH || vis != lastVis ||
              icon != lastIcon || top != lastTop ||
              !EqualRect(&wr, &lastWR) || !EqualRect(&cr, &lastCR);
    if (changed) {
        int expected = seen && u9log_frame() <= g_placementExpectUntil;
        if (logged < 40) {
            logged++;
            u9log_line("GLWIN placement %s at frame %u: screen %dx%d; ours %dx%d at"
                       " %d,%d, client %dx%d, %s%s%s; foreground 0x%08X, game"
                       " 0x%08X; renderer %s",
                       !seen ? "baseline" : expected ? "restored" : "CHANGED",
                       u9log_frame(), sw, sh,
                       (int)(wr.right - wr.left), (int)(wr.bottom - wr.top),
                       (int)wr.left, (int)wr.top, (int)cr.right, (int)cr.bottom,
                       vis ? "visible" : "HIDDEN", icon ? ", MINIMISED" : "",
                       top ? ", topmost" : ", NOT topmost",
                       (unsigned)(uintptr_t)GetForegroundWindow(),
                       (unsigned)(uintptr_t)g_parent,
                       suspended ? "suspended" : "running");
            if (seen && !expected) u9gl_probe_all_windows("placement changed");
            u9log_flush();
        }
        if (seen && !expected) pending = 1;
        lastSW = sw; lastSH = sh; lastVis = vis; lastIcon = icon; lastTop = top;
        lastWR = wr; lastCR = cr;
        seen = 1;
    }
    if (pending && !suspended && u9cfg_int("gl_replace_on_change", 0)) {
        pending = 0;
        g_placementExpectUntil = u9log_frame() + 2;
        g_surfaceActive = 0;
        u9log_line("GLWIN re-placing our window at frame %u - something moved it"
                   " (gl_replace_on_change=0 disables)", u9log_frame());
    }
}

static void follow_focus(void)
{
    static unsigned logged;
    HWND  fg;
    DWORD pid = 0;
    int   inFront;

    if (!g_wnd || !g_detached || !g_surfaceActive || g_displayClosed) return;
    if (!u9cfg_int("gl_topmost", 1) || !u9cfg_int("gl_topmost_follows_focus", 1))
        return;

    fg = GetForegroundWindow();
    if (!fg) return;
    GetWindowThreadProcessId(fg, &pid);
    inFront = pid == GetCurrentProcessId();
    if (!inFront && g_belowForeground && g_parent && u9gl_background_on() &&
        GetWindow(g_wnd, GW_HWNDNEXT) != g_parent)
        SetWindowPos(g_parent, g_wnd, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (inFront == !g_belowForeground) return;

    g_placementExpectUntil = u9log_frame() + 2;
    SetWindowPos(g_wnd, inFront ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!inFront && g_parent && u9gl_background_on())
        SetWindowPos(g_parent, g_wnd, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    g_belowForeground = !inFront;
    if (logged < 40) {
        char a[48];
        logged++;
        window_label(fg, a, sizeof a);
        u9log_line("GLFOCUS frame %u: %s (foreground %s)", u9log_frame(),
                   inFront ? "the game is in front again - our window back in the"
                             " topmost band"
                           : "another program is in front - our window drops to"
                             " the bottom so the window with the keyboard shows",
                   a);
        u9log_flush();
    }
}

static void sync_game_window(const char *why)
{
    static unsigned logged;
    RECT cr, gr;
    POINT tl = {0, 0};

    if (!g_parent || !g_wnd || !IsWindow(g_parent)) return;
    if (!GetClientRect(g_wnd, &cr) || !ClientToScreen(g_wnd, &tl)) return;
    if (!GetWindowRect(g_parent, &gr)) return;
    if (gr.left == tl.x && gr.top == tl.y && gr.right - gr.left == cr.right &&
        gr.bottom - gr.top == cr.bottom)
        return;
    if (logged < 30) {
        logged++;
        u9log_line("GLWIN frame: game window %ldx%ld at %ld,%ld -> %ldx%ld at %ld,%ld (%s)",
                   gr.right - gr.left, gr.bottom - gr.top, gr.left, gr.top,
                   cr.right, cr.bottom, tl.x, tl.y, why);
    }
    SetWindowPos(g_parent, NULL, tl.x, tl.y, cr.right, cr.bottom,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static int  g_clipOn;
static RECT g_clipRect;

static void release_cursor(const char *why)
{
    static unsigned logged;
    RECT cur;

    if (!g_clipOn) return;
    if (GetClipCursor(&cur) && EqualRect(&cur, &g_clipRect)) ClipCursor(NULL);
    g_clipOn = 0;
    if (logged < 20) {
        logged++;
        u9log_line("GLMOUSE frame %u: the cursor is free again (%s)", u9log_frame(), why);
    }
}

static void confine_cursor(void)
{
    static int      enabled = -1;
    static unsigned logged;
    HWND  fg;
    DWORD pid = 0;
    RECT  rc, cur;
    POINT tl, br;

    if (enabled < 0) enabled = u9cfg_int("gl_confine_cursor", 1) != 0;
    if (!enabled) return;
    if (!g_parent || !IsWindowVisible(g_parent)) { release_cursor("no game window"); return; }
    fg = GetForegroundWindow();
    if (!fg) return;
    GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) { release_cursor("another program is in front"); return; }
    if (!GetClientRect(g_parent, &rc)) return;
    tl.x = rc.left;  tl.y = rc.top;
    br.x = rc.right; br.y = rc.bottom;
    if (!ClientToScreen(g_parent, &tl) || !ClientToScreen(g_parent, &br)) return;
    SetRect(&rc, tl.x, tl.y, br.x, br.y);
    if (IsRectEmpty(&rc)) return;
    if (g_clipOn && EqualRect(&rc, &g_clipRect) && GetClipCursor(&cur) && EqualRect(&cur, &rc))
        return;
    if (!ClipCursor(&rc)) return;
    if (logged < 20) {
        logged++;
        u9log_line("GLMOUSE frame %u: the cursor is confined to the game, %ldx%ld at %ld,%ld%s",
                   u9log_frame(), rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top,
                   g_clipOn ? " (put back - something had cleared it)" : "");
    }
    g_clipRect = rc;
    g_clipOn = 1;
}

int u9gl_own_present(void *self)
{
    if (!self || self != g_boundRenderer || !g_rc)
        return 0;

    resolve_takeover();
    poll_toggle();
    poll_mark();
    poll_filter();
    watch_focus();
    follow_focus();
    confine_cursor();
    if (g_displayClosed) {
        u9gl_discard_frame();
        return 0;
    }
    watch_placement(self);

    if (!g_surfaceActive) {
        if (!u9gl_activate_surface())
            return 0;
        g_surfaceActive = 1;
    }

    if (!g_takeover && !g_detached) {
        u9gl_discard_frame();
        if (g_wnd) ShowWindow(g_wnd, SW_HIDE);
        return 0;
    }

    {
        int fw, fh;
        u9glpost_begin(g_surfW, g_surfH, &fw, &fh);
        g_frameW = fw;
        g_frameH = fh;
    }
    u9gl_set_surface(g_frameW, g_frameH, g_surfW > 0 ? g_frameW / g_surfW : 1);
    glViewport(0, 0, g_frameW, g_frameH);
    {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        const char *fog = *(const char *const *)
                          ((const char *)self + U9_OFF_FOG_OBJECT);
        unsigned c = fog ? *(const unsigned *)(fog + U9_FOGOBJ_COLOUR)
                         : U9_CLEAR_NO_FOG;
        static unsigned announced;
        if (announced != c) {
            announced = c;
            u9log_line("GLCLEAR colour 0x%08X (%s)", c,
                       fog ? "fogObject+4" : "no fog object, 0xFF808080");
        }
        r = (float)((c >> 16) & 0xFF) / 255.0f;
        g = (float)((c >>  8) & 0xFF) / 255.0f;
        b = (float)( c        & 0xFF) / 255.0f;

        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        {
            unsigned ew = *(unsigned *)((char *)self + U9_OFF_WIDTH);
            unsigned eh = *(unsigned *)((char *)self + U9_OFF_HEIGHT);
            int vw, vh, vx, vy;
            u9gl_picture_box(ew, eh, g_frameW, g_frameH, &vx, &vy, &vw, &vh);
            glEnable(GL_SCISSOR_TEST);
            glScissor(vx, vy, vw, vh);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    {
        int hold = u9cfg_int("hold_context", 1);
        if (!hold) wglMakeCurrent(g_dc, g_rc);

        if (u9cfg_int("frame_work", 1)) {
            u9gl_flush_frame(*(unsigned *)((char *)self + U9_OFF_WIDTH),
                             *(unsigned *)((char *)self + U9_OFF_HEIGHT));
            u9shot_before_post();
            u9glpost_end(g_surfW, g_surfH);
            u9shot_after_post(g_surfW, g_surfH);
            u9metrics_overlay(g_surfW, g_surfH);
            u9console_draw(g_surfW, g_surfH);
        }
        u9glpost_end(g_surfW, g_surfH);

        if (!hold) wglMakeCurrent(NULL, NULL);
    }

    u9gl_dump_poll(g_glFrames);
    u9selfsample_poll();
    if (u9cfg_int("present", 1)) {
        BOOL swapped = SwapBuffers(g_dc);
        u9metrics_present(swapped ? 1 : 0);
    }
    u9metrics_frame_end();
    u9telemetry_frame_end();

    if (g_glFrames < 3 || (g_glFrames % 300) == 0) {
        GLenum   err = glGetError();
        unsigned tris, runs, dropped;
        float    zMin, zMax;
        u9gl_raster_stats(&tris, &runs, &dropped, &zMin, &zMax);
        u9log_line("GLREND frame %d presented, glGetError=0x%04X, "
                   "runs=%u dropped=%u z=%.3f..%.3f",
                   g_glFrames, (unsigned)err, runs, dropped, zMin, zMax);
    }
    g_glFrames++;
    return g_detached ? 0 : 1;
}

#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif


static unsigned g_dumpSeq;

void u9feed_glide_calls_report(const char *when);

void u9gl_dump_frame(const char *why)
{
    int w = g_surfW, h = g_surfH;
    unsigned rowBytes, dataBytes;
    unsigned char *px;
    char path[MAX_PATH];
    HANDLE f;
    DWORD wrote;
    unsigned char hdr[54];
    unsigned fileSize;

    u9feed_glide_calls_report(why);
    if (!g_rc || w <= 0 || h <= 0) return;

    rowBytes  = ((unsigned)w * 3u + 3u) & ~3u;
    dataBytes = rowBytes * (unsigned)h;
    px = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dataBytes);
    if (!px) return;

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, px);

    if (!GetTempPathA(sizeof(path), path)) { HeapFree(GetProcessHeap(), 0, px); return; }
    wsprintfA(path + lstrlenA(path), "u9gl_frame_%u.bmp", g_dumpSeq++);

    fileSize = 54u + dataBytes;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    *(unsigned *)(hdr + 2)  = fileSize;
    *(unsigned *)(hdr + 10) = 54;
    *(unsigned *)(hdr + 14) = 40;
    *(int *)(hdr + 18)      = w;
    *(int *)(hdr + 22)      = h;
    *(unsigned short *)(hdr + 26) = 1;
    *(unsigned short *)(hdr + 28) = 24;
    *(unsigned *)(hdr + 34) = dataBytes;

    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, hdr, sizeof(hdr), &wrote, NULL);
        WriteFile(f, px, dataBytes, &wrote, NULL);
        CloseHandle(f);
        u9log_line("GLDUMP %s -> %s (%dx%d)", why, path, w, h);
        u9gl_log_runs();
    } else {
        u9log_line("GLDUMP failed to create %s, err=%lu", path, GetLastError());
    }
    HeapFree(GetProcessHeap(), 0, px);
}

void u9watch_revive(void);
void u9watch_stream(int frame);
void u9watch_dump_all(void);

static int      g_dumpTimed;
static unsigned g_dumpAt;

void u9gl_dump_after(unsigned seconds)
{
    g_dumpAt = u9time_now() + seconds * 1000u;
    g_dumpTimed = 1;
    u9log_line("GLDUMP timed: in %u s", seconds);
}

void u9gl_dump_poll(int frame)
{
    if (u9keys_pressed(VK_F9)) u9gl_dump_frame("F9");
    if (g_dumpTimed && (LONG)(u9time_now() - g_dumpAt) >= 0) {
        g_dumpTimed = 0;
        u9gl_dump_frame("timed");
    }
    u9watch_stream(frame);
    if (u9keys_pressed(VK_F6)) u9watch_dump_all();
    if (u9keys_pressed(VK_F8)) u9watch_revive();
    {
        int at = u9cfg_int("dump_frame", 600);
        if (at > 0 && frame == at) u9gl_dump_frame("dump_frame");
    }
}
