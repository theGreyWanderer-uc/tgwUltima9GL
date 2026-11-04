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

static int __attribute__((unused)) readable4(uintptr_t p)
{
    static uintptr_t okBase, okEnd;
    static DWORD okTick;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD tick;

    if (p < 0x10000u) return 0;
    tick = GetTickCount();
    if (tick != okTick) { okTick = tick; okEnd = 0; }
    if (p >= okBase && p + 4 <= okEnd) return 1;
    if (!VirtualQuery((void *)p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    okBase = (uintptr_t)mbi.BaseAddress;
    okEnd  = okBase + mbi.RegionSize;
    return (p + 4 <= okEnd);
}

static int __attribute__((unused)) is_actor_vtable(uintptr_t vt)
{
    return vt == 0x00748F08u || vt == 0x00747E28u;
}

#endif
