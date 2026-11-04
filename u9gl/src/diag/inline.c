#include <windows.h>
#include <stdint.h>
#include "log.h"
#include "diag.h"

#define U9INLINE_MAX 512

#define TRAMP_SIZE 32
static uint8_t *g_tramps;

typedef struct {
    uintptr_t target;
    unsigned  stolen;
    uint8_t   original[16];
    void     *tramp;
    int       active;
} InlineHook;

static InlineHook g_hooks[U9INLINE_MAX];
static unsigned   g_hookCount;

void *u9inline_hook(uintptr_t target, void *replacement, unsigned stolen,
                    const uint8_t *expect)
{
    InlineHook *h;
    uint8_t *tramp;
    DWORD old;
    unsigned i;

    if (g_hookCount >= U9INLINE_MAX) {
        u9log_line("INLINE 0x%08X: hook table full (%u of %u used) - NOT hooked",
                   (unsigned)target, g_hookCount, (unsigned)U9INLINE_MAX);
        return NULL;
    }
    if (stolen < 5 || stolen > 16) {
        u9log_line("INLINE 0x%08X: %u stolen bytes is out of range - NOT hooked",
                   (unsigned)target, stolen);
        return NULL;
    }

    if (!expect) {
        u9log_line("INLINE 0x%08X: no expected bytes given - verification "
                   "SKIPPED; found %02X %02X %02X %02X %02X",
                   (unsigned)target,
                   ((const uint8_t *)target)[0], ((const uint8_t *)target)[1],
                   ((const uint8_t *)target)[2], ((const uint8_t *)target)[3],
                   ((const uint8_t *)target)[4]);
    } else {
        for (i = 0; i < stolen; i++) {
            if (((const uint8_t *)target)[i] == expect[i]) continue;
            u9log_line("INLINE 0x%08X byte %u is 0x%02X, expected 0x%02X"
                       " - NOT hooked", (unsigned)target, i,
                       ((const uint8_t *)target)[i], expect[i]);
            return NULL;
        }
    }

    if (!g_tramps)
        g_tramps = (uint8_t *)VirtualAlloc(NULL, U9INLINE_MAX * TRAMP_SIZE,
                                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramps) {
        u9log_line("INLINE 0x%08X: no memory for the trampolines", (unsigned)target);
        return NULL;
    }
    tramp = g_tramps + g_hookCount * TRAMP_SIZE;

    h = &g_hooks[g_hookCount];
    h->target = target;
    h->stolen = stolen;
    for (i = 0; i < stolen; i++) h->original[i] = ((const uint8_t *)target)[i];

    for (i = 0; i < stolen; i++) tramp[i] = h->original[i];
    tramp[stolen] = 0xE9;
    *(int32_t *)(tramp + stolen + 1) =
        (int32_t)((target + stolen) - (uintptr_t)(tramp + stolen + 5));

    if (!VirtualProtect((void *)target, stolen, PAGE_EXECUTE_READWRITE, &old)) {
        u9log_line("INLINE 0x%08X could not be made writable", (unsigned)target);
        return NULL;
    }
    ((uint8_t *)target)[0] = 0xE9;
    *(int32_t *)(target + 1) =
        (int32_t)((uintptr_t)replacement - (target + 5));
    for (i = 5; i < stolen; i++) ((uint8_t *)target)[i] = 0x90;
    VirtualProtect((void *)target, stolen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)target, stolen);

    h->tramp = tramp;
    h->active = 1;
    g_hookCount++;
    u9log_line("INLINE 0x%08X hooked, %u bytes stolen, trampoline at 0x%08X",
               (unsigned)target, stolen, (unsigned)(uintptr_t)tramp);
    if (g_hookCount == U9INLINE_MAX * 9 / 10)
        u9log_line("INLINE WARNING hook table 90%% full (%u of %u) - raise U9INLINE_MAX",
                   g_hookCount, (unsigned)U9INLINE_MAX);
    return tramp;
}

void u9inline_remove_all(void)
{
    unsigned i, k;
    DWORD old;

    for (i = 0; i < g_hookCount; i++) {
        InlineHook *h = &g_hooks[i];
        if (!h->active) continue;
        if (VirtualProtect((void *)h->target, h->stolen,
                           PAGE_EXECUTE_READWRITE, &old)) {
            for (k = 0; k < h->stolen; k++)
                ((uint8_t *)h->target)[k] = h->original[k];
            VirtualProtect((void *)h->target, h->stolen, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void *)h->target,
                                  h->stolen);
        }
        h->active = 0;
    }
}
