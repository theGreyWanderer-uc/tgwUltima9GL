#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../common/lifecycle.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "focusprobe.h"
#include "../common/u9time.h"

void     u9log_line(const char *fmt, ...);
uint32_t u9log_frame(void);

#define APP_OBJECT      0x00850228u
#define APP_HWND        0x04u
#define APP_WINDOWS     0x3Cu
#define QUIT_REQUESTED  0x00AE7C94u
#define MODAL_OWNER     0x01325A58u

#define MAX_LINES       600u
#define STACK_DWORDS    65536u

static const struct { uint32_t ret; const char *what; } k_marks[] = {
    { 0x0073FA85u, "deactivate-stub ShowWindow(minimise)" },
    { 0x005D5D67u, "teardown DestroyWindow(game window)" },
    { 0x005D5CE6u, "teardown DestroyWindow(app+0x68)" },
    { 0x00553AC3u, "main loop teardown call" },
    { 0x0049EA37u, "WM_DESTROY handler" },
};

static int      g_wanted;
static int      g_state;
static int      g_everHooked;
static HHOOK    g_pre, g_post;
static DWORD    g_t0;
static unsigned g_lines, g_dropped;
static unsigned g_activates, g_deactivates, g_minimised;

static void flog(const char *fmt, ...)
{
    char buf[640];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("FOCUS f%u %7lu %s", (unsigned)u9log_frame(),
               (unsigned long)(u9time_now() - g_t0), buf);
}

static uintptr_t app_object(void)
{
    return readable4(APP_OBJECT) ? rd32(APP_OBJECT) : 0;
}

static HWND game_window(void)
{
    uintptr_t app = app_object();
    if (!app || !readable4(app + APP_HWND)) return NULL;
    return (HWND)(uintptr_t)rd32(app + APP_HWND);
}

static const char *msg_name(UINT m)
{
    switch (m) {
    case WM_ACTIVATE:    return "WM_ACTIVATE";
    case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
    case WM_SETFOCUS:    return "WM_SETFOCUS";
    case WM_KILLFOCUS:   return "WM_KILLFOCUS";
    case WM_SIZE:        return "WM_SIZE";
    case WM_CLOSE:       return "WM_CLOSE";
    case WM_DESTROY:     return "WM_DESTROY";
    case WM_NCDESTROY:   return "WM_NCDESTROY";
    case WM_SYSCOMMAND:  return "WM_SYSCOMMAND";
    default:             return NULL;
    }
}

static void window_text(HWND w, HWND game, char *out, size_t cap)
{
    char cls[48];
    if (!w) { snprintf(out, cap, "none"); return; }
    if (w == game) { snprintf(out, cap, "GAME"); return; }
    cls[0] = 0;
    GetClassNameA(w, cls, sizeof cls);
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        snprintf(out, cap, "%08X '%s'%s", (unsigned)(uintptr_t)w, cls,
                 pid == GetCurrentProcessId() ? "" : " (another process)");
    }
}

static void state_text(HWND game, char *out, size_t cap)
{
    uintptr_t app = app_object();
    char fg[96];
    int quit = readable4(QUIT_REQUESTED) ? (int)rd32(QUIT_REQUESTED) : -1;
    int wins = (app && readable4(app + APP_WINDOWS)) ? (int)rd32(app + APP_WINDOWS) : -1;
    int modal = readable4(MODAL_OWNER) ? (rd32(MODAL_OWNER) != 0) : -1;
    int vis = game && IsWindow(game) ? (IsWindowVisible(game) != 0) : -1;
    int icon = game && IsWindow(game) ? (IsIconic(game) != 0) : -1;

    window_text(GetForegroundWindow(), game, fg, sizeof fg);
    snprintf(out, cap, "quit=%d windows=%d modal=%d visible=%d minimised=%d fg=%s",
             quit, wins, modal, vis, icon, fg);
}

static void inside_text(char *out, size_t cap)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    const uint32_t *p = (const uint32_t *)__builtin_frame_address(0);
    const uint32_t *base = (const uint32_t *)tib->StackBase;
    size_t n = 0;
    unsigned i, k, seen = 0;

    out[0] = 0;
    for (i = 0; i < STACK_DWORDS && p + i < base; i++) {
        uint32_t v = p[i];
        for (k = 0; k < sizeof k_marks / sizeof k_marks[0]; k++) {
            if (v != k_marks[k].ret || (seen & (1u << k))) continue;
            seen |= 1u << k;
            if (n + 4 < cap)
                n += (size_t)snprintf(out + n, cap - n, "%s%08X %s",
                                      n ? "; " : "", (unsigned)v, k_marks[k].what);
        }
    }
    if (!n) snprintf(out, cap, "-");
}

static void params_text(UINT m, WPARAM wp, LPARAM lp, char *out, size_t cap)
{
    switch (m) {
    case WM_ACTIVATE:
        snprintf(out, cap, "%s%s, other %08X",
                 LOWORD(wp) == WA_INACTIVE ? "WA_INACTIVE" :
                 LOWORD(wp) == WA_ACTIVE ? "WA_ACTIVE" :
                 LOWORD(wp) == WA_CLICKACTIVE ? "WA_CLICKACTIVE" : "?",
                 HIWORD(wp) ? " (minimised)" : "", (unsigned)lp);
        break;
    case WM_ACTIVATEAPP:
        snprintf(out, cap, "%s, thread %u", wp ? "activated" : "DEactivated",
                 (unsigned)lp);
        break;
    case WM_SIZE:
        snprintf(out, cap, "%s %ux%u",
                 wp == SIZE_MINIMIZED ? "SIZE_MINIMIZED" :
                 wp == SIZE_RESTORED ? "SIZE_RESTORED" :
                 wp == SIZE_MAXIMIZED ? "SIZE_MAXIMIZED" : "other",
                 (unsigned)LOWORD(lp), (unsigned)HIWORD(lp));
        break;
    case WM_SYSCOMMAND:
        snprintf(out, cap, "%s (0x%04X)",
                 (wp & 0xFFF0) == SC_CLOSE ? "SC_CLOSE" :
                 (wp & 0xFFF0) == SC_MINIMIZE ? "SC_MINIMIZE" :
                 (wp & 0xFFF0) == SC_RESTORE ? "SC_RESTORE" :
                 (wp & 0xFFF0) == SC_KEYMENU ? "SC_KEYMENU" : "other",
                 (unsigned)(wp & 0xFFF0));
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        snprintf(out, cap, "other %08X", (unsigned)wp);
        break;
    default:
        snprintf(out, cap, "wParam %X lParam %X", (unsigned)wp, (unsigned)lp);
    }
}

static void log_message(const char *when, HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    const char *name = msg_name(m);
    HWND game;
    char who[96], params[96], state[200], inside[200];

    if (!name) return;
    if (m == WM_SYSCOMMAND && (wp & 0xFFF0) == SC_KEYMENU) return;
    if (g_lines >= MAX_LINES) { g_dropped++; return; }
    g_lines++;

    game = game_window();
    window_text(w, game, who, sizeof who);
    params_text(m, wp, lp, params, sizeof params);
    state_text(game, state, sizeof state);
    inside_text(inside, sizeof inside);
    flog("%-6s %s %s: %s | %s | inside: %s", when, name, who, params, state, inside);
}

static LRESULT CALLBACK hook_pre(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && lp) {
        const CWPSTRUCT *c = (const CWPSTRUCT *)lp;
        if (c->message == WM_ACTIVATE && c->hwnd == game_window()) {
            if (LOWORD(c->wParam) == WA_INACTIVE) g_deactivates++;
            else g_activates++;
        }
        if (c->message == WM_SIZE && c->wParam == SIZE_MINIMIZED) g_minimised++;
        log_message("before", c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(g_pre, code, wp, lp);
}

static LRESULT CALLBACK hook_post(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && lp) {
        const CWPRETSTRUCT *c = (const CWPRETSTRUCT *)lp;
        if (c->message == WM_ACTIVATE || c->message == WM_CLOSE ||
            c->message == WM_SYSCOMMAND)
            log_message("after", c->hwnd, c->message, c->wParam, c->lParam);
    }
    return CallNextHookEx(g_post, code, wp, lp);
}

static void focus_remove(void)
{
    if (g_post) { UnhookWindowsHookEx(g_post); g_post = NULL; }
    if (g_pre)  { UnhookWindowsHookEx(g_pre);  g_pre = NULL; }
    g_state = 0;
}

void u9focus_install(void)
{
    g_wanted = u9cfg_int("diag_focus_watch", 0);
    if (!g_wanted) return;
    g_t0 = u9time_now();
    flog("focus watch: on; the message hooks go in on the first frame the game"
         " window exists");
}

void u9focus_tick(void)
{
    HWND game;
    HMODULE mod = NULL;
    DWORD tid;

    if (!g_wanted || g_state) return;
    game = game_window();
    if (!game || !IsWindow(game)) return;

    tid = GetWindowThreadProcessId(game, NULL);
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)hook_pre, &mod);
    g_pre  = SetWindowsHookExA(WH_CALLWNDPROC, hook_pre, mod, tid);
    g_post = SetWindowsHookExA(WH_CALLWNDPROCRET, hook_post, mod, tid);
    if (!g_pre || !g_post) {
        DWORD err = GetLastError();
        focus_remove();
        g_state = -1;
        flog("focus watch: SetWindowsHookEx FAILED, err=%lu - nothing will be logged",
             (unsigned long)err);
        return;
    }
    g_state = 1;
    g_everHooked = 1;
    u9life_push("focus watch hooks", focus_remove);
    {
        char state[200];
        state_text(game, state, sizeof state);
        flog("focus watch: hooked the game window %08X's thread %lu (this thread %lu);"
             " %s", (unsigned)(uintptr_t)game, (unsigned long)tid,
             (unsigned long)GetCurrentThreadId(), state);
    }
}

void u9focus_report(void)
{
    if (!g_wanted) return;
    u9log_line("FOCUS watch: %s; game window WM_ACTIVATE %u active / %u inactive,"
               " %u minimise(s); %u lines, %u dropped over the cap of %u",
               g_everHooked ? "hooked" : g_state < 0 ? "hook FAILED" : "never hooked",
               g_activates, g_deactivates, g_minimised, g_lines, g_dropped,
               (unsigned)MAX_LINES);
}
