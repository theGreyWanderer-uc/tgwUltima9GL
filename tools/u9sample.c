#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXMOD  512
#define MAXTHR  256
#define TOPT    3
#define MAXSAMP 20000

typedef struct { char name[64]; char path[MAX_PATH]; DWORD base, size; } Mod;
static Mod mods[MAXMOD];
static int nmods;

typedef struct {
    DWORD tid; HANDLE h; DWORD start;
    ULONGLONG c0, c1, c2, c3;
    int prio;
} Thr;
static Thr thr[MAXTHR];
static int nthr;

static DWORD eips[TOPT][MAXSAMP];
static int   neip[TOPT];
static WORD  fpucw[TOPT];
static DWORD mxcsr[TOPT];

static int g_delayMs = 40000;
static int g_samples = 3000;

static FILE *g_out;
#define P(...) do { printf(__VA_ARGS__); if (g_out) fprintf(g_out, __VA_ARGS__); } while (0)

typedef LONG (WINAPI *NtQIT)(HANDLE, int, PVOID, ULONG, PULONG);

static ULONGLONG ft(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static ULONGLONG cpu100ns(HANDLE h)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
    return ft(k) + ft(u);
}

static int modidx(DWORD a)
{
    int i;
    for (i = 0; i < nmods; i++)
        if (a >= mods[i].base && a < mods[i].base + mods[i].size) return i;
    return -1;
}

static void where(DWORD a, char *buf, size_t n)
{
    int m = modidx(a);
    if (m < 0) snprintf(buf, n, "0x%08lX (no module)", (unsigned long)a);
    else snprintf(buf, n, "%s+0x%lX", mods[m].name, (unsigned long)(a - mods[m].base));
}

static int cmp_dword(const void *a, const void *b)
{
    DWORD x = *(const DWORD *)a, y = *(const DWORD *)b;
    return x < y ? -1 : x > y;
}

typedef struct { DWORD key; int n; } Cnt;
static int cmp_cnt(const void *a, const void *b) { return ((const Cnt *)b)->n - ((const Cnt *)a)->n; }

static int cmp_thr_cpu(const void *a, const void *b)
{
    ULONGLONG x = ((const Thr *)a)->c3 - ((const Thr *)a)->c2;
    ULONGLONG y = ((const Thr *)b)->c3 - ((const Thr *)b)->c2;
    return x < y ? 1 : x > y ? -1 : 0;
}

static DWORD find_u9(const DWORD *done, int ndone)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    DWORD pid = 0;
    int i, skip;
    pe.dwSize = sizeof(pe);
    if (s == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(s, &pe)) {
        do {
            if (lstrcmpiA(pe.szExeFile, "u9.exe") != 0 && lstrcmpiA(pe.szExeFile, "u9-laa.exe") != 0) continue;
            for (skip = 0, i = 0; i < ndone; i++) if (done[i] == pe.th32ProcessID) skip = 1;
            if (!skip) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(s, &pe));
    }
    CloseHandle(s);
    return pid;
}

static void load_modules(DWORD pid)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32 me;
    nmods = 0;
    me.dwSize = sizeof(me);
    if (s == INVALID_HANDLE_VALUE) return;
    if (Module32First(s, &me)) {
        do {
            if (nmods >= MAXMOD) break;
            lstrcpynA(mods[nmods].name, me.szModule, sizeof mods[0].name);
            lstrcpynA(mods[nmods].path, me.szExePath, sizeof mods[0].path);
            mods[nmods].base = (DWORD)(DWORD_PTR)me.modBaseAddr;
            mods[nmods].size = me.modBaseSize;
            nmods++;
        } while (Module32Next(s, &me));
    }
    CloseHandle(s);
}

static void open_threads(DWORD pid)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    NtQIT ntqit = (NtQIT)(void (*)(void))GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                                        "NtQueryInformationThread");
    nthr = 0;
    te.dwSize = sizeof(te);
    if (s == INVALID_HANDLE_VALUE) return;
    if (Thread32First(s, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || nthr >= MAXTHR) continue;
            thr[nthr].tid = te.th32ThreadID;
            thr[nthr].h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                     THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!thr[nthr].h) continue;
            thr[nthr].start = 0;
            if (ntqit)
                ntqit(thr[nthr].h, 9, &thr[nthr].start, sizeof(DWORD), NULL);
            thr[nthr].prio = GetThreadPriority(thr[nthr].h);
            thr[nthr].c0 = cpu100ns(thr[nthr].h);
            nthr++;
        } while (Thread32Next(s, &te));
    }
    CloseHandle(s);
}

static void report_thread(int j, DWORD tid)
{
    static Cnt mc[MAXMOD + 1], ac[MAXSAMP];
    int nm = 0, na = 0, x;
    char w[128];

    P("  -- sampled tid %lu: %d samples, FPU CW 0x%04X, MXCSR 0x%08lX\n",
      (unsigned long)tid, neip[j], fpucw[j], (unsigned long)mxcsr[j]);
    for (x = 0; x < neip[j]; x++) {
        int m = modidx(eips[j][x]), y, f = 0;
        for (y = 0; y < nm; y++) if (mc[y].key == (DWORD)(m + 1)) { mc[y].n++; f = 1; break; }
        if (!f) { mc[nm].key = (DWORD)(m + 1); mc[nm].n = 1; nm++; }
    }
    qsort(mc, nm, sizeof mc[0], cmp_cnt);
    for (x = 0; x < nm && x < 10; x++)
        P("     %5.1f%%  %s\n", 100.0 * mc[x].n / neip[j],
          mc[x].key ? mods[mc[x].key - 1].name : "(no module)");
    qsort(eips[j], neip[j], sizeof(DWORD), cmp_dword);
    for (x = 0; x < neip[j]; x++) {
        if (na && ac[na - 1].key == eips[j][x]) ac[na - 1].n++;
        else { ac[na].key = eips[j][x]; ac[na].n = 1; na++; }
    }
    qsort(ac, na, sizeof ac[0], cmp_cnt);
    for (x = 0; x < na && x < 15; x++) {
        where(ac[x].key, w, sizeof w);
        P("       %4d  %s\n", ac[x].n, w);
    }
}

static void sample(DWORD pid)
{
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    FILETIME c, e, k, u, now;
    int top[TOPT], toptid[TOPT], i, j, si;
    LARGE_INTEGER q0, q1, qf;
    double ms;
    DWORD_PTR pam, sam;
    char path[MAX_PATH], w[128];

    if (!hp) { printf("pid %lu: OpenProcess failed %lu\n", (unsigned long)pid, GetLastError()); return; }
    GetProcessTimes(hp, &c, &e, &k, &u);
    for (;;) {
        if (WaitForSingleObject(hp, 0) == WAIT_OBJECT_0) {
            printf("pid %lu exited before %d s - not sampled\n", (unsigned long)pid, g_delayMs / 1000);
            CloseHandle(hp);
            return;
        }
        GetSystemTimeAsFileTime(&now);
        if ((ft(now) - ft(c)) / 10000 >= (ULONGLONG)g_delayMs) break;
        Sleep(500);
    }

    if (GetTempPathA(sizeof path, path)) {
        char name[64];
        snprintf(name, sizeof name, "u9gl_sample_%lu.txt", (unsigned long)pid);
        if (strlen(path) + strlen(name) < sizeof path) {
            strcat(path, name);
            g_out = fopen(path, "w");
        }
    }

    load_modules(pid);
    open_threads(pid);

    Sleep(1000);
    for (i = 0; i < nthr; i++) thr[i].c1 = cpu100ns(thr[i].h);
    for (j = 0; j < TOPT; j++) {
        int best = -1, x;
        for (i = 0; i < nthr; i++) {
            int used = 0;
            for (x = 0; x < j; x++) if (top[x] == i) used = 1;
            if (used) continue;
            if (best < 0 || thr[i].c1 - thr[i].c0 > thr[best].c1 - thr[best].c0) best = i;
        }
        top[j] = best;
        toptid[j] = best >= 0 ? (int)thr[best].tid : 0;
        neip[j] = 0;
    }

    timeBeginPeriod(1);
    for (i = 0; i < nthr; i++) thr[i].c2 = cpu100ns(thr[i].h);
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    for (si = 0; si < g_samples; si++) {
        for (j = 0; j < TOPT; j++) {
            HANDLE h;
            CONTEXT cx;
            if (top[j] < 0) continue;
            h = thr[top[j]].h;
            memset(&cx, 0, sizeof cx);
            cx.ContextFlags = CONTEXT_CONTROL |
                              (si == 0 ? (CONTEXT_FLOATING_POINT | CONTEXT_EXTENDED_REGISTERS) : 0);
            if (SuspendThread(h) == (DWORD)-1) continue;
            if (GetThreadContext(h, &cx)) {
                eips[j][neip[j]++] = cx.Eip;
                if (si == 0) {
                    fpucw[j] = (WORD)cx.FloatSave.ControlWord;
                    memcpy(&mxcsr[j], &cx.ExtendedRegisters[24], 4);
                }
            }
            ResumeThread(h);
        }
        if (WaitForSingleObject(hp, 0) == WAIT_OBJECT_0) break;
        Sleep(1);
    }
    QueryPerformanceCounter(&q1);
    for (i = 0; i < nthr; i++) thr[i].c3 = cpu100ns(thr[i].h);
    timeEndPeriod(1);
    ms = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;

    P("==== u9.exe pid %lu: sampled %.0f ms, %d rounds\n", (unsigned long)pid, ms, si);
    for (i = 0; i < nmods; i++)
        if (!lstrcmpiA(mods[i].name, "u9log.dll") || !lstrcmpiA(mods[i].name, "u9fix.dll") ||
            !lstrcmpiA(mods[i].name, "dsound.dll") || !lstrcmpiA(mods[i].name, "opengl32.dll") ||
            !lstrcmpiA(mods[i].name, "nvoglv32.dll"))
            P("  module %-14s %s\n", mods[i].name, mods[i].path);
    GetProcessAffinityMask(hp, &pam, &sam);
    P("  priority class 0x%lX, affinity 0x%lX of 0x%lX, %d threads, %d modules\n",
      (unsigned long)GetPriorityClass(hp), (unsigned long)pam, (unsigned long)sam, nthr, nmods);

    qsort(thr, nthr, sizeof thr[0], cmp_thr_cpu);
    P("  busiest threads over the sampling window (CPU ms, %% of wall):\n");
    for (i = 0; i < nthr && i < 10; i++) {
        double cms = (double)(thr[i].c3 - thr[i].c2) / 10000.0;
        where(thr[i].start, w, sizeof w);
        P("    tid %6lu  %7.1f ms  %5.1f%%  prio %3d  start %s\n",
          (unsigned long)thr[i].tid, cms, ms > 0 ? 100.0 * cms / ms : 0.0, thr[i].prio, w);
    }
    for (j = 0; j < TOPT; j++)
        if (toptid[j] && neip[j]) report_thread(j, (DWORD)toptid[j]);
    if (g_out) {
        fclose(g_out);
        g_out = NULL;
        printf("  report: %s\n", path);
    }

    for (i = 0; i < nthr; i++) CloseHandle(thr[i].h);
    CloseHandle(hp);
}

#if __has_include("u9gl_version.h")
#include "u9gl_version.h"
#else
#define U9GL_VERSION_FULL "standalone"
#endif

int main(int argc, char **argv)
{
    DWORD done[64];
    int runs = argc > 1 ? atoi(argv[1]) : 2;
    int ndone = 0;
    DWORD t0 = GetTickCount();

    if (argc > 2) g_delayMs = atoi(argv[2]) * 1000;
    if (argc > 3) g_samples = atoi(argv[3]);
    if (runs < 1 || runs > 64) runs = 2;
    if (g_delayMs < 0) g_delayMs = 40000;
    if (g_samples < 1 || g_samples > MAXSAMP) g_samples = 3000;

    printf("u9sample %s: waiting for u9.exe (%d run(s), %d s in, %d samples; 45 min limit)\n",
           U9GL_VERSION_FULL, runs, g_delayMs / 1000, g_samples);
    fflush(stdout);
    while (ndone < runs && GetTickCount() - t0 < 45u * 60u * 1000u) {
        DWORD pid = find_u9(done, ndone);
        HANDLE hp;
        if (!pid) { Sleep(500); continue; }
        printf("found u9.exe pid %lu\n", (unsigned long)pid);
        fflush(stdout);
        sample(pid);
        fflush(stdout);
        done[ndone++] = pid;
        if (ndone >= runs) break;
        hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (hp) { WaitForSingleObject(hp, INFINITE); CloseHandle(hp); }
    }
    printf("u9sample: done\n");
    return 0;
}
