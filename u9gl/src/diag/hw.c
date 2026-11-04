#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include "log.h"
#include "diag.h"

#define HW_SLOTS 4

static uintptr_t g_addr[HW_SLOTS];
static uintptr_t g_owner;
static uint32_t  g_ownerVt;
static int       g_armed;
static unsigned  g_hits, g_expected, g_recycled;
static int       g_filterKnown = 1;

static int expected_writer(uint32_t eip)
{
    return (eip >= 0x004DD0A0u && eip <= 0x004DD0B0u)
        || (eip >= 0x004AE3B0u && eip <= 0x004AE780u)
        || (eip >= 0x004DD490u && eip <= 0x004DD770u)
        || (eip >= 0x004DD200u && eip <= 0x004DD218u);
}

static DWORD dr7_for(unsigned used)
{
    DWORD dr7 = 0;
    unsigned i;
    for (i = 0; i < used; i++) {
        dr7 |= (DWORD)1u << (2 * i);
        dr7 |= (DWORD)0x1u << (16 + 4 * i);
        dr7 |= (DWORD)0x3u << (18 + 4 * i);
    }
    return dr7;
}

static int apply_to_thread(DWORD tid, unsigned used)
{
    HANDLE h;
    CONTEXT cx;
    int ok = 0;

    h = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                   THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!h) return 0;

    if (tid != GetCurrentThreadId()) SuspendThread(h);

    ZeroMemory(&cx, sizeof(cx));
    cx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(h, &cx)) {
        cx.Dr0 = used > 0 ? (DWORD)g_addr[0] : 0;
        cx.Dr1 = used > 1 ? (DWORD)g_addr[1] : 0;
        cx.Dr2 = used > 2 ? (DWORD)g_addr[2] : 0;
        cx.Dr3 = used > 3 ? (DWORD)g_addr[3] : 0;
        cx.Dr6 = 0;
        cx.Dr7 = dr7_for(used);
        cx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        ok = SetThreadContext(h, &cx) ? 1 : 0;
    }
    if (tid != GetCurrentThreadId()) ResumeThread(h);
    CloseHandle(h);
    return ok;
}

static int for_each_thread(unsigned used)
{
    HANDLE snap;
    THREADENTRY32 te;
    DWORD me = GetCurrentProcessId();
    int done = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == me)
                done += apply_to_thread(te.th32ThreadID, used);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return done;
}

void u9hw_arm_at(uintptr_t obj, const unsigned *offs, unsigned n)
{
    unsigned i, threads;

    g_filterKnown = 0;

    if (g_armed || !obj || !offs || !n) return;
    if (n > HW_SLOTS) n = HW_SLOTS;
    for (i = 0; i < n; i++) g_addr[i] = obj + offs[i];
    for (; i < HW_SLOTS; i++) g_addr[i] = 0;
    g_owner = obj;
    g_ownerVt = u9diag_vtable(obj);

    threads = (unsigned)for_each_thread(n);
    if (!threads) {
        u9diag_line("HW watch: could not set debug registers on any thread");
        return;
    }
    g_armed = 1;
    u9diag_line("");
    u9diag_line("HW watch armed on object 0x%08X, %u threads", (unsigned)obj,
                threads);
    for (i = 0; i < n; i++)
        u9diag_line("   DR%u -> 0x%08X  (+0x%03X)", i, (unsigned)g_addr[i],
                    offs[i]);
    u9log_line("HW watch armed on 0x%08X across %u threads", (unsigned)obj,
               threads);
}

void u9hw_arm(uintptr_t obj)
{
    static const unsigned off[HW_SLOTS] = { 0x284u, 0x28Cu, 0x298u, 0x2A8u };
    unsigned i, threads;

    if (g_armed || !obj) return;
    for (i = 0; i < HW_SLOTS; i++) g_addr[i] = obj + off[i];
    g_owner = obj;
    g_ownerVt = u9diag_vtable(obj);

    threads = (unsigned)for_each_thread(HW_SLOTS);
    if (!threads) {
        u9diag_line("HW watch: could not set debug registers on any thread");
        return;
    }
    g_armed = 1;
    u9diag_line("");
    u9diag_line("HW watch armed on object 0x%08X, %u threads", (unsigned)obj,
                threads);
    for (i = 0; i < HW_SLOTS; i++)
        u9diag_line("   DR%u -> 0x%08X  (+0x%03X)", i, (unsigned)g_addr[i],
                    off[i]);
    u9log_line("HW watch armed on 0x%08X across %u threads", (unsigned)obj,
               threads);
}

void u9hw_disarm(void)
{
    if (!g_armed) return;
    g_armed = 0;
    for_each_thread(0);
    u9diag_line("HW watch disarmed: %u unexpected, %u from known engine writers,"
                " %u after the memory was recycled",
                g_hits - g_expected - g_recycled, g_expected, g_recycled);
}

void u9hw_follow(uintptr_t candidate)
{
    if (!g_armed || !candidate || candidate == g_owner) return;
    if (u9diag_vtable(g_owner) == g_ownerVt) return;
    u9diag_line("HW watch: object 0x%08X is gone, following 0x%08X",
                (unsigned)g_owner, (unsigned)candidate);
    g_armed = 0;
    u9hw_arm(candidate);
}

#define EIP_SLOTS 24

static int      g_counter;
static unsigned g_limit, g_counted;
static uint32_t g_eip[EIP_SLOTS];
static unsigned g_eipHits[EIP_SLOTS], g_eipOther;

void u9hw_arm_counter(uintptr_t addr, unsigned limit)
{
    unsigned threads, i;
    if (g_armed || !addr) return;
    g_addr[0] = addr;
    for (i = 1; i < HW_SLOTS; i++) g_addr[i] = 0;
    g_owner = addr;
    g_counter = 1;
    g_limit = limit ? limit : 20000u;
    threads = (unsigned)for_each_thread(1);
    if (!threads) {
        u9log_line("HWCOUNT could not set debug registers on any thread");
        g_counter = 0;
        return;
    }
    g_armed = 1;
    u9log_line("HWCOUNT armed on 0x%08X across %u threads, tallying writers up to %u hits",
               (unsigned)addr, threads, g_limit);
}

int u9hw_counter_done(void) { return g_counter && g_counted >= g_limit; }

void u9hw_counter_report(const char *why)
{
    unsigned i, j, order[EIP_SLOTS], n = 0;
    if (!g_counter) return;
    for (i = 0; i < EIP_SLOTS; i++) if (g_eipHits[i]) order[n++] = i;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && g_eipHits[order[j]] > g_eipHits[order[j-1]]; j--) {
            unsigned t = order[j]; order[j] = order[j-1]; order[j-1] = t;
        }
    u9log_line("HWCOUNT %s: %u writes to 0x%08X tallied, %u writers, %u from writers past the table",
               why, g_counted, (unsigned)g_addr[0], n, g_eipOther);
    for (i = 0; i < n; i++)
        u9log_line("HWCOUNT   next EIP 0x%08X  %u writes  (the store is just before it)",
                   (unsigned)g_eip[order[i]], g_eipHits[order[i]]);
}

static int counter_hit(CONTEXT *cx)
{
    uint32_t eip = (uint32_t)cx->Eip;
    unsigned i;
    if (g_counted >= g_limit) { cx->Dr7 = 0; cx->Dr6 = 0; return 1; }
    g_counted++;
    for (i = 0; i < EIP_SLOTS; i++) {
        if (g_eip[i] == eip) { g_eipHits[i]++; break; }
        if (!g_eip[i]) { g_eip[i] = eip; g_eipHits[i] = 1; break; }
    }
    if (i == EIP_SLOTS) g_eipOther++;
    cx->Dr6 = 0;
    return 1;
}

int u9hw_is_armed(void)      { return g_armed; }
uintptr_t u9hw_owner(void)   { return g_owner; }

int u9hw_handle(void *ctx)
{
    CONTEXT *cx = (CONTEXT *)ctx;
    unsigned i;
    DWORD which;

    if (!g_armed || !cx) return 0;
    which = cx->Dr6 & 0xFu;
    if (!which) return 0;
    if (g_counter) return counter_hit(cx);

    for (i = 0; i < HW_SLOTS; i++) {
        if (!(which & (1u << i))) continue;
        g_hits++;
        if (u9diag_vtable(g_owner) != g_ownerVt) { g_recycled++; continue; }
        if (g_filterKnown && expected_writer((uint32_t)cx->Eip)) {
            g_expected++; continue;
        }
        if (g_hits <= 40) {
            uintptr_t a = g_addr[i];
            u9diag_line("");
            u9diag_line("HW WRITE  *** UNEXPECTED WRITER ***"
                        "  to 0x%08X (+0x%03X of object 0x%08X)",
                        (unsigned)a, (unsigned)(a - g_owner), (unsigned)g_owner);
            u9diag_line("   next EIP 0x%08X  (the store is immediately before)",
                        (unsigned)cx->Eip);
            u9diag_line("   value now 0x%08X  %s", u9diag_u32(a),
                        u9diag_u32(u9diag_u32(a)) ? "-> readable"
                                                  : "NOT A POINTER");
            u9diag_line("   EAX=%08X EBX=%08X ECX=%08X EDX=%08X",
                        (unsigned)cx->Eax, (unsigned)cx->Ebx,
                        (unsigned)cx->Ecx, (unsigned)cx->Edx);
            u9diag_line("   ESI=%08X EDI=%08X EBP=%08X ESP=%08X",
                        (unsigned)cx->Esi, (unsigned)cx->Edi,
                        (unsigned)cx->Ebp, (unsigned)cx->Esp);
            u9diag_line("   frame %u", (unsigned)u9log_frame());
        }
    }
    cx->Dr6 = 0;
    return 1;
}
