#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>
#include "metrics.h"
#include "keys.h"
#include "../common/perfclock.h"
#include "../common/lifecycle.h"
#include "../telemetry/publisher.h"
#include "../../include/u9telemetry.h"

void u9log_line(const char *fmt, ...);
int  u9cfg_int(const char *key, int def);

static U9GLMetrics g_m;
static uint64_t    g_memorySampleQpc;
static int         g_enabled;
static int         g_overlay;
enum { PAGE_OFF = 0, PAGE_GENERAL, PAGE_OBJECTS, PAGE_TEXTURES, PAGE__N };
static const char *const g_pageName[PAGE__N] = { "off", "general", "objects", "textures" };
static unsigned    g_sampleEvery;
static unsigned    g_summaryEvery;
static uint64_t    g_overlayDraws;

#define WINDOW 16
typedef struct { uint64_t frame; uint64_t priv; } Sample;
static Sample   g_win[WINDOW];
static unsigned g_winCount, g_winNext;
static uint64_t g_winLastFrame, g_winLastPresents;
static int      g_winHeld;

static DWORD    g_lastTick;
static uint64_t g_lastFrameNo;
static double   g_fps;

static unsigned g_texUploadsPrev, g_texEvictPrev;
static unsigned g_texUploadsPerSec, g_texEvictPerSec;
static double   g_uploadMsMaxRun, g_uploadMsMax;
static double   g_ovLoadMsMaxRun, g_ovLoadMsMax;
static unsigned g_ovFiles, g_ovUsed, g_ovRejected;
static uint64_t g_tickQpc;
static unsigned g_tickCached, g_tickPending, g_tickUploads, g_tickDupes, g_tickEvictions;
static uint64_t g_tickTexBytes;
static unsigned g_tickOvFiles, g_tickOvUsed, g_tickOvRejected;

double   u9gl_upload_take_ms(void);
unsigned u9gl_tex_budget_mb(void);
uint64_t u9gl_tex_cache_bytes(void);
uint64_t u9gl_tex_budget_bytes(void);
unsigned u9gl_override_bytes(unsigned *count);
void     u9gl_tex_stats(unsigned *cacheCount, unsigned *uploads, unsigned *dupes,
                        unsigned *pending, unsigned *mb, unsigned *evictions);
#include "../texture/texoverride.h"
#include "../diag/objstats.h"
#include "../common/u9time.h"
#include "regions.h"

#define U9_AREA_SLOTS_USABLE_PUB 2045u

typedef struct {
    DWORD  cb;
    DWORD  PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivateUsage;
} U9_PMC_EX;

typedef BOOL (WINAPI *GetPMIFn)(HANDLE, U9_PMC_EX *, DWORD);
static GetPMIFn g_getPMI;
static int      g_pmiTried;

static void resolve_pmi(void)
{
    HMODULE m;
    if (g_pmiTried) return;
    g_pmiTried = 1;
    m = GetModuleHandleA("kernel32.dll");
    if (m) g_getPMI = (GetPMIFn)(void *)GetProcAddress(m, "K32GetProcessMemoryInfo");
    if (!g_getPMI) {
        m = LoadLibraryA("psapi.dll");
        if (m) g_getPMI = (GetPMIFn)(void *)GetProcAddress(m, "GetProcessMemoryInfo");
    }
    u9log_line("METRICS process-memory query %s",
               g_getPMI ? "available" : "UNAVAILABLE - memory columns stay zero");
}

static void sample_memory(void)
{
    U9_PMC_EX pmc;
    resolve_pmi();
    if (!g_getPMI) return;
    memset(&pmc, 0, sizeof pmc);
    pmc.cb = sizeof pmc;
    if (!g_getPMI(GetCurrentProcess(), &pmc, sizeof pmc)) return;

    g_m.private_bytes    = (uint64_t)pmc.PrivateUsage;
    g_m.working_set      = (uint64_t)pmc.WorkingSetSize;
    g_m.peak_working_set = (uint64_t)pmc.PeakWorkingSetSize;
    g_m.pagefile         = (uint64_t)pmc.PagefileUsage;
    g_memorySampleQpc    = u9perf_now();

    if (g_winLastFrame && g_m.frame_no > g_winLastFrame) {
        uint64_t ran = g_m.frame_no - g_winLastFrame, shown = g_m.presents - g_winLastPresents;
        if (shown * 2 < ran) {
            if (!g_winHeld)
                u9log_line("METRICS memory window restarted: %u of %u frames since the last sample were"
                           " not presented (loading, a movie or an inactive window)",
                           (unsigned)(ran - shown), (unsigned)ran);
            g_winHeld = 1;
            g_winCount = 0;
            g_winNext = 0;
        } else {
            g_winHeld = 0;
        }
    }
    g_winLastFrame = g_m.frame_no;
    g_winLastPresents = g_m.presents;

    g_win[g_winNext].frame = g_m.frame_no;
    g_win[g_winNext].priv  = g_m.private_bytes;
    g_winNext = (g_winNext + 1) % WINDOW;
    if (g_winCount < WINDOW) g_winCount++;
}

static double slope_per_frame(uint64_t *spanFrames)
{
    unsigned i, half, oldest;
    uint64_t minOld = 0, minNew = 0, fOld = 0, fNew = 0;
    int      haveOld = 0, haveNew = 0;

    if (spanFrames) *spanFrames = 0;
    if (g_winCount < 4) return 0.0;

    half   = g_winCount / 2;
    oldest = (g_winNext + WINDOW - g_winCount) % WINDOW;

    for (i = 0; i < g_winCount; i++) {
        const Sample *sm = &g_win[(oldest + i) % WINDOW];
        if (i < half) {
            if (!haveOld || sm->priv < minOld) { minOld = sm->priv; fOld = sm->frame; haveOld = 1; }
        } else {
            if (!haveNew || sm->priv < minNew) { minNew = sm->priv; fNew = sm->frame; haveNew = 1; }
        }
    }
    if (!haveOld || !haveNew || fNew <= fOld) return 0.0;
    if (spanFrames) *spanFrames = fNew - fOld;
    return ((double)(int64_t)(minNew - minOld)) / (double)(fNew - fOld);
}

static double slope_raw(void);
static int floor_rising(double sl)
{
    return sl > 1024.0 && slope_raw() > 512.0 && g_m.presents > 1800 && g_winCount >= WINDOW;
}

static double slope_raw(void)
{
    unsigned oldest, newest;
    uint64_t df;
    if (g_winCount < 2) return 0.0;
    oldest = (g_winNext + WINDOW - g_winCount) % WINDOW;
    newest = (g_winNext + WINDOW - 1) % WINDOW;
    df = g_win[newest].frame - g_win[oldest].frame;
    if (!df) return 0.0;
    return ((double)(int64_t)(g_win[newest].priv - g_win[oldest].priv)) / (double)df;
}

void u9metrics_init(void)
{
    memset(&g_m, 0, sizeof g_m);
    g_memorySampleQpc = 0;
    g_enabled      = u9cfg_int("metrics", 1);
    g_overlay      = u9cfg_int("metrics_overlay", 0);
    if (g_overlay < PAGE_OFF || g_overlay >= PAGE__N) g_overlay = PAGE_GENERAL;
    g_sampleEvery  = (unsigned)u9cfg_int("metrics_sample_frames", 60);
    g_summaryEvery = (unsigned)u9cfg_int("metrics_summary_frames", 300);
    if (g_sampleEvery  < 1) g_sampleEvery  = 1;
    if (g_summaryEvery < 1) g_summaryEvery = 1;
    g_lastTick = u9time_now();
    if (g_enabled)
        u9log_line("METRICS on: memory sampled every %u frames, summary every"
                   " %u, overlay %s (Ctrl+F12 cycles off/general/objects/textures)",
                   g_sampleEvery, g_summaryEvery, g_pageName[g_overlay]);
}

void u9metrics_engine_frame(void) { g_m.frame_no++; }

void u9metrics_present(int ok)
{
    if (ok) g_m.presents++;
    else    g_m.present_failed++;
}

void u9metrics_draw(unsigned vertices)
{
    g_m.draw_calls++;
    g_m.vertices += vertices;
}

void u9metrics_tex_created(unsigned bytes)
{
    g_m.textures_alive++;
    g_m.textures_created++;
    g_m.texture_bytes += bytes;
}

void u9metrics_tex_destroyed(unsigned bytes)
{
    if (g_m.textures_alive) g_m.textures_alive--;
    g_m.textures_destroyed++;
    g_m.texture_bytes = (g_m.texture_bytes > bytes) ? g_m.texture_bytes - bytes : 0;
}

void u9metrics_buffer_created(unsigned bytes)
{
    g_m.buffers_alive++;
    g_m.buffer_bytes += bytes;
}

void u9metrics_buffer_destroyed(unsigned bytes)
{
    if (g_m.buffers_alive) g_m.buffers_alive--;
    g_m.buffer_bytes = (g_m.buffer_bytes > bytes) ? g_m.buffer_bytes - bytes : 0;
}

const U9GLMetrics *u9metrics_current(void) { return &g_m; }

int u9metrics_telemetry_state(uint64_t *memory_qpc)
{
    *memory_qpc = g_memorySampleQpc;
    return g_enabled;
}

void u9metrics_figures(U9MetricsFigures *out)
{
    out->tick_qpc = g_tickQpc;
    out->fps = g_fps;
    out->tex_cached = g_tickCached; out->tex_pending = g_tickPending;
    out->tex_uploads = g_tickUploads; out->tex_dupes = g_tickDupes;
    out->tex_evictions = g_tickEvictions;
    out->tex_bytes = g_tickTexBytes;
    out->tex_budget_bytes = u9gl_tex_budget_bytes();
    out->tex_uploads_per_sec = g_texUploadsPerSec;
    out->tex_evictions_per_sec = g_texEvictPerSec;
    out->upload_ms_max = g_uploadMsMax;
    out->ov_enabled = u9tex_enabled();
    out->ov_files = g_tickOvFiles; out->ov_used = g_tickOvUsed; out->ov_rejected = g_tickOvRejected;
    out->ov_load_ms_max = g_ovLoadMsMax;
    out->floor_slope = slope_per_frame(&out->slope_span_frames);
    out->raw_slope = slope_raw();
    out->window_samples = g_winCount;
    out->window_capacity = WINDOW;
    out->floor_rising = floor_rising(out->floor_slope);
}

static void sample_heavy(void);

void u9metrics_frame_end(void)
{
    if (!g_enabled) return;
    sample_heavy();

    if ((g_m.frame_no % g_sampleEvery) == 0) sample_memory();

    {
        U9TexStats ts;
        double up = u9gl_upload_take_ms();
        if (up > g_uploadMsMaxRun) g_uploadMsMaxRun = up;
        u9tex_take_stats(&ts);
        if (ts.loadMsMax > g_ovLoadMsMaxRun) g_ovLoadMsMaxRun = ts.loadMsMax;
        g_ovFiles = ts.files; g_ovUsed = ts.used; g_ovRejected = ts.rejected;
    }

    {
        DWORD now = u9time_now();
        DWORD dt  = now - g_lastTick;
        if (dt >= 1000u) {
            unsigned cc, up, du, pe, mb, ev;
            g_fps = (double)(g_m.frame_no - g_lastFrameNo) * 1000.0 / (double)dt;
            g_lastTick    = now;
            g_lastFrameNo = g_m.frame_no;

            u9gl_tex_stats(&cc, &up, &du, &pe, &mb, &ev);
            g_texUploadsPerSec = (unsigned)((up - g_texUploadsPrev) * 1000u / dt);
            g_texEvictPerSec   = (unsigned)((ev - g_texEvictPrev) * 1000u / dt);
            g_texUploadsPrev = up;
            g_texEvictPrev   = ev;
            g_uploadMsMax = g_uploadMsMaxRun; g_uploadMsMaxRun = 0.0;
            g_ovLoadMsMax = g_ovLoadMsMaxRun; g_ovLoadMsMaxRun = 0.0;
            g_tickCached = cc; g_tickPending = pe; g_tickUploads = up;
            g_tickDupes = du; g_tickEvictions = ev;
            g_tickTexBytes = u9gl_tex_cache_bytes();
            g_tickOvFiles = g_ovFiles; g_tickOvUsed = g_ovUsed; g_tickOvRejected = g_ovRejected;
            g_tickQpc = u9perf_now();
        }
    }

    if ((g_m.frame_no % g_summaryEvery) == 0) {
        uint64_t span = 0;
        double   sl   = slope_per_frame(&span);
        u9log_line("METRICS f=%u present=%u(+%u failed) fps=%.1f "
                   "draws=%u verts=%u | tex %u alive / %u MB (%u created, %u freed) "
                   "| priv %u MB ws %u MB peak %u MB | slope %+.2f KB/frame over %u frames",
                   (unsigned)g_m.frame_no, (unsigned)g_m.presents,
                   (unsigned)g_m.present_failed, g_fps,
                   (unsigned)g_m.draw_calls_last, (unsigned)g_m.vertices_last,
                   (unsigned)g_m.textures_alive,
                   (unsigned)(g_m.texture_bytes / (1024u * 1024u)),
                   (unsigned)g_m.textures_created, (unsigned)g_m.textures_destroyed,
                   (unsigned)(g_m.private_bytes / (1024u * 1024u)),
                   (unsigned)(g_m.working_set / (1024u * 1024u)),
                   (unsigned)(g_m.peak_working_set / (1024u * 1024u)),
                   sl / 1024.0, (unsigned)span);
        u9log_line("METRICS   overlay drew %u times (%s)",
                   (unsigned)g_overlayDraws, g_pageName[g_overlay]);
        if (floor_rising(sl))
            u9log_line("METRICS   floor is RISING %+.2f KB/frame - that is a leak"
                       " shape, not oscillation (raw first-to-last reads %+.2f)",
                       sl / 1024.0, slope_raw() / 1024.0);
    }

    g_m.draw_calls_last = g_m.draw_calls;
    g_m.vertices_last   = g_m.vertices;
    g_m.draw_calls = 0;
    g_m.vertices   = 0;
}

static int      g_fontBase = -1;
static int      g_charW, g_charH;
static int      g_fontTried;
static GLint    g_savedScissor[4];
static int      g_hadScissor;

static void font_init(void)
{
    HDC        dc;
    HFONT      font, old;
    TEXTMETRICA tm;

    if (g_fontTried) return;
    g_fontTried = 1;

    dc = wglGetCurrentDC();
    if (!dc) return;

    font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       FIXED_PITCH | FF_MODERN, "Consolas");
    if (!font) return;

    old = (HFONT)SelectObject(dc, font);
    g_fontBase = (int)glGenLists(96);
    if (g_fontBase > 0 && wglUseFontBitmapsA(dc, 32, 96, (DWORD)g_fontBase)) {
        GetTextMetricsA(dc, &tm);
        g_charW = tm.tmAveCharWidth;
        g_charH = tm.tmHeight;
    } else {
        if (g_fontBase > 0) glDeleteLists((GLuint)g_fontBase, 96);
        g_fontBase = -1;
        u9log_line("METRICS overlay: wglUseFontBitmaps failed, no text");
    }
    SelectObject(dc, old);
    DeleteObject(font);
}

void u9metrics_text(float x, float y, const char *s)
{
    if (g_fontBase < 0) return;
    glRasterPos2f(x, y);
    glListBase((GLuint)(g_fontBase - 32));
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

int u9metrics_text_ready(void) { return g_fontBase >= 0; }

int u9metrics_text_init(void) { font_init(); return g_fontBase >= 0; }
void u9metrics_char_size(int *w, int *h) { *w = g_charW ? g_charW : 8; *h = g_charH ? g_charH : 15; }

static void draw_text(float x, float y, const char *s)
{
    if (g_fontBase < 0) return;
    glRasterPos2f(x, y);
    glListBase((GLuint)(g_fontBase - 32));
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

void u9metrics_toggle_poll(void)
{
    if (u9keys_pressed(VK_F12)) {
        g_overlay = (g_overlay + 1) % PAGE__N;
        u9log_line("METRICS overlay %s", g_pageName[g_overlay]);
    }
}

typedef char Line[64];

static void page_title(Line *line, int *n)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d/%d", g_overlay, PAGE__N - 1);
    snprintf(line[(*n)++], sizeof line[0], "^F12 %-11s%6s", g_pageName[g_overlay], buf);
}

static void page_general(Line *line, int *n, double sl)
{
    page_title(line, n);
    snprintf(line[(*n)++], sizeof line[0], "FPS          %8.1f", g_fps);
    snprintf(line[(*n)++], sizeof line[0], "U9 frame     %8u", (unsigned)g_m.frame_no);
    snprintf(line[(*n)++], sizeof line[0], "GL presents  %8u", (unsigned)g_m.presents);
    if (g_m.present_failed)
        snprintf(line[(*n)++], sizeof line[0], "SWAP FAILED  %8u", (unsigned)g_m.present_failed);
    if (g_m.frame_no > g_m.presents)
        snprintf(line[(*n)++], sizeof line[0], "  not shown  %8u",
                 (unsigned)(g_m.frame_no - g_m.presents));
    snprintf(line[(*n)++], sizeof line[0], "Draws/frame  %8u", (unsigned)g_m.draw_calls_last);
    snprintf(line[(*n)++], sizeof line[0], "Verts/frame  %8u", (unsigned)g_m.vertices_last);
    snprintf(line[(*n)++], sizeof line[0], " ");
    snprintf(line[(*n)++], sizeof line[0], "Private MB   %8u",
             (unsigned)(g_m.private_bytes / (1024u * 1024u)));
    snprintf(line[(*n)++], sizeof line[0], "Working MB   %8u",
             (unsigned)(g_m.working_set / (1024u * 1024u)));
    snprintf(line[(*n)++], sizeof line[0], "floor KB/f   %+8.2f", sl / 1024.0);
    snprintf(line[(*n)++], sizeof line[0], " ");
    snprintf(line[(*n)++], sizeof line[0], "GL tex MB    %8u",
             (unsigned)(g_m.texture_bytes / (1024u * 1024u)));
    snprintf(line[(*n)++], sizeof line[0], "GL tex alive %8u", (unsigned)g_m.textures_alive);
    {
        const char *u9gl_mip_filter_name(void);
        float u9gl_anisotropy(void);
        snprintf(line[(*n)++], sizeof line[0], "^F4 mip    %10s", u9gl_mip_filter_name());
        snprintf(line[(*n)++], sizeof line[0], "anisotropy %10.0fx", u9gl_anisotropy());
        {
            const char *u9gl_depth_mode_name(void);
            snprintf(line[(*n)++], sizeof line[0], "^F5 depth  %10s", u9gl_depth_mode_name());
        }
    }
    if (g_m.buffers_alive)
        snprintf(line[(*n)++], sizeof line[0], "GL buf MB    %8u",
                 (unsigned)(g_m.buffer_bytes / (1024u * 1024u)));
    {
        int u9avatar_position(int *, int *, int *);
        int u9console_current_map(void);
        int ax, ay, az, map = u9console_current_map();
        snprintf(line[(*n)++], sizeof line[0], " ");
        if (map >= 0) {
            snprintf(line[(*n)++], sizeof line[0], "Map#: %d", map);
            snprintf(line[(*n)++], sizeof line[0], "MapN: %.40s",
                     u9regions_exists(map) ? u9regions_name(map) : "?");
        }
        if (u9avatar_position(&ax, &ay, &az)) {
            snprintf(line[(*n)++], sizeof line[0], "Avatar X   %10d", ax);
            snprintf(line[(*n)++], sizeof line[0], "Avatar Y   %10d", ay);
            snprintf(line[(*n)++], sizeof line[0], "Avatar Z   %10d", az);
        } else
            snprintf(line[(*n)++], sizeof line[0], "Avatar     %10s", "n/a");
    }
}

static void row(Line *line, int *n, const char *label, const char *value)
{
    snprintf(line[(*n)++], sizeof line[0], "%-11s%11s", label, value);
}

static void rowu(Line *line, int *n, const char *label, unsigned v)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%u", v);
    row(line, n, label, buf);
}

static void page_objects(Line *line, int *n)
{
    const U9ObjStats *s;
    char buf[24];

    s = u9objstats_get();

    page_title(line, n);
    if (!s->valid) {
        row(line, n, "Fast area", "n/a");
        return;
    }
    rowu(line, n, "Objects", s->objects);
    snprintf(buf, sizeof buf, "%u/%u", s->worstCell, U9_AREA_SLOTS_USABLE_PUB);
    row(line, n, " worst cell", buf);
    rowu(line, n, " drawn", s->drawn);
    rowu(line, n, " moving", s->moving);
    snprintf(buf, sizeof buf, "%u/%u", s->fixed, s->nonfixed);
    row(line, n, " fixed/move", buf);
    rowu(line, n, " types", s->uniqueTypes);
    snprintf(buf, sizeof buf, "%u x%u", s->topType[0], s->topCount[0]);
    row(line, n, " top type", buf);
    rowu(line, n, " cam ctr ?", s->inCamera);
    row(line, n, " ", "");
    rowu(line, n, "Cells live", s->cellsActive);
    snprintf(buf, sizeof buf, "%u/%u", s->cellsDrawn, s->cellsCollide);
    row(line, n, " draw/coll", buf);
    rowu(line, n, " with slots", s->cellsWithSlots);
    rowu(line, n, " slots KB", s->slotBytes / 1024u);
    snprintf(buf, sizeof buf, "%u/%u/%u", s->queue[0], s->queue[1], s->queue[2]);
    row(line, n, " loading", buf);
    snprintf(buf, sizeof buf, "%u/%u", s->terrainMeshes, s->terrainMeshMax);
    row(line, n, "Terrain", buf);
    row(line, n, " ", "");
    rowu(line, n, "Mesh types", s->meshTypes);
    snprintf(buf, sizeof buf, "%u/%u", s->meshInUse, s->meshIdle);
    row(line, n, " used/idle", buf);
    rowu(line, n, " handles", s->meshUsers);
    snprintf(buf, sizeof buf, "%.1f", s->meshInUse ? (double)s->meshUsers / s->meshInUse : 0.0);
    row(line, n, " per mesh", buf);
    snprintf(buf, sizeof buf, "%.1f/%.1f", s->meshBytesPool / 1048576.0, s->poolSize / 1048576.0);
    row(line, n, "Mesh MB", buf);
    snprintf(buf, sizeof buf, "%.1f", s->meshBytesOutside / 1048576.0);
    row(line, n, " outside MB", buf);
    rowu(line, n, " largest KB", s->poolLargestFree / 1024u);
    row(line, n, " ", "");
    snprintf(buf, sizeof buf, "%.1f/%.0f", s->nonfixedUsed / 1048576.0, s->nonfixedMax / 1048576.0);
    row(line, n, "Items MB", buf);
    snprintf(buf, sizeof buf, "%.1f/%.0f", s->fixedUsed / 1048576.0, s->fixedMax / 1048576.0);
    row(line, n, " fixed MB", buf);
    snprintf(buf, sizeof buf, "%.2f", s->sampleMs);
    row(line, n, "sample ms", buf);
}

static uint64_t g_addrFreeBytes, g_addrLargestBytes;
static double   g_addrWalkMs;
static CRITICAL_SECTION g_addrLock;

static void sample_address_space(void)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *p;
    SIZE_T freeTotal = 0, largest = 0;
    LARGE_INTEGER f, t0, t1;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    GetSystemInfo(&si);
    p = (unsigned char *)si.lpMinimumApplicationAddress;
    while (p < (unsigned char *)si.lpMaximumApplicationAddress) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || !mbi.RegionSize) break;
        if (mbi.State == MEM_FREE) {
            freeTotal += mbi.RegionSize;
            if (mbi.RegionSize > largest) largest = mbi.RegionSize;
        }
        p += mbi.RegionSize;
    }
    QueryPerformanceCounter(&t1);
    EnterCriticalSection(&g_addrLock);
    g_addrFreeBytes    = freeTotal;
    g_addrLargestBytes = largest;
    g_addrWalkMs    = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    LeaveCriticalSection(&g_addrLock);
}

#define COBJMACROS
#include <dxgi1_4.h>
static const GUID g_iidFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const GUID g_iidAdapter3 = { 0x645967a4, 0x1392, 0x4310, { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };
static HMODULE        g_dxgi;
static IDXGIAdapter3 *g_adapter3;
static int            g_vramTried, g_vramKind;
static uint64_t       g_vramDedicated, g_vramLocalUse, g_vramLocalBudget, g_vramSharedUse, g_vramSharedBudget;
static double         g_vramQueryMs;
static char           g_vramAdapter[128];

static void vram_open(void)
{
    typedef HRESULT (WINAPI *CreateFactory1Fn)(REFIID, void **);
    CreateFactory1Fn create;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    DXGI_ADAPTER_DESC1 desc;
    g_vramTried = 1;
    g_dxgi = LoadLibraryA("dxgi.dll");
    create = g_dxgi ? (CreateFactory1Fn)(void *)GetProcAddress(g_dxgi, "CreateDXGIFactory1") : NULL;
    if (!create || FAILED(create(&g_iidFactory1, (void **)&factory))) return;
    if (SUCCEEDED(IDXGIFactory1_EnumAdapters1(factory, 0, &adapter))) {
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc))) {
            g_vramDedicated = desc.DedicatedVideoMemory;
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, g_vramAdapter, sizeof g_vramAdapter, NULL, NULL);
        }
        if (FAILED(IDXGIAdapter1_QueryInterface(adapter, &g_iidAdapter3, (void **)&g_adapter3))) g_adapter3 = NULL;
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
}

static void sample_vram(void)
{
    DXGI_QUERY_VIDEO_MEMORY_INFO local, shared;
    LARGE_INTEGER f, t0, t1;
    if (!g_vramTried) vram_open();
    if (!g_adapter3) return;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (FAILED(IDXGIAdapter3_QueryVideoMemoryInfo(g_adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) ||
        FAILED(IDXGIAdapter3_QueryVideoMemoryInfo(g_adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared))) return;
    QueryPerformanceCounter(&t1);
    EnterCriticalSection(&g_addrLock);
    g_vramKind = 1;
    g_vramLocalUse = local.CurrentUsage; g_vramLocalBudget = local.Budget;
    g_vramSharedUse = shared.CurrentUsage; g_vramSharedBudget = shared.Budget;
    g_vramQueryMs = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    LeaveCriticalSection(&g_addrLock);
}

typedef struct { uint64_t qpc, count, ticks, maxTicks; } HeavyClock;
static HeavyClock g_areaClock, g_addrClock, g_vramClock, g_ovClock, g_texListClock;
#include "gltexlist.h"
static U9TexListItem g_texList[U9GL_TEXLIST_MAX];
static unsigned      g_texListCount, g_texListEntries, g_texListFrame;
static unsigned   g_ovResCount;
static uint64_t   g_ovResBytes;

#define AREA_REQUEST_SECONDS 5u

static int heavy_due(const HeavyClock *c, int page, unsigned group, unsigned seconds)
{
    if (g_overlay == page && (g_m.frame_no % 30u == 0 || !c->qpc)) return 1;
    if (!(u9telemetry_requested_groups() & U9TM_GROUP_BIT(group))) return 0;
    return !c->qpc || u9perf_now() - c->qpc >= u9perf_frequency() * seconds;
}

static HANDLE        g_addrThread, g_addrStopEvent;
static volatile LONG g_addrWanted, g_vramWanted;
static int           g_addrTried, g_addrLogged, g_vramLogged;

static void clock_done(HeavyClock *c, uint64_t began)
{
    uint64_t now = u9perf_now();
    EnterCriticalSection(&g_addrLock);
    c->qpc = now;
    c->count++;
    c->ticks += now - began;
    if (now - began > c->maxTicks) c->maxTicks = now - began;
    LeaveCriticalSection(&g_addrLock);
}

static DWORD WINAPI addr_thread(LPVOID unused)
{
    (void)unused;
    while (WaitForSingleObject(g_addrStopEvent, 1000) == WAIT_TIMEOUT) {
        uint64_t t;
        if (InterlockedExchange(&g_addrWanted, 0)) { t = u9perf_now(); sample_address_space(); clock_done(&g_addrClock, t); }
        if (InterlockedExchange(&g_vramWanted, 0)) { t = u9perf_now(); sample_vram(); clock_done(&g_vramClock, t); }
    }
    return 0;
}

static void addr_stop(void)
{
    SetEvent(g_addrStopEvent);
    if (g_addrThread && WaitForSingleObject(g_addrThread, 200) == WAIT_OBJECT_0) {
        CloseHandle(g_addrThread);
        CloseHandle(g_addrStopEvent);
    }
    g_addrThread = NULL;
}

static int addr_start(void)
{
    if (g_addrTried) return g_addrThread != NULL;
    g_addrTried = 1;
    InitializeCriticalSection(&g_addrLock);
    g_addrStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_addrThread = g_addrStopEvent ? CreateThread(NULL, 0, addr_thread, NULL, 0, NULL) : NULL;
    if (g_addrThread) {
        u9life_push("address-space worker", addr_stop);
        u9log_line("METRICS address space and video memory read on their own thread, once a second while wanted");
    } else {
        u9log_line("METRICS system worker not started (err %lu) - address space walked on the game thread,"
                   " video memory not reported",
                   GetLastError());
    }
    return g_addrThread != NULL;
}

static void heavy_done(HeavyClock *c, uint64_t began)
{
    uint64_t t = u9perf_now();
    c->qpc = t;
    c->count++;
    c->ticks += t - began;
    if (t - began > c->maxTicks) c->maxTicks = t - began;
}

static void sample_heavy(void)
{
    uint64_t t;
    int addrWanted = g_overlay == PAGE_TEXTURES ||
                     (u9telemetry_requested_groups() & U9TM_GROUP_BIT(U9TM_GROUP_ADDRESS_SPACE));
    if (addrWanted) {
        if (addr_start()) InterlockedExchange(&g_addrWanted, 1);
        else if (heavy_due(&g_addrClock, PAGE_TEXTURES, U9TM_GROUP_ADDRESS_SPACE, 1)) {
            t = u9perf_now(); sample_address_space(); heavy_done(&g_addrClock, t);
        }
    }
    if ((g_overlay == PAGE_TEXTURES || (u9telemetry_requested_groups() & U9TM_GROUP_BIT(U9TM_GROUP_VRAM))) &&
        addr_start()) InterlockedExchange(&g_vramWanted, 1);
    if (!g_vramLogged && g_vramClock.count) {
        U9MetricsHeavy h;
        g_vramLogged = 1;
        u9metrics_heavy(&h);
        u9log_line("METRICS video memory from %s%s%s: card %u MB, the game uses %u of a %u MB budget",
                   h.vram_kind ? "DXGI 1.4, adapter 0 - " : "nothing - DXGI 1.4 not available",
                   h.vram_kind ? g_vramAdapter : "", h.vram_kind ? "" : "",
                   (unsigned)(h.vram_dedicated_bytes >> 20), (unsigned)(h.vram_local_use_bytes >> 20),
                   (unsigned)(h.vram_local_budget_bytes >> 20));
    }
    if (!g_addrLogged && g_addrClock.count) {
        U9MetricsHeavy h;
        g_addrLogged = 1;
        u9metrics_heavy(&h);
        u9log_line("METRICS address space walk took %.2f ms (free %u MB, largest block %u MB)",
                   h.addr_walk_ms, (unsigned)(h.addr_free_bytes >> 20), (unsigned)(h.addr_largest_bytes >> 20));
    }
    if (heavy_due(&g_areaClock, PAGE_OBJECTS, U9TM_GROUP_AREA, AREA_REQUEST_SECONDS)) {
        t = u9perf_now(); u9objstats_sample(); heavy_done(&g_areaClock, t);
    } else if (heavy_due(&g_texListClock, -1, U9TM_GROUP_TEXTURE_LIST, 5)) {
        t = u9perf_now();
        g_texListCount = u9gl_tex_recent(g_texList, U9GL_TEXLIST_MAX, &g_texListEntries, &g_texListFrame);
        heavy_done(&g_texListClock, t);
    } else if (u9tex_enabled() && heavy_due(&g_ovClock, PAGE_TEXTURES, U9TM_GROUP_OVERRIDE_RESIDENT, 1)) {
        t = u9perf_now(); g_ovResBytes = u9gl_override_bytes(&g_ovResCount); heavy_done(&g_ovClock, t);
    }
}

static void heavy_cost(const HeavyClock *c, U9MetricsSamplerCost *out)
{
    double us = 1000000.0 / (double)u9perf_frequency();
    out->count = c->count;
    out->mean_us = c->count ? (double)c->ticks * us / (double)c->count : 0.0;
    out->max_us = (double)c->maxTicks * us;
}

void u9metrics_heavy(U9MetricsHeavy *out)
{
    out->area_qpc = g_areaClock.qpc;
    if (g_addrTried) EnterCriticalSection(&g_addrLock);
    out->addr_qpc = g_addrClock.qpc;
    out->addr_free_bytes = g_addrFreeBytes; out->addr_largest_bytes = g_addrLargestBytes;
    out->addr_walk_ms = g_addrWalkMs;
    heavy_cost(&g_addrClock, &out->addr_cost);
    out->vram_qpc = g_vramClock.qpc;
    out->vram_kind = g_vramKind;
    out->vram_dedicated_bytes = g_vramDedicated;
    out->vram_local_use_bytes = g_vramLocalUse; out->vram_local_budget_bytes = g_vramLocalBudget;
    out->vram_shared_use_bytes = g_vramSharedUse; out->vram_shared_budget_bytes = g_vramSharedBudget;
    out->vram_query_ms = g_vramQueryMs;
    heavy_cost(&g_vramClock, &out->vram_cost);
    if (g_addrTried) LeaveCriticalSection(&g_addrLock);

    out->ov_res_qpc = g_ovClock.qpc;
    out->ov_res_count = g_ovResCount; out->ov_res_bytes = g_ovResBytes;
    heavy_cost(&g_areaClock, &out->area_cost);
    heavy_cost(&g_ovClock, &out->ov_cost);
    out->tex_list_qpc = g_texListClock.qpc;
    out->tex_list = g_texList;
    out->tex_list_count = g_texListCount;
    out->tex_list_entries = g_texListEntries;
    out->tex_list_frame = g_texListFrame;
    heavy_cost(&g_texListClock, &out->tex_list_cost);
}

static void page_textures(Line *line, int *n)
{
    unsigned cc, up, du, pe, mb, ev, ovCount = 0, ovBytes;
    char buf[24];

    u9gl_tex_stats(&cc, &up, &du, &pe, &mb, &ev);

    page_title(line, n);
    snprintf(line[(*n)++], sizeof line[0], "FPS          %8.1f", g_fps);
    snprintf(line[(*n)++], sizeof line[0], " ");
    snprintf(buf, sizeof buf, "%u/%u", mb, u9gl_tex_budget_mb());
    snprintf(line[(*n)++], sizeof line[0], "Tex budget MB%9s", buf);
    snprintf(line[(*n)++], sizeof line[0], "Tex cached   %8u", cc);
    snprintf(line[(*n)++], sizeof line[0], "Uploads/s    %8u", g_texUploadsPerSec);
    snprintf(line[(*n)++], sizeof line[0], "Evictions/s  %8u", g_texEvictPerSec);
    snprintf(line[(*n)++], sizeof line[0], "Upload ms max%9.1f", g_uploadMsMax);
    snprintf(line[(*n)++], sizeof line[0], " ");
    snprintf(line[(*n)++], sizeof line[0], "Private MB   %8u",
             (unsigned)(g_m.private_bytes / (1024u * 1024u)));
    {
        U9MetricsHeavy h;
        u9metrics_heavy(&h);
        snprintf(line[(*n)++], sizeof line[0], "Addr free MB %8u", (unsigned)(h.addr_free_bytes >> 20));
        snprintf(line[(*n)++], sizeof line[0], "Largest blk  %8u", (unsigned)(h.addr_largest_bytes >> 20));
    }
    snprintf(line[(*n)++], sizeof line[0], " ");
    {
        U9MetricsHeavy h;
        u9metrics_heavy(&h);
        if (h.vram_kind) {
            snprintf(buf, sizeof buf, "%u/%u", (unsigned)(h.vram_local_use_bytes >> 20), (unsigned)(h.vram_local_budget_bytes >> 20));
            snprintf(line[(*n)++], sizeof line[0], "VRAM MB    %11s", buf);
            snprintf(line[(*n)++], sizeof line[0], "VRAM card MB %8u", (unsigned)(h.vram_dedicated_bytes >> 20));
        } else {
            snprintf(line[(*n)++], sizeof line[0], "VRAM        %10s", "n/a");
        }
    }
    snprintf(line[(*n)++], sizeof line[0], " ");
    if (!u9tex_enabled()) {
        snprintf(line[(*n)++], sizeof line[0], "Override    %10s", "off");
    } else {
        ovBytes = (unsigned)g_ovResBytes; ovCount = g_ovResCount;
        snprintf(line[(*n)++], sizeof line[0], "Override    %10s", "on");
        snprintf(line[(*n)++], sizeof line[0], "  files      %8u", g_ovFiles);
        snprintf(buf, sizeof buf, "%u/%u", g_ovUsed, g_ovRejected);
        snprintf(line[(*n)++], sizeof line[0], "  used/rej  %10s", buf);
        snprintf(buf, sizeof buf, "%u (%u MB)", ovCount, ovBytes / (1024u * 1024u));
        snprintf(line[(*n)++], sizeof line[0], "  held    %12s", buf);
        snprintf(line[(*n)++], sizeof line[0], "  load ms max%9.1f", g_ovLoadMsMax);
    }
}

void u9metrics_overlay(int surfW, int surfH)
{
    Line     line[32];
    int      n = 0, i, w, h, x, y;
    uint64_t span = 0;
    double   sl;

    if (!g_enabled || !g_overlay || surfW <= 0 || surfH <= 0) return;
    font_init();


    sl = slope_per_frame(&span);

    {
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_SCISSOR_BOX, g_savedScissor);
        g_hadScissor = (sc != GL_FALSE);
        if (g_hadScissor) glDisable(GL_SCISSOR_TEST);
    }

    if (g_overlay == PAGE_TEXTURES)     page_textures(line, &n);
    else if (g_overlay == PAGE_OBJECTS) page_objects(line, &n);
    else                                page_general(line, &n, sl);

    w = (g_charW ? g_charW : 8) * 22 + 16;
    h = (g_charH ? g_charH : 15) * n + 12;

    if (g_savedScissor[2] > 0 && g_savedScissor[3] > 0 &&
        g_savedScissor[2] <= surfW && g_savedScissor[3] <= surfH) {
        x = g_savedScissor[0] + g_savedScissor[2] - w - 8;
        y = surfH - (g_savedScissor[1] + g_savedScissor[3]) + 8;
    } else {
        x = surfW - w - 8;
        y = 8;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    {
        static int said;
        if (!said) {
            said = 1;
            u9log_line("METRICS overlay drawing: surf %dx%d, fontBase %d, "
                       "char %dx%d, panel at %d,%d %dx%d, scissor %d,%d %dx%d, "
                       "ctx %s, err 0x%04X",
                       surfW, surfH, g_fontBase, g_charW, g_charH,
                       x, y, w, h, g_savedScissor[0], g_savedScissor[1],
                       g_savedScissor[2], g_savedScissor[3],
                       wglGetCurrentContext() ? "current" : "NOT CURRENT",
                       (unsigned)glGetError());
        }
    }

    glViewport(0, 0, surfW, surfH);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)surfW, (double)surfH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.05f, 0.07f, 0.12f, 0.82f);
    glBegin(GL_QUADS);
    glVertex2f((float)x,       (float)y);
    glVertex2f((float)(x + w), (float)y);
    glVertex2f((float)(x + w), (float)(y + h));
    glVertex2f((float)x,       (float)(y + h));
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(0.25f, 0.85f, 0.45f);
    glBegin(GL_LINE_LOOP);
    glVertex2f((float)x + 0.5f,       (float)y + 0.5f);
    glVertex2f((float)(x + w) - 0.5f, (float)y + 0.5f);
    glVertex2f((float)(x + w) - 0.5f, (float)(y + h) - 0.5f);
    glVertex2f((float)x + 0.5f,       (float)(y + h) - 0.5f);
    glEnd();
    if (sl > 102.4) glColor3f(1.0f, 0.75f, 0.1f);
    else            glColor3f(0.85f, 0.95f, 0.85f);

    for (i = 0; i < n; i++)
        draw_text((float)(x + 8),
                  (float)(y + 10 + i * (g_charH ? g_charH : 15)), line[i]);

    g_overlayDraws++;

    {
        static int probed;
        if (!probed && g_overlayDraws > 3) {
            unsigned char rgb[3] = { 0, 0, 0 };
            unsigned char out[3] = { 0, 0, 0 };
            probed = 1;
            glReadBuffer(GL_BACK);
            glReadPixels(x + w / 2, surfH - (y + h / 2), 1, 1,
                         GL_RGB, GL_UNSIGNED_BYTE, rgb);
            glReadPixels(x - 60, surfH - (y + h / 2), 1, 1,
                         GL_RGB, GL_UNSIGNED_BYTE, out);
            u9log_line("METRICS probe: inside panel (%u,%u,%u), outside it "
                       "(%u,%u,%u), err 0x%04X - panel colour is about "
                       "(13,18,31) over black",
                       rgb[0], rgb[1], rgb[2], out[0], out[1], out[2],
                       (unsigned)glGetError());
        }
    }

    glColor3f(1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    if (g_hadScissor) {
        glScissor(g_savedScissor[0], g_savedScissor[1],
                  g_savedScissor[2], g_savedScissor[3]);
        glEnable(GL_SCISSOR_TEST);
    }
}
