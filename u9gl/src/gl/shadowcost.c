#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "shadowcost.h"
#include "gpuprof.h"
#include "glshadow.h"
#include "glbackface.h"
#include "../common/perfclock.h"
#include "../injection/config.h"
#include "../fix/gold.h"
#include "../fix/lightwatch.h"
#include "gleffects.h"
#include <math.h>

void u9log_line(const char *fmt, ...);
void u9gl_guard_stats(unsigned *tris);
void u9gl_back_face_stats(unsigned *tris, unsigned *runs, unsigned *dropped, unsigned *polys);
extern int g_guardPct;
extern unsigned g_sunMemLive;

int      g_scostOn;
uint64_t g_scostGuardTsc;
unsigned g_scostGuardCalls;

static uint64_t g_part[U9SC_N];
typedef struct {
    unsigned frames;
    uint64_t frameSum, frameMax;
    uint64_t partSum[U9SC_N], partMax[U9SC_N];
    uint64_t guardTsc, guardMax;
    unsigned guardCalls;
    uint64_t memUsSum; unsigned memUsMax;
    uint64_t guardTris, casterTris, cubes, memHeld, sunTris;
} Window;
static Window   g_w, g_last;
#define SC_BANDS 4
static const float g_band[SC_BANDS] = { 500.0f, 1000.0f, 2000.0f, 4000.0f };
typedef struct {
    unsigned builds, changes, lastType;
    float    prevPos[3];
    double   camD, avD, reach;
    uint64_t frameTris, memTris, qpc;
} CubeWin;
typedef struct {
    unsigned frames;
    uint64_t lights[SC_BANDS + 1], cubes[SC_BANDS + 1];
    int      avKnown;
} LampWin;
static CubeWin g_cw[U9SHADOW_MAX], g_cwLast[U9SHADOW_MAX];
typedef struct {
    unsigned builds;
    uint64_t t[U9SC_IN_N], runs, drawn, culled;
    unsigned gChoices, gTested, gListed, gChecks, gMismatch;
    unsigned kKept, kMade, kChecks, kMismatch;
} InWin;
static unsigned g_gPrev[5];
static unsigned g_kPrev[4];
static unsigned g_refusedPrev, g_sunBuildsPrev, g_refusedLast, g_sunMapsLast;
static unsigned g_evictPrev, g_evictLast;
extern int      g_u9ReadCache;
extern unsigned g_u9ReadCalls, g_u9ReadAsked;
static unsigned g_rcPrev[2], g_rcLast[2];
extern unsigned g_cfgLookups, g_cfgRemembered;
static unsigned g_cfPrev[2], g_cfLast[2];
static InWin   g_iw, g_iwLast;
static LampWin g_lw, g_lwLast;
static float   g_scAv[3], g_scCam[3];
static int     g_scAvOk;
static uint64_t g_lastQpc, g_winQpc, g_winTsc, g_lastWinMs;
static uint64_t g_frameGuardTsc;
static unsigned g_lastBuilds;
static double   g_lastTscPerMs;

void u9scost_init(void)
{
    g_scostOn = u9cfg_int("diag_shadow_cost", 0) != 0;
    if (g_scostOn) u9log_line("SHADOWCOST on: a line a second (diag_shadow_cost 1)");
}

static float dist3(const float a[3], const float b[3])
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

void u9scost_lights(const float cam[3], const float *pos, int strideFloats, int n)
{
    int i, b, ax, ay, az;
    if (!g_scostOn) return;
    memcpy(g_scCam, cam, sizeof g_scCam);
    g_scAvOk = u9avatar_position(&ax, &ay, &az);
    if (g_scAvOk) { g_scAv[0] = (float)ax; g_scAv[1] = (float)ay; g_scAv[2] = (float)az; }
    else memcpy(g_scAv, cam, sizeof g_scAv);
    g_lw.frames++;
    if (g_scAvOk) g_lw.avKnown++;
    for (i = 0; i < n; i++) {
        float d = dist3(pos + (size_t)i * (size_t)strideFloats, g_scAv);
        for (b = 0; b < SC_BANDS; b++) if (d <= g_band[b]) g_lw.lights[b]++;
        g_lw.lights[SC_BANDS]++;
    }
}

void u9scost_inside(const uint64_t t[U9SC_IN_N], unsigned runs, unsigned drawn, unsigned culled)
{
    int i;
    if (!g_scostOn) return;
    g_iw.builds++;
    for (i = 0; i < U9SC_IN_N; i++) g_iw.t[i] += t[i];
    g_iw.runs += runs;
    g_iw.drawn += drawn;
    g_iw.culled += culled;
}

void u9scost_cube(int slot, const float pos[3], float reach, int type, unsigned frameTris, unsigned memTris,
                  uint64_t qpcTicks)
{
    CubeWin *c;
    float d;
    int b;
    if (!g_scostOn || slot < 0 || slot >= U9SHADOW_MAX) return;
    c = &g_cw[slot];
    if (c->builds && (fabsf(c->prevPos[0] - pos[0]) > 4.0f || fabsf(c->prevPos[1] - pos[1]) > 4.0f ||
                      fabsf(c->prevPos[2] - pos[2]) > 4.0f)) c->changes++;
    memcpy(c->prevPos, pos, sizeof c->prevPos);
    c->lastType = (unsigned)type;
    c->builds++;
    c->camD += dist3(pos, g_scCam);
    d = dist3(pos, g_scAv);
    c->avD += d;
    c->reach += reach;
    c->frameTris += frameTris;
    c->memTris += memTris;
    c->qpc += qpcTicks;
    for (b = 0; b < SC_BANDS; b++) if (d <= g_band[b]) g_lw.cubes[b]++;
    g_lw.cubes[SC_BANDS]++;
}

void u9scost_add(int part, uint64_t qpcTicks)
{
    if (part >= 0 && part < U9SC_N) g_part[part] += qpcTicks;
}

static double qms(uint64_t t)  { return (double)t * 1000.0 / (double)u9perf_frequency(); }
static double tms(uint64_t t)  { return g_lastTscPerMs > 0.0 ? (double)t / g_lastTscPerMs : 0.0; }
static double per(uint64_t s, unsigned n) { return n ? (double)s / n : 0.0; }

static void gpu_part(char *buf, size_t size, const char *name)
{
    const U9GpuState *g = u9gpu_state();
    int i = u9gpu_find(name);
    size_t len = strlen(buf);
    if (len >= size) return;
    if (i < 0 || !g->have || !(g->latest.region_mask & (1u << i))) snprintf(buf + len, size - len, " %s -", name);
    else snprintf(buf + len, size - len, " %s %.2f", name, g->latest.region_ms[i]);
}

static void line(void (*out)(const char *fmt, ...), const char *tag)
{
    const Window *w = &g_last;
    const U9GpuState *g = u9gpu_state();
    char gpu[256];
    unsigned n = w->frames;
    gpu[0] = 0;
    if (g->supported != 1) snprintf(gpu, sizeof gpu, " none (no timer queries)");
    else if (!g->have) snprintf(gpu, sizeof gpu, " no sample yet");
    else {
        snprintf(gpu, sizeof gpu, " (frame %llu) frame %.2f", (unsigned long long)g->latest.frame, g->latest.frame_ms);
        gpu_part(gpu, sizeof gpu, "scene");
        gpu_part(gpu, sizeof gpu, "pool_shadows");
        gpu_part(gpu, sizeof gpu, "sun_shadows");
        gpu_part(gpu, sizeof gpu, "water");
        gpu_part(gpu, sizeof gpu, "ao");
        gpu_part(gpu, sizeof gpu, "bloom");
        gpu_part(gpu, sizeof gpu, "rays");
        gpu_part(gpu, sizeof gpu, "composite");
    }
    out("%s %u frames in %llu ms, frame mean %.2f max %.2f ms | CPU ms a frame, mean/max: guard test %.3f/%.3f"
        " (%.0f triangles tested), lamp cubes %.3f/%.3f (%.1f cubes; memory choosing %.3f/%.3f of it), sun %.3f/%.3f",
        tag, n, (unsigned long long)g_lastWinMs, qms((uint64_t)per(w->frameSum, n)), qms(w->frameMax),
        tms((uint64_t)per(w->guardTsc, n)), tms(w->guardMax), per(w->guardCalls, n),
        qms((uint64_t)per(w->partSum[U9SC_CUBES], n)), qms(w->partMax[U9SC_CUBES]), per(w->cubes, n),
        per(w->memUsSum, n) / 1000.0, w->memUsMax / 1000.0,
        qms((uint64_t)per(w->partSum[U9SC_SUN], n)), qms(w->partMax[U9SC_SUN]));
    out("%s casters a frame: caster-only %.0f (guard %.0f of them, back faces the rest), sun memory held %.0f,"
        " sun near map %.0f | GPU ms, last sample:%s | switches: guard %d, backfaces %s, lamp memory %s,"
        " by face %s, grid %s, runbox %s, keep %s, fire %s, skip moving %s, margin %.0f, evict %s",
        tag, per(w->casterTris, n), per(w->guardTris, n), per(w->memHeld, n), per(w->sunTris, n), gpu,
        g_guardPct > 0 ? g_guardPct : 0, g_bfOn ? "on" : "off", g_shadowMem ? "on" : "off",
        g_lmFaces ? "on" : "off", g_lmGrid ? "on" : "off", g_lmRunBox ? "on" : "off", g_lmKeep ? "on" : "off",
        g_shadowFire ? "on" : "off",
        g_shadowMemAnimated ? "off" : "on", (double)g_sunMemMargin, g_sunEvict ? "on" : "off");
    {
        const LampWin *l = &g_lwLast;
        double f = l->frames ? (double)l->frames : 1.0;
        char buf[480];
        size_t len;
        int s, half;
        out("%s lamps a frame round the %s, within 500/1000/2000/4000/any: lights %.1f/%.1f/%.1f/%.1f/%.1f,"
            " with a cube %.1f/%.1f/%.1f/%.1f/%.1f",
            tag, l->avKnown * 2 >= (int)l->frames ? "Avatar" : "camera (Avatar unknown)",
            l->lights[0] / f, l->lights[1] / f, l->lights[2] / f, l->lights[3] / f, l->lights[4] / f,
            l->cubes[0] / f, l->cubes[1] / f, l->cubes[2] / f, l->cubes[3] / f, l->cubes[4] / f);
        for (half = 0; half < 2; half++) {
            len = (size_t)snprintf(buf, sizeof buf, "%s cubes %d-%d:", tag, half * 4, half * 4 + 3);
            for (s = half * 4; s < half * 4 + 4 && s < U9SHADOW_MAX; s++) {
                const CubeWin *c = &g_cwLast[s];
                double b = c->builds ? (double)c->builds : 1.0;
                const char *kind = c->lastType == U9TYPE_FILL_LIGHT ? "fill" : (int)c->lastType == U9TYPE_FIRE_LIGHT ? "fire" :
                                   (int)c->lastType < 0 ? "effect" : "lamp";
                if (len >= sizeof buf) break;
                if (!c->builds) len += (size_t)snprintf(buf + len, sizeof buf - len, " s%d none;", s);
                else len += (size_t)snprintf(buf + len, sizeof buf - len,
                                             " s%d %s cam %.0f av %.0f reach %.0f frame %.0f mem %.0f %.2f ms x%u%s;",
                                             s, kind, c->camD / b, c->avD / b, c->reach / b, c->frameTris / b,
                                             c->memTris / b, qms(c->qpc) / b, c->builds,
                                             c->changes ? " (light changed)" : "");
            }
            out("%s", buf);
        }
    }
    {
        const InWin *w2 = &g_iwLast;
        double b = w2->builds ? (double)w2->builds : 1.0;
        out("%s inside a cube, ms a build (%u builds): setup %.3f, fixture pass %.3f, memory choice %.3f,"
            " six faces: switch %.3f, frame runs %.3f, memory %.3f; finish %.3f | runs walked a face %.0f,"
            " drawn %.0f and culled %.0f over six faces | choice (grid or runbox): %u, tested %.0f of %.0f a choice,"
            " %u checked, %u differed",
            tag, w2->builds, qms(w2->t[U9SC_IN_SETUP]) / b, qms(w2->t[U9SC_IN_FIXTURE]) / b,
            qms(w2->t[U9SC_IN_SELECT]) / b, qms(w2->t[U9SC_IN_SWITCH]) / b, qms(w2->t[U9SC_IN_RUNS]) / b,
            qms(w2->t[U9SC_IN_MEM]) / b, qms(w2->t[U9SC_IN_FINISH]) / b,
            w2->runs / b, w2->drawn / b, w2->culled / b, w2->gChoices,
            w2->gChoices ? (double)w2->gTested / w2->gChoices : 0.0,
            w2->gChoices ? (double)w2->gListed / w2->gChoices : 0.0, w2->gChecks, w2->gMismatch);
        out("%s lamp choices: %u made, %u kept (the memory the same), %u kept ones checked, %u differed",
            tag, w2->kMade, w2->kKept, w2->kChecks, w2->kMismatch);
    }
    u9shadow_mem_census(out, tag[0] == 'S' ? "SHADOWMEM" : tag, g_refusedLast, g_sunMapsLast);
    out("%s memory evicted (the farthest, the memory full) %u this second", tag[0] == 'S' ? "SHADOWMEM" : tag, g_evictLast);
    out("%s readable checks %u this second, %u asked Windows (readcache %d)", tag, g_rcLast[0], g_rcLast[1], g_u9ReadCache);
    out("%s settings lookups on the game thread %u this second, %u answered from memory", tag, g_cfLast[0], g_cfLast[1]);
}

static void log_out(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("%s", buf);
}

void u9scost_frame_end(void)
{
    uint64_t now, tsc, guard;
    unsigned gt, bt, runs, dropped, polys, i;
    if (!g_scostOn) {
        g_lastQpc = g_winQpc = 0;
        memset(g_part, 0, sizeof g_part);
        return;
    }
    now = u9perf_now();
    tsc = u9scost_tsc();
    if (!g_winQpc) {
        memset(&g_w, 0, sizeof g_w);
        memset(g_part, 0, sizeof g_part);
        g_winQpc = g_lastQpc = now;
        g_winTsc = tsc;
        g_frameGuardTsc = g_scostGuardTsc = 0;
        g_scostGuardCalls = 0;
        g_lastBuilds = g_shadowBuilds;
        memset(g_cw, 0, sizeof g_cw);
        memset(&g_lw, 0, sizeof g_lw);
        memset(&g_iw, 0, sizeof g_iw);
        return;
    }
    g_w.frames++;
    g_w.frameSum += now - g_lastQpc;
    if (now - g_lastQpc > g_w.frameMax) g_w.frameMax = now - g_lastQpc;
    g_lastQpc = now;
    for (i = 0; i < U9SC_N; i++) {
        g_w.partSum[i] += g_part[i];
        if (g_part[i] > g_w.partMax[i]) g_w.partMax[i] = g_part[i];
        g_part[i] = 0;
    }
    guard = g_scostGuardTsc - g_frameGuardTsc;
    g_frameGuardTsc = g_scostGuardTsc;
    g_w.guardTsc += guard;
    if (guard > g_w.guardMax) g_w.guardMax = guard;
    g_w.memUsSum += g_shadowMemUs;
    if (g_shadowMemUs > g_w.memUsMax) g_w.memUsMax = g_shadowMemUs;
    u9gl_guard_stats(&gt);
    u9gl_back_face_stats(&bt, &runs, &dropped, &polys);
    g_w.guardTris += gt;
    g_w.casterTris += bt;
    g_w.cubes += g_shadowBuilds - g_lastBuilds;
    g_lastBuilds = g_shadowBuilds;
    g_w.memHeld += g_sunMemLive;
    g_w.sunTris += g_sunTris;
    if (now - g_winQpc >= u9perf_frequency()) {
        g_w.guardCalls = g_scostGuardCalls;
        g_scostGuardCalls = 0;
        g_lastWinMs = (now - g_winQpc) * 1000u / u9perf_frequency();
        if (g_lastWinMs) g_lastTscPerMs = (double)(tsc - g_winTsc) / (double)g_lastWinMs;
        g_last = g_w;
        memcpy(g_cwLast, g_cw, sizeof g_cw);
        memset(g_cw, 0, sizeof g_cw);
        g_lwLast = g_lw;
        memset(&g_lw, 0, sizeof g_lw);
        g_iw.gChoices = g_gridChoices - g_gPrev[0];
        g_iw.gTested = g_gridTested - g_gPrev[1];
        g_iw.gListed = g_gridListed - g_gPrev[2];
        g_iw.gChecks = g_gridChecks - g_gPrev[3];
        g_iw.gMismatch = g_gridMismatch - g_gPrev[4];
        g_gPrev[0] = g_gridChoices; g_gPrev[1] = g_gridTested; g_gPrev[2] = g_gridListed;
        g_gPrev[3] = g_gridChecks; g_gPrev[4] = g_gridMismatch;
        g_iw.kKept = g_keepKept - g_kPrev[0];
        g_iw.kMade = g_keepMade - g_kPrev[1];
        g_iw.kChecks = g_keepChecks - g_kPrev[2];
        g_iw.kMismatch = g_keepMismatch - g_kPrev[3];
        g_kPrev[0] = g_keepKept; g_kPrev[1] = g_keepMade; g_kPrev[2] = g_keepChecks; g_kPrev[3] = g_keepMismatch;
        g_refusedLast = g_memRefused - g_refusedPrev;
        g_refusedPrev = g_memRefused;
        g_sunMapsLast = g_sunBuilds - g_sunBuildsPrev;
        g_evictLast = g_memEvicted - g_evictPrev;
        g_evictPrev = g_memEvicted;
        g_rcLast[0] = g_u9ReadCalls - g_rcPrev[0]; g_rcPrev[0] = g_u9ReadCalls;
        g_rcLast[1] = g_u9ReadAsked - g_rcPrev[1]; g_rcPrev[1] = g_u9ReadAsked;
        g_cfLast[0] = g_cfgLookups - g_cfPrev[0]; g_cfPrev[0] = g_cfgLookups;
        g_cfLast[1] = g_cfgRemembered - g_cfPrev[1]; g_cfPrev[1] = g_cfgRemembered;
        g_sunBuildsPrev = g_sunBuilds;
        g_iwLast = g_iw;
        memset(&g_iw, 0, sizeof g_iw);
        memset(&g_w, 0, sizeof g_w);
        g_winQpc = now;
        g_winTsc = tsc;
        line(log_out, "SHADOWCOST");
    }
}

void u9scost_report(void (*out)(const char *fmt, ...))
{
    if (!g_scostOn) { out("shadow cost timing off (pool shadow cost on, or diag_shadow_cost = 1)"); return; }
    if (!g_last.frames) { out("shadow cost timing on: no full second yet"); return; }
    line(out, "last second:");
}
