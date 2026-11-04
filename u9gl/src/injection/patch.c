#include <windows.h>
#include "patch.h"

int u9patch_begin(U9Patch *p, void *vtblAddr, unsigned slotCount)
{
    p->vtbl    = (void **)vtblAddr;
    p->count   = 0;
    p->applied = 0;
    if (!VirtualProtect(p->vtbl, slotCount * sizeof(void *),
                        PAGE_READWRITE, &p->oldProtect))
        return 0;
    p->applied = 1;
    return 1;
}

void *u9patch_slot(U9Patch *p, unsigned slot, void *replacement)
{
    void *orig;
    if (!p->applied || p->count >= U9_PATCH_MAX_SLOTS)
        return NULL;
    orig = p->vtbl[slot];
    p->slots[p->count]     = slot;
    p->originals[p->count] = orig;
    p->count++;
    p->vtbl[slot] = replacement;
    return orig;
}

void u9patch_end(U9Patch *p, unsigned slotCount)
{
    DWORD tmp;
    if (!p->applied) return;
    VirtualProtect(p->vtbl, slotCount * sizeof(void *), p->oldProtect, &tmp);
}

void u9patch_revert(U9Patch *p, unsigned slotCount)
{
    DWORD tmp, old;
    unsigned i;
    if (!p->applied || p->count == 0) return;
    if (!VirtualProtect(p->vtbl, slotCount * sizeof(void *),
                        PAGE_READWRITE, &old))
        return;
    for (i = 0; i < p->count; i++)
        p->vtbl[p->slots[i]] = p->originals[i];
    VirtualProtect(p->vtbl, slotCount * sizeof(void *), old, &tmp);
    p->count   = 0;
    p->applied = 0;
}
