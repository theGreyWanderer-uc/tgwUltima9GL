#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>

#include "texoverride.h"
#include "texdecode.h"
#include "../injection/config.h"
#include "../common/lifecycle.h"
#include "../diag/log.h"

#define TEXOVR_COMPOSED_FIRST 8000

#define TEXOVR_DEFAULT_MAX 2048

enum { ST_UNTRIED = 0, ST_GOOD, ST_BAD };

typedef struct {
    int           id, id2;
    unsigned char state;
    char          file[MAX_PATH];
} Entry;

static int      g_enabled = -1;
static int      g_indexed;
static char     g_dir[MAX_PATH];
static Entry   *g_index;
static unsigned g_count, g_cap;
static unsigned g_maxEdge;
static unsigned g_hits, g_bad, g_skippedFiles, g_goodFiles;
static double   g_loadMsMax;

int u9tex_enabled(void)
{
    if (g_enabled < 0) g_enabled = u9cfg_int("texture_override", 0) ? 1 : 0;
    return g_enabled;
}

static int is_pot(unsigned v) { return v && !(v & (v - 1)); }

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = (const Entry *)a, *y = (const Entry *)b;
    if (x->id != y->id)   return x->id < y->id ? -1 : 1;
    if (x->id2 != y->id2) return x->id2 < y->id2 ? -1 : 1;
    return 0;
}

static int parse_name(const char *name, int *id, int *id2)
{
    const char *dot = strrchr(name, '.'), *p = name;
    long a = 0, b = 0;
    int digits;

    if (!dot || (lstrcmpiA(dot, ".png") && lstrcmpiA(dot, ".tga") &&
                 lstrcmpiA(dot, ".bmp")))
        return 0;
    if (dot - name >= 2 && dot[-2] == '_' && (dot[-1] == 'a' || dot[-1] == 'A'))
        return 0;

    for (digits = 0; *p >= '0' && *p <= '9'; p++, digits++) a = a * 10 + (*p - '0');
    if (!digits || digits > 6 || *p != '_') return 0;
    p++;
    for (digits = 0; *p >= '0' && *p <= '9'; p++, digits++) b = b * 10 + (*p - '0');
    if (!digits || digits > 6) return 0;
    if (p != dot && *p != '_' && *p != '.') return 0;

    *id = (int)a;
    *id2 = (int)b;
    return 1;
}

static void add_entry(int id, int id2, const char *file)
{
    if (g_count == g_cap) {
        unsigned ncap = g_cap ? g_cap * 2u : 256u;
        Entry *n = g_index
            ? (Entry *)HeapReAlloc(GetProcessHeap(), 0, g_index, ncap * sizeof(Entry))
            : (Entry *)HeapAlloc(GetProcessHeap(), 0, ncap * sizeof(Entry));
        if (!n) return;
        g_index = n;
        g_cap = ncap;
    }
    g_index[g_count].id = id;
    g_index[g_count].id2 = id2;
    g_index[g_count].state = ST_UNTRIED;
    lstrcpynA(g_index[g_count].file, file, MAX_PATH);
    g_count++;
}

static void texovr_remove(void)
{
    u9log_line("TEXOVR shutdown: %u replacements used, %u files rejected",
               g_hits, g_bad);
    if (g_index) HeapFree(GetProcessHeap(), 0, g_index);
    g_index = NULL;
    g_count = g_cap = 0;
}

static void find_dir(void)
{
    HMODULE self = NULL;
    char *slash;
    g_dir[0] = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(void *)&u9tex_enabled, &self))
        return;
    if (!GetModuleFileNameA(self, g_dir, sizeof(g_dir))) { g_dir[0] = 0; return; }
    slash = strrchr(g_dir, '\\');
    if (!slash) { g_dir[0] = 0; return; }
    lstrcpynA(slash + 1, "textures", (int)(sizeof(g_dir) - (slash + 1 - g_dir)));
}

static void build_index(void)
{
    char pattern[MAX_PATH + 8];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    GLint glMax = 0;
    unsigned i, dups = 0;

    g_indexed = 1;

    g_maxEdge = (unsigned)u9cfg_int("texture_override_max", TEXOVR_DEFAULT_MAX);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glMax);
    if (glMax > 0 && g_maxEdge > (unsigned)glMax) g_maxEdge = (unsigned)glMax;

    find_dir();
    if (!g_dir[0]) {
        u9log_line("TEXOVR *** could not locate the DLL folder - override inactive");
        return;
    }
    wsprintfA(pattern, "%s\\*", g_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            int id, id2;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!parse_name(fd.cFileName, &id, &id2)) { g_skippedFiles++; continue; }
            if (id >= TEXOVR_COMPOSED_FIRST) {
                u9log_line("TEXOVR ignoring %s - ids from %d up are composed at "
                           "runtime and cannot be replaced", fd.cFileName,
                           TEXOVR_COMPOSED_FIRST);
                continue;
            }
            add_entry(id, id2, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (g_count) qsort(g_index, g_count, sizeof(Entry), cmp_entry);
    for (i = 1; i < g_count; i++)
        if (!cmp_entry(&g_index[i - 1], &g_index[i])) {
            g_index[i].state = ST_BAD;
            if (++dups <= 20)
                u9log_line("TEXOVR duplicate for %d/%d: using %s, ignoring %s",
                           g_index[i].id, g_index[i].id2,
                           g_index[i - 1].file, g_index[i].file);
        }

    g_goodFiles = g_count - dups;
    u9log_line("TEXOVR on: %u replacement files indexed in %s "
               "(max edge %u, GL max %d; %u other files ignored)",
               g_goodFiles, g_dir, g_maxEdge, (int)glMax, g_skippedFiles);
    u9life_push("texture override index", texovr_remove);
}

static Entry *find(int id, int id2)
{
    unsigned lo = 0, hi = g_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2u;
        Entry *e = &g_index[mid];
        if (e->id == id && e->id2 == id2) {
            while (e > g_index && e[-1].id == id && e[-1].id2 == id2) e--;
            return e;
        }
        if (e->id < id || (e->id == id && e->id2 < id2)) lo = mid + 1u;
        else hi = mid;
    }
    return NULL;
}

static void reject(Entry *e, const char *why, unsigned w, unsigned h)
{
    e->state = ST_BAD;
    g_bad++;
    u9log_line("TEXOVR *** %s rejected (%ux%u): %s - the engine texture is used",
               e->file, w, h, why);
}

int u9tex_override(int id, int id2,
                   const unsigned char *engineRgba,
                   unsigned engineW, unsigned engineH,
                   U9TexImage *out)
{
    Entry *e;
    char path[MAX_PATH * 2];
    const char *why = "unknown";
    unsigned w = 0, h = 0, i, n;
    unsigned char *px;

    if (!u9tex_enabled()) return 0;
    if (id < 0 || id >= TEXOVR_COMPOSED_FIRST) return 0;
    if (!g_indexed) build_index();
    if (!g_count) return 0;

    e = find(id, id2);
    if (!e || e->state == ST_BAD) return 0;

    wsprintfA(path, "%s\\%s", g_dir, e->file);
    {
        LARGE_INTEGER f, t0, t1;
        double ms;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        px = u9tex_decode_file(path, &w, &h, &why);
        QueryPerformanceCounter(&t1);
        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
        if (ms > g_loadMsMax) g_loadMsMax = ms;
    }
    if (!px) { reject(e, why, 0, 0); return 0; }

    if (!is_pot(w) || !is_pot(h)) {
        HeapFree(GetProcessHeap(), 0, px);
        reject(e, "width and height must be powers of two", w, h);
        return 0;
    }
    if (w > g_maxEdge || h > g_maxEdge) {
        HeapFree(GetProcessHeap(), 0, px);
        reject(e, "larger than texture_override_max / the GL limit", w, h);
        return 0;
    }

    if (engineRgba) {
        int opaque = 1;
        n = engineW * engineH;
        for (i = 0; i < n; i++)
            if (engineRgba[i * 4u + 3u] != 255u) { opaque = 0; break; }
        if (opaque) {
            n = w * h;
            for (i = 0; i < n; i++) px[i * 4u + 3u] = 255u;
        }
    }

    if (e->state == ST_UNTRIED) {
        e->state = ST_GOOD;
        u9log_line("TEXOVR id=%d/%d %ux%u -> %ux%u from %s%s", id, id2,
                   engineW, engineH, w, h, e->file,
                   (w * engineH != h * engineW)
                       ? "  (aspect differs from the engine texture)" : "");
    }
    g_hits++;
    out->w = w;
    out->h = h;
    out->rgba = px;
    return 1;
}

U9TexQuery u9tex_query(int id, int id2, char *file, unsigned fileSize)
{
    Entry *e;
    if (file && fileSize) file[0] = 0;
    if (!u9tex_enabled()) return U9TEX_Q_OFF;
    if (id >= TEXOVR_COMPOSED_FIRST) return U9TEX_Q_COMPOSED;
    if (!g_indexed) build_index();
    e = (id >= 0 && g_count) ? find(id, id2) : NULL;
    if (!e) return U9TEX_Q_NONE;
    if (file && fileSize) lstrcpynA(file, e->file, (int)fileSize);
    return e->state == ST_GOOD ? U9TEX_Q_USED : e->state == ST_BAD ? U9TEX_Q_REJECTED : U9TEX_Q_UNTRIED;
}

const char *u9tex_folder(void) { return g_dir; }

void u9tex_take_stats(U9TexStats *s)
{
    s->files     = g_goodFiles;
    s->used      = g_hits;
    s->rejected  = g_bad;
    s->loadMsMax = g_loadMsMax;
    g_loadMsMax  = 0.0;
}
