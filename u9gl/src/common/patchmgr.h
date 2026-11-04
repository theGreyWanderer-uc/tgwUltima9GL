#ifndef U9_PATCHMGR_H
#define U9_PATCHMGR_H

#include <stdint.h>
#include "symbols.h"

typedef enum {
    U9PATCH_VTABLE,
    U9PATCH_CALLSITE,
    U9PATCH_STUB,
    U9PATCH_INLINE
} U9PatchMechanism;

#define U9PF_EXCLUSIVE  0u
#define U9PF_CHAINABLE  1u

typedef struct {
    const char      *name;
    U9PatchMechanism mechanism;
    U9Symbol         target;
    unsigned         length;
    const uint8_t   *expected;
    unsigned         expectedLen;
    unsigned         flags;
} U9PatchSpec;

void *u9patch_install(const U9PatchSpec *spec, void *replacement);

int u9patch_install_bytes(const U9PatchSpec *spec,
                          const uint8_t *bytes, unsigned n);

void u9patch_remove_all(void);

void u9patch_manifest(void);

void u9patch_usage(unsigned *used, unsigned *max, unsigned *fullRefusals);

#endif
