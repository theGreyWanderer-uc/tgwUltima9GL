#include <windows.h>
#include <stdio.h>
#include "../diag/log.h"
#include "../common/u9time.h"
#include "console.h"

static int      g_armed;
static DWORD    g_due;
static HANDLE   g_proc;
static DWORD    g_started;

static int sampler_path(char *out, DWORD cap)
{
    HMODULE self = NULL;
    char *slash;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&sampler_path, &self) || !GetModuleFileNameA(self, out, cap))
        return 0;
    slash = strrchr(out, '\\');
    if (!slash || (DWORD)(slash - out) + 14 >= cap) return 0;
    lstrcpyA(slash + 1, "u9sample.exe");
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

void u9selfsample_after(unsigned seconds, void (*out)(const char *fmt, ...))
{
    char path[MAX_PATH];
    if (g_proc) { out("a sample is already running - wait for its notice"); return; }
    if (!sampler_path(path, sizeof path)) {
        out("no u9sample.exe beside u9log.dll - stage it (tools/stage.ps1) into the u9gl folder");
        return;
    }
    g_due = u9time_now() + seconds * 1000u;
    g_armed = 1;
    out("CPU sample in %u s, about 7 s long: close the console and stay where the frame rate is wrong;"
        " a notice says when it is done", seconds);
}

void u9selfsample_poll(void)
{
    if (g_armed && (LONG)(u9time_now() - g_due) >= 0) {
        char path[MAX_PATH], cmd[MAX_PATH + 32];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        g_armed = 0;
        if (!sampler_path(path, sizeof path)) { u9console_notice("sample: u9sample.exe is gone"); return; }
        _snprintf(cmd, sizeof cmd, "\"%s\" 1 0 4000", path);
        cmd[sizeof cmd - 1] = 0;
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (!CreateProcessA(path, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            u9console_notice("sample: could not start u9sample.exe (error %lu)", GetLastError());
            return;
        }
        CloseHandle(pi.hThread);
        g_proc = pi.hProcess;
        g_started = u9time_now();
        u9log_line("SAMPLE started %s (pid %lu)", path, (unsigned long)pi.dwProcessId);
        u9console_notice("CPU sample started - keep playing for about 7 s");
    }
    if (g_proc && WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(g_proc, &code);
        CloseHandle(g_proc);
        g_proc = NULL;
        u9console_notice("CPU sample finished after %.1f s: %%TEMP%%\\u9gl_sample_%lu.txt",
                         (double)(u9time_now() - g_started) / 1000.0, (unsigned long)GetCurrentProcessId());
        u9log_line("SAMPLE finished, exit code %lu", (unsigned long)code);
    }
}
