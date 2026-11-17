#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "metrics.h"
#include "idlist.h"

int u9cfg_int(const char *key, int def);

#include "u9renderer.h"
#include "../../addresses.h"
#include "../diag/log.h"
#include "../injection/config.h"
#include "../texture/texoverride.h"

#define U9GL_TEXCACHE 4096

typedef struct {
    int      id, id2;
    unsigned key;
    GLuint   tex;
    int      w, h;
    unsigned fmt;
    unsigned lastUsed;
    unsigned bytes;
    unsigned lastUpload;
    unsigned srcHash;
    unsigned char mr, mg, mb;
    unsigned char opaque;
    unsigned char overridden;
} TexEntry;

#define U9GL_TEX_BUDGET_BYTES (192u * 1024u * 1024u)
#define U9GL_TEX_MIN_AGE      600u

#define U9GL_NO_KEY 0xFFFFFFFFu

static TexEntry g_cache[U9GL_TEXCACHE];

#define U9GL_ALPHA_REF 128u

#define U9GL_PENDING 1024
typedef struct {
    int      id, id2;
    unsigned fmt, w, h, bytes;
    unsigned levels;
    unsigned srcLevels;
    GLuint   reuseTex;
    unsigned srcHash;
    unsigned char *data;
} Pending;
static Pending  g_pending[U9GL_PENDING];
static unsigned g_pendCount, g_pendDropped;
static unsigned g_cacheCount;
static unsigned g_uploads, g_failed, g_descMiss;
static int      g_cacheFullLogged;
static unsigned g_dumped;
static unsigned g_dupIdent;
static int      g_mipFailLogged;
static int      g_lodBiasWarned;
static unsigned g_mipHist[16];
static unsigned g_ownLoads, g_ownFallbacks;
static unsigned g_chainFromArchive, g_chainFiltered;
static unsigned g_chainChecked, g_chainSuspect;
static unsigned g_composedFromMgr, g_composedFallback;
static unsigned g_recomposed, g_refreshes;
static double   g_uploadMs;
static double   g_frameUploadMs;
static double   g_msDecode, g_msChain, g_msGL;
static unsigned g_subUploads, g_fullUploads, g_skippedUnchanged;

enum {
    U9GL_ABANDON_PENDING_DUP = 0,
    U9GL_ABANDON_PENDING_FULL,
    U9GL_ABANDON_DIMS,
    U9GL_ABANDON_NO_SRC,
    U9GL_ABANDON_FORMAT,
    U9GL_ABANDON_UNREADABLE,
    U9GL_ABANDON_ALLOC,
    U9GL_ABANDON__N
};
static unsigned g_abandoned[U9GL_ABANDON__N];

static unsigned g_refreshArrived;
static unsigned g_refreshBecameCreate;
static const char *const g_abandonName[U9GL_ABANDON__N] = {
    "already queued this frame", "pending queue full", "bad log2 dimensions",
    "no texel source (includes F-8 empty slots)", "size contradicts format",
    "source not readable", "allocation failed"
};
static void abandon(int counted, int reason)
{
    if (counted) g_abandoned[reason]++;
}
static double   g_msGL0, g_msGLn, g_msStorage;
static unsigned g_nStorage;
static double   g_msL0b[4];
static unsigned g_nL0b[4];
static unsigned g_nGL0, g_nGLn;
static LARGE_INTEGER g_qpf;
static double now_ms(void)
{
    LARGE_INTEGER t;
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)g_qpf.QuadPart;
}
static unsigned g_alphaProbed;
static unsigned g_fmtProbed;
static unsigned char g_lastMeanR, g_lastMeanG, g_lastMeanB, g_lastOpaque;

static unsigned engine_mip_levels(const void *renderer, const void *mat,
                                  unsigned *lodBiasOut);

const char *u9gl_texfmt_name(unsigned f)
{
    switch (f) {
    case 0x02: return "ALPHA_8";
    case 0x04: return "ALPHA_INTENSITY_44";
    case 0x05: return "P_8";
    case 0x0A: return "RGB_565";
    case 0x0B: return "ARGB_1555";
    case 0x0D: return "ALPHA_INTENSITY_88";
    default:   return "?";
    }
}

static unsigned pack_key(unsigned fmt, unsigned key)
{
    unsigned r = (key >> 16) & 0xFF, g = (key >> 8) & 0xFF, b = key & 0xFF;
    switch (fmt) {
    case 0x0A: return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case 0x0B: return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    default:   return 0xFFFFFFFFu;
    }
}

static void mipbias_report(const void *renderer)
{
    const char *mgr;
    static int done;
    if (done || !renderer) return;
    mgr = *(const char *const *)((const char *)renderer + U9_OFF_TEXMGR);
    if (!mgr) return;
    done = 1;
    {
        unsigned flag = *(const unsigned char *)(mgr + 0x39);
        int      bias = *(const int *)(mgr + 0x3C);
        u9log_line("GLMIP  mgr+0x39 flag=%u  mgr+0x3C bias=%d  -> effective LOD "
                   "bias %s (uploads at full size are %s)",
                   flag, bias,
                   (flag && bias > 0) ? "NON-ZERO" : "0",
                   (flag && bias > 0) ? "WRONG - the engine uploads smaller"
                                      : "correct");
    }
}

static void palette_report(void)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)U9_G_TEX_PALETTE;
    static int done;
    unsigned i, nonzero = 0, fourth = 0;
    if (done) return;
    done = 1;
    if (IsBadReadPtr(p, U9_PAL_ENTRIES * 4)) {
        u9log_line("GLPAL  0x%08X unreadable", U9_G_TEX_PALETTE);
        return;
    }
    for (i = 0; i < U9_PAL_ENTRIES * 4; i++) if (p[i]) nonzero++;
    for (i = 0; i < U9_PAL_ENTRIES; i++) if (p[i*4+3]) fourth++;
    u9log_line("GLPAL  0x%08X: %u of %u bytes non-zero, %u entries with a "
               "non-zero 4th byte", U9_G_TEX_PALETTE, nonzero,
               U9_PAL_ENTRIES * 4, fourth);
    u9log_line("GLPAL    [0]=%u,%u,%u,%u  [1]=%u,%u,%u,%u  [128]=%u,%u,%u,%u  "
               "[254]=%u,%u,%u,%u  [255]=%u,%u,%u,%u",
               p[0],p[1],p[2],p[3], p[4],p[5],p[6],p[7],
               p[512],p[513],p[514],p[515],
               p[1016],p[1017],p[1018],p[1019],
               p[1020],p[1021],p[1022],p[1023]);
}

static int decode(unsigned fmt, const unsigned char *src, unsigned n,
                  unsigned char *dst)
{
    palette_report();
    const uint16_t *s16 = (const uint16_t *)src;
    const uint32_t *pal = (const uint32_t *)(uintptr_t)U9_G_TEX_PALETTE;
    unsigned i;

    switch (fmt) {
    case 0x0A:
        for (i = 0; i < n; i++) {
            uint16_t v = s16[i];
            dst[i*4+0] = (unsigned char)(((v >> 11) & 0x1F) * 255 / 31);
            dst[i*4+1] = (unsigned char)(((v >>  5) & 0x3F) * 255 / 63);
            dst[i*4+2] = (unsigned char)(( v        & 0x1F) * 255 / 31);
            dst[i*4+3] = 255;
        }
        return 1;
    case 0x0B:
        for (i = 0; i < n; i++) {
            uint16_t v = s16[i];
            dst[i*4+0] = (unsigned char)(((v >> 10) & 0x1F) * 255 / 31);
            dst[i*4+1] = (unsigned char)(((v >>  5) & 0x1F) * 255 / 31);
            dst[i*4+2] = (unsigned char)(( v        & 0x1F) * 255 / 31);
            dst[i*4+3] = (v & 0x8000) ? 255 : 0;
        }
        return 1;
    case 0x05:
        {
            const unsigned char *pb = (const unsigned char *)pal;
            unsigned k, any = 0;
            for (k = 0; k < U9_PAL_ENTRIES * 4 && !any; k++) any |= pb[k];
            if (!any) {
                u9log_line("GLTEX *** P_8 texture but the palette at 0x%08X is "
                           "empty - refusing to decode (would render black)",
                           U9_G_TEX_PALETTE);
                return 0;
            }
        }
        for (i = 0; i < n; i++) {
            unsigned idx = src[i];
            uint32_t c   = pal[idx];
            dst[i*4+0] = (unsigned char)( c        & 0xFF);
            dst[i*4+1] = (unsigned char)((c >>  8) & 0xFF);
            dst[i*4+2] = (unsigned char)((c >> 16) & 0xFF);
            dst[i*4+3] = (idx == U9_PAL_TRANSPARENT) ? 0 : 255;
        }
        return 1;
    case 0x02:
        for (i = 0; i < n; i++) {
            dst[i*4+0] = dst[i*4+1] = dst[i*4+2] = 255;
            dst[i*4+3] = src[i];
        }
        return 1;
    case 0x04:
        for (i = 0; i < n; i++) {
            unsigned char a = (unsigned char)((src[i] >> 4) * 17);
            unsigned char l = (unsigned char)((src[i] & 0x0F) * 17);
            dst[i*4+0] = dst[i*4+1] = dst[i*4+2] = l;
            dst[i*4+3] = a;
        }
        return 1;
    case 0x0D:
        for (i = 0; i < n; i++) {
            uint16_t v = s16[i];
            unsigned char l = (unsigned char)( v       & 0xFF);
            unsigned char a = (unsigned char)((v >> 8) & 0xFF);
            dst[i*4+0] = dst[i*4+1] = dst[i*4+2] = l;
            dst[i*4+3] = a;
        }
        return 1;
    default:
        return 0;
    }
}

typedef void *(U9CALL *GetTexelSourceFn)(void *mgr, int id, int id2, int lodBias);

typedef void *(U9CALL *TexLoadFn)(void *archive, int id, int id2, int lodBias);

static int f8_call_is_safe(int id, int id2);

static void *texel_load_own(int id, int id2, int lodBias)
{
    void *archive;
    if (id < 0 || id2 < 0) return NULL;
    archive = *(void *const *)(uintptr_t)U9_TEX_ARCHIVE;
    if (!archive) return NULL;

    if ((unsigned)id >= U9_TEXSRC_MAX_ID && !f8_call_is_safe(id, id2))
        return NULL;

    return ((TexLoadFn)(uintptr_t)((unsigned)id >= U9_TEXSRC_MAX_ID
                                   ? U9_TEX_LOAD_UI : U9_TEX_LOAD_BUFFER))
           (archive, id, id2, lodBias);
}

typedef void *(U9CALL *GetBitmapDescFn)(void *mgr, int texId);

static unsigned desc_bytes_per_texel(int id)
{
    GetBitmapDescFn get = (GetBitmapDescFn)U9_BITMAP_GET_DESC;
    void *bmgr = *(void *const *)(uintptr_t)U9_G_BITMAP_MGR;
    const unsigned char *desc;

    if (id < 0 || !bmgr) return 0;
    desc = (const unsigned char *)get(bmgr, id);
    if (!desc) return 0;
    return desc[U9_BMDESC_IS_16BIT] ? 2u : 1u;
}

static unsigned texel_size_own(int id, unsigned w, unsigned h)
{
    unsigned bpt = desc_bytes_per_texel(id);
    return bpt ? w * h * bpt : 0u;
}

static void texel_free_own(void *p)
{
    if (p) ((void (__cdecl *)(void *))(uintptr_t)U9_ENGINE_FREE)(p);
}

static unsigned texel_size(const void *renderer, int id, int id2)
{
    const char *mgr, *entry;
    if (id < 0 || (unsigned)id >= U9_TEXSRC_MAX_ID || id2 < 0) return 0;
    mgr = *(const char *const *)((const char *)renderer + U9_OFF_TEXMGR);
    if (!mgr) return 0;
    entry = *(const char *const *)(mgr + U9_TEXSRC_ARRAY + (unsigned)id * 4u);
    if (!entry) return 0;
    return *(const unsigned *)
           (entry + (unsigned)id2 * U9_TEXSRC_SLOT_STRIDE + U9_TEXSRC_SIZE);
}


unsigned u9gl_format_selector(const void *mat)
{
    GetBitmapDescFn get = (GetBitmapDescFn)U9_BITMAP_GET_DESC;
    void *mgr = *(void *const *)(uintptr_t)U9_G_BITMAP_MGR;
    const unsigned char *desc;
    int id;

    if (!mat || !mgr) return 0xFFFFFFFFu;
    id = *(const int16_t *)((const char *)mat + U9_MAT_TEXID);
    if (id < 0) return 0xFFFFFFFFu;
    desc = (const unsigned char *)get(mgr, id);
    if (!desc) return 0xFFFFFFFFu;
    return desc[U9_BMDESC_FMT_SELECT];
}

unsigned u9gl_format_for_material(const void *mat)
{
    GetBitmapDescFn get = (GetBitmapDescFn)U9_BITMAP_GET_DESC;
    void *mgr = *(void *const *)(uintptr_t)U9_G_BITMAP_MGR;
    const unsigned char *desc;
    unsigned flags;
    int id, c;

    if (!mat || !mgr) return 0;
    id    = *(const int16_t *)((const char *)mat + U9_MAT_TEXID);
    flags = *(const unsigned *)((const char *)mat + U9_MAT_FLAGS);
    if (id < 0) return 0;

    desc = (const unsigned char *)get(mgr, id);
    if (!desc) {
        g_descMiss++;
        if (g_descMiss <= 8)
            u9log_line("GLTEX descriptor not loaded for id=%d - deferring upload "
                       "rather than guessing (engine would fall back to id 0)", id);
        return 0;
    }

    c = desc[U9_BMDESC_FMT_SELECT];
    if (flags & U9_MATF_FMT_SELECT)
        return (c == 1) ? 0x0Bu : (c == 2) ? 0x0Du : (c == 3) ? 0x02u : 0x0Au;

    return (c == 2) ? 0x04u : 0x02u;
}

static unsigned g_f8Census, g_f8LastResident, g_f8Empties;
static const void *g_f8LastArchive;

static const char *f8_archive(void)
{
    return *(const char *const *)(uintptr_t)U9_TEX_ARCHIVE;
}

#define F8_NA        (-1)
#define F8_EMPTY      0
#define F8_RESIDENT   1

static int f8_slot_state(int id, int *slotOut)
{
    const char *arc = f8_archive();
    int rec;

    if (slotOut) *slotOut = 0;
    if (!arc) return F8_NA;
    if ((unsigned)id < U9_TEXUI_ID_MIN || (unsigned)id >= U9_TEXUI_ID_MAX)
        return F8_NA;
    rec = *(const int *)(arc + U9_TEXUI_SLOTS + (unsigned)id * 4u);
    if (slotOut) *slotOut = rec;
    return (rec == 0 || rec == -1) ? F8_EMPTY : F8_RESIDENT;
}

static void f8_census(void)
{
    const char *arc = f8_archive();
    unsigned i, resident = 0, empty = 0, pending = 0, freeSlots = 0;
    static unsigned lastFree = 0xFFFFFFFFu;

    if (arc != g_f8LastArchive) {
        u9log_line("F8 archive pointer changed: 0x%08X -> 0x%08X at frame %u"
                   " - every composed slot table moved with it",
                   (unsigned)(uintptr_t)g_f8LastArchive,
                   (unsigned)(uintptr_t)arc, u9log_frame());
        g_f8LastArchive = arc;
    }
    if (!arc) return;

    for (i = U9_TEXUI_ID_MIN; i < U9_TEXUI_ID_MAX; i++) {
        int v = *(const int *)(arc + U9_TEXUI_SLOTS + i * 4u);
        if (v == 0 || v == -1) empty++; else resident++;
        if (v == 0) freeSlots++; else if (v == -1) pending++;
    }
    if (!g_f8Census || resident != g_f8LastResident || freeSlots != lastFree) {
        u9log_line("F8 composed slots: %u resident, %u empty (%u free, %u pending"
                   " reclaim), of 1024, pending list %d  (was %u resident) frame %u",
                   resident, empty, freeSlots, pending,
                   *(const int *)(arc + U9_TEXUI_PENDING_COUNT),
                   g_f8LastResident, u9log_frame());
        g_f8LastResident = resident;
        lastFree = freeSlots;
    }
    g_f8Census++;
}

static int f8_call_is_safe(int id, int id2)
{
    static unsigned lastFrame = 0xFFFFFFFFu;
    unsigned frame = u9log_frame();
    int slot, state;

    if (u9cfg_int("watch_composed", 1) && frame != lastFrame) {
        lastFrame = frame;
        f8_census();
    }

    state = f8_slot_state(id, &slot);
    if (state != F8_EMPTY) return 1;

    g_f8Empties++;
    if (g_f8Empties <= 20 || (g_f8Empties % 500) == 0)
        u9log_line("F8 empty slot id=%d/%d slot=0x%08X frame %u - skipped;"
                   " calling would fault at 0x0041D95F (%u so far)",
                   id, id2, (unsigned)slot, frame, g_f8Empties);

    if (!u9cfg_int("fix_composed_slot", 1)) {
        u9log_line("F8 fix_composed_slot=0, making the call anyway");
        u9log_flush();
        return 1;
    }
    return 0;
}


#define U9GL_CW_MAX 48
static struct { int id, id2; unsigned char changed; } g_cw[U9GL_CW_MAX];
static unsigned g_cwCount, g_cwFrame = 0xFFFFFFFFu, g_cwLastSig, g_cwPrinted;

static void cw_flush(void)
{
    char line[600];
    unsigned n = 0, i, sig = 2166136261u;
    if (!g_cwCount) return;
    for (i = 0; i < g_cwCount; i++) {
        sig ^= (unsigned)(g_cw[i].id * 131 + g_cw[i].id2 * 7 + g_cw[i].changed);
        sig *= 16777619u;
    }
    if (sig != g_cwLastSig) {
        for (i = 0; i < g_cwCount && n + 24 < sizeof(line); i++)
            n += (unsigned)sprintf(line + n, "%s%d/%d%s", i ? " " : "",
                                   g_cw[i].id, g_cw[i].id2,
                                   g_cw[i].changed ? "*" : "");
        line[n] = 0;
        u9log_line("CW f%u %s", g_cwFrame, line);
        g_cwLastSig = sig;
        g_cwPrinted++;
    }
    g_cwCount = 0;
}

static void cw_note(int id, int id2, int changed)
{
    unsigned f, i;
    if (!u9cfg_int("watch_compass", 1)) return;
    f = u9log_frame();
    if (f != g_cwFrame) { cw_flush(); g_cwFrame = f; }
    for (i = 0; i < g_cwCount; i++)
        if (g_cw[i].id == id && g_cw[i].id2 == id2) {
            if (changed) g_cw[i].changed = 1;
            return;
        }
    if (g_cwCount >= U9GL_CW_MAX) return;
    g_cw[g_cwCount].id      = id;
    g_cw[g_cwCount].id2     = id2;
    g_cw[g_cwCount].changed = (unsigned char)(changed ? 1 : 0);
    g_cwCount++;
}

static const unsigned char *texel_source(const void *renderer, int id, int id2,
                                         int allowAccessor)
{
    char *mgr;
    const char *entry;
    const unsigned char *data;

    if (id < 0 || id2 < 0) return NULL;
    mgr = *(char *const *)((const char *)renderer + U9_OFF_TEXMGR);

    if (!mgr && (unsigned)id < U9_TEXSRC_MAX_ID) return NULL;

    entry = (mgr && (unsigned)id < U9_TEXSRC_MAX_ID)
          ? *(const char *const *)(mgr + U9_TEXSRC_ARRAY + (unsigned)id * 4u)
          : NULL;
    if (entry) {
        data = *(const unsigned char *const *)
               (entry + (unsigned)id2 * U9_TEXSRC_SLOT_STRIDE + U9_TEXSRC_DATA);
        if (data) return data;
    }
    if (!allowAccessor || !u9cfg_int("tex_accessor", 1))
        return NULL;
    if (!f8_call_is_safe(id, id2)) return NULL;
    return (const unsigned char *)
           ((GetTexelSourceFn)U9_TEX_GET_SOURCE)(mgr, id, id2, 0);
}

static void why_fail(int id, int id2, const char *reason, unsigned a, unsigned b);

static unsigned bytes_per_texel(unsigned fmt)
{
    return (fmt == 0x0A || fmt == 0x0B || fmt == 0x0D) ? 2u : 1u;
}

static unsigned g_texBytes;
static unsigned g_evictions;
static unsigned g_redownload;
unsigned u9log_frame(void);


static int cached_index(int id, int id2, unsigned fmt, unsigned key)
{
    unsigned i;
    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].id == id && g_cache[i].id2 == id2 &&
            g_cache[i].fmt == fmt && g_cache[i].key == key) {
            g_cache[i].lastUsed = u9log_frame();
            return (int)i;
        }
    return -1;
}

static int cached(int id, int id2, unsigned fmt, unsigned key)
{
    return cached_index(id, id2, fmt, key) >= 0;
}


static int evict_one(void)
{
    unsigned i, best = 0;
    unsigned now = u9log_frame();
    int found = 0;
    for (i = 0; i < g_cacheCount; i++) {
        if (now - g_cache[i].lastUsed < U9GL_TEX_MIN_AGE) continue;
        if (!found || g_cache[i].lastUsed < g_cache[best].lastUsed) {
            best = i; found = 1;
        }
    }
    if (!found) return 0;

    glDeleteTextures(1, &g_cache[best].tex);
    u9metrics_tex_destroyed(g_cache[best].bytes);
    g_texBytes -= g_cache[best].bytes;
    g_evictions++;
    if (g_evictions <= 10 || (g_evictions % 200) == 0)
        u9log_line("GLTEX evicted id=%d/%d (%dx%d, %u KB, idle %u frames) - "
                   "%u evictions, cache now %u entries / %u MB",
                   g_cache[best].id, g_cache[best].id2,
                   g_cache[best].w, g_cache[best].h, g_cache[best].bytes / 1024u,
                   now - g_cache[best].lastUsed, g_evictions,
                   g_cacheCount - 1, (g_texBytes) / (1024u * 1024u));
    g_cache[best] = g_cache[g_cacheCount - 1];
    g_cacheCount--;
    return 1;
}

extern int g_u9ReadCache;
static int region_ok(const unsigned char *a, const unsigned char **stop)
{
    static const unsigned char *okBase[8], *okEnd[8];
    static DWORD okTick;
    static unsigned okNext;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD tick = GetTickCount();
    int i, n = g_u9ReadCache < 1 ? 1 : g_u9ReadCache > 8 ? 8 : g_u9ReadCache;
    if (tick != okTick) {
        okTick = tick;
        for (i = 0; i < 8; i++) okEnd[i] = NULL;
    }
    for (i = 0; i < n; i++)
        if (okEnd[i] && a >= okBase[i] && a < okEnd[i]) { *stop = okEnd[i]; return 1; }
    if (!VirtualQuery(a, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    i = (int)(okNext++ % (unsigned)n);
    okBase[i] = (const unsigned char *)mbi.BaseAddress;
    okEnd[i]  = okBase[i] + mbi.RegionSize;
    *stop = okEnd[i];
    return 1;
}

static int readable_range(const void *p, unsigned n)
{
    const unsigned char *a = (const unsigned char *)p;
    const unsigned char *end = a + n;

    if (!p || !n) return 0;
    while (a < end) {
        const unsigned char *stop;
        if (!region_ok(a, &stop)) return 0;
        if (stop <= a) return 0;
        a = stop;
    }
    return 1;
}

#define U9GL_WHYFAIL 32
static int      g_whyId[U9GL_WHYFAIL];
static unsigned g_whyCount;

static void why_fail(int id, int id2, const char *reason, unsigned a, unsigned b)
{
    unsigned i;
    for (i = 0; i < g_whyCount; i++) if (g_whyId[i] == id) return;
    if (g_whyCount >= U9GL_WHYFAIL) return;
    g_whyId[g_whyCount++] = id;
    u9log_line("GLPRE  id=%d/%d NOT prefetched: %s (%u, %u)", id, id2, reason, a, b);
}

#define U9GL_OFFER_MAX 2048
static struct { int id, id2; unsigned hits; } g_offer[U9GL_OFFER_MAX];
static unsigned g_offerCount, g_offerCalls, g_offerNullMat, g_offerOverflow;

static void note_offered(int id, int id2)
{
    unsigned i;
    g_offerCalls++;
    for (i = 0; i < g_offerCount; i++)
        if (g_offer[i].id == id && g_offer[i].id2 == id2) { g_offer[i].hits++; return; }
    if (g_offerCount >= U9GL_OFFER_MAX) { g_offerOverflow++; return; }
    g_offer[g_offerCount].id = id; g_offer[g_offerCount].id2 = id2;
    g_offer[g_offerCount].hits = 1; g_offerCount++;
}

void u9gl_offered_report(void)
{
    unsigned i, n = 0;
    char line[700];
    u9log_line("OFFER prefetch called %u times, %u with a NULL material,"
               " %u distinct ids%s", g_offerCalls, g_offerNullMat, g_offerCount,
               g_offerOverflow ? " (TABLE FULL, a floor)" : "");
    n = 0;
    for (i = 0; i < g_offerCount; i++) {
        n += (unsigned)sprintf(line + n, "%s%d/%d", n ? " " : "",
                               g_offer[i].id, g_offer[i].id2);
        if (n + 16 >= sizeof(line) || i + 1 == g_offerCount) {
            line[n] = 0;
            u9log_line("OFFER   %s", line);
            n = 0;
        }
    }
}

void u9gl_prefetch(const void *renderer, const void *mat)
{
    int id, id2, log2w, log2h;
    unsigned fmt, w, h, want, have, i;
    const unsigned char *src;
    unsigned char *copy;
    void *ownBuf = NULL;
    GLuint refreshTex = 0;
    unsigned srcHash = 0;
    unsigned total = 0, chainLevels = 0, srcLevels = 0;
    int counted = 0;

    if (!renderer || !mat) { g_offerNullMat++; return; }
    id  = *(const int16_t *)((const char *)mat + U9_MAT_TEXID);
    id2 = *(const int16_t *)((const char *)mat + U9_MAT_TEXID2);
    note_offered(id, id2);
    if (id < 0 || id2 < 0) return;

    fmt = u9gl_format_for_material(mat);
    if (!fmt) { why_fail(id, id2, "no format derived (descriptor missing?)", 0, 0); return; }
    {
        int hit = cached_index(id, id2, fmt, U9GL_NO_KEY);
        if (hit >= 0) {
            unsigned mflags = *(const unsigned *)((const char *)mat + U9_MAT_FLAGS);
            int stale = (mflags & 0x40u) && u9cfg_int("tex_honour_redownload", 1);

            if (!stale && (unsigned)id >= U9_TEXSRC_MAX_ID &&
                u9cfg_int("tex_recache_composed", 1) &&
                g_cache[hit].lastUpload != u9log_frame()) {
                refreshTex = g_cache[hit].tex;
                stale = 1;
                counted = 1;
                g_recomposed++;
                if (g_recomposed <= 4 || (g_recomposed % 2000) == 0)
                    u9log_line("GLTEX id=%d/%d: composed texture, re-reading "
                               "for this frame (%u so far)",
                               id, id2, g_recomposed);
            }
            if (!stale)
                return;

            refreshTex = g_cache[hit].tex;
            if (!(mflags & 0x40u)) {
            } else {
                g_redownload++;
                counted = 1;
                if (g_redownload <= 12 || (g_redownload % 500) == 0)
                    u9log_line("GLTEX id=%d/%d: material flag 0x40 - the engine "
                               "is re-downloading this texture. Refreshing ours "
                               "in place (%u so far)", id, id2, g_redownload);
            }
        }
    }
    for (i = 0; i < g_pendCount; i++)
        if (g_pending[i].id == id && g_pending[i].id2 == id2 &&
            g_pending[i].fmt == fmt) { abandon(counted, U9GL_ABANDON_PENDING_DUP); return; }
    if (g_pendCount >= U9GL_PENDING) {
        g_pendDropped++;
        abandon(counted, U9GL_ABANDON_PENDING_FULL);
        why_fail(id, id2, "pending queue full", g_pendCount, U9GL_PENDING);
        return;
    }

    log2w = *(const int16_t *)((const char *)mat + U9_MAT_LOG2W);
    log2h = *(const int16_t *)((const char *)mat + U9_MAT_LOG2H);
    if (log2w < 0 || log2w > 11 || log2h < 0 || log2h > 11) {
        abandon(counted, U9GL_ABANDON_DIMS);
        why_fail(id, id2, "bad log2 dimensions", (unsigned)log2w, (unsigned)log2h);
        return;
    }
    w = 1u << log2w;  h = 1u << log2h;
    want = w * h * bytes_per_texel(fmt);

    {
        int composed = ((unsigned)id >= U9_TEXSRC_MAX_ID);
        int useOwn   = u9cfg_int("tex_own_loader", 1) && !composed;
        void *own    = useOwn ? texel_load_own(id, id2, 0) : NULL;

        if (own) {
            ownBuf = own;
            src = (const unsigned char *)own;
            g_ownLoads++;
        } else {
            src = texel_source(renderer, id, id2, 1);
            if (composed) g_composedFromMgr++;
            else if (u9cfg_int("tex_own_loader", 1)) g_ownFallbacks++;
            if (!src && composed) {
                own = texel_load_own(id, id2, 0);
                if (own) { ownBuf = own; src = (const unsigned char *)own;
                           g_composedFallback++; }
            }
        }
    }
    if (!src) { abandon(counted, U9GL_ABANDON_NO_SRC);
                why_fail(id, id2, "no texel source even with the accessor", 0, 0);
                goto done; }

    have = texel_size_own(id, w, h);
    if (!have) have = texel_size(renderer, id, id2);
    if (have && w && h) {
        unsigned bpt = have / (w * h);
        if (bpt == 1 && bytes_per_texel(fmt) == 2) {
            why_fail(id, id2, "descriptor said 16-bit, buffer is 8-bit: "
                              "using ALPHA_8 instead of", fmt, 0x02);
            fmt = 0x02;
        } else if (bpt == 2 && bytes_per_texel(fmt) == 1) {
            why_fail(id, id2, "descriptor said 8-bit, buffer is 16-bit: "
                              "using RGB_565 instead of", fmt, 0x0A);
            fmt = 0x0A;
        }
        want = w * h * bytes_per_texel(fmt);
    }
    if (have && want > have) {
        abandon(counted, U9GL_ABANDON_FORMAT);
        why_fail(id, id2, "size contradicts format even after correction", want, have);
        goto done;
    }

    if (!readable_range(src, want)) {
        abandon(counted, U9GL_ABANDON_UNREADABLE);
        why_fail(id, id2, "source buffer is not readable for the whole copy",
                 want, have);
        goto done;
    }

    if (u9cfg_int("tex_source_check", 1)) {
        static unsigned checked, same, differ;
        if (checked < 32u) {
            void *own = ownBuf ? NULL : texel_load_own(id, id2, 0);
            const unsigned char *a = ownBuf ? texel_source(renderer, id, id2, 1)
                                            : (const unsigned char *)own;
            checked++;
            if (!a) {
                u9log_line("GLSRC id=%d/%d: the other path had no buffer where"
                           " this one did", id, id2);
            } else if (!readable_range(a, want)) {
                differ++;
                u9log_line("GLSRC id=%d/%d %ux%u fmt=0x%02X: archive buffer is"
                           " SHORTER than %u bytes - the cached one is not level 0",
                           id, id2, w, h, fmt, want);
            } else if (memcmp(a, src, want) == 0) {
                same++;
                if (same <= 4 || (same % 8) == 0)
                    u9log_line("GLSRC id=%d/%d %ux%u fmt=0x%02X: archive and"
                               " manager agree over %u bytes  (%u same, %u differ)",
                               id, id2, w, h, fmt, want, same, differ);
            } else {
                unsigned k, first = want;
                for (k = 0; k < want; k++)
                    if (a[k] != src[k]) { first = k; break; }
                differ++;
                u9log_line("GLSRC id=%d/%d %ux%u fmt=0x%02X: archive and manager"
                           " DIFFER, first at byte %u of %u  (%u same, %u differ)",
                           id, id2, w, h, fmt, first, want, same, differ);
            }
            texel_free_own(own);
        }
    }

    {
        static unsigned n, agree, clash;
        unsigned mine = texel_size_own(id, w, h);
        if (n < 64u && have && mine) {
            n++;
            if (mine == have) agree++; else clash++;
            if (mine != have || agree <= 4 || (agree % 16) == 0)
                u9log_line("GLSZ  id=%d/%d %ux%u: descriptor says %u bytes, "
                           "Glide's manager says %u  -> %s  (%u agree, %u clash)",
                           id, id2, w, h, mine, have,
                           mine == have ? "same" : "DIFFERENT", agree, clash);
        }
    }

    if (fmt == 0x0A && g_fmtProbed < 6u && w * h >= 4096u) {
        const uint16_t *v = (const uint16_t *)src;
        unsigned k, cnt = w * h;
        if (readable_range(src, cnt * 2u)) {
            unsigned long long r5=0,g6=0,b5=0, ra=0,ga=0,ba=0, rq=0,gq=0,bq=0;
            g_fmtProbed++;
            for (k = 0; k < cnt; k++) {
                uint16_t t = v[k];
                r5 += ((t >> 11) & 0x1Fu) * 255u / 31u;
                g6 += ((t >>  5) & 0x3Fu) * 255u / 63u;
                b5 += ( t        & 0x1Fu) * 255u / 31u;
                ra += ((t >> 10) & 0x1Fu) * 255u / 31u;
                ga += ((t >>  5) & 0x1Fu) * 255u / 31u;
                ba += ( t        & 0x1Fu) * 255u / 31u;
                rq += ((t >> 11) & 0x1Fu) * 255u / 31u;
                gq += ((t >>  6) & 0x1Fu) * 255u / 31u;
                bq += ((t >>  1) & 0x1Fu) * 255u / 31u;
            }
            u9log_line("GLFMT id=%d/%d %ux%u: mean as RGB_565 #%02X%02X%02X, "
                       "as ARGB_1555 #%02X%02X%02X, as RGBA_5551 #%02X%02X%02X",
                       id, id2, w, h,
                       (unsigned)(r5/cnt),(unsigned)(g6/cnt),(unsigned)(b5/cnt),
                       (unsigned)(ra/cnt),(unsigned)(ga/cnt),(unsigned)(ba/cnt),
                       (unsigned)(rq/cnt),(unsigned)(gq/cnt),(unsigned)(bq/cnt));
        }
    }

    if (fmt == 0x0B && g_alphaProbed < 6u) {
        const uint16_t *v = (const uint16_t *)src;
        unsigned k, hi = 0, lo = 0, rHi = 0, rLo = 0, cnt = w * h;
        if (readable_range(src, cnt * 2u)) {
            g_alphaProbed++;
            for (k = 0; k < cnt; k++) {
                if (v[k] & 0x8000u) hi++;
                if (v[k] & 0x0001u) lo++;
                rHi += (v[k] >> 10) & 0x1Fu;
                rLo += (v[k] >> 11) & 0x1Fu;
            }
            u9log_line("GLALPHA id=%d/%d %ux%u fmt=0x0B: bit15 set in %u%% of "
                       "texels (ARGB_1555 reading), bit0 set in %u%% "
                       "(RGBA_5551 reading); mean red %u vs %u of 31",
                       id, id2, w, h, hi * 100u / cnt, lo * 100u / cnt,
                       rHi / cnt, rLo / cnt);
        }
    }

    {
        unsigned bias = 0;
        chainLevels = engine_mip_levels(renderer, mat, &bias);
        total = want;
        if (ownBuf && chainLevels) {
            unsigned bpt = bytes_per_texel(fmt), i;
            for (i = 1; i <= chainLevels; i++) {
                unsigned lw = w >> i, lh = h >> i;
                if (!lw || !lh) { chainLevels = i - 1; break; }
                total += lw * lh * bpt;
            }
            if (chainLevels && readable_range(src, total)) {
                srcLevels = chainLevels;
            } else {
                total = want;
            }
        }
        if (bias && !g_lodBiasWarned) {
            g_lodBiasWarned = 1;
            u9log_line("GLTEX *** GameMipMapLevel > 1: engine lodBias=%u, so it "
                       "loads textures %u level(s) smaller than the %ux%u we "
                       "derive from the material. GL will be sharper than Glide "
                       "and the sizes disagree. Set GameMipMapLevel=1.",
                       bias, bias, w, h);
        }
        if (srcLevels) g_chainFromArchive++; else if (chainLevels) g_chainFiltered++;
    }

    if ((unsigned)id >= U9_TEXSRC_MAX_ID && u9cfg_int("tex_skip_unchanged", 1)) {
        unsigned h = 2166136261u, k = 0;
        if (u9cfg_int("tex_hash_full", 1)) {
            while ((k < want) && (((uintptr_t)(src + k)) & 3u)) {
                h ^= src[k]; h *= 16777619u; k++;
            }
            for (; k + 4u <= want; k += 4u) {
                h ^= *(const unsigned *)(src + k); h *= 16777619u;
            }
            for (; k < want; k++) { h ^= src[k]; h *= 16777619u; }
        } else {
            unsigned step = want / 256u;
            if (!step) step = 1u;
            for (k = 0; k < want; k += step) { h ^= src[k]; h *= 16777619u; }
        }
        h ^= want; if (!h) h = 1u;
        srcHash = h;
        {
            static unsigned lastWatch; static int watchSeen;
            int wid = u9cfg_int("compose_watch_id", 0);
            if (wid && id == wid) {
                if (!watchSeen || lastWatch != h) {
                    u9log_line("COMPOSE id=%d/%d source hash %08X -> %08X"
                               " at frame %u%s", id, id2, lastWatch, h,
                               u9log_frame(), watchSeen ? "" : "  (first sight)");
                    lastWatch = h; watchSeen = 1;
                }
            }
        }
        {
            int w = cached_index(id, id2, fmt, U9GL_NO_KEY);
            cw_note(id, id2, !(w >= 0 && g_cache[w].srcHash == h));
        }
        if (refreshTex) {
            int e = cached_index(id, id2, fmt, U9GL_NO_KEY);
            if (e >= 0 && g_cache[e].srcHash == h) {
                g_cache[e].lastUpload = u9log_frame();
                g_skippedUnchanged++;
                goto done;
            }
        }
    }

    copy = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, total);
    if (!copy) { abandon(counted, U9GL_ABANDON_ALLOC); goto done; }
    memcpy(copy, src, total);

    g_pending[g_pendCount].id    = id;
    g_pending[g_pendCount].id2   = id2;
    g_pending[g_pendCount].fmt   = fmt;
    g_pending[g_pendCount].w     = w;
    g_pending[g_pendCount].h     = h;
    g_pending[g_pendCount].bytes     = total;
    g_pending[g_pendCount].data      = copy;
    g_pending[g_pendCount].levels    = chainLevels;
    g_pending[g_pendCount].reuseTex  = refreshTex;
    g_pending[g_pendCount].srcHash   = srcHash;
    g_pending[g_pendCount].srcLevels = srcLevels;
    g_pendCount++;
done:
    texel_free_own(ownBuf);
}

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

static unsigned engine_mip_levels(const void *renderer, const void *mat,
                                  unsigned *lodBiasOut)
{
    GetBitmapDescFn get = (GetBitmapDescFn)U9_BITMAP_GET_DESC;
    void *bmgr = *(void *const *)(uintptr_t)U9_G_BITMAP_MGR;
    const unsigned char *desc;
    const char *r = (const char *)renderer;
    unsigned mipCount, bias;
    int id, log2w, log2h;

    if (lodBiasOut) *lodBiasOut = 0;
    if (!r || !mat || !bmgr) return 0;
    if (!*(const unsigned char *)(r + U9_OFF_MIP_ENABLED)) return 0;

    id = *(const int16_t *)((const char *)mat + U9_MAT_TEXID);
    if (id < 0) return 0;
    desc = (const unsigned char *)get(bmgr, id);
    if (!desc) return 0;
    mipCount = desc[U9_BMDESC_MIP_COUNT];

    log2w = *(const int16_t *)((const char *)mat + U9_MAT_LOG2W);
    log2h = *(const int16_t *)((const char *)mat + U9_MAT_LOG2H);
    bias  = (log2w > U9_MIP_MIN_LOG2 && log2h > U9_MIP_MIN_LOG2)
          ? *(const unsigned *)(r + U9_OFF_MIP_LODBIAS) : 0u;
    if (bias > mipCount) bias = mipCount;
    if (lodBiasOut) *lodBiasOut = bias;
    {
        static int announced;
        if (mipCount < 16u) g_mipHist[mipCount]++;
        if (!announced) {
            announced = 1;
            u9log_line("GLMIP  slot 47 state: GameMipMapLevel=%u enabled=%u "
                       "lodBias=%u (renderer +0x64/+0x5C/+0x60). First texture "
                       "id=%d has mipCount=%u -> %u levels below the base; GL "
                       "now builds that many and stops where Glide stops.",
                       *(const unsigned *)(r + U9_OFF_MIP_LEVEL),
                       (unsigned)*(const unsigned char *)(r + U9_OFF_MIP_ENABLED),
                       *(const unsigned *)(r + U9_OFF_MIP_LODBIAS),
                       id, mipCount, mipCount - bias);
        }
    }
    return mipCount - bias;
}



int u9gl_mips_enabled(void);

static unsigned resident_bytes(unsigned w, unsigned h, unsigned levels)
{
    unsigned n = w * h * 4u, total = n, i;
    if (!u9gl_mips_enabled()) return n;
    for (i = 1; i <= levels && n > 4u; i++) { n /= 4u; total += n; }
    return total;
}



static int g_useMips = -1;
static int g_mipFilter = -1;

int u9gl_mips_enabled(void)
{
    if (g_useMips < 0) g_useMips = u9cfg_int("mipmaps", 1);
    return g_useMips;
}

static const char *mip_name(int m)
{
    return m == 0 ? "OFF - GL_LINEAR, no mip sampling"
         : m == 1 ? "NEAREST LEVEL - GL_LINEAR_MIPMAP_NEAREST, Glide's default"
                  : "TRILINEAR - GL_LINEAR_MIPMAP_LINEAR, blends two levels";
}

const char *u9gl_mip_filter_name(void)
{
    if (g_mipFilter < 0) g_mipFilter = u9cfg_int("mip_filter", 1);
    return g_mipFilter == 0 ? "off" : g_mipFilter == 1 ? "nearest" : "trilinear";
}

const char *u9gl_cycle_mip_filter(void)
{
    if (g_mipFilter < 0) g_mipFilter = u9cfg_int("mip_filter", 1);
    g_mipFilter = (g_mipFilter + 1) % 3;
    return mip_name(g_mipFilter);
}

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

static float g_aniso = -1.0f;

float u9gl_anisotropy(void)
{
    if (g_aniso < 0.0f) {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        int want = u9cfg_int("anisotropy", 8);
        g_aniso = 0.0f;
        if (ext && strstr(ext, "GL_EXT_texture_filter_anisotropic") && want > 1) {
            GLfloat cap = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &cap);
            g_aniso = ((float)want < cap) ? (float)want : cap;
            u9log_line("GLTEX anisotropic filtering %.0fx (asked %d, driver "
                       "caps at %.0f)", g_aniso, want, cap);
        } else {
            u9log_line("GLTEX anisotropic filtering off (%s)",
                       want <= 1 ? "disabled in config"
                                 : "GL_EXT_texture_filter_anisotropic absent");
        }
    }
    return g_aniso;
}

GLint u9gl_min_filter(void)
{
    if (!u9gl_mips_enabled()) return GL_LINEAR;
    if (g_mipFilter < 0) g_mipFilter = u9cfg_int("mip_filter", 1);
    switch (g_mipFilter) {
    case 0:  return GL_LINEAR;
    case 2:  return GL_LINEAR_MIPMAP_LINEAR;
    default: return GL_LINEAR_MIPMAP_NEAREST;
    }
}

static int is_pot(unsigned v) { return v && !(v & (v - 1)); }

static void box_half(const unsigned char *s, unsigned w, unsigned h,
                     unsigned char *d, unsigned dw, unsigned dh)
{
    unsigned sx = (w > 1) ? 2u : 1u, sy = (h > 1) ? 2u : 1u;
    unsigned x, y;
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            unsigned ax, ay, n = 0, aw = 0;
            unsigned wr = 0, wg = 0, wb = 0, fr = 0, fg = 0, fb = 0, fa = 0;
            for (ay = 0; ay < sy; ay++) {
                for (ax = 0; ax < sx; ax++) {
                    const unsigned char *t =
                        s + ((((y * sy) + ay) * w) + (x * sx) + ax) * 4u;
                    unsigned a = t[3];
                    n++;
                    aw += a; fa += a;
                    fr += t[0]; fg += t[1]; fb += t[2];
                    wr += t[0] * a; wg += t[1] * a; wb += t[2] * a;
                }
            }
            {
                unsigned char *o = d + ((y * dw) + x) * 4u;
                if (aw) { o[0] = (unsigned char)(wr / aw);
                          o[1] = (unsigned char)(wg / aw);
                          o[2] = (unsigned char)(wb / aw); }
                else    { o[0] = (unsigned char)(fr / n);
                          o[1] = (unsigned char)(fg / n);
                          o[2] = (unsigned char)(fb / n); }
                o[3] = (unsigned char)(fa / n);
            }
        }
    }
}

static int level_has_contrast(const unsigned char *rgba, unsigned n)
{
    unsigned i, lo = 255, hi = 0;
    for (i = 0; i < n; i++) {
        unsigned g = rgba[i * 4u + 1];
        if (g < lo) lo = g;
        if (g > hi) hi = g;
        if (hi - lo > 24u) return 1;
    }
    return 0;
}

static int alpha_is_binary(const unsigned char *rgba, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        unsigned a = rgba[i * 4u + 3u];
        if (a != 0u && a != 255u) return 0;
    }
    return 1;
}

static double alpha_coverage(const unsigned char *rgba, unsigned n, unsigned ref)
{
    unsigned i, c = 0;
    if (!n) return 0.0;
    for (i = 0; i < n; i++) if (rgba[i * 4u + 3u] >= ref) c++;
    return (double)c / (double)n;
}

static void preserve_coverage(unsigned char *rgba, unsigned n,
                              double want, unsigned ref)
{
    unsigned hist[256], i, cum = 0, target, t = ref;
    double k;

    if (!n || want <= 0.0) return;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++) hist[rgba[i * 4u + 3u]]++;

    target = (unsigned)(want * (double)n + 0.5);
    if (!target) return;
    for (i = 255; i > 0; i--) {
        cum += hist[i];
        if (cum >= target) { t = i; break; }
    }
    if (!t) return;

    k = (double)ref / (double)t;
    if (k > 16.0) k = 16.0;
    if (k < 1.01) return;

    for (i = 0; i < n; i++) {
        unsigned a = (unsigned)((double)rgba[i * 4u + 3u] * k + 0.5);
        rgba[i * 4u + 3u] = (unsigned char)(a > 255u ? 255u : a);
    }
}

static unsigned g_alphaChecked, g_alphaLost;

static double alpha_mean(const unsigned char *rgba, unsigned n)
{
    unsigned long long s = 0;
    unsigned i;
    for (i = 0; i < n; i++) s += rgba[i * 4u + 3u];
    return n ? (double)s / (double)n : 255.0;
}

static double alpha_check(int id, int id2, unsigned level, unsigned w, unsigned h,
                          const unsigned char *rgba, double parentMean)
{
    double m = alpha_mean(rgba, w * h);
    g_alphaChecked++;
    if (m + 1.0 < parentMean) {
        g_alphaLost++;
        if (g_alphaLost <= 20)
            u9log_line("GLALPHA *** id=%d/%d level %u (%ux%u): mean alpha %.1f, "
                       "its parent %.1f - opacity LOST while building mips",
                       id, id2, level, w, h, m, parentMean);
    }
    return m;
}

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
typedef void (APIENTRY *TexStorage2DFn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static TexStorage2DFn g_texStorage;
static int            g_storageProbed;
static unsigned       g_storageUsed, g_storageAbsent;

static int tex_storage_ready(void)
{
    if (!g_storageProbed) {
        const char *ext;
        g_storageProbed = 1;
        if (!u9cfg_int("tex_storage", 1)) {
            u9log_line("GLTEX glTexStorage2D disabled in config");
            return 0;
        }
        g_texStorage = (TexStorage2DFn)(void *)wglGetProcAddress("glTexStorage2D");
        ext = (const char *)glGetString(GL_EXTENSIONS);
        if (!g_texStorage && ext && strstr(ext, "GL_ARB_texture_storage"))
            g_texStorage = (TexStorage2DFn)(void *)wglGetProcAddress("glTexStorage2DARB");
        u9log_line("GLTEX glTexStorage2D %s - %s",
                   g_texStorage ? "present" : "MISSING",
                   g_texStorage ? "whole mip chain allocated in one call"
                                : "falling back to a glTexImage2D per level");
    }
    return g_texStorage != 0;
}

static unsigned chain_levels(unsigned w, unsigned h)
{
    unsigned m = (w > h) ? w : h, n = 1;
    while (m > 1u) { m >>= 1; n++; }
    return n;
}

static int g_subUpload;

static void tex_level(unsigned level, unsigned w, unsigned h, const void *px)
{
    double t = now_ms();
    if (g_subUpload)
        glTexSubImage2D(GL_TEXTURE_2D, (GLint)level, 0, 0,
                        (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    else
        glTexImage2D(GL_TEXTURE_2D, (GLint)level, GL_RGBA,
                     (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    {
        double dt = now_ms() - t;
        g_msGL += dt;
        if (level == 0) {
            g_msGL0 += dt; g_nGL0++;
            unsigned px = w * h, b = 0;
            while (px > 4096u && b < 3u) { px >>= 2; b++; }
            g_msL0b[b] += dt; g_nL0b[b]++;
        }
        else            { g_msGLn += dt; g_nGLn++; }
    }
}

static unsigned upload_archive_chain(const Pending *p, const unsigned char *rgba0)
{
    unsigned bpt = bytes_per_texel(p->fmt);
    unsigned off = p->w * p->h * bpt;
    unsigned last = 0, i;
    unsigned char *tmp = NULL;
    unsigned char *refA = NULL, *refB = NULL;
    const unsigned char *refCur = NULL;
    unsigned refW = p->w, refH = p->h;
    int checking = 0;

    if (p->srcLevels && g_chainChecked < 6u &&
        level_has_contrast(rgba0, p->w * p->h)) {
        refA = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (p->w * p->h * 4u) + 16u);
        refB = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (p->w * p->h * 4u) + 16u);
        if (refA && refB) { checking = 1; refCur = rgba0; g_chainChecked++; }
    }

    tex_level(0, p->w, p->h, rgba0);

    for (i = 1; i <= p->srcLevels; i++) {
        unsigned lw = p->w >> i, lh = p->h >> i, n;
        if (!lw || !lh) break;
        n = lw * lh;
        if (off + n * bpt > p->bytes) break;
        if (!tmp) {
            tmp = (unsigned char *)HeapAlloc(GetProcessHeap(), 0,
                                             (p->w * p->h) + 16u);
            if (!tmp) break;
        }
        if (!decode(p->fmt, p->data + off, n, tmp)) break;

        if (checking && refCur) {
            unsigned char *refNext = (last & 1u) ? refB : refA;
            unsigned k, diff = 0;
            box_half(refCur, refW, refH, refNext, lw, lh);
            for (k = 0; k < n * 4u; k++)
                diff += (unsigned)(refNext[k] > tmp[k] ? refNext[k] - tmp[k]
                                                       : tmp[k] - refNext[k]);
            diff /= (n * 4u);
            if (diff > 40u) g_chainSuspect++;
            u9log_line("GLCHAIN id=%d/%d level %u (%ux%u): archive vs "
                       "box-filtered mean |diff| = %u/255 %s",
                       p->id, p->id2, i, lw, lh, diff,
                       diff > 40u ? "*** SUSPECT - wrong offset for this level"
                                  : "(consistent)");
            refCur = refNext; refW = lw; refH = lh;
        }

        tex_level(i, lw, lh, tmp);
        off += n * bpt;
        last = i;
    }
    {
        unsigned cw = (last ? p->w >> last : p->w);
        unsigned ch = (last ? p->h >> last : p->h);
        const unsigned char *cur = last ? tmp : rgba0;
        unsigned char *a = NULL, *b = NULL;
        int binaryAlpha = alpha_is_binary(rgba0, p->w * p->h);
        double wantCov = binaryAlpha
                       ? alpha_coverage(rgba0, p->w * p->h, U9GL_ALPHA_REF) : 0.0;
        double parentA = alpha_mean(cur, cw * ch);
        while (cw > 1u || ch > 1u) {
            unsigned nw = (cw > 1u) ? cw / 2u : 1u;
            unsigned nh = (ch > 1u) ? ch / 2u : 1u;
            unsigned char *dst;
            if (!a) {
                a = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (p->w * p->h * 4u) + 16u);
                b = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (p->w * p->h * 4u) + 16u);
                if (!a || !b) break;
            }
            dst = (last & 1u) ? b : a;
            box_half(cur, cw, ch, dst, nw, nh);
            if (binaryAlpha)
                preserve_coverage(dst, nw * nh, wantCov, U9GL_ALPHA_REF);
            parentA = alpha_check(p->id, p->id2, last + 1u, nw, nh, dst, parentA);
            tex_level(++last, nw, nh, dst);
            cur = dst; cw = nw; ch = nh;
        }
        if (a) HeapFree(GetProcessHeap(), 0, a);
        if (b) HeapFree(GetProcessHeap(), 0, b);
    }
    if (tmp) HeapFree(GetProcessHeap(), 0, tmp);
    if (refA) HeapFree(GetProcessHeap(), 0, refA);
    if (refB) HeapFree(GetProcessHeap(), 0, refB);
    if (!g_subUpload)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)last);
    return last;
}

static void upload_rgba(unsigned w, unsigned h, const unsigned char *src,
                        unsigned maxLevels)
{
    unsigned char *a = NULL, *b = NULL;
    const unsigned char *cur = src;
    unsigned cw = w, ch = h, level = 0;

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)maxLevels);
    if (!u9gl_mips_enabled() || !maxLevels || !is_pot(w) || !is_pot(h)) return;

    while ((cw > 1u || ch > 1u) && level < maxLevels) {
        unsigned nw = (cw > 1u) ? cw / 2u : 1u;
        unsigned nh = (ch > 1u) ? ch / 2u : 1u;
        unsigned char *dst;
        if (!a) {
            a = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (w * h * 4u) + 16u);
            b = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (w * h * 4u) + 16u);
            if (!a || !b) break;
        }
        dst = (level & 1u) ? b : a;
        box_half(cur, cw, ch, dst, nw, nh);
        alpha_check(-1, -1, level + 1u, nw, nh, dst, alpha_mean(cur, cw * ch));
        glTexImage2D(GL_TEXTURE_2D, (GLint)++level, GL_RGBA,
                     (GLsizei)nw, (GLsizei)nh, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, dst);
        cur = dst; cw = nw; ch = nh;
    }
    if (a) HeapFree(GetProcessHeap(), 0, a);
    if (b) HeapFree(GetProcessHeap(), 0, b);
    if (level < maxLevels && (cw > 1u || ch > 1u)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)level);
        if (!g_mipFailLogged) {
            g_mipFailLogged = 1;
            u9log_line("GLTEX *** could not build a mip chain for %ux%u - that "
                       "texture stops at level %u", w, h, level);
        }
    }
}

static unsigned rgba_fingerprint(const unsigned char *rgba, unsigned w, unsigned h)
{
    const unsigned *px = (const unsigned *)rgba;
    unsigned i, n = w * h, f = 2166136261u;
    for (i = 0; i < n; i++) { f ^= px[i]; f *= 16777619u; }
    f ^= w; f *= 16777619u;
    f ^= h; f *= 16777619u;
    return f ? f : 1u;
}

static void dump_texture(int id, int id2, unsigned w, unsigned h, unsigned fmt,
                         const unsigned char *rgba, unsigned fp)
{
    char dir[MAX_PATH], path[MAX_PATH];
    unsigned base;
    int pass;

    if (!GetTempPathA(sizeof(dir), dir)) return;
    lstrcatA(dir, "u9gl_textures\\");
    CreateDirectoryA(dir, NULL);

    for (pass = 0; pass < 2; pass++) {
        unsigned rowBytes  = (w * 3u + 3u) & ~3u;
        unsigned dataBytes = rowBytes * h;
        unsigned char *out = (unsigned char *)
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dataBytes);
        unsigned char hdr[54];
        HANDLE f; DWORD wrote; unsigned x, y;
        if (!out) return;
        for (y = 0; y < h; y++) {
            const unsigned char *srcRow = rgba + (h - 1 - y) * w * 4u;
            unsigned char *dstRow = out + y * rowBytes;
            for (x = 0; x < w; x++) {
                if (pass == 0) {
                    dstRow[x*3+0] = srcRow[x*4+2];
                    dstRow[x*3+1] = srcRow[x*4+1];
                    dstRow[x*3+2] = srcRow[x*4+0];
                } else {
                    dstRow[x*3+0] = dstRow[x*3+1] = dstRow[x*3+2] = srcRow[x*4+3];
                }
            }
        }
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'B'; hdr[1] = 'M';
        *(unsigned *)(hdr + 2)  = 54u + dataBytes;
        *(unsigned *)(hdr + 10) = 54;
        *(unsigned *)(hdr + 14) = 40;
        *(int *)(hdr + 18) = (int)w;
        *(int *)(hdr + 22) = (int)h;
        *(unsigned short *)(hdr + 26) = 1;
        *(unsigned short *)(hdr + 28) = 24;
        *(unsigned *)(hdr + 34) = dataBytes;

        lstrcpynA(path, dir, MAX_PATH);
        base = lstrlenA(path);
        if (fp)
            wsprintfA(path + base, "c%08X_%05d_%02d_%ux%u_fmt%02X%s.bmp",
                      fp, id, id2, w, h, fmt, pass ? "_a" : "");
        else
            wsprintfA(path + base, "%05d_%02d_%ux%u_fmt%02X%s.bmp",
                      id, id2, w, h, fmt, pass ? "_a" : "");
        f = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                        fp ? CREATE_NEW : CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, hdr, sizeof(hdr), &wrote, NULL);
            WriteFile(f, out, dataBytes, &wrote, NULL);
            CloseHandle(f);
        }
        HeapFree(GetProcessHeap(), 0, out);
    }
}

int u9gl_magenta_keyed(int texId)
{
    static char list[256];
    static int read;
    if (!read) {
        if (!u9cfg_int("fix_magenta_key", 1)) { read = 1; list[0] = list[1] = 0; return 0; }
        const char *s = u9cfg_str("gl_magenta_key_textures",
                                   "1046,1623,2348,2365,2372,2501,3104,4462,4561,4960,5017,5034,5036,5219,5353,5602,5603,5984,6146,6273,6390,6395,7002,7348");
        size_t i, n = 1;
        read = 1;
        list[0] = ',';
        for (i = 0; s[i] && n < sizeof list - 2; i++)
            if (s[i] != ' ') list[n++] = s[i];
        if (n > 1) list[n++] = ',';
        list[n] = 0;
    }
    if (texId < 0 || !list[1]) return 0;
    return u9_id_listed(list, texId);
}

static unsigned key_magenta(unsigned fmt, const void *data, unsigned w, unsigned h,
                            unsigned char *rgba, int *usedCorner)
{
    const uint16_t *s16 = (const uint16_t *)data;
    unsigned i, n = w * h, keyed = 0, packed = pack_key(fmt, 0x00FF00FFu);
    unsigned mask = (fmt == 0x0B) ? 0x7FFFu : 0xFFFFu;
    *usedCorner = 0;
    if (packed == 0xFFFFFFFFu || !n) return 0;
    for (i = 0; i < n; i++)
        if ((unsigned)(s16[i] & mask) == packed) { rgba[i * 4 + 3] = 0; keyed++; }
    if (!keyed && w > 1 && h > 1) {
        unsigned c = s16[0] & mask, count = 0;
        if ((s16[w - 1] & mask) == c && (s16[(h - 1) * w] & mask) == c && (s16[n - 1] & mask) == c) {
            for (i = 0; i < n; i++) if ((unsigned)(s16[i] & mask) == c) count++;
            if (count * 2 >= n) {
                for (i = 0; i < n; i++)
                    if ((unsigned)(s16[i] & mask) == c) rgba[i * 4 + 3] = 0;
                keyed = count;
                *usedCorner = 1;
            }
        }
    }
    return keyed;
}

static int defringe_listed(int texId)
{
    static char list[256];
    static int read;
    if (!read) {
        const char *s = u9cfg_str("gl_defringe_textures", "2553");
        size_t i, n = 1;
        read = 1;
        list[0] = ',';
        for (i = 0; s[i] && n < sizeof list - 2; i++)
            if (s[i] != ' ') list[n++] = s[i];
        if (n > 1) list[n++] = ',';
        list[n] = 0;
    }
    if (texId < 0 || !list[1]) return 0;
    return u9_id_listed(list, texId);
}

static int is_pinkish(const unsigned char *p)
{
    int r = p[0], g = p[1], b = p[2], lo = r < b ? r : b;
    if (!p[3]) return 0;
    if (r >= 248 && b >= 248 && g <= 8) return 1;
    return lo >= 120 && (r > b ? r - b : b - r) <= 70 && g * 100 <= lo * 45;
}

static unsigned defringe(unsigned char *rgba, unsigned w, unsigned h)
{
    unsigned char *bad;
    unsigned x, y, total = 0, pass, left = 0;
    if (!w || !h) return 0;
    bad = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)w * h);
    if (!bad) return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (is_pinkish(rgba + ((size_t)y * w + x) * 4)) { bad[(size_t)y * w + x] = 1; left++; }
    for (pass = 0; pass < 8 && left; pass++) {
        unsigned fixed = 0;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                size_t i = (size_t)y * w + x;
                unsigned sr = 0, sg = 0, sb = 0, n = 0;
                int dx, dy;
                if (bad[i] != 1) continue;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++) {
                        int nx = (int)x + dx, ny = (int)y + dy;
                        size_t j;
                        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= (int)w || ny >= (int)h) continue;
                        j = (size_t)ny * w + (size_t)nx;
                        if (bad[j] == 1 || !rgba[j * 4 + 3]) continue;
                        sr += rgba[j * 4]; sg += rgba[j * 4 + 1]; sb += rgba[j * 4 + 2]; n++;
                    }
                if (!n) continue;
                rgba[i * 4] = (unsigned char)(sr / n);
                rgba[i * 4 + 1] = (unsigned char)(sg / n);
                rgba[i * 4 + 2] = (unsigned char)(sb / n);
                bad[i] = 2;
                fixed++;
            }
        }
        for (x = 0; x < w * h; x++) if (bad[x] == 2) bad[x] = 0;
        total += fixed;
        left -= fixed;
        if (!fixed) break;
    }
    HeapFree(GetProcessHeap(), 0, bad);
    return total;
}

static unsigned g_compKey[8];
static int      g_compKeys = -1;

static void composed_keys_read(void)
{
    const char *s = u9cfg_str("gl_composed_key_colours", "7D3E");
    g_compKeys = 0;
    while (s && *s && g_compKeys < 8) {
        char *end;
        unsigned long v;
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        v = strtoul(s, &end, 16);
        if (end == s) break;
        g_compKey[g_compKeys++] = (unsigned)(v & 0x7FFFu);
        s = end;
    }
}

static unsigned key_composed(unsigned fmt, const void *data, unsigned n, unsigned char *rgba)
{
    const uint16_t *s16 = (const uint16_t *)data;
    unsigned i, k, keyed = 0;
    if (g_compKeys < 0) composed_keys_read();
    if (fmt != 0x0B || !g_compKeys) return 0;
    for (i = 0; i < n; i++) {
        unsigned v = s16[i] & 0x7FFFu;
        for (k = 0; k < (unsigned)g_compKeys; k++)
            if (v == g_compKey[k]) { rgba[i * 4 + 3] = 0; keyed++; break; }
    }
    return keyed;
}

void u9gl_upload_pending(void)
{
    LARGE_INTEGER t0, t1, freq;
    unsigned did = 0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    while (g_pendCount) {
        did++;
        Pending *p = &g_pending[g_pendCount - 1];
        unsigned n = p->w * p->h;
        unsigned char *rgba;

        int slot = p->reuseTex ? cached_index(p->id, p->id2, p->fmt, U9GL_NO_KEY)
                               : -1;
        int refresh = (slot >= 0 && g_cache[slot].tex == p->reuseTex);

        if (p->reuseTex) g_refreshArrived++;

        if (!refresh && g_cacheCount >= U9GL_TEXCACHE &&
            !cached(p->id, p->id2, p->fmt, U9GL_NO_KEY)) {
            static unsigned full;
            full++;
            if (full <= 4 || (full % 1000) == 0)
                u9log_line("GLTEX *** cache table full (%u entries, %u MB): "
                           "id=%d/%d %ux%u not uploaded (%u so far)",
                           g_cacheCount, (unsigned)(g_texBytes >> 20),
                           p->id, p->id2, p->w, p->h, full);
        }

        if ((refresh || !cached(p->id, p->id2, p->fmt, U9GL_NO_KEY)) &&
            (refresh || g_cacheCount < U9GL_TEXCACHE) &&
            (rgba = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, n * 4u)) != NULL) {
            double tDec = now_ms();
            int decoded = decode(p->fmt, p->data, n, rgba);
            g_msDecode += now_ms() - tDec;
            if (!decoded) {
                u9log_line("GLTEX upload_pending: decode failed id=%d/%d "
                           "%ux%u fmt=0x%02X", p->id, p->id2, p->w, p->h, p->fmt);
            } else {
                GLuint tex;
                int storage = 0;
                if (p->id >= 8000) {
                    static unsigned logged;
                    unsigned keyed = key_composed(p->fmt, p->data, n, rgba);
                    if (keyed && logged++ < 16)
                        u9log_line("GLTEX composed keyed id=%d/%d: %u/%u texels", p->id, p->id2, keyed, n);
                }
                if (defringe_listed(p->id)) {
                    static unsigned logged;
                    unsigned fixed = defringe(rgba, p->w, p->h);
                    p->srcLevels = 0;
                    if (logged++ < 24)
                        u9log_line("GLTEX defringed id=%d/%d %ux%u: %u pixels", p->id, p->id2, p->w, p->h, fixed);
                }
                if (u9gl_magenta_keyed(p->id)) {
                    static unsigned logged;
                    int corner;
                    unsigned keyed = key_magenta(p->fmt, p->data, p->w, p->h, rgba, &corner);
                    p->srcLevels = 0;
                    if (logged++ < 40)
                        u9log_line("GLTEX magenta keyed id=%d/%d fmt=0x%02X: %u/%u texels%s", p->id, p->id2,
                                   p->fmt, keyed, n, corner ? " (no magenta: its corner colour)" : "");
                }
                unsigned char *ovEngineRgba = NULL;
                unsigned ovEngineW = p->w, ovEngineH = p->h;
                {
                    U9TexImage ov;
                    if (u9tex_override(p->id, p->id2, rgba, p->w, p->h, &ov)) {
                        ovEngineRgba = rgba;
                        rgba = ov.rgba;
                        p->w = ov.w;
                        p->h = ov.h;
                        p->srcLevels = 0;
                    }
                }

                if (refresh && tex_storage_ready() &&
                    (g_cache[slot].w != (int)p->w || g_cache[slot].h != (int)p->h)) {
                    glDeleteTextures(1, &g_cache[slot].tex);
                    u9metrics_tex_destroyed(g_cache[slot].bytes);
                    g_texBytes -= g_cache[slot].bytes;
                    g_cache[slot] = g_cache[g_cacheCount - 1];
                    g_cacheCount--;
                    refresh = 0;
                    slot = -1;
                }

                if (refresh) tex = p->reuseTex;
                else         glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);

                if (!refresh && tex_storage_ready() &&
                    u9gl_mips_enabled() && is_pot(p->w) && is_pot(p->h)) {
                    double tSt = now_ms();
                    g_texStorage(GL_TEXTURE_2D,
                                 (GLsizei)chain_levels(p->w, p->h),
                                 GL_RGBA8, (GLsizei)p->w, (GLsizei)p->h);
                    g_msStorage += now_ms() - tSt;
                    g_nStorage++;
                    storage = 1;
                    g_storageUsed++;
                } else if (!refresh) {
                    g_storageAbsent++;
                }

                g_subUpload = storage ||
                              (refresh && u9cfg_int("tex_subimage", 1) &&
                               g_cache[slot].w == (int)p->w &&
                               g_cache[slot].h == (int)p->h &&
                               g_cache[slot].fmt == p->fmt);
                if (g_subUpload) g_subUploads++; else g_fullUploads++;
                if (!refresh) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    u9gl_min_filter());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                }
                {
                    unsigned k, cnt = p->w * p->h, op = 0;
                    unsigned long long sr = 0, sg = 0, sb = 0;
                    for (k = 0; k < cnt; k++) {
                        sr += rgba[k*4+0]; sg += rgba[k*4+1]; sb += rgba[k*4+2];
                        if (rgba[k*4+3] >= U9GL_ALPHA_REF) op++;
                    }
                    g_lastMeanR = (unsigned char)(cnt ? sr / cnt : 0);
                    g_lastMeanG = (unsigned char)(cnt ? sg / cnt : 0);
                    g_lastMeanB = (unsigned char)(cnt ? sb / cnt : 0);
                    g_lastOpaque = (unsigned char)(cnt ? (op * 100u) / cnt : 0);
                }
                {
                    double tCh = now_ms();
                    if (u9gl_mips_enabled()) upload_archive_chain(p, rgba);
                    else                     upload_rgba(p->w, p->h, rgba, 0u);
                    g_msChain += now_ms() - tCh;
                }
                if (u9cfg_int("dump_textures", 0)) {
                    if (ovEngineRgba)
                        dump_texture(p->id, p->id2, ovEngineW, ovEngineH, p->fmt,
                                     ovEngineRgba, 0u);
                    else
                        dump_texture(p->id, p->id2, p->w, p->h, p->fmt, rgba,
                                     (unsigned)p->id >= U9_TEXSRC_MAX_ID
                                         ? rgba_fingerprint(rgba, p->w, p->h) : 0u);
                    g_dumped++;
                }
                if (!refresh) {
                    unsigned d;
                    for (d = 0; d < g_cacheCount; d++)
                        if (g_cache[d].id == p->id && g_cache[d].id2 == p->id2) {
                            g_dupIdent++;
                            if (g_dupIdent <= 20)
                                u9log_line("GLTEX *** DUPLICATE id=%d/%d already "
                                           "cached as fmt 0x%02X, uploading fmt 0x%02X",
                                           p->id, p->id2, g_cache[d].fmt, p->fmt);
                            break;
                        }
                }
                {
                    unsigned need = resident_bytes(p->w, p->h, 16u);
                    unsigned e;
                    if (refresh) {
                        e = (unsigned)slot;
                        g_texBytes -= g_cache[e].bytes;
                        g_texBytes += need;
                        g_refreshes++;
                        u9log_line("RECOMPOSE id=%d/%d fmt=0x%02X frame %u"
                                   " - pixels replaced", p->id, p->id2, p->fmt,
                                   u9log_frame());
                    } else {
                        if (p->reuseTex) g_refreshBecameCreate++;
                        while (g_texBytes + need > U9GL_TEX_BUDGET_BYTES &&
                               g_cacheCount > 0) {
                            if (!evict_one()) break;
                        }
                        u9metrics_tex_created(need);
                        g_texBytes += need;
                        e = g_cacheCount++;
                        g_uploads++;
                    }
                    g_cache[e].bytes      = need;
                    g_cache[e].lastUsed   = u9log_frame();
                    g_cache[e].id         = p->id;
                    g_cache[e].id2        = p->id2;
                    g_cache[e].key        = U9GL_NO_KEY;
                    g_cache[e].lastUpload = u9log_frame();
                    g_cache[e].srcHash    = p->srcHash;
                    g_cache[e].mr         = g_lastMeanR;
                    g_cache[e].mg         = g_lastMeanG;
                    g_cache[e].mb         = g_lastMeanB;
                    g_cache[e].opaque     = g_lastOpaque;
                    g_cache[e].tex        = tex;
                    g_cache[e].w          = (int)p->w;
                    g_cache[e].h          = (int)p->h;
                    g_cache[e].fmt        = p->fmt;
                    g_cache[e].overridden = (unsigned char)(ovEngineRgba != NULL);
                }
                if (!refresh && g_uploads <= 40)
                    u9log_line("GLTEX upload #%u id=%d/%d %ux%u fmt=0x%02X %s "
                               "-> GL %u  [cache %u/%u]",
                               g_uploads, p->id, p->id2, p->w, p->h, p->fmt,
                               u9gl_texfmt_name(p->fmt), (unsigned)tex,
                               g_cacheCount, (unsigned)U9GL_TEXCACHE);
                if (ovEngineRgba) HeapFree(GetProcessHeap(), 0, ovEngineRgba);
            }
            HeapFree(GetProcessHeap(), 0, rgba);
        }
        HeapFree(GetProcessHeap(), 0, p->data);
        g_pendCount--;
        if (g_dumped && (g_dumped % 200) == 0)
            u9log_line("GLTEX dumped %u textures to TEMP dir u9gl_textures", g_dumped);
    }
    if (did) {
        double ms;
        QueryPerformanceCounter(&t1);
        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        g_uploadMs += ms;
        g_frameUploadMs += ms;
        if (ms > 8.0 && u9cfg_int("perf_log", 0))
            u9log_line("GLTEX upload burst: %u textures in %.1f ms (frame %u) "
                       "= decode %.1f + filter %.1f + glTexImage2D %.1f",
                       did, ms, u9log_frame(), g_msDecode,
                       g_msChain - g_msGL, g_msGL);
        if (ms > 8.0 && u9cfg_int("perf_log", 0)) {
            u9log_line("GLTEX   of those, %u replaced pixels (glTexSubImage2D) "
                       "and %u redefined storage (glTexImage2D)",
                       g_subUploads, g_fullUploads);
            u9log_line("GLTEX   storage: %u whole-chain, %u per-level. "
                       "glTexStorage2D itself: %u calls %.1f ms (%.0f us each)",
                       g_storageUsed, g_storageAbsent, g_nStorage, g_msStorage,
                       g_nStorage ? g_msStorage * 1000.0 / g_nStorage : 0.0);
            {
                static const char *names[4] = { "<=64x64", "128x128", "256x256", "bigger" };
                unsigned b;
                for (b = 0; b < 4u; b++)
                    if (g_nL0b[b])
                        u9log_line("GLTEX   level 0 %-8s %4u calls %6.1f ms  "
                                   "%6.0f us each", names[b], g_nL0b[b],
                                   g_msL0b[b], g_msL0b[b] * 1000.0 / g_nL0b[b]);
            }
            u9log_line("GLTEX   GL split: level 0 %u calls %.1f ms (%.0f us each)"
                       ", levels 1+ %u calls %.1f ms (%.0f us each)",
                       g_nGL0, g_msGL0, g_nGL0 ? g_msGL0 * 1000.0 / g_nGL0 : 0.0,
                       g_nGLn, g_msGLn, g_nGLn ? g_msGLn * 1000.0 / g_nGLn : 0.0);
        }
        g_subUploads = g_fullUploads = 0;
        g_msGL0 = g_msGLn = 0.0; g_nGL0 = g_nGLn = 0;
        g_msStorage = 0.0; g_nStorage = 0;
        memset(g_msL0b, 0, sizeof g_msL0b); memset(g_nL0b, 0, sizeof g_nL0b);
        g_msDecode = g_msChain = g_msGL = 0.0;
    }
}

double u9gl_upload_ms(void) { double v = g_uploadMs; g_uploadMs = 0.0; return v; }

GLuint u9gl_texture_for(const void *renderer, const void *mat, int id, int id2,
                        unsigned fmt, unsigned key)
{
    int      log2w, log2h, w, h;
    unsigned i, n;
    const unsigned char *src;
    unsigned char *rgba;
    GLuint  tex;

    if (id < 0) return 0;

    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].id == id && g_cache[i].id2 == id2 &&
            g_cache[i].key == key &&
            (g_cache[i].fmt == fmt || !u9cfg_int("tex_match_format", 1)))
            return g_cache[i].tex;

    if (!mat || !renderer) return 0;
    if (*(const int16_t *)((const char *)mat + U9_MAT_TEXID)  != (int16_t)id ||
        *(const int16_t *)((const char *)mat + U9_MAT_TEXID2) != (int16_t)id2) {
        why_fail(id, id2, "material recycled before the flush; it now describes",
                 (unsigned)*(const int16_t *)((const char *)mat + U9_MAT_TEXID),
                 (unsigned)*(const int16_t *)((const char *)mat + U9_MAT_TEXID2));
        return 0;
    }

    log2w = *(const int16_t *)((const char *)mat + U9_MAT_LOG2W);
    log2h = *(const int16_t *)((const char *)mat + U9_MAT_LOG2H);
    if (log2w < 0 || log2w > 11 || log2h < 0 || log2h > 11) return 0;
    w = 1 << log2w;
    h = 1 << log2h;
    n = (unsigned)w * (unsigned)h;

    {
        const char *mgr = *(const char *const *)
                          ((const char *)renderer + U9_OFF_TEXMGR);
        const char *rec = mgr ? *(const char *const *)(mgr + U9_TEXMGR_CURRENT)
                              : NULL;
        if (rec && *(const int16_t *)(rec + U9_TEXREC_ID) == id) {
            int recAspect = (int)*(const unsigned *)(rec + U9_TEXREC_ASPECT);
            int recLarge  = (int)*(const unsigned *)(rec + U9_TEXREC_LARGE_LOD);
            int wantAspect = 3 - (log2w - log2h);
            int wantLarge  = 8 - (log2w > log2h ? log2w : log2h);
            if (recAspect != wantAspect || recLarge != wantLarge)
                u9log_line("GLTEX *** DIMENSION MISMATCH id=%d/%d %dx%d: "
                           "record aspect=%d lod=%d, dimensions imply %d/%d",
                           id, id2, w, h, recAspect, recLarge,
                           wantAspect, wantLarge);
        }
    }

    mipbias_report(renderer);
    src = NULL;
    for (i = 0; i < g_pendCount; i++)
        if (g_pending[i].id == id && g_pending[i].id2 == id2 &&
            g_pending[i].fmt == fmt) { src = g_pending[i].data; break; }
    if (!src || !fmt) {
        g_failed++;
        why_fail(id, id2, "no texture at draw time (fmt/key/pending/cache)", fmt, key);
        return 0;
    }

    rgba = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, n * 4u);
    if (!rgba) return 0;
    if (!decode(fmt, src, n, rgba)) {
        HeapFree(GetProcessHeap(), 0, rgba);
        g_failed++;
        if (g_failed < 8)
            u9log_line("GLTEX unsupported format 0x%02X for id=%d/%d", fmt, id, id2);
        return 0;
    }

    if (key != U9GL_NO_KEY) {
        unsigned packed = pack_key(fmt, key);
        if (packed != 0xFFFFFFFFu) {
            const uint16_t *s16 = (const uint16_t *)src;
            unsigned mask = (fmt == 0x0B) ? 0x7FFFu : 0xFFFFu;
            unsigned keyed = 0;
            for (i = 0; i < n; i++)
                if ((unsigned)(s16[i] & mask) == packed) { rgba[i*4+3] = 0; keyed++; }
            if (g_uploads < 12)
                u9log_line("GLTEX   chromakey 0x%06X -> packed 0x%04X, %u/%u texels keyed",
                           key, packed, keyed, n);
        }
    }

    int ovUsed = 0;
    {
        U9TexImage ov;
        if (u9tex_override(id, id2, rgba, (unsigned)w, (unsigned)h, &ov)) {
            HeapFree(GetProcessHeap(), 0, rgba);
            rgba = ov.rgba;
            w = (int)ov.w;
            h = (int)ov.h;
            ovUsed = 1;
        }
    }

    if (g_cacheCount >= U9GL_TEXCACHE) {
        HeapFree(GetProcessHeap(), 0, rgba);
        if (!g_cacheFullLogged) {
            g_cacheFullLogged = 1;
            u9log_line("GLTEX *** texture cache full at %u entries - drawing "
                       "untextured rather than leaking an upload per frame",
                       (unsigned)U9GL_TEXCACHE);
        }
        return 0;
    }

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, u9gl_min_filter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    upload_rgba((unsigned)w, (unsigned)h, rgba, 16u);
    HeapFree(GetProcessHeap(), 0, rgba);

    {
        {
            unsigned need = resident_bytes((unsigned)w, (unsigned)h, 16u);
            while (g_texBytes + need > U9GL_TEX_BUDGET_BYTES && g_cacheCount > 0) {
                if (!evict_one()) break;
            }
            u9metrics_tex_created(need);
            g_texBytes += need;
            g_cache[g_cacheCount].bytes    = need;
            g_cache[g_cacheCount].lastUsed = u9log_frame();
        }
        g_cache[g_cacheCount].id  = id;
        g_cache[g_cacheCount].id2 = id2;
        g_cache[g_cacheCount].key = key;
        g_cache[g_cacheCount].tex = tex;
        g_cache[g_cacheCount].w   = w;
        g_cache[g_cacheCount].h   = h;
        g_cache[g_cacheCount].fmt = fmt;
        g_cache[g_cacheCount].overridden = (unsigned char)ovUsed;
        g_cacheCount++;
    }
    for (i = 0; i < g_pendCount; i++)
        if (g_pending[i].id == id && g_pending[i].id2 == id2 &&
            g_pending[i].fmt == fmt) {
            HeapFree(GetProcessHeap(), 0, g_pending[i].data);
            g_pending[i] = g_pending[--g_pendCount];
            break;
        }
    g_uploads++;
    if (g_uploads <= 40)
        u9log_line("GLTEX upload #%u id=%d/%d %dx%d (log2 %d,%d) fmt=0x%02X %s "
                   "-> GL %u  [cache %u/%u]",
                   g_uploads, id, id2, w, h, log2w, log2h, fmt,
                   u9gl_texfmt_name(fmt), (unsigned)tex,
                   g_cacheCount, (unsigned)U9GL_TEXCACHE);
    return tex;
}

void u9gl_texture_stats(unsigned *uploads, unsigned *failed, unsigned *cached)
{
    *uploads = g_uploads;
    *failed  = g_failed;
    *cached  = g_cacheCount;
}

int u9gl_texture_look(unsigned tex, unsigned *rgb, unsigned *opaquePct)
{
    unsigned i;
    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].tex == (GLuint)tex) {
            *rgb = ((unsigned)g_cache[i].mr << 16) |
                   ((unsigned)g_cache[i].mg <<  8) |  (unsigned)g_cache[i].mb;
            *opaquePct = g_cache[i].opaque;
            return 1;
        }
    return 0;
}

int u9gl_texture_ident(unsigned glTex, int *id, int *id2, unsigned *fmt,
                       int *w, int *h)
{
    unsigned i;
    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].tex == (GLuint)glTex) {
            *id = g_cache[i].id; *id2 = g_cache[i].id2;
            *fmt = g_cache[i].fmt; *w = g_cache[i].w; *h = g_cache[i].h;
            return 1;
        }
    return 0;
}

int u9gl_texture_is_override(unsigned glTex)
{
    unsigned i;
    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].tex == (GLuint)glTex) return g_cache[i].overridden ? 1 : 0;
    return -1;
}

void u9gl_source_report(int force)
{
    static unsigned last;
    if (!force && g_ownLoads + g_ownFallbacks == last) return;
    last = g_ownLoads + g_ownFallbacks;
    u9log_line("GLSRC  texel source: %u static from the ARCHIVE (Glide-free), "
               "%u composed (id>=8000) from the manager's LIVE buffer, "
               "%u archive fallbacks, %u manager fallbacks. Mip chains: %u from "
               "the archive, %u box-filtered",
               g_ownLoads, g_composedFromMgr, g_composedFallback,
               g_ownFallbacks, g_chainFromArchive, g_chainFiltered);
    {
        unsigned considered = g_redownload + g_recomposed;
        unsigned disposed   = g_skippedUnchanged + g_refreshes;
        u9log_line("GLSRC  composed-texture refresh: %u considered "
                   "(%u per-frame re-read + %u by the engine's 0x40 flag) -> "
                   "%u SKIPPED as identical, %u re-uploaded",
                   considered, g_recomposed, g_redownload,
                   g_skippedUnchanged, g_refreshes);
        if (g_cwPrinted)
            u9log_line("CW  composed-texture picture changed on %u frames"
                       " (watch_compass)", g_cwPrinted);
        if (g_f8Empties)
            u9log_line("F8  composed slots found EMPTY and skipped: %u"
                       " - each one would have faulted at 0x0041D95F", g_f8Empties);
        {
            unsigned ab = 0, k;
            for (k = 0; k < U9GL_ABANDON__N; k++) ab += g_abandoned[k];
            if (ab) {
                u9log_line("GLSRC  abandoned before disposition: %u", ab);
                for (k = 0; k < U9GL_ABANDON__N; k++)
                    if (g_abandoned[k])
                        u9log_line("GLSRC    %-44s %u",
                                   g_abandonName[k], g_abandoned[k]);
            }
            disposed += ab;

            if (g_refreshArrived) {
                unsigned dropped = g_refreshArrived - g_refreshes
                                 - g_refreshBecameCreate;
                u9log_line("GLSRC  refresh candidates at the flush: %u arrived,"
                           " %u refreshed in place, %u uploaded as a NEW texture"
                           " (evicted or resized since queueing), %u dropped",
                           g_refreshArrived, g_refreshes,
                           g_refreshBecameCreate, dropped);
                disposed += g_refreshBecameCreate + dropped;
            }
        }
        if (considered != disposed)
            u9log_line("GLSRC  WARNING: %u considered but %u disposed of "
                       "(difference %d) - a refresh path is STILL not accounted for",
                       considered, disposed, (int)considered - (int)disposed);
        else
            u9log_line("GLSRC  accounting balances: %u considered, %u disposed of",
                       considered, disposed);
    }
}

void u9gl_alpha_report(void)
{
    u9log_line("GLALPHA check: %u generated mip levels checked, %u lost opacity%s",
               g_alphaChecked, g_alphaLost,
               g_alphaLost ? " *** see the GLALPHA lines above" : "");
}

void u9gl_mip_report(void)
{
    char line[256];
    unsigned i, n = 0, total = 0;
    u9gl_alpha_report();
    for (i = 0; i < 16u; i++) total += g_mipHist[i];
    if (!total) return;
    for (i = 0; i < 16u; i++)
        if (g_mipHist[i] && n + 16u < sizeof line)
            n += (unsigned)sprintf(line + n, "%u:%u ", i, g_mipHist[i]);
    line[n] = 0;
    u9log_line("GLMIP  mipCount across %u textures (level:count) = %s"
               " - GL builds mipCount levels and stops where Glide stops",
               total, line);
}

void u9gl_tex_stats(unsigned *cacheCount, unsigned *uploads, unsigned *dupes,
                    unsigned *pending, unsigned *mb, unsigned *evictions)
{
    *cacheCount = g_cacheCount;
    *uploads    = g_uploads;
    *dupes      = g_dupIdent;
    *pending    = g_pendCount;
    *mb         = g_texBytes / (1024u * 1024u);
    *evictions  = g_evictions;
}

double u9gl_upload_take_ms(void)
{
    double ms = g_frameUploadMs;
    g_frameUploadMs = 0.0;
    return ms;
}

unsigned u9gl_tex_budget_mb(void) { return U9GL_TEX_BUDGET_BYTES / (1024u * 1024u); }

#include "gltexlist.h"
unsigned u9gl_tex_recent(U9TexListItem *out, unsigned max, unsigned *entries, unsigned *frame)
{
    unsigned i, n = 0, k;
    if (entries) *entries = g_cacheCount;
    if (frame) *frame = u9log_frame();
    for (i = 0; i < g_cacheCount; i++) {
        const TexEntry *e = &g_cache[i];
        if (n == max && e->lastUsed <= out[n - 1].lastUsed) continue;
        k = n < max ? n++ : max - 1;
        while (k > 0 && out[k - 1].lastUsed < e->lastUsed) { out[k] = out[k - 1]; k--; }
        out[k].id = e->id; out[k].id2 = e->id2;
        out[k].w = (unsigned)e->w; out[k].h = (unsigned)e->h;
        out[k].lastUsed = e->lastUsed;
        out[k].overridden = e->overridden;
        out[k].keyed = e->key != U9GL_NO_KEY;
    }
    return n;
}
uint64_t u9gl_tex_cache_bytes(void) { return g_texBytes; }
uint64_t u9gl_tex_budget_bytes(void) { return U9GL_TEX_BUDGET_BYTES; }
int u9gl_mip_filter_mode(void)
{
    if (g_mipFilter < 0) g_mipFilter = u9cfg_int("mip_filter", 1);
    return g_mipFilter;
}
float u9gl_anisotropy_cached(void) { return g_aniso; }

unsigned u9gl_override_bytes(unsigned *count)
{
    unsigned i, bytes = 0, n = 0;
    for (i = 0; i < g_cacheCount; i++)
        if (g_cache[i].overridden) { bytes += g_cache[i].bytes; n++; }
    if (count) *count = n;
    return bytes;
}
