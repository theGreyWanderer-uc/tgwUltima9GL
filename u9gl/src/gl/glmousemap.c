#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../common/lifecycle.h"
#include "../injection/config.h"
#include "glmousemap.h"

void u9log_line(const char *fmt, ...);
int  u9gl_picture_screen_rect(RECT *out);

#define APP_OBJECT     0x00850228u
#define APP_HWND       0x04u
#define APP_CAMERA     0xBCu
#define CAM_SCALED     0x108u
#define CAM_SCALE_X    0x10Cu
#define CAM_SCALE_Y    0x110u

typedef BOOL (WINAPI *SetCursorPosFn)(int, int);

static int            g_enabled;
static SetCursorPosFn o_setCursor;
static HHOOK          g_hook;
static int            g_hookState;

static int   g_active;
static HWND  g_game;
static POINT g_gameOrigin;
static RECT  g_pic;
static float g_readX, g_readY;
static float g_writeX, g_writeY;

static unsigned g_msgsMapped, g_setsMapped, g_setsPassed;

static const uintptr_t GRID_CALLERS[] = {
    0x00497493u,
    0x00498FF8u, 0x0049928Eu, 0x004993F3u, 0x0049958Du, 0x00499744u
};

static int read_ok(uintptr_t p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery((LPCVOID)p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (uintptr_t)mbi.BaseAddress + mbi.RegionSize >= p + n;
}

static uint32_t rd(uintptr_t p) { return read_ok(p, 4) ? *(const volatile uint32_t *)p : 0; }

static float rdf(uintptr_t p)
{
    float f = 0.0f;
    if (read_ok(p, 4)) memcpy(&f, (const void *)p, 4);
    return f;
}

static int is_grid_caller(uintptr_t ra)
{
    unsigned i;
    for (i = 0; i < sizeof GRID_CALLERS / sizeof GRID_CALLERS[0]; i++)
        if (GRID_CALLERS[i] == ra) return 1;
    return 0;
}

static int iround(float f) { return f < 0.0f ? (int)(f - 0.5f) : (int)(f + 0.5f); }

static BOOL WINAPI h_setCursor(int x, int y)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    if (g_active && is_grid_caller(ra)) {
        float gx = (float)(x - g_gameOrigin.x) / g_writeX;
        float gy = (float)(y - g_gameOrigin.y) / g_writeY;
        int nx = g_pic.left + iround(gx * (float)(g_pic.right - g_pic.left) / 640.0f);
        int ny = g_pic.top  + iround(gy * (float)(g_pic.bottom - g_pic.top) / 480.0f);
        if (++g_setsMapped <= 6)
            u9log_line("MOUSEMAP cursor put back by 0x%08X: %d,%d -> %d,%d (grid %.1f,%.1f)",
                       (unsigned)(ra - 6u), x, y, nx, ny, gx, gy);
        x = nx;
        y = ny;
    } else if (g_active) {
        g_setsPassed++;
    }
    return o_setCursor(x, y);
}

static LRESULT CALLBACK hook_getmsg(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && (wp & PM_REMOVE) && lp && g_active) {
        MSG *m = (MSG *)lp;
        if (m->message >= WM_MOUSEFIRST && m->message <= 0x209u && m->hwnd == g_game) {
            int   sx = (short)LOWORD(m->lParam) + g_gameOrigin.x;
            int   sy = (short)HIWORD(m->lParam) + g_gameOrigin.y;
            int   pw = g_pic.right - g_pic.left, ph = g_pic.bottom - g_pic.top;
            float gx = (float)(sx - g_pic.left) * 640.0f / (float)pw;
            float gy = (float)(sy - g_pic.top)  * 480.0f / (float)ph;
            int   ex, ey;
            if (gx < 0.0f) gx = 0.0f;
            if (gy < 0.0f) gy = 0.0f;
            if (gx > 639.9f) gx = 639.9f;
            if (gy > 479.9f) gy = 479.9f;
            ex = (int)(((float)(int)gx + 0.5f) * g_readX);
            ey = (int)(((float)(int)gy + 0.5f) * g_readY);
            if (ex > 32767) ex = 32767;
            if (ey > 32767) ey = 32767;
            m->lParam = MAKELPARAM((WORD)(short)ex, (WORD)(short)ey);
            g_msgsMapped++;
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static void hook_remove(void)
{
    g_active = 0;
    if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = NULL; }
}

static void report(void)
{
    if (!g_enabled) return;
    u9log_line("MOUSEMAP session: %u mouse messages mapped onto the picture, %u cursor"
               " put-backs mapped, %u other SetCursorPos calls passed unchanged",
               g_msgsMapped, g_setsMapped, g_setsPassed);
}

static int near1(float a, float b) { float d = a - b; return d > -1.0f && d < 1.0f; }

void u9gl_mousemap_tick(void)
{
    static unsigned logged;
    uintptr_t app, cam;
    HWND   w;
    RECT   cr, pic;
    POINT  tl = {0, 0};
    float  rx, ry, wx, wy;
    int    active, pw, ph;

    if (!g_enabled || !o_setCursor) return;

    app = rd(APP_OBJECT);
    w   = app ? (HWND)(uintptr_t)rd(app + APP_HWND) : NULL;
    cam = app ? rd(app + APP_CAMERA) : 0;
    if (!w || !IsWindow(w) || !cam) { g_active = 0; return; }

    if (!g_hookState) {
        HMODULE mod = NULL;
        DWORD tid = GetWindowThreadProcessId(w, NULL);
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(void *)hook_getmsg, &mod);
        g_hook = SetWindowsHookExA(WH_GETMESSAGE, hook_getmsg, mod, tid);
        if (!g_hook) {
            g_hookState = -1;
            u9log_line("MOUSEMAP SetWindowsHookEx FAILED, err=%lu - the mouse is measured"
                       " on the game window as stock", (unsigned long)GetLastError());
        } else {
            g_hookState = 1;
            u9life_push("mouse map message hook", hook_remove);
        }
    }
    if (g_hookState != 1) { g_active = 0; return; }

    wx = rdf(cam + CAM_SCALE_X);
    wy = rdf(cam + CAM_SCALE_Y);
    if (!(wx > 0.01f && wx < 100.0f && wy > 0.01f && wy < 100.0f)) { g_active = 0; return; }
    if (read_ok(cam + CAM_SCALED, 1) && *(const volatile uint8_t *)(cam + CAM_SCALED)) {
        rx = wx; ry = wy;
    } else {
        rx = 1.0f; ry = 1.0f;
    }
    if (!GetClientRect(w, &cr) || !ClientToScreen(w, &tl) ||
        !u9gl_picture_screen_rect(&pic)) {
        g_active = 0;
        return;
    }
    pw = pic.right - pic.left;
    ph = pic.bottom - pic.top;
    if (pw < 16 || ph < 16) { g_active = 0; return; }

    active = !(pic.left == tl.x && pic.top == tl.y &&
               near1((float)pw, 640.0f * rx) && near1((float)ph, 480.0f * ry) &&
               near1(rx, wx) && near1(ry, wy));

    g_game = w;
    g_gameOrigin = tl;
    g_pic = pic;
    g_readX = rx;  g_readY = ry;
    g_writeX = wx; g_writeY = wy;
    if (active != g_active && logged < 20) {
        logged++;
        if (active)
            u9log_line("MOUSEMAP on: the engine measures the mouse at %.0fx%.0f on its window"
                       " (%ldx%ld at %ld,%ld), the picture is %dx%d at %ld,%ld - the mouse is"
                       " measured on the picture", 640.0f * rx, 480.0f * ry, cr.right,
                       cr.bottom, tl.x, tl.y, pw, ph, pic.left, pic.top);
        else
            u9log_line("MOUSEMAP off: the picture is the engine's window, at its size");
    }
    g_active = active;
}

void u9gl_mousemap_narrow_clip(RECT *rc)
{
    RECT r;
    if (!g_active || !rc) return;
    if (IntersectRect(&r, rc, &g_pic)) *rc = r;
}

void u9gl_mousemap_install(void)
{
    static uint8_t     expect[4];
    static U9PatchSpec spec;
    HMODULE  user = GetModuleHandleA("user32.dll");
    FARPROC  real = user ? GetProcAddress(user, "SetCursorPos") : 0;
    uintptr_t at  = u9sym(U9SYM_IAT_SETCURSORPOS);
    uint32_t held;
    void    *prev;

    g_enabled = u9cfg_int("gl_mouse_map", 1) != 0;
    if (!g_enabled) {
        u9log_line("MOUSEMAP off by config - the mouse is measured on the game window, as"
                   " stock (an engine size the picture does not have can spin the camera)");
        return;
    }
    if (!real || !at) {
        u9log_line("MOUSEMAP NOT installed: SetCursorPos or its import slot not resolved");
        g_enabled = 0;
        return;
    }
    held = *(const volatile uint32_t *)at;
    if (held != (uint32_t)(uintptr_t)real) {
        HMODULE owner = 0;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCSTR)(uintptr_t)held, &owner) || !owner ||
            owner == GetModuleHandleA(0)) {
            u9log_line("MOUSEMAP NOT installed: u9.exe's SetCursorPos slot holds %08X, which"
                       " no loaded module owns", (unsigned)held);
            g_enabled = 0;
            return;
        }
        u9log_line("MOUSEMAP u9.exe's SetCursorPos already goes to %08X - chained through it",
                   (unsigned)held);
    }
    memcpy(expect, &held, 4);
    spec = (U9PatchSpec){ "MOUSEMAP_SETCURSORPOS", U9PATCH_VTABLE, U9SYM_IAT_SETCURSORPOS,
                          4, expect, 4, U9PF_EXCLUSIVE };
    prev = u9patch_install(&spec, (void *)h_setCursor);
    if (!prev) {
        u9log_line("MOUSEMAP NOT installed: the SetCursorPos slot was refused (see the"
                   " manifest)");
        g_enabled = 0;
        return;
    }
    o_setCursor = (SetCursorPosFn)prev;
    u9life_push_at_exit("mouse map report", report);
    u9log_line("MOUSEMAP installed: where the picture is not the game window at the engine's"
               " size, the mouse is measured on the picture (6 cursor put-backs mapped)");
}
