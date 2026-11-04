#include <windows.h>
#include <stdint.h>
#include "buildid.h"

void u9log_line(const char *fmt, ...);

#define IMAGE_BASE 0x00400000u

typedef struct {
    uintptr_t     addr;
    const uint8_t bytes[8];
    unsigned      len;
    const char   *what;
} Signature;

static const Signature SIGS[] = {
    { 0x004163F0u, { 0x32, 0xC0, 0xC3 }, 3,
      "Actor_NeedsRebuild: XOR AL,AL / RET" },
    { 0x0043CB5Cu, { 0xC7, 0x86, 0x78, 0x01, 0x00, 0x00 }, 6,
      "Actor_ResetTransientState: MOV [ESI+0x178], imm" },
    { 0x004DD0F0u, { 0x83, 0xEC, 0x08, 0x53, 0x55, 0x56 }, 6,
      "Actor_Tick_PruneRefs entry" },
    { 0x0064975Du, { 0x8B, 0x95, 0xE8, 0x00, 0x00, 0x00 }, 6,
      "SpellTeleport_Tick: MOV EDX,[EBP+0xE8], the F-5 site" },
    { 0x00649747u, { 0xE8, 0xE4, 0x4F, 0xE7, 0xFF }, 5,
      "SpellTeleport_Tick: CALL 0x004BE730, the second F-5 site" },
    { 0x004F6250u, { 0xE8 }, 1,
      "Actor_Create: CALL operator new, base actor" },
};
#define SIG_COUNT (sizeof(SIGS) / sizeof(SIGS[0]))

static U9BuildId   g_build = U9BUILD_UNKNOWN;
static int         g_done;
static const char *g_mismatch;

static int readable(uintptr_t p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

U9BuildId u9build_detect(void)
{
    unsigned i, k;

    if (g_done) return g_build;
    g_done = 1;

    if ((uintptr_t)GetModuleHandleA(NULL) != IMAGE_BASE) {
        g_mismatch = "image is not loaded at 0x00400000";
        return g_build;
    }
    for (i = 0; i < SIG_COUNT; i++) {
        if (!readable(SIGS[i].addr, SIGS[i].len)) {
            g_mismatch = SIGS[i].what;
            u9log_line("BUILD probe %u at 0x%08X not readable (%s)",
                       i, (unsigned)SIGS[i].addr, SIGS[i].what);
            return g_build;
        }
        for (k = 0; k < SIGS[i].len; k++) {
            if (((const uint8_t *)SIGS[i].addr)[k] == SIGS[i].bytes[k])
                continue;
            g_mismatch = SIGS[i].what;
            u9log_line("BUILD probe %u at 0x%08X: byte %u is 0x%02X,"
                       " expected 0x%02X (%s)", i, (unsigned)SIGS[i].addr, k,
                       ((const uint8_t *)SIGS[i].addr)[k], SIGS[i].bytes[k],
                       SIGS[i].what);
            return g_build;
        }
    }
    g_build = U9BUILD_1_19F;
    return g_build;
}

const char *u9build_name(void)
{
    return (u9build_detect() == U9BUILD_1_19F) ? "1.19F" : "unknown";
}

int u9build_is_exact(void)
{
    return u9build_detect() == U9BUILD_1_19F;
}

const char *u9build_mismatch(void)
{
    u9build_detect();
    return g_mismatch;
}
