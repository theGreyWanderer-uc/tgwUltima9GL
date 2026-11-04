#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include "gpuprof.h"
#include "../common/perfclock.h"

void u9log_line(const char *fmt, ...);

#define GL_TIMESTAMP_               0x8E28
#define GL_QUERY_RESULT_            0x8866
#define GL_QUERY_RESULT_AVAILABLE_  0x8867
typedef unsigned long long GLuint64_;
typedef void (APIENTRY *GenQueriesFn)(GLsizei, GLuint *);
typedef void (APIENTRY *QueryCounterFn)(GLuint, GLenum);
typedef void (APIENTRY *GetQueryObjectivFn)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *GetQueryObjectui64vFn)(GLuint, GLenum, GLuint64_ *);
static GenQueriesFn          fGenQueries;
static QueryCounterFn        fQueryCounter;
static GetQueryObjectivFn    fGetQueryObjectiv;
static GetQueryObjectui64vFn fGetQueryObjectui64v;

#define QUERIES (2 + 2 * U9GPU_MAX_REGIONS)
typedef struct {
    int      busy;
    uint64_t frame, issuedQpc, seq;
    uint32_t began, ended;
    GLuint   q[QUERIES];
} Slot;

static char      g_names[U9GPU_MAX_REGIONS][24];
static int       g_regions;
static Slot      g_ring[U9GPU_RING];
static Slot     *g_cur;
static uint64_t  g_seq;
static U9GpuState g_state = { -1, 0, 0, 0, { 0 } };

int u9gpu_find(const char *name)
{
    int i;
    for (i = 0; i < g_regions; i++) if (!strcmp(g_names[i], name)) return i;
    return -1;
}

int u9gpu_region(const char *name)
{
    int i = u9gpu_find(name);
    if (i >= 0) return i;
    if (g_regions >= U9GPU_MAX_REGIONS) return -1;
    strncpy(g_names[g_regions], name, sizeof g_names[0] - 1);
    return g_regions++;
}

const U9GpuState *u9gpu_state(void) { return &g_state; }

static void *gl_proc(const char *name)
{
    INT_PTR p = (INT_PTR)wglGetProcAddress(name);
    return (p >= -1 && p <= 3) ? NULL : (void *)p;
}

static int setup(void)
{
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    const char *ver = (const char *)glGetString(GL_VERSION);
    int major = ver ? ver[0] - '0' : 0, minor = ver && ver[1] == '.' ? ver[2] - '0' : 0, i;
    g_state.supported = 0;
    if (!(major > 3 || (major == 3 && minor >= 3) || (ext && strstr(ext, "GL_ARB_timer_query")))) {
        u9log_line("GPUPROF off: no timer queries (GL %s)", ver ? ver : "?");
        return 0;
    }
    fGenQueries = (GenQueriesFn)gl_proc("glGenQueries");
    fQueryCounter = (QueryCounterFn)gl_proc("glQueryCounter");
    fGetQueryObjectiv = (GetQueryObjectivFn)gl_proc("glGetQueryObjectiv");
    fGetQueryObjectui64v = (GetQueryObjectui64vFn)gl_proc("glGetQueryObjectui64v");
    if (!fGenQueries || !fQueryCounter || !fGetQueryObjectiv || !fGetQueryObjectui64v) {
        u9log_line("GPUPROF off: timer query entry points missing");
        return 0;
    }
    for (i = 0; i < U9GPU_RING; i++) fGenQueries(QUERIES, g_ring[i].q);
    glGetError();
    g_state.supported = 1;
    u9log_line("GPUPROF ready: %d slots of %d timestamp queries, %d regions", U9GPU_RING, QUERIES, g_regions);
    return 1;
}

void u9gpu_frame_begin(uint64_t frame, int sample)
{
    int i;
    g_cur = NULL;
    if (!sample) return;
    if (g_state.supported < 0) setup();
    if (g_state.supported != 1) return;
    for (i = 0; i < U9GPU_RING; i++) if (!g_ring[i].busy) break;
    if (i == U9GPU_RING) { g_state.skipped++; return; }
    g_cur = &g_ring[i];
    g_cur->busy = 1;
    g_cur->frame = frame;
    g_cur->issuedQpc = u9perf_now();
    g_cur->seq = ++g_seq;
    g_cur->began = g_cur->ended = 0;
    fQueryCounter(g_cur->q[0], GL_TIMESTAMP_);
}

void u9gpu_begin(int region)
{
    if (!g_cur || region < 0 || region >= g_regions) return;
    fQueryCounter(g_cur->q[2 + 2 * region], GL_TIMESTAMP_);
    g_cur->began |= 1u << region;
}

void u9gpu_end(int region)
{
    if (!g_cur || region < 0 || region >= g_regions || !(g_cur->began & (1u << region))) return;
    fQueryCounter(g_cur->q[3 + 2 * region], GL_TIMESTAMP_);
    g_cur->ended |= 1u << region;
}

static void collect(void)
{
    Slot *oldest = NULL;
    GLint ready = 0;
    GLuint64_ t0 = 0, t1 = 0, a, b;
    int i;
    for (i = 0; i < U9GPU_RING; i++)
        if (g_ring[i].busy && &g_ring[i] != g_cur && (!oldest || g_ring[i].seq < oldest->seq)) oldest = &g_ring[i];
    if (!oldest) return;
    fGetQueryObjectiv(oldest->q[1], GL_QUERY_RESULT_AVAILABLE_, &ready);
    if (!ready) return;
    fGetQueryObjectui64v(oldest->q[0], GL_QUERY_RESULT_, &t0);
    fGetQueryObjectui64v(oldest->q[1], GL_QUERY_RESULT_, &t1);
    memset(&g_state.latest, 0, sizeof g_state.latest);
    g_state.latest.frame = oldest->frame;
    g_state.latest.issued_qpc = oldest->issuedQpc;
    g_state.latest.read_qpc = u9perf_now();
    g_state.latest.frame_ms = t1 > t0 ? (double)(t1 - t0) / 1e6 : 0.0;
    for (i = 0; i < g_regions; i++) {
        uint32_t bit = 1u << i;
        if (!(oldest->began & oldest->ended & bit)) continue;
        a = b = 0;
        fGetQueryObjectui64v(oldest->q[2 + 2 * i], GL_QUERY_RESULT_, &a);
        fGetQueryObjectui64v(oldest->q[3 + 2 * i], GL_QUERY_RESULT_, &b);
        g_state.latest.region_ms[i] = b > a ? (double)(b - a) / 1e6 : 0.0;
        g_state.latest.region_mask |= bit;
    }
    oldest->busy = 0;
    g_state.samples++;
    g_state.have = 1;
}

void u9gpu_report(void)
{
    char regions[512];
    size_t n = 0;
    int i;
    regions[0] = 0;
    for (i = 0; i < g_regions && n < sizeof regions - 40; i++)
        if (g_state.latest.region_mask & (1u << i))
            n += (size_t)snprintf(regions + n, sizeof regions - n, " %s=%.3f", g_names[i], g_state.latest.region_ms[i]);
    u9log_line("GPUPROF totals: %s, %llu samples, %llu skipped (ring full); last sample frame %llu, %.3f ms GPU:%s",
               g_state.supported == 1 ? "timer queries" : g_state.supported == 0 ? "no timer queries" : "never asked",
               (unsigned long long)g_state.samples, (unsigned long long)g_state.skipped,
               (unsigned long long)g_state.latest.frame, g_state.latest.frame_ms, n ? regions : " none");
}

void u9gpu_frame_end(void)
{
    if (g_cur) {
        fQueryCounter(g_cur->q[1], GL_TIMESTAMP_);
        g_cur = NULL;
    }
    if (g_state.supported == 1) collect();
}
