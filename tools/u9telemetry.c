#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
#include "../u9gl/src/telemetry/transport.h"
#include "../u9gl/src/common/perfclock.h"

typedef struct SyntheticRecord {
    U9TelemetryHeader header;
    U9TelemetryGroupEntry directory[18];
    uint64_t gap0[2];
    U9TelemetryFrames frames;
    U9TelemetryResources resources;
    uint64_t gap1;
    U9TelemetryMemory memory;
    U9TelemetryPacing pacing;
    U9TelemetryRenderer renderer;
    U9TelemetryWorld world;
    U9TelemetryTextureCache texture_cache;
    U9TelemetryOverrides overrides;
    U9TelemetryMemoryTrend memory_trend;
    U9TelemetryArea area;
    U9TelemetryAddressSpace address_space;
    U9TelemetryVram vram;
    U9TelemetryOverrideResident override_resident;
    U9TelemetryGpu gpu;
    U9TelemetryAreaCells area_cells;
    U9TelemetryTextureList texture_list;
    U9TelemetryMeshList mesh_list;
    uint64_t unknown[4];
} SyntheticRecord;

static unsigned g_splitMs;
void u9telemetry_test_write_pause(void) { if (g_splitMs) Sleep(g_splitMs); }

static unsigned option(int argc, char **argv, const char *key, unsigned fallback)
{
    int i;
    for (i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], key)) return (unsigned)strtoul(argv[i + 1], NULL, 10);
    return fallback;
}
static int flag(int argc, char **argv, const char *key)
{
    int i;
    for (i = 1; i < argc; i++) if (!strcmp(argv[i], key)) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    U9TelemetryTransport transport = {0}, collision = {0};
    SyntheticRecord sample;
    uint32_t requested = 0, now_requested;
    unsigned seconds = option(argc, argv, "--seconds", 5), delay = option(argc, argv, "--delay-ms", 0);
    unsigned oddAfter = option(argc, argv, "--odd-after", 0), limit = option(argc, argv, "--samples", 0);
    int stress = flag(argc, argv, "--stress"), wrapped = flag(argc, argv, "--wrap");
    uint64_t end, next = 0, n, now, frequency;
    g_splitMs = option(argc, argv, "--split-ms", 0);
    printf("PROCESS %lu\n", GetCurrentProcessId()); fflush(stdout);
    if (delay) Sleep(delay);
    if (!u9telemetry_transport_open(&transport, U9TM_PRODUCER_SYNTHETIC, "synthetic-test")) {
        fprintf(stderr, "Synthetic publisher: install failed, winerr=%lu\n", GetLastError()); return 1;
    }
    if (u9telemetry_transport_open(&collision, U9TM_PRODUCER_SYNTHETIC, "collision-test") ||
        GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Synthetic publisher: existing mapping was not refused\n"); return 2;
    }
    if (wrapped) __atomic_store_n(&((U9TelemetryHeader *)transport.view)->sequence, UINT32_MAX - 1u, __ATOMIC_SEQ_CST);
    if (flag(argc, argv, "--incompatible")) transport.identity.major = 3;
    frequency = transport.identity.qpc_frequency;
    end = u9perf_now() + seconds * frequency;
    printf("READY %s\n", transport.name); fflush(stdout);
    while ((now = u9perf_now()) < end) {
        if ((limit && transport.published >= limit) || (!stress && now < next)) { Sleep(1); continue; }
        n = transport.published + 1;
        now_requested = u9telemetry_transport_requests(&transport, now);
        if (now_requested != requested) { requested = now_requested; printf("REQUEST 0x%08x\n", (unsigned)requested); fflush(stdout); }
        memset(&sample, 0, sizeof sample);
        sample.header.snapshot_bytes = sizeof sample;
        sample.header.group_count = 18;
        sample.header.requested_groups = requested;
#define ENTRY(slot, gid, field) do { sample.directory[slot].id = gid; sample.directory[slot].version = 1; \
        sample.directory[slot].offset = (uint32_t)offsetof(SyntheticRecord, field); \
        sample.directory[slot].bytes = sizeof sample.field; sample.directory[slot].flags = U9TM_GROUP_AVAILABLE; } while (0)
        ENTRY(0, U9TM_GROUP_FRAMES, frames);
        ENTRY(1, U9TM_GROUP_RESOURCES, resources);
        ENTRY(2, U9TM_GROUP_MEMORY, memory);
        ENTRY(3, U9TM_GROUP_PACING, pacing);
        ENTRY(4, U9TM_GROUP_RENDERER, renderer);
        ENTRY(5, U9TM_GROUP_WORLD, world);
        ENTRY(6, U9TM_GROUP_TEXTURE_CACHE, texture_cache);
        ENTRY(7, U9TM_GROUP_OVERRIDES, overrides);
        ENTRY(8, U9TM_GROUP_MEMORY_TREND, memory_trend);
        ENTRY(9, U9TM_GROUP_AREA, area);
        ENTRY(10, U9TM_GROUP_ADDRESS_SPACE, address_space);
        ENTRY(11, U9TM_GROUP_VRAM, vram);
        sample.directory[11].version = 2;
        ENTRY(12, U9TM_GROUP_OVERRIDE_RESIDENT, override_resident);
        ENTRY(13, U9TM_GROUP_GPU, gpu);
        ENTRY(14, U9TM_GROUP_AREA_CELLS, area_cells);
        ENTRY(15, U9TM_GROUP_TEXTURE_LIST, texture_list);
        ENTRY(16, U9TM_GROUP_MESH_LIST, mesh_list);
        ENTRY(17, 30, unknown);
#undef ENTRY
        sample.unknown[0] = now; sample.unknown[1] = n; sample.unknown[2] = n * 3; sample.unknown[3] = n * 7;
        sample.frames.collected_qpc = sample.resources.collected_qpc = sample.memory.collected_qpc = now;
        sample.frames.frame_no = sample.frames.presents = n * 6;
        sample.frames.present_failed = n % 5;
        sample.frames.draw_calls_last = n % 4096;
        sample.frames.vertices_last = (n % 4096) * 3;
        sample.resources.textures_alive = n;
        sample.resources.texture_bytes = n * 4096;
        sample.resources.textures_created = n * 2;
        sample.resources.textures_destroyed = n;
        sample.memory.private_bytes = sample.memory.pagefile = n * 1024;
        sample.memory.working_set = n * 512;
        sample.memory.peak_working_set = n * 2048;
        sample.pacing.collected_qpc = sample.renderer.collected_qpc = sample.world.collected_qpc = now;
        sample.texture_cache.collected_qpc = sample.overrides.collected_qpc = sample.memory_trend.collected_qpc = now;
        sample.pacing.fps_milli = 60000 - n % 1000;
        sample.renderer.mip_mode = n % 3; sample.renderer.anisotropy_milli = 8000; sample.renderer.depth_mode = (n + 1) % 3;
        sample.world.map_valid = 1; sample.world.map = 9; sample.world.avatar_valid = 1;
        sample.world.avatar_x = -(int64_t)n; sample.world.avatar_y = (int64_t)n * 2; sample.world.avatar_z = -5;
        sample.texture_cache.cached = n; sample.texture_cache.cache_bytes = n * 65536; sample.texture_cache.budget_bytes = 201326592;
        sample.texture_cache.pending = n % 4; sample.texture_cache.uploads = n * 3; sample.texture_cache.duplicates = n / 2;
        sample.texture_cache.evictions = n / 3; sample.texture_cache.uploads_per_sec = n % 50;
        sample.texture_cache.evictions_per_sec = n % 7; sample.texture_cache.upload_max_us = n * 10;
        sample.overrides.enabled = 1; sample.overrides.files = 300; sample.overrides.used = n % 300; sample.overrides.rejected = 2;
        sample.overrides.load_max_us = n % 9000;
        sample.memory_trend.floor_slope_milli = -(int64_t)n * 1000; sample.memory_trend.raw_slope_milli = (int64_t)n * 10;
        sample.memory_trend.span_frames = 480; sample.memory_trend.window_samples = n < 16 ? n : 16;
        sample.memory_trend.window_capacity = 16; sample.memory_trend.floor_rising = 0;
        {
            unsigned w;
            uint64_t *words[4] = { &sample.area.collected_qpc, &sample.address_space.collected_qpc,
                                   &sample.vram.collected_qpc, &sample.override_resident.collected_qpc };
            size_t sizes[4] = { sizeof sample.area, sizeof sample.address_space, sizeof sample.vram, sizeof sample.override_resident };
            for (w = 0; w < 4; w++) {
                size_t k;
                words[w][0] = now;
                for (k = 1; k < sizes[w] / 8; k++) words[w][k] = n + k;
            }
            sample.vram.source = 1;
            {
                uint64_t *g = &sample.gpu.collected_qpc;
                size_t k;
                g[0] = now;
                for (k = 1; k < sizeof sample.gpu / 8; k++) g[k] = n + k;
                sample.gpu.support = 1; sample.gpu.region_mask = 0x1FF;
            }
            {
                unsigned c = 0, cx, cy;
                sample.area_cells.collected_qpc = now;
                sample.area_cells.grid_w = sample.area_cells.grid_h = 128;
                sample.area_cells.avatar_valid = 1; sample.area_cells.avatar_x = 62; sample.area_cells.avatar_y = 42;
                sample.area_cells.slot_capacity = 2045;
                for (cy = 40; cy < 45; cy++)
                    for (cx = 60; cx < 66; cx++, c++) {
                        uint64_t objects = (n * 7 + c * 53) % 1800;
                        sample.area_cells.cell[c] = cx | (uint64_t)cy << 8 | objects << 16 | (objects / 2) << 32 |
                                                    (uint64_t)(c % 4) << 44 | (uint64_t)(cx == 62 && cy == 42 ? 7 : 3) << 56;
                    }
                sample.area_cells.count = c;
            }
            {
                unsigned k;
                sample.texture_list.collected_qpc = sample.mesh_list.collected_qpc = now;
                sample.texture_list.cache_entries = 300 + n % 50; sample.texture_list.count = 40;
                for (k = 0; k < 40; k++)
                    sample.texture_list.item[k] = (uint64_t)(4000 + k) | (uint64_t)(k % 4) << 16 | (uint64_t)(64u << (k % 4)) << 24 |
                        (uint64_t)(64u << (k % 3)) << 36 | (uint64_t)((n + k * 7) % 900) << 48 | (uint64_t)(k % 9 == 0) << 60 | (uint64_t)(k % 5 == 0) << 61;
                sample.mesh_list.mesh_types = 700; sample.mesh_list.count = 25;
                for (k = 0; k < 25; k++)
                    sample.mesh_list.item[k] = (uint64_t)(1200 + k * 3) | (uint64_t)(250 - k * 9 + n % 5) << 16 |
                        (uint64_t)(4096u * (k + 1)) << 32 | (uint64_t)(k % 4 != 0) << 60;
            }
        }
        if (!u9telemetry_transport_publish(&transport, &sample.header)) return 3;
        if (wrapped && transport.published == 1) { printf("WRAPPED %u\n", ((U9TelemetryHeader *)transport.view)->sequence); fflush(stdout); }
        if (oddAfter && transport.published == oddAfter) {
            Sleep(500);
            __atomic_add_fetch(&((U9TelemetryHeader *)transport.view)->sequence, 1u, __ATOMIC_SEQ_CST);
            memset(transport.view + offsetof(SyntheticRecord, memory), 0xFF, sizeof sample.memory);
            printf("HOLDING_ODD\n"); fflush(stdout);
            while (u9perf_now() < end) Sleep(10);
            break;
        }
        next = u9perf_now() + (frequency + U9TM_PUBLISH_HZ - 1) / U9TM_PUBLISH_HZ;
    }
    printf("SUMMARY samples=%" PRIu64 " publish_us_mean=%.3f publish_us_max=%.3f\n",
           transport.published, transport.published ? transport.publish_ticks * 1000000.0 / frequency / transport.published : 0.0,
           transport.publish_max_ticks * 1000000.0 / frequency);
    u9telemetry_transport_close(&transport);
    return 0;
}
