#include <windows.h>
#include "keys.h"
#include "../common/lifecycle.h"
#include "../diag/log.h"

#define KEYS_POLL_MS 10

static volatile LONG g_latched[256];
static volatile LONG g_stop;
static HANDLE        g_thread;
static int           g_tried;
static int           g_fallbackWas[256];

static int ctrl_only(void)
{
    return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
           (GetAsyncKeyState(VK_MENU)    & 0x8000) == 0;
}

static DWORD WINAPI key_thread(LPVOID unused)
{
    int was[256] = {0};
    int vk;
    (void)unused;
    while (!g_stop) {
        int ctrl = ctrl_only();
        for (vk = VK_F1; vk <= VK_F12; vk++) {
            int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (!ctrl) { was[vk] = down; continue; }
            if (down && !was[vk]) InterlockedExchange(&g_latched[vk], 1);
            was[vk] = down;
        }
        Sleep(KEYS_POLL_MS);
    }
    return 0;
}

static void keys_stop(void)
{
    g_stop = 1;
    if (g_thread) {
        WaitForSingleObject(g_thread, 100);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

static void keys_start(void)
{
    if (g_tried) return;
    g_tried = 1;
    g_thread = CreateThread(NULL, 0, key_thread, NULL, 0, NULL);
    if (g_thread) {
        u9life_push("test-key thread", keys_stop);
        u9log_line("KEYS test keys Ctrl+F1-F12 watched every %d ms on their own thread",
                   KEYS_POLL_MS);
    } else {
        u9log_line("KEYS thread not started (err %lu) - test keys are read once"
                   " per frame, and a tap during a stalled frame can be missed",
                   GetLastError());
    }
}

int u9keys_pressed(int vk)
{
    if (vk <= 0 || vk > 255) return 0;
    keys_start();
    if (g_thread)
        return InterlockedExchange(&g_latched[vk], 0) != 0;
    {
        int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        int fired = down && !g_fallbackWas[vk] && ctrl_only();
        g_fallbackWas[vk] = down;
        return fired;
    }
}
