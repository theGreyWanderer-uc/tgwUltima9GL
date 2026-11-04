#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../addresses.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "meshstat.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define MS_RISERS   5
#define MS_SOURCES  3
#define MS_HELD     6
#define SRC_SLOTS   256

typedef void *(U9CALL *CreateFn)(void *shape, void *object);
typedef void *(U9CALL *CopyFn)(void *shape, void *head, void *element, int flag);
typedef void  (U9CALL *DestroyFn)(void *shape);

static CreateFn  o_create;
static CopyFn    o_copy;
static DestroyFn o_destroy;

enum { K_CREATE, K_COPY, K_DESTROY };

typedef struct {
    const char   *name;
    U9Symbol      sym;
    int           kind;
    uint8_t       expect[5];
    uintptr_t     call;
    unsigned      total, last;
} Site;

static Site g_site[] = {
    { .name = "MESHSTAT_CREATE_56D30E",  .sym = U9SYM_MESHSTAT_CREATE_56D30E,  .kind = K_CREATE,  .expect = { 0xE8, 0xFD, 0x74, 0xFF, 0xFF } },
    { .name = "MESHSTAT_COPY_564A48",    .sym = U9SYM_MESHSTAT_COPY_564A48,    .kind = K_COPY,    .expect = { 0xE8, 0xB3, 0x00, 0x00, 0x00 } },
    { .name = "MESHSTAT_COPY_56F422",    .sym = U9SYM_MESHSTAT_COPY_56F422,    .kind = K_COPY,    .expect = { 0xE8, 0xD9, 0x56, 0xFF, 0xFF } },
    { .name = "MESHSTAT_COPY_56F681",    .sym = U9SYM_MESHSTAT_COPY_56F681,    .kind = K_COPY,    .expect = { 0xE8, 0x7A, 0x54, 0xFF, 0xFF } },
    { .name = "MESHSTAT_DESTROY_564DAF", .sym = U9SYM_MESHSTAT_DESTROY_564DAF, .kind = K_DESTROY, .expect = { 0xE8, 0xBC, 0xFF, 0xFF, 0xFF } },
    { .name = "MESHSTAT_DESTROY_56DC31", .sym = U9SYM_MESHSTAT_DESTROY_56DC31, .kind = K_DESTROY, .expect = { 0xE8, 0x3A, 0x71, 0xFF, 0xFF } },
    { .name = "MESHSTAT_DESTROY_56F54A", .sym = U9SYM_MESHSTAT_DESTROY_56F54A, .kind = K_DESTROY, .expect = { 0xE8, 0x21, 0x58, 0xFF, 0xFF } },
};
#define SITES ((int)(sizeof g_site / sizeof g_site[0]))

typedef struct {
    uint32_t key;
    unsigned now, total;
} Source;

static Source   g_src[SRC_SLOTS];
static unsigned g_srcOverflow;

static int32_t  g_prev[U9_MESH_ENTRIES];
static uint8_t  g_streak[U9_MESH_ENTRIES];
static uint8_t  g_fell[U9_MESH_ENTRIES];
static DWORD    g_loadedAt[U9_MESH_ENTRIES];
static int32_t  g_suspectAt[U9_MESH_ENTRIES];
static int32_t  g_delta[U9_MESH_ENTRIES];

static int      g_on, g_hooks, g_haveLast;
static DWORD    g_every, g_next, g_t0, g_nextHeld, g_heldMs;
static unsigned g_streakMin, g_lines, g_suspects;
static uint32_t g_lastHandles;

static void slog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("MESHSTAT %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static int appearance_of_entry(uint32_t e)
{
    uint32_t off = e - U9_MESH_TABLE;
    if (e < U9_MESH_TABLE || off >= U9_MESH_ENTRIES * U9_MESH_ENTRY) return -1;
    if (off % U9_MESH_ENTRY) return -1;
    return (int)(off / U9_MESH_ENTRY);
}

static int shape_type(const void *shape)
{
    uint32_t rec;
    if (!readable4((uintptr_t)shape)) return -1;
    rec = rd32((uintptr_t)shape);
    if (!readable4(rec + U9_OBJ_TYPE)) return -1;
    return *(const volatile uint16_t *)(uintptr_t)(rec + U9_OBJ_TYPE);
}

static int shape_appearance(const void *shape)
{
    uintptr_t h = (uintptr_t)shape + 0x54;
    if (!readable4(h)) return -1;
    return appearance_of_entry(rd32(h));
}

static void count_source(const void *head)
{
    int type = shape_type(head), app = shape_appearance(head);
    uint32_t key = ((uint32_t)(type & 0xFFFF) << 16) | (uint32_t)((app + 1) & 0xFFFF);
    unsigned i, h;
    if (!key) key = 0xFFFF0000u;
    h = (key * 2654435761u) >> 24;
    for (i = 0; i < SRC_SLOTS; i++) {
        Source *s = &g_src[(h + i) & (SRC_SLOTS - 1)];
        if (s->key == key || !s->key) {
            s->key = key;
            s->now++;
            s->total++;
            return;
        }
    }
    g_srcOverflow++;
}

static Site *site_of(uintptr_t ret)
{
    int i;
    for (i = 0; i < SITES; i++)
        if (g_site[i].call + 5 == ret) return &g_site[i];
    return NULL;
}

static void *U9CALL h_create(void *shape, void *object)
{
    Site *s = site_of((uintptr_t)__builtin_return_address(0));
    void *r = o_create(shape, object);
    if (s) s->total++;
    return r;
}

static void *U9CALL h_copy(void *shape, void *head, void *element, int flag)
{
    Site *s = site_of((uintptr_t)__builtin_return_address(0));
    void *r = o_copy(shape, head, element, flag);
    if (s) s->total++;
    count_source(head);
    return r;
}

static void U9CALL h_destroy(void *shape)
{
    Site *s = site_of((uintptr_t)__builtin_return_address(0));
    if (s) s->total++;
    o_destroy(shape);
}

void u9meshstat_install(void)
{
    int i;

    if (!u9cfg_int("diag_mesh_stat", 0)) return;
    g_t0 = u9time_now();
    g_every = (DWORD)u9cfg_int("diag_mesh_stat_ms", 5000);
    if (g_every < 1000) g_every = 1000;
    g_streakMin = (unsigned)u9cfg_int("diag_mesh_stat_streak", 6);
    if (g_streakMin < 2) g_streakMin = 2;
    if (g_streakMin > 250) g_streakMin = 250;
    g_heldMs = (DWORD)u9cfg_int("diag_mesh_stat_held_s", 300) * 1000u;

    for (i = 0; i < SITES; i++) {
        Site *s = &g_site[i];
        U9PatchSpec spec = { s->name, U9PATCH_CALLSITE, s->sym, 5, s->expect, 5,
                             U9PF_EXCLUSIVE };
        void *repl = s->kind == K_CREATE ? (void *)h_create
                   : s->kind == K_COPY   ? (void *)h_copy : (void *)h_destroy;
        void *prev;
        s->call = u9sym(s->sym);
        prev = u9patch_install(&spec, repl);
        if (!prev) { slog("%s at %08X REFUSED (see the manifest)", s->name, (unsigned)s->call); continue; }
        if (s->kind == K_CREATE)    o_create  = (CreateFn)prev;
        else if (s->kind == K_COPY) o_copy    = (CopyFn)prev;
        else                        o_destroy = (DestroyFn)prev;
        g_hooks++;
    }
    g_on = 1;
    g_next = u9time_now();
    g_nextHeld = g_next + 60000;
    slog("mesh census: %d of %d collision-shape call sites hooked; a line every %lu ms"
         " when anything moved, SUSPECT after %u rising lines, HELD past %lu s",
         g_hooks, SITES, (unsigned long)g_every, g_streakMin,
         (unsigned long)(g_heldMs / 1000));
}

static void log_held(const char *when, DWORD now)
{
    const unsigned char *table = (const unsigned char *)(uintptr_t)U9_MESH_TABLE;
    int top[MS_HELD];
    unsigned n = 0, i, k, held = 0;
    char out[300];
    size_t m = 0;

    for (k = 0; k < MS_HELD; k++) top[k] = -1;
    for (i = 0; i < U9_MESH_ENTRIES; i++) {
        int32_t c;
        if (!g_loadedAt[i] || g_fell[i] || now - g_loadedAt[i] < g_heldMs) continue;
        c = g_prev[i];
        if (c < 1 || !rd32((uintptr_t)(table + i * U9_MESH_ENTRY))) continue;
        held++;
        for (k = 0; k < MS_HELD; k++) {
            if (top[k] < 0 || c > g_prev[top[k]]) {
                unsigned j;
                for (j = MS_HELD - 1; j > k; j--) top[j] = top[j - 1];
                top[k] = (int)i;
                break;
            }
        }
    }
    out[0] = 0;
    for (k = 0; k < MS_HELD && top[k] >= 0; k++, n++)
        m += (size_t)snprintf(out + m, sizeof out - m, " %d:%d(%lus)", top[k], g_prev[top[k]],
                              (unsigned long)((now - g_loadedAt[top[k]]) / 1000));
    slog("HELD %s: %u meshes loaded over %lu s with a count that never fell%s%s",
         when, held, (unsigned long)(g_heldMs / 1000), n ? "; largest counts" : "", out);
}

static void rank(int *list, unsigned n, int k, const int32_t *key)
{
    unsigned i, j;
    for (i = 0; i < n; i++) {
        if (list[i] < 0 || key[k] > key[list[i]]) {
            for (j = n - 1; j > i; j--) list[j] = list[j - 1];
            list[i] = k;
            return;
        }
    }
}

static size_t sources_text(char *out, size_t cap, int now)
{
    int best[MS_SOURCES];
    int32_t key[SRC_SLOTS];
    unsigned i, k;
    size_t m = 0;

    for (k = 0; k < MS_SOURCES; k++) best[k] = -1;
    for (i = 0; i < SRC_SLOTS; i++) {
        key[i] = (int32_t)(now ? g_src[i].now : g_src[i].total);
        if (g_src[i].key && key[i] > 0) rank(best, MS_SOURCES, (int)i, key);
    }
    out[0] = 0;
    for (k = 0; k < MS_SOURCES && best[k] >= 0; k++) {
        const Source *s = &g_src[best[k]];
        int type = (int)(s->key >> 16), app = (int)(s->key & 0xFFFF) - 1;
        m += (size_t)snprintf(out + m, cap - m, " id %d app %d x%d",
                              type == 0xFFFF ? -1 : type, app, key[best[k]]);
        if (m >= cap) { m = cap - 1; break; }
    }
    return m;
}

void u9meshstat_tick(void)
{
    const unsigned char *table = (const unsigned char *)(uintptr_t)U9_MESH_TABLE;
    const uint32_t *sizes = (const uint32_t *)(uintptr_t)U9_MESH_SIZES;
    DWORD now;
    unsigned i, k, loaded = 0, inUse = 0, idle = 0, nk[3] = { 0, 0, 0 };
    uint32_t handles = 0, poolBase = 0, poolSize = 0, poolFree = 0;
    uint32_t bytesPool = 0, bytesOut = 0;
    int rise[MS_RISERS];
    char risers[160], sites[200], src[200];
    size_t n = 0;

    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;
    if (!now) now = 1;
    if (!readable4(U9_MESH_TABLE) || !readable4(U9_MESH_SIZES)) return;
    if (readable4(U9_MESH_POOL)) {
        uint32_t pool = rd32(U9_MESH_POOL);
        if (readable4(pool) && readable4(pool + 0x20)) {
            poolBase = rd32(pool);
            poolSize = rd32(pool + 4);
            poolFree = rd32(pool + 0x20);
        }
    }

    for (k = 0; k < MS_RISERS; k++) rise[k] = -1;
    for (i = 0; i < U9_MESH_ENTRIES; i++) {
        const unsigned char *e = table + i * U9_MESH_ENTRY;
        uint32_t mesh = rd32((uintptr_t)e);
        int32_t c = (int32_t)rd32((uintptr_t)(e + 4)), d;

        g_delta[i] = 0;
        if (!mesh) {
            g_loadedAt[i] = 0;
            g_fell[i] = 0;
            g_streak[i] = 0;
            g_suspectAt[i] = 0;
            g_prev[i] = 0;
            continue;
        }
        loaded++;
        if (c >= 1) { inUse++; handles += (uint32_t)c; } else idle++;
        if (poolSize && mesh >= poolBase && mesh - poolBase < poolSize) bytesPool += sizes[i];
        else                                                           bytesOut += sizes[i];

        if (!g_loadedAt[i]) {
            g_loadedAt[i] = now;
            g_prev[i] = c;
            continue;
        }
        d = c - g_prev[i];
        g_prev[i] = c;
        g_delta[i] = d;
        if (d < 0) { g_fell[i] = 1; g_streak[i] = 0; continue; }
        if (d == 0) continue;
        rank(rise, MS_RISERS, (int)i, g_delta);
        if (g_streak[i] < 255) g_streak[i]++;
        if (g_streak[i] >= g_streakMin && c >= 64 &&
            (!g_suspectAt[i] || c >= 2 * g_suspectAt[i])) {
            g_suspectAt[i] = c;
            g_suspects++;
            slog("SUSPECT appearance %u: its count rose on %u lines with no fall, now %d"
                 " (+%d this line); diag_mesh_watch=%u traces who takes them",
                 i, (unsigned)g_streak[i], c, d, i);
        }
    }

    risers[0] = 0;
    for (k = 0; k < MS_RISERS && rise[k] >= 0; k++)
        n += (size_t)snprintf(risers + n, sizeof risers - n, " %d:+%d=%d",
                              rise[k], g_delta[rise[k]], g_prev[rise[k]]);

    n = 0;
    sites[0] = 0;
    for (k = 0; k < (unsigned)SITES; k++) {
        Site *s = &g_site[k];
        unsigned d = s->total - s->last;
        s->last = s->total;
        nk[s->kind] += d;
        if (d && n + 24 < sizeof sites)
            n += (size_t)snprintf(sites + n, sizeof sites - n, " %08X:%u", (unsigned)s->call, d);
    }
    sources_text(src, sizeof src, 1);
    for (k = 0; k < SRC_SLOTS; k++) g_src[k].now = 0;

    if (g_haveLast && !nk[0] && !nk[1] && !nk[2] &&
        handles == g_lastHandles && rise[0] < 0)
        goto held;
    g_lines++;
    slog("meshes %u loaded, %u in use, %u idle; %lu handles (%+ld); bytes pool %lu + outside %lu,"
         " largest free %lu | rising:%s",
         loaded, inUse, idle, (unsigned long)handles,
         g_haveLast ? (long)handles - (long)g_lastHandles : 0L,
         (unsigned long)bytesPool, (unsigned long)bytesOut, (unsigned long)poolFree,
         risers[0] ? risers : " none");
    if (nk[0] || nk[1] || nk[2])
        slog("  shapes: %u built, %u copied, %u destroyed;%s | copies from%s",
             nk[0], nk[1], nk[2], sites, src[0] ? src : " -");
    g_lastHandles = handles;
    g_haveLast = 1;

held:
    if ((LONG)(now - g_nextHeld) >= 0) {
        g_nextHeld = now + 60000;
        log_held("now", now);
    }
}

void u9meshstat_report(void)
{
    unsigned k, nk[3] = { 0, 0, 0 };
    char src[200];

    if (!g_on) return;
    for (k = 0; k < (unsigned)SITES; k++) nk[g_site[k].kind] += g_site[k].total;
    sources_text(src, sizeof src, 0);
    slog("mesh census: %u lines, %u SUSPECT reports; shapes %u built, %u copied, %u destroyed"
         " (built + copied - destroyed = %ld still alive or destroyed elsewhere)",
         g_lines, g_suspects, nk[0], nk[1], nk[2], (long)nk[0] + (long)nk[1] - (long)nk[2]);
    for (k = 0; k < (unsigned)SITES; k++)
        slog("  %s %08X: %u", g_site[k].name, (unsigned)g_site[k].call, g_site[k].total);
    slog("  copies by source, session:%s%s", src[0] ? src : " none",
         g_srcOverflow ? " (some sources not tracked: table full)" : "");
    log_held("at exit", u9time_now());
}
