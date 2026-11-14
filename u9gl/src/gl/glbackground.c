#include <windows.h>
#include <stdint.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../common/lifecycle.h"
#include "../injection/config.h"
#include "glbackground.h"

void u9log_line(const char *fmt, ...);

#define APP_OBJECT        0x00850228u
#define APP_HWND          0x04u
#define QUIT_REQUESTED    0x00AE7C94u
#define FOCUS_SLOT52_RET  0x005C3C2Fu
#define DSBCAPS_GLOBAL    0x00008000u

static int      g_on;
static int      g_owner;
static unsigned g_minSkipped, g_minPassed, g_suspendSkipped, g_suspendKept;
static int      g_hookState;
static HHOOK    g_getMsg;
static int      g_wasBehind;
static unsigned g_mouseDropped, g_clicksSwallowed;
static int      g_cursorBumps;
static int      g_wasInFront = 1;

static int read_ok(uintptr_t p)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (uintptr_t)mbi.BaseAddress + mbi.RegionSize >= p + 4;
}

static uint32_t rd(uintptr_t p) { return read_ok(p) ? *(const volatile uint32_t *)p : 0; }

static HWND game_window(void)
{
    uintptr_t app = rd(APP_OBJECT);
    return app ? (HWND)(uintptr_t)rd(app + APP_HWND) : NULL;
}

static int game_in_play(HWND w)
{
    return rd(QUIT_REQUESTED) == 0 && w && IsWindow(w) &&
           IsWindowVisible(w) && !IsIconic(w);
}

static BOOL WINAPI h_show(HWND w, int cmd)
{
    if (g_on && g_owner && cmd == SW_SHOWMINIMIZED && w == game_window() &&
        game_in_play(w)) {
        if (++g_minSkipped <= 20)
            u9log_line("BGPLAY the game lost the focus: its minimise skipped (%u)",
                       g_minSkipped);
        return TRUE;
    }
    if (cmd == SW_SHOWMINIMIZED && ++g_minPassed <= 20)
        u9log_line("BGPLAY minimise passed on to ShowWindow (%s)",
                   !g_owner ? "slot 52 not ours" :
                   rd(QUIT_REQUESTED) ? "the game is quitting" : "the window is not in play");
    return ShowWindow(w, cmd);
}

static BOOL (WINAPI *volatile g_showPtr)(HWND, int) = h_show;

static void report(void)
{
    if (!g_on) return;
    u9log_line("BGPLAY session: minimise skipped %u, passed %u; slot 52 ridden out %u,"
               " performed %u; mouse messages dropped behind other windows %u, returning"
               " clicks swallowed %u", g_minSkipped, g_minPassed, g_suspendSkipped,
               g_suspendKept, g_mouseDropped, g_clicksSwallowed);
}

static int this_process_in_front(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return 1;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static int is_button_down(UINT m)
{
    return m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN ||
           m == WM_LBUTTONDBLCLK || m == WM_RBUTTONDBLCLK || m == WM_MBUTTONDBLCLK;
}

static int is_button_up(UINT m)
{
    return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP;
}

static LRESULT CALLBACK hook_getmsg(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && lp && g_on) {
        MSG *m = (MSG *)lp;
        if (m->message >= WM_MOUSEFIRST && m->message <= 0x209u &&
            m->hwnd == game_window()) {
            if (!this_process_in_front() && is_button_down(m->message)) {
                SetForegroundWindow(m->hwnd);
                g_clicksSwallowed++;
                if (g_clicksSwallowed <= 20)
                    u9log_line("BGPLAY a click on the game from behind: brought to the"
                               " front, %s (%u)", this_process_in_front() ? "granted" :
                               "NOT granted", g_clicksSwallowed);
                m->message = WM_NULL;
                g_wasBehind = 2;
            } else if (!this_process_in_front()) {
                g_wasBehind = 1;
                g_mouseDropped++;
                if (m->message == WM_MOUSEMOVE) SetCursor(LoadCursor(NULL, IDC_ARROW));
                m->message = WM_NULL;
            } else if (g_wasBehind && is_button_down(m->message)) {
                g_clicksSwallowed++;
                if (g_clicksSwallowed <= 20)
                    u9log_line("BGPLAY the click that brought the game back was"
                               " swallowed (%u)", g_clicksSwallowed);
                m->message = WM_NULL;
                g_wasBehind = 2;
            } else if (g_wasBehind == 2 && is_button_up(m->message)) {
                m->message = WM_NULL;
                g_wasBehind = 0;
            } else if (g_wasBehind != 2) {
                g_wasBehind = 0;
            }
        }
    }
    return CallNextHookEx(g_getMsg, code, wp, lp);
}

static void cursor_behind(int behind)
{
    if (behind && !g_cursorBumps) {
        int count = ShowCursor(TRUE);
        g_cursorBumps = 1;
        while (count < 0 && g_cursorBumps < 64) { count = ShowCursor(TRUE); g_cursorBumps++; }
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    } else if (!behind && g_cursorBumps) {
        while (g_cursorBumps > 0) { ShowCursor(FALSE); g_cursorBumps--; }
    }
}

static void hook_remove(void)
{
    if (g_getMsg) { UnhookWindowsHookEx(g_getMsg); g_getMsg = NULL; }
    cursor_behind(0);
}

void u9gl_background_tick(void)
{
    HWND w;
    HMODULE mod = NULL;
    DWORD tid;

    if (!g_on) return;
    if (g_hookState == 1) {
        int inFront = this_process_in_front();
        if (inFront != g_wasInFront) {
            g_wasInFront = inFront;
            cursor_behind(!inFront);
        }
        if (!inFront) {
            static unsigned logged;
            w = game_window();
            if (w && GetCapture() == w) {
                ReleaseCapture();
                if (++logged <= 20)
                    u9log_line("BGPLAY the game let go of the mouse behind other windows"
                               " (%u)", logged);
            }
        }
        return;
    }
    if (g_hookState) return;
    w = game_window();
    if (!w || !IsWindow(w)) return;
    tid = GetWindowThreadProcessId(w, NULL);
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)hook_getmsg, &mod);
    g_getMsg = SetWindowsHookExA(WH_GETMESSAGE, hook_getmsg, mod, tid);
    if (!g_getMsg) {
        g_hookState = -1;
        u9log_line("BGPLAY mouse filter: SetWindowsHookEx FAILED, err=%lu - the game"
                   " will still steer the cursor from behind other windows",
                   (unsigned long)GetLastError());
        return;
    }
    g_hookState = 1;
    u9life_push("background play mouse filter", hook_remove);
    u9log_line("BGPLAY mouse filter on the game window %08X's thread %lu",
               (unsigned)(uintptr_t)w, (unsigned long)tid);
}

int u9gl_background_on(void)
{
    return g_on && g_owner;
}

static int flags_patch(const char *name, U9Symbol sym, const uint8_t expect[8])
{
    uint8_t bytes[8];
    uint32_t v;
    unsigned i;
    U9PatchSpec spec;
    static U9PatchSpec specs[4];
    static uint8_t     expects[4][8];
    static unsigned    used;

    if (used >= 4) return 0;
    for (i = 0; i < 8; i++) { bytes[i] = expect[i]; expects[used][i] = expect[i]; }
    v = (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8) | ((uint32_t)bytes[6] << 16) |
        ((uint32_t)bytes[7] << 24);
    v |= DSBCAPS_GLOBAL;
    bytes[4] = (uint8_t)v; bytes[5] = (uint8_t)(v >> 8);
    bytes[6] = (uint8_t)(v >> 16); bytes[7] = (uint8_t)(v >> 24);

    spec.name = name;
    spec.mechanism = U9PATCH_STUB;
    spec.target = sym;
    spec.length = 8;
    spec.expected = expects[used];
    spec.expectedLen = 8;
    spec.flags = U9PF_EXCLUSIVE;
    specs[used] = spec;
    return u9patch_install_bytes(&specs[used++], bytes, 8);
}

void u9gl_background_install(void)
{
    static const uint8_t expShow[6] = { 0xFF, 0x15, 0x04, 0x03, 0x74, 0x00 };
    static const U9PatchSpec showSpec = {
        "BGPLAY_MINIMISE", U9PATCH_STUB, U9SYM_DEACTIVATE_SHOWWINDOW, 6, expShow, 6,
        U9PF_EXCLUSIVE
    };
    static const uint8_t expA[8]   = { 0xC7, 0x44, 0x24, 0x3C, 0xA0, 0x00, 0x01, 0x00 };
    static const uint8_t expA3[8]  = { 0xC7, 0x44, 0x24, 0x3C, 0xB0, 0x00, 0x03, 0x00 };
    static const uint8_t expB[8]   = { 0xC7, 0x44, 0x24, 0x2C, 0xA0, 0x00, 0x01, 0x00 };
    static const uint8_t expBN[8]  = { 0xC7, 0x44, 0x24, 0x2C, 0xA0, 0x01, 0x01, 0x00 };
    uint8_t call[6];
    uint32_t p = (uint32_t)(uintptr_t)&g_showPtr;
    int okShow, sound, nSound = 0;

    g_on = u9cfg_int("gl_background_play", 0);
    if (!g_on) return;
    sound = u9cfg_int("gl_background_sound", 1);

    call[0] = 0xFF; call[1] = 0x15;
    call[2] = (uint8_t)p; call[3] = (uint8_t)(p >> 8);
    call[4] = (uint8_t)(p >> 16); call[5] = (uint8_t)(p >> 24);
    okShow = u9patch_install_bytes(&showSpec, call, 6);

    if (sound) {
        nSound += flags_patch("BGPLAY_SND_A", U9SYM_SNDBUF_FLAGS_A, expA);
        nSound += flags_patch("BGPLAY_SND_A3D", U9SYM_SNDBUF_FLAGS_A3D, expA3);
        nSound += flags_patch("BGPLAY_SND_B", U9SYM_SNDBUF_FLAGS_B, expB);
        nSound += flags_patch("BGPLAY_SND_BN", U9SYM_SNDBUF_FLAGS_BNOTIFY, expBN);
    }
    u9life_push_at_exit("background play report", report);
    u9log_line("BGPLAY on: the minimise redirect %s; sound in the background %s (%d of 4"
               " buffer flags); slot 52 rides out a focus loss once it is ours",
               okShow ? "installed" : "REFUSED (see the manifest)",
               sound ? "on" : "off", nSound);
    if (!okShow) g_on = 0;
}

void u9gl_background_set_owner(int ours)
{
    g_owner = ours;
}

int u9gl_background_keep_running(uintptr_t caller)
{
    HWND w;

    if (!g_on || caller != FOCUS_SLOT52_RET) return 0;
    w = game_window();
    if (!game_in_play(w)) {
        if (++g_suspendKept <= 20)
            u9log_line("BGPLAY slot 52 from the focus path performed: %s",
                       rd(QUIT_REQUESTED) ? "the game is quitting" :
                       !w || !IsWindow(w) ? "no game window" :
                       IsIconic(w) ? "the window is minimised" : "the window is hidden");
        return 0;
    }
    if (++g_suspendSkipped <= 20)
        u9log_line("BGPLAY slot 52 from the focus path ridden out: the game keeps"
                   " drawing (%u)", g_suspendSkipped);
    return 1;
}
