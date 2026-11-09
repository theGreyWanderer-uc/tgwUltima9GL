#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "patchmgr.h"
#include "buildid.h"
#include "lifecycle.h"

void  u9log_line(const char *fmt, ...);
void *u9inline_hook(uintptr_t target, void *replacement, unsigned stolen,
                    const uint8_t *expect);
void  u9inline_remove_all(void);

#define U9PATCH_MAX  512
#define SAVE_MAX     16

typedef enum {
    ST_INSTALLED = 0,
    ST_REMOVED,
    ST_REFUSED_BUILD,
    ST_REFUSED_CONFLICT,
    ST_REFUSED_BYTES,
    ST_REFUSED_MECHANISM,
    ST_REFUSED_PROTECT
} Status;

static const char *STATUS_TEXT[] = {
    "installed",
    "removed",
    "REFUSED  unrecognised build",
    "REFUSED  range already owned",
    "REFUSED  expected bytes did not match",
    "REFUSED  mechanism failed",
    "REFUSED  could not make the range writable"
};

typedef struct {
    const U9PatchSpec *spec;
    U9PatchSpec own;
    uintptr_t   addr;
    unsigned    length;
    uint8_t     saved[SAVE_MAX];
    Status      status;
    const char *conflictWith;
    int         usesInline;
} Record;

static Record   g_rec[U9PATCH_MAX];
static unsigned g_count;
static unsigned g_fullRefusals;
static int      g_registered;

static Record *record_new(const U9PatchSpec *spec, uintptr_t addr)
{
    Record *r;
    if (g_count >= U9PATCH_MAX) {
        g_fullRefusals++;
        u9log_line("PATCH registry full (%u of %u) - '%s' NOT installed",
                   g_count, (unsigned)U9PATCH_MAX,
                   spec && spec->name ? spec->name : "?");
        return 0;
    }
    r = &g_rec[g_count++];
    if (g_count == U9PATCH_MAX * 9 / 10)
        u9log_line("PATCH WARNING registry 90%% full (%u of %u) - raise U9PATCH_MAX",
                   g_count, (unsigned)U9PATCH_MAX);
    memset(r, 0, sizeof(*r));
    r->own  = *spec;
    r->spec = &r->own;
    r->addr = addr;
    r->length = spec->length;
    return r;
}

static const Record *find_overlap(const U9PatchSpec *spec, uintptr_t addr)
{
    unsigned i;
    uintptr_t a0 = addr, a1 = addr + spec->length;

    for (i = 0; i < g_count; i++) {
        const Record *o = &g_rec[i];
        if (o->status != ST_INSTALLED) continue;
        if (a1 <= o->addr || o->addr + o->length <= a0) continue;
        if ((spec->flags & U9PF_CHAINABLE) && (o->spec->flags & U9PF_CHAINABLE))
            continue;
        return o;
    }
    return 0;
}

static int bytes_match(uintptr_t addr, const U9PatchSpec *spec)
{
    unsigned i;
    if (!spec->expected || !spec->expectedLen) return 1;
    for (i = 0; i < spec->expectedLen; i++)
        if (((const uint8_t *)addr)[i] != spec->expected[i]) {
            u9log_line("PATCH %s: byte %u at 0x%08X is 0x%02X, expected 0x%02X",
                       spec->name, i, (unsigned)(addr + i),
                       ((const uint8_t *)addr)[i], spec->expected[i]);
            return 0;
        }
    return 1;
}

static int write_bytes(uintptr_t addr, const uint8_t *bytes, unsigned n)
{
    DWORD old;
    if (!VirtualProtect((void *)addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)addr, bytes, n);
    VirtualProtect((void *)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, n);
    return 1;
}

static void ensure_registered(void)
{
    if (g_registered) return;
    g_registered = 1;
    u9life_push("patches", u9patch_remove_all);
}

static Record *prepare(const U9PatchSpec *spec)
{
    uintptr_t addr;
    Record *r;
    const Record *clash;
    unsigned i;

    addr = u9sym(spec->target);
    if (!addr) {
        r = record_new(spec, 0);
        if (r) r->status = ST_REFUSED_BUILD;
        return 0;
    }
    clash = find_overlap(spec, addr);
    if (clash) {
        r = record_new(spec, addr);
        if (r) {
            r->status = ST_REFUSED_CONFLICT;
            r->conflictWith = clash->spec->name;
        }
        return 0;
    }
    if (!bytes_match(addr, spec)) {
        r = record_new(spec, addr);
        if (r) r->status = ST_REFUSED_BYTES;
        return 0;
    }
    r = record_new(spec, addr);
    if (!r) return 0;
    if (spec->length > SAVE_MAX) { r->status = ST_REFUSED_MECHANISM; return 0; }
    for (i = 0; i < spec->length; i++)
        r->saved[i] = ((const uint8_t *)addr)[i];
    return r;
}

void *u9patch_install(const U9PatchSpec *spec, void *replacement)
{
    Record *r = prepare(spec);
    uintptr_t addr;
    void *previous = 0;

    if (!r) return 0;
    addr = r->addr;

    switch (spec->mechanism) {
    case U9PATCH_VTABLE: {
        uint32_t v = (uint32_t)(uintptr_t)replacement;
        previous = (void *)(uintptr_t)*(const uint32_t *)addr;
        if (!write_bytes(addr, (const uint8_t *)&v, 4)) {
            r->status = ST_REFUSED_PROTECT;
            return 0;
        }
        break;
    }
    case U9PATCH_CALLSITE: {
        int32_t rel;
        uintptr_t site = addr;
        previous = (void *)(site + 5 + (uintptr_t)*(const int32_t *)(site + 1));
        rel = (int32_t)((uintptr_t)replacement - (site + 5));
        if (!write_bytes(site + 1, (const uint8_t *)&rel, 4)) {
            r->status = ST_REFUSED_PROTECT;
            return 0;
        }
        break;
    }
    case U9PATCH_INLINE:
        previous = u9inline_hook(addr, replacement, spec->length,
                                 spec->expected);
        if (!previous) { r->status = ST_REFUSED_MECHANISM; return 0; }
        r->usesInline = 1;
        break;
    default:
        r->status = ST_REFUSED_MECHANISM;
        return 0;
    }
    r->status = ST_INSTALLED;
    ensure_registered();
    return previous;
}

int u9patch_install_bytes(const U9PatchSpec *spec,
                          const uint8_t *bytes, unsigned n)
{
    Record *r = prepare(spec);
    if (!r) return 0;
    if (n != spec->length) { r->status = ST_REFUSED_MECHANISM; return 0; }
    if (!write_bytes(r->addr, bytes, n)) {
        r->status = ST_REFUSED_PROTECT;
        return 0;
    }
    r->status = ST_INSTALLED;
    ensure_registered();
    return 1;
}

void u9patch_remove_all(void)
{
    int i;
    int sawInline = 0;

    for (i = (int)g_count - 1; i >= 0; i--) {
        Record *r = &g_rec[i];
        if (r->status != ST_INSTALLED) continue;
        if (r->usesInline) { sawInline = 1; continue; }
        write_bytes(r->addr, r->saved, r->length);
        r->status = ST_REMOVED;
    }
    if (sawInline) u9inline_remove_all();
    g_count = 0;
    g_registered = 0;
}

void u9patch_manifest(void)
{
    static const char *MECH[] = { "VTABLE", "CALLSITE", "STUB", "INLINE" };
    unsigned i;

    u9log_line("");
    u9log_line("BUILD    %s  (%s)", u9build_name(),
               u9build_is_exact() ? "exact" : "NOT RECOGNISED");
    if (!u9build_is_exact() && u9build_mismatch())
        u9log_line("         mismatch: %s", u9build_mismatch());
    u9log_line("PATCHES  (%u of %u registry slots used so far)", g_count, (unsigned)U9PATCH_MAX);
    if (!g_count) {
        u9log_line("  (none requested)");
        return;
    }
    for (i = 0; i < g_count; i++) {
        const Record *r = &g_rec[i];
        u9log_line("  %-14s %-8s 0x%08X  %s", r->spec->name,
                   MECH[r->spec->mechanism], (unsigned)r->addr,
                   STATUS_TEXT[r->status]);
        u9log_line("                 %s", u9sym_name(r->spec->target));
        if (r->status == ST_REFUSED_CONFLICT)
            u9log_line("                 range owned by %s", r->conflictWith);
    }
}

void u9patch_usage(unsigned *used, unsigned *max, unsigned *fullRefusals)
{
    if (used) *used = g_count;
    if (max) *max = U9PATCH_MAX;
    if (fullRefusals) *fullRefusals = g_fullRefusals;
}
