#ifndef U9FIX_MEM_H
#define U9FIX_MEM_H

#include <windows.h>
#include <stdint.h>

#ifndef U9CALL
#define U9CALL __attribute__((thiscall))
#endif

#define ACTOR_TABLE     0x0098F324u
#define ACTOR_RECORDS   0x0098F31Cu
#define ENTITY_TABLE    0x008515D0u
#define RECORD_STRIDE   0x13Cu
#define ACTOR_COUNT     512u
#define IMAGE_LO        0x00401000u
#define IMAGE_HI        0x00800000u

static uint32_t __attribute__((unused)) rd32(uintptr_t p) { return *(const volatile uint32_t *)p; }

#define U9READ_SLOTS 8
extern int      g_u9ReadCache;
extern unsigned g_u9ReadCalls, g_u9ReadAsked;
static int __attribute__((unused)) readable4(uintptr_t p)
{
    static uintptr_t okBase[U9READ_SLOTS], okEnd[U9READ_SLOTS];
    static DWORD okTick;
    static unsigned okNext;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD tick;
    int i, n = g_u9ReadCache;

    if (p < 0x10000u) return 0;
    if (n < 1) n = 1;
    if (n > U9READ_SLOTS) n = U9READ_SLOTS;
    g_u9ReadCalls++;
    tick = GetTickCount();
    if (tick != okTick) {
        okTick = tick;
        for (i = 0; i < U9READ_SLOTS; i++) okEnd[i] = 0;
    }
    for (i = 0; i < n; i++)
        if (p >= okBase[i] && p + 4 <= okEnd[i]) return 1;
    g_u9ReadAsked++;
    if (!VirtualQuery((void *)p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    i = (int)(okNext++ % (unsigned)n);
    okBase[i] = (uintptr_t)mbi.BaseAddress;
    okEnd[i]  = okBase[i] + mbi.RegionSize;
    return (p + 4 <= okEnd[i]);
}

static int __attribute__((unused)) is_actor_vtable(uintptr_t vt)
{
    return vt == 0x00748F08u || vt == 0x00747E28u;
}

#endif
