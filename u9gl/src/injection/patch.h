#ifndef U9_PATCH_H
#define U9_PATCH_H

#include <stdint.h>

#define U9_PATCH_MAX_SLOTS 64

typedef struct {
    void   **vtbl;
    unsigned slots[U9_PATCH_MAX_SLOTS];
    void    *originals[U9_PATCH_MAX_SLOTS];
    unsigned count;
    unsigned long oldProtect;
    int      applied;
} U9Patch;

int  u9patch_begin(U9Patch *p, void *vtblAddr, unsigned slotCount);

void *u9patch_slot(U9Patch *p, unsigned slot, void *replacement);

void u9patch_end(U9Patch *p, unsigned slotCount);

void u9patch_revert(U9Patch *p, unsigned slotCount);

#endif
