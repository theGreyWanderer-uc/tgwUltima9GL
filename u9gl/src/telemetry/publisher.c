#include <inttypes.h>
#include <string.h>
#include "transport.h"
#include "metrics_snapshot.h"
#include "publisher.h"
#include "../common/perfclock.h"
#include "../common/lifecycle.h"
#include "../injection/config.h"
#include "../diag/log.h"
#include "u9gl_version.h"
#include "../gl/gpuprof.h"
#include "../fix/playerstats.h"

int   u9gl_mip_filter_mode(void);
float u9gl_anisotropy_cached(void);
int   u9gl_depth_mode(void);
int   u9console_current_map(void);
int   u9avatar_position(int *x, int *y, int *z);

static U9TelemetryTransport g_transport;
static uint64_t g_next, g_frames, g_frameTicks, g_frameMax, g_copyTicks, g_copyMax, g_requestChanges;
static uint32_t g_requested;
#define WORLD_READ_HZ 2u
static uint64_t g_worldNext, g_worldQpc, g_worldReads, g_worldTicks, g_worldMax;
static int g_worldMap = -1, g_worldAvatar, g_worldX, g_worldY, g_worldZ;
static U9PlayerStats g_player;

#define U9TM_CELL_SLOTS_USABLE 2045u

static void heavy_line(const char *name, const U9MetricsSamplerCost *c)
{
    u9log_line("TELEMETRY sampler %-9s count=%" PRIu64 " us_mean=%.3f us_max=%.3f", name, c->count, c->mean_us, c->max_us);
}

static void telemetry_remove(void)
{
    U9MetricsHeavy heavy;
    u9metrics_heavy(&heavy);
    heavy_line("area", &heavy.area_cost);
    heavy_line("address", &heavy.addr_cost);
    heavy_line("vram", &heavy.vram_cost);
    heavy_line("overrides", &heavy.ov_cost);
    heavy_line("texlist", &heavy.tex_list_cost);
    u9gpu_report();
    double us = 1000000.0 / (double)g_transport.identity.qpc_frequency;
    u9log_line("TELEMETRY totals: frames=%" PRIu64 " samples=%" PRIu64
               " adapter_us_mean=%.3f adapter_us_max=%.3f publish_us_mean=%.3f publish_us_max=%.3f"
               " frame_us_mean=%.3f frame_us_max=%.3f world_reads=%" PRIu64 " world_us_mean=%.3f world_us_max=%.3f"
               " request_changes=%" PRIu64 " requests_last=0x%08x",
               g_frames, g_transport.published,
               g_transport.published ? g_copyTicks * us / g_transport.published : 0.0, g_copyMax * us,
               g_transport.published ? g_transport.publish_ticks * us / g_transport.published : 0.0,
               g_transport.publish_max_ticks * us,
               g_frames ? g_frameTicks * us / g_frames : 0.0, g_frameMax * us,
               g_worldReads, g_worldReads ? g_worldTicks * us / g_worldReads : 0.0, g_worldMax * us,
               g_requestChanges, (unsigned)g_requested);
    u9telemetry_transport_close(&g_transport);
}

void u9telemetry_install(void)
{
    if (!u9cfg_int("diag_telemetry", 0)) return;
    if (!u9telemetry_transport_open(&g_transport, U9TM_PRODUCER_GAME, U9GL_VERSION_FULL)) {
        u9log_line("TELEMETRY unavailable: mapping install failed, winerr=%lu", GetLastError());
        return;
    }
    u9life_push_at_exit("telemetry mapping", telemetry_remove);
    u9log_line("TELEMETRY ready: %s, v%u.%u, %u of %u bytes, at most %u Hz; copies existing metrics",
               g_transport.name, U9TM_VERSION_MAJOR, U9TM_VERSION_MINOR,
               (unsigned)sizeof(U9TelemetryGameRecord), U9TM_MAPPING_BYTES, U9TM_PUBLISH_HZ);
    if (!g_transport.request)
        u9log_line("TELEMETRY requests unavailable: request block not created; on-demand groups stay off");
}

static uint32_t g_combatSeen;
static int g_stunNow, g_stunSeen;
static uint64_t g_spellsSeen;

void u9telemetry_frame_end(void)
{
    U9TelemetryGameRecord sample;
    U9TelemetryInputs in;
    U9MetricsFigures figures;
    U9MetricsHeavy heavy;
    uint64_t began, elapsed, worldSpent = 0;
    uint32_t requested;
    if (!g_transport.view) return;
    began = u9perf_now();
    g_frames++;
    g_spellsSeen |= u9player_spells_scan();
    {
        int st, stun;
        if (u9player_combat_scan(&st, &stun)) {
            if (st >= 0 && st < 32) g_combatSeen |= 1u << st;
            if (stun > 0) g_stunSeen = 1;
            g_stunNow = stun;
        }
    }
    if (began >= g_next) {
        requested = u9telemetry_transport_requests(&g_transport, began);
        if (requested != g_requested) { g_requested = requested; g_requestChanges++; }
        if (began >= g_worldNext) {
            uint64_t t = u9perf_now();
            g_worldMap = u9console_current_map();
            g_worldAvatar = u9avatar_position(&g_worldX, &g_worldY, &g_worldZ);
            u9player_read(&g_player);
            u9player_purse(&g_player);
            g_player.activeSpells = g_spellsSeen;
            g_spellsSeen = 0;
            g_player.combatStates = g_combatSeen;
            g_player.stunMs = g_stunNow > 0 ? g_stunNow : g_stunSeen ? 1 : 0;
            g_combatSeen = 0; g_stunSeen = 0;
            g_worldQpc = t;
            worldSpent = u9perf_now() - t;
            g_worldReads++; g_worldTicks += worldSpent;
            if (worldSpent > g_worldMax) g_worldMax = worldSpent;
            g_worldNext = t + g_transport.identity.qpc_frequency / WORLD_READ_HZ;
        }
        memset(&in, 0, sizeof in);
        in.available = u9metrics_telemetry_state(&in.memory_qpc);
        u9metrics_figures(&figures);
        if (in.memory_qpc < g_transport.identity.session_start_qpc) in.memory_qpc = 0;
        if (figures.tick_qpc < g_transport.identity.session_start_qpc) figures.tick_qpc = 0;
        u9metrics_heavy(&heavy);
        if (heavy.area_qpc < g_transport.identity.session_start_qpc) heavy.area_qpc = 0;
        if (heavy.addr_qpc < g_transport.identity.session_start_qpc) heavy.addr_qpc = 0;
        if (heavy.vram_qpc < g_transport.identity.session_start_qpc) heavy.vram_qpc = 0;
        if (heavy.ov_res_qpc < g_transport.identity.session_start_qpc) heavy.ov_res_qpc = 0;
        if (heavy.tex_list_qpc < g_transport.identity.session_start_qpc) heavy.tex_list_qpc = 0;
        in.heavy = &heavy;
        in.area = u9objstats_get();
        in.cell_slots_usable = U9TM_CELL_SLOTS_USABLE;
        {
            static const char *const regions[] = { "scene", "beam", "rays", "bloom", "ao", "pool_shadows", "sun_shadows", "water", "composite" };
            const U9GpuState *g = u9gpu_state();
            unsigned r;
            in.gpu_support = g->supported < 0 ? 0 : g->supported ? 1 : 2;
            in.gpu_have = g->have;
            in.gpu_frame = g->latest.frame; in.gpu_issued_qpc = g->latest.issued_qpc;
            if (in.gpu_issued_qpc < g_transport.identity.session_start_qpc) in.gpu_have = 0;
            in.gpu_samples = g->samples; in.gpu_skipped = g->skipped;
            in.gpu_frame_ms = g->latest.frame_ms;
            for (r = 0; r < sizeof regions / sizeof regions[0]; r++) {
                int i = u9gpu_find(regions[r]);
                if (i < 0 || !(g->latest.region_mask & (1u << i))) continue;
                in.gpu_region_ms[r] = g->latest.region_ms[i];
                in.gpu_mask |= 1u << r;
            }
        }
        in.metrics = u9metrics_current();
        in.figures = &figures;
        in.now = began;
        in.mip_mode = u9gl_mip_filter_mode();
        in.depth_mode = u9gl_depth_mode();
        in.anisotropy = u9gl_anisotropy_cached();
        in.world_qpc = g_worldQpc;
        in.map = g_worldMap;
        in.avatar_valid = g_worldAvatar;
        in.avatar_x = g_worldX; in.avatar_y = g_worldY; in.avatar_z = g_worldZ;
        in.player = &g_player;
        u9telemetry_metrics_copy(&in, &sample);
        sample.header.requested_groups = requested;
        elapsed = u9perf_now() - began - worldSpent;
        g_copyTicks += elapsed;
        if (elapsed > g_copyMax) g_copyMax = elapsed;
        u9telemetry_transport_publish(&g_transport, &sample.header);
        g_next = u9perf_now() + (g_transport.identity.qpc_frequency + U9TM_PUBLISH_HZ - 1) / U9TM_PUBLISH_HZ;
    }
    elapsed = u9perf_now() - began;
    g_frameTicks += elapsed;
    if (elapsed > g_frameMax) g_frameMax = elapsed;
}

uint32_t u9telemetry_requested_groups(void)
{
    return g_requested;
}
