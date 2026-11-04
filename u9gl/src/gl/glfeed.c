#include <windows.h>
#include <stdint.h>

#include "../diag/log.h"
#include "../common/symbols.h"
#include "../common/patchmgr.h"

int u9cfg_int(const char *key, int def);

#define U9FEED_MAX_ENTRIES 32
static unsigned g_drop[U9FEED_MAX_ENTRIES];
static int      g_installed;

#define DROP(i) (g_drop[i]++)

static void __stdcall n_drawTriangle(const void *a, const void *b, const void *c)
{ (void)a;(void)b;(void)c; DROP(0); }
static void __stdcall n_drawPolygon(int n, const int *idx, const void *v)
{ (void)n;(void)idx;(void)v; DROP(1); }
static void __stdcall n_drawPolygonVertexList(int n, const void *v)
{ (void)n;(void)v; DROP(2); }
static void __stdcall n_drawPlanarPolygon(int n, const int *idx, const void *v)
{ (void)n;(void)idx;(void)v; DROP(3); }
static void __stdcall n_drawPlanarPolygonVertexList(int n, const void *v)
{ (void)n;(void)v; DROP(4); }
static void __stdcall n_drawLine(const void *a, const void *b)
{ (void)a;(void)b; DROP(5); }
static void __stdcall n_drawPoint(const void *a)
{ (void)a; DROP(6); }
static void __stdcall n_aaDrawTriangle(const void *a, const void *b, const void *c,
                                       int ab, int bc, int ca)
{ (void)a;(void)b;(void)c;(void)ab;(void)bc;(void)ca; DROP(7); }
static void __stdcall n_aaDrawPolygon(int n, const int *idx, const void *v)
{ (void)n;(void)idx;(void)v; DROP(8); }
static void __stdcall n_aaDrawPolygonVertexList(int n, const void *v)
{ (void)n;(void)v; DROP(9); }
static void __stdcall n_aaDrawLine(const void *a, const void *b)
{ (void)a;(void)b; DROP(10); }
static void __stdcall n_aaDrawPoint(const void *a)
{ (void)a; DROP(11); }

static void __stdcall n_texDownloadMipMap(unsigned tmu, unsigned addr,
                                          unsigned evenOdd, void *info)
{ (void)tmu;(void)addr;(void)evenOdd;(void)info; DROP(12); }
static void __stdcall n_texDownloadMipMapLevel(unsigned tmu, unsigned addr,
        unsigned thisLod, unsigned largeLod, unsigned aspect, unsigned fmt,
        unsigned evenOdd, void *data)
{ (void)tmu;(void)addr;(void)thisLod;(void)largeLod;(void)aspect;(void)fmt;
  (void)evenOdd;(void)data; DROP(13); }
static void __stdcall n_texDownloadMipMapLevelPartial(unsigned tmu, unsigned addr,
        unsigned thisLod, unsigned largeLod, unsigned aspect, unsigned fmt,
        unsigned evenOdd, void *data, unsigned start, unsigned end)
{ (void)tmu;(void)addr;(void)thisLod;(void)largeLod;(void)aspect;(void)fmt;
  (void)evenOdd;(void)data;(void)start;(void)end; DROP(14); }
static void __stdcall n_texDownloadTable(unsigned tmu, unsigned type, void *data)
{ (void)tmu;(void)type;(void)data; DROP(15); }
static void __stdcall n_texDownloadTablePartial(unsigned tmu, unsigned type,
        void *data, unsigned start, unsigned end)
{ (void)tmu;(void)type;(void)data;(void)start;(void)end; DROP(16); }

#define GRP_GEOM 0
#define GRP_TEX  1

struct Entry {
    U9Symbol    sym;
    const char *name;
    void       *replacement;
    int         group;
};

static const struct Entry ENTRIES[] = {
    { U9SYM_GR_DRAW_TRIANGLE,       "_grDrawTriangle@12",                (void *)n_drawTriangle, GRP_GEOM },
    { U9SYM_GR_DRAW_POLYGON,        "_grDrawPolygon@12",                 (void *)n_drawPolygon, GRP_GEOM },
    { U9SYM_GR_DRAW_POLY_VLIST,     "_grDrawPolygonVertexList@8",        (void *)n_drawPolygonVertexList, GRP_GEOM },
    { U9SYM_GR_DRAW_PLANAR_POLY,    "_grDrawPlanarPolygon@12",           (void *)n_drawPlanarPolygon, GRP_GEOM },
    { U9SYM_GR_DRAW_PLANAR_VLIST,   "_grDrawPlanarPolygonVertexList@8",  (void *)n_drawPlanarPolygonVertexList, GRP_GEOM },
    { U9SYM_GR_DRAW_LINE,           "_grDrawLine@8",                     (void *)n_drawLine, GRP_GEOM },
    { U9SYM_GR_DRAW_POINT,          "_grDrawPoint@4",                    (void *)n_drawPoint, GRP_GEOM },
    { U9SYM_GR_AA_DRAW_TRIANGLE,    "_grAADrawTriangle@24",              (void *)n_aaDrawTriangle, GRP_GEOM },
    { U9SYM_GR_AA_DRAW_POLYGON,     "_grAADrawPolygon@12",               (void *)n_aaDrawPolygon, GRP_GEOM },
    { U9SYM_GR_AA_DRAW_POLY_VLIST,  "_grAADrawPolygonVertexList@8",      (void *)n_aaDrawPolygonVertexList, GRP_GEOM },
    { U9SYM_GR_AA_DRAW_LINE,        "_grAADrawLine@8",                   (void *)n_aaDrawLine, GRP_GEOM },
    { U9SYM_GR_AA_DRAW_POINT,       "_grAADrawPoint@4",                  (void *)n_aaDrawPoint, GRP_GEOM },

    { U9SYM_GR_TEX_DL_MIPMAP,       "_grTexDownloadMipMap@16",             (void *)n_texDownloadMipMap,             GRP_TEX },
    { U9SYM_GR_TEX_DL_LEVEL,        "_grTexDownloadMipMapLevel@32",        (void *)n_texDownloadMipMapLevel,        GRP_TEX },
    { U9SYM_GR_TEX_DL_PARTIAL,      "_grTexDownloadMipMapLevelPartial@40", (void *)n_texDownloadMipMapLevelPartial, GRP_TEX },
    { U9SYM_GR_TEX_DL_TABLE,        "_grTexDownloadTable@12",              (void *)n_texDownloadTable,              GRP_TEX },
    { U9SYM_GR_TEX_DL_TABLE_PART,   "_grTexDownloadTablePartial@20",       (void *)n_texDownloadTablePartial,       GRP_TEX },
};
#define N_ENTRIES (sizeof(ENTRIES) / sizeof(ENTRIES[0]))
typedef char u9feed_drop_array_is_big_enough[
    (N_ENTRIES <= U9FEED_MAX_ENTRIES) ? 1 : -1];

static FARPROC resolve(HMODULE h, const char *decorated)
{
    char bare[64];
    const char *at;
    FARPROC p = GetProcAddress(h, decorated);
    if (p) return p;
    if (decorated[0] == '_') {
        unsigned n = 0;
        at = decorated + 1;
        while (*at && *at != '@' && n < sizeof(bare) - 1) bare[n++] = *at++;
        bare[n] = 0;
        return GetProcAddress(h, bare);
    }
    return NULL;
}

#define U9GLIDE_CALLERS 16
static int       g_silenced;
static unsigned  g_glideCalls;
static uintptr_t g_glideCaller[U9GLIDE_CALLERS];
static unsigned  g_glideCallerHits[U9GLIDE_CALLERS];
static unsigned  g_glideCallersLost;

static void glide_call_note(uintptr_t from)
{
    unsigned i;
    g_glideCalls++;
    for (i = 0; i < U9GLIDE_CALLERS; i++) {
        if (g_glideCaller[i] == from) { g_glideCallerHits[i]++; return; }
        if (!g_glideCaller[i]) { g_glideCaller[i] = from; g_glideCallerHits[i] = 1u; return; }
    }
    g_glideCallersLost++;
}
#define GNOP_NOTE() glide_call_note((uintptr_t)__builtin_return_address(0))

void u9feed_glide_calls_report(const char *when)
{
    unsigned i;
    if (!g_silenced) {
        u9log_line("GLIDE2X (%s) calls after silencing: not counted - silencing "
                   "is not installed in this mode", when);
        return;
    }
    u9log_line("GLIDE2X (%s) calls after silencing: %u (calls before it installed "
               "are not counted)%s", when, g_glideCalls,
               g_glideCallersLost ? ", more callers than the table holds" : "");
    for (i = 0; i < U9GLIDE_CALLERS && g_glideCaller[i]; i++)
        u9log_line("GLIDE2X   caller 0x%08X x%u", (unsigned)g_glideCaller[i],
                   g_glideCallerHits[i]);
}

static unsigned __stdcall gnop0 (void) { GNOP_NOTE(); return 0u; }
static unsigned __stdcall gnop4 (unsigned a)
{ GNOP_NOTE(); (void)a; return 0u; }
static unsigned __stdcall gnop8 (unsigned a, unsigned b)
{ GNOP_NOTE(); (void)a;(void)b; return 0u; }
static unsigned __stdcall gnop12(unsigned a, unsigned b, unsigned c)
{ GNOP_NOTE(); (void)a;(void)b;(void)c; return 0u; }
static unsigned __stdcall gnop16(unsigned a, unsigned b, unsigned c, unsigned d)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d; return 0u; }
static unsigned __stdcall gnop20(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e; return 0u; }
static unsigned __stdcall gnop24(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f; return 0u; }
static unsigned __stdcall gnop28(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return 0u; }
static unsigned __stdcall gnop32(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g, unsigned h)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; return 0u; }
static unsigned __stdcall gnop40(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g, unsigned h,
                                 unsigned i, unsigned j)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
  return 0u; }
static unsigned __stdcall gnop48(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g, unsigned h,
                                 unsigned i, unsigned j, unsigned k, unsigned l)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
  (void)k;(void)l; return 0u; }
static unsigned __stdcall gnop60(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g, unsigned h,
                                 unsigned i, unsigned j, unsigned k, unsigned l,
                                 unsigned m, unsigned n, unsigned o)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
  (void)k;(void)l;(void)m;(void)n;(void)o; return 0u; }
static unsigned __stdcall gnop64(unsigned a, unsigned b, unsigned c, unsigned d,
                                 unsigned e, unsigned f, unsigned g, unsigned h,
                                 unsigned i, unsigned j, unsigned k, unsigned l,
                                 unsigned m, unsigned n, unsigned o, unsigned p)
{ GNOP_NOTE(); (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
  (void)k;(void)l;(void)m;(void)n;(void)o;(void)p; return 0u; }

static void *nop_for(unsigned purge)
{
    switch (purge) {
    case 0:  return (void *)gnop0;   case 4:  return (void *)gnop4;
    case 8:  return (void *)gnop8;   case 12: return (void *)gnop12;
    case 16: return (void *)gnop16;  case 20: return (void *)gnop20;
    case 24: return (void *)gnop24;  case 28: return (void *)gnop28;
    case 32: return (void *)gnop32;  case 40: return (void *)gnop40;
    case 48: return (void *)gnop48;  case 60: return (void *)gnop60;
    case 64: return (void *)gnop64;
    }
    return 0;
}

static void silence_everything(HMODULE h)
{
    const unsigned char *b = (const unsigned char *)h;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_EXPORT_DIRECTORY *ed;
    const unsigned *nameRvas;
    unsigned i, n, ok = 0, refused = 0, unmatched = 0, fullBefore;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    nt = (const IMAGE_NT_HEADERS *)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    if (!nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress)
        return;
    ed = (const IMAGE_EXPORT_DIRECTORY *)
         (b + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    nameRvas = (const unsigned *)(b + ed->AddressOfNames);
    n = ed->NumberOfNames;
    u9patch_usage(NULL, NULL, &fullBefore);

    for (i = 0; i < n; i++) {
        const char *nm = (const char *)(b + nameRvas[i]);
        const char *at = nm;
        const char *p2;
        unsigned purge = 0;
        void *stub;
        FARPROC fn;
        U9PatchSpec spec;

        while (*at && *at != '@') at++;
        if (*at != '@') { unmatched++; continue; }
        p2 = at + 1;
        if (!*p2) { unmatched++; continue; }
        while (*p2 >= '0' && *p2 <= '9') { purge = purge * 10u + (unsigned)(*p2 - '0'); p2++; }
        if (*p2) { unmatched++; continue; }

        stub = nop_for(purge);
        if (!stub) { unmatched++; continue; }
        fn = GetProcAddress(h, nm);
        if (!fn) { unmatched++; continue; }

        u9sym_set_runtime(U9SYM_GR_SILENCE, (uintptr_t)fn);
        spec.name        = nm;
        spec.mechanism   = U9PATCH_INLINE;
        spec.target      = U9SYM_GR_SILENCE;
        spec.length      = 5;
        spec.expected    = NULL;
        spec.expectedLen = 0;
        spec.flags       = U9PF_EXCLUSIVE;
        if (u9patch_install(&spec, stub)) ok++;
        else refused++;
    }
    g_silenced = ok > 0u;
    {
        unsigned used, max, full;
        u9patch_usage(&used, &max, &full);
        full -= fullBefore;
        refused -= full;
        if (refused == 0u && full == 0u)
            u9log_line("FEED nGlide is INERT: %u of %u exports silenced (%u unmatched)."
                       " With no context, no call into it is safe. Registry %u of %u.",
                       ok, n, unmatched, used, max);
        else
            u9log_line("FEED WARNING nGlide is NOT inert: %u of %u exports silenced, %u refused,"
                       " %u not installed because the patch registry is full (%u of %u), %u unmatched."
                       " A call into one left live is unsafe.",
                       ok, n, refused, full, used, max, unmatched);
    }
}

void u9feed_install(void)
{
    static int done;
    HMODULE h;
    unsigned i, ok = 0;

    int cutGeom, cutTex;

    if (done) return;
    cutGeom = !u9cfg_int("feed_glide", 1);
    cutTex  = !u9cfg_int("feed_glide_tex", 1);

    if (u9cfg_int("own_renderer_life", 0)) { cutGeom = 1; cutTex = 1; }
    if (u9cfg_int("own_renderer_life", 0) && u9cfg_int("silence_glide", 1)) {
        h = GetModuleHandleA("glide2x.dll");
        if (!h) return;
        done = 1;
        silence_everything(h);
        return;
    }
    if (!cutGeom && !cutTex) { done = 1; return; }

    h = GetModuleHandleA("glide2x.dll");
    if (!h) {
        if (u9cfg_int("own_renderer_life", 0)) {
            done = 1;
            u9log_line("FEED nothing to cut - glide2x was never loaded"
                       " (own_renderer_life). That is the point.");
        }
        return;
    }
    done = 1;

    for (i = 0; i < N_ENTRIES; i++) {
        U9PatchSpec spec;
        FARPROC     fn;
        if (ENTRIES[i].group == GRP_GEOM && !cutGeom) continue;
        if (ENTRIES[i].group == GRP_TEX  && !cutTex)  continue;
        fn = resolve(h, ENTRIES[i].name);
        if (!fn) { u9log_line("FEED %s not exported", ENTRIES[i].name); continue; }

        u9sym_set_runtime(ENTRIES[i].sym, (uintptr_t)fn);
        u9log_line("FEED %-34s -> 0x%08X", ENTRIES[i].name, (unsigned)(uintptr_t)fn);

        spec.name        = ENTRIES[i].name;
        spec.mechanism   = U9PATCH_INLINE;
        spec.target      = ENTRIES[i].sym;
        spec.length      = 5;
        spec.expected    = NULL;
        spec.expectedLen = 0;
        spec.flags       = U9PF_EXCLUSIVE;

        if (u9patch_install(&spec, ENTRIES[i].replacement)) ok++;
        else u9log_line("FEED %s NOT hooked - see the INLINE line above for why",
                        ENTRIES[i].name);
    }
    g_installed = (ok != 0);
    u9log_line("FEED cut: geometry %s, texture downloads %s - %u entry points"
               " silenced. The engine still projects, pools, sorts, flushes and"
               " applies materials; nGlide simply receives nothing.",
               cutGeom ? "YES" : "no", cutTex ? "YES" : "no", ok);
}

void u9feed_report(void)
{
    unsigned i, total = 0;
    u9feed_glide_calls_report("teardown");
    if (!g_installed) return;
    for (i = 0; i < N_ENTRIES; i++) {
        total += g_drop[i];
        if (g_drop[i])
            u9log_line("FEED dropped %-34s %u", ENTRIES[i].name, g_drop[i]);
    }
    u9log_line("FEED dropped %u calls into nGlide in total", total);
    if (!total)
        u9log_line("FEED WARNING: nothing was dropped. Either the engine reaches"
                   " nGlide by an entry point not in this list, or the hooks did"
                   " not take - a flat memory floor would then mean nothing.");
}
