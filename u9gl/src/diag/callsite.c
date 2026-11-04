#include <windows.h>
#include <stdint.h>
#include "log.h"
#include "diag.h"

typedef struct {
    uintptr_t site;
    uintptr_t original;
    DWORD     prot;
    int       patched;
} CallPatch;

static CallPatch g_calls[12];
static unsigned  g_callCount;

uintptr_t u9call_patch(uintptr_t site, void *replacement)
{
    CallPatch *p;
    uintptr_t orig;
    int32_t rel;
    DWORD old;

    if (g_callCount >= 12) return 0;
    if (*(const uint8_t *)site != 0xE8) {
        u9log_line("CALLSITE 0x%08X does not start with E8 (found 0x%02X)"
                   " - not patched", (unsigned)site, *(const uint8_t *)site);
        return 0;
    }
    rel  = *(const int32_t *)(site + 1);
    orig = site + 5 + (uintptr_t)rel;

    if (!VirtualProtect((void *)site, 5, PAGE_EXECUTE_READWRITE, &old)) {
        u9log_line("CALLSITE 0x%08X could not be made writable", (unsigned)site);
        return 0;
    }
    *(int32_t *)(site + 1) =
        (int32_t)((uintptr_t)replacement - (site + 5));
    VirtualProtect((void *)site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)site, 5);

    p = &g_calls[g_callCount++];
    p->site = site;
    p->original = orig;
    p->prot = old;
    p->patched = 1;
    u9log_line("CALLSITE 0x%08X: 0x%08X -> 0x%08X", (unsigned)site,
               (unsigned)orig, (unsigned)(uintptr_t)replacement);
    u9diag_line("call site 0x%08X redirected (was 0x%08X)",
                (unsigned)site, (unsigned)orig);
    return orig;
}

void u9call_revert_all(void)
{
    unsigned i;
    DWORD old;

    for (i = 0; i < g_callCount; i++) {
        CallPatch *p = &g_calls[i];
        if (!p->patched) continue;
        if (VirtualProtect((void *)p->site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            *(int32_t *)(p->site + 1) =
                (int32_t)(p->original - (p->site + 5));
            VirtualProtect((void *)p->site, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void *)p->site, 5);
        }
        p->patched = 0;
    }
}
