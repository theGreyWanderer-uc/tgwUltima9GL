#include <stdio.h>
#include <string.h>
#include "../../u9gl/src/telemetry/metrics_snapshot.h"

static int all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    while (n--) if (*b++) return 0;
    return 1;
}

int main(void)
{
    U9GLMetrics m = {0}, before;
    U9MetricsFigures f = {0};
    U9TelemetryInputs in = {0};
    U9TelemetryGameRecord sample;
    unsigned i;
    m.frame_no = UINT64_C(9007199254740993);
    m.presents = 8; m.present_failed = 2; m.draw_calls_last = 4; m.vertices_last = 12;
    m.textures_alive = 3; m.texture_bytes = UINT64_C(4294967296);
    m.textures_created = 5; m.textures_destroyed = 2;
    m.private_bytes = 100; m.working_set = 80; m.peak_working_set = 120; m.pagefile = 90;
    m.buffers_alive = 1; m.buffer_bytes = 64;
    f.tick_qpc = 95; f.fps = 59.9996; f.tex_cached = 7; f.tex_bytes = UINT64_C(5000000000);
    f.tex_budget_bytes = 201326592; f.tex_uploads = 40; f.tex_evictions = 3;
    f.tex_uploads_per_sec = 6; f.upload_ms_max = 1.25; f.ov_enabled = 1; f.ov_files = 9;
    f.ov_load_ms_max = 0.0004;
    f.floor_slope = -1536.25; f.raw_slope = 2.0; f.slope_span_frames = 480;
    f.window_samples = 16; f.window_capacity = 16; f.floor_rising = 1;
    before = m;
    in.metrics = &m; in.figures = &f; in.available = 1; in.memory_qpc = 90; in.now = 100;
    in.mip_mode = 2; in.depth_mode = 1; in.anisotropy = 8.0;
    in.world_qpc = 98; in.map = 9; in.avatar_valid = 1; in.avatar_x = -12; in.avatar_y = 2000000; in.avatar_z = 30;
    u9telemetry_metrics_copy(&in, &sample);
    if (sample.header.snapshot_bytes != sizeof sample || sample.header.group_count != U9TM_GAME_GROUPS) return 1;
    for (i = 0; i < U9TM_GAME_GROUPS; i++) {
        const U9TelemetryGroupEntry *e = &sample.directory[i];
        if (e->offset % 8 || e->bytes % 8 || (i < 9 && e->flags != U9TM_GROUP_AVAILABLE) || (i >= 9 && e->flags) ||
            e->offset < U9TM_HEADER_BYTES + U9TM_GAME_GROUPS * U9TM_ENTRY_BYTES ||
            (i && (e->id <= e[-1].id || e->offset < e[-1].offset + e[-1].bytes))) return 2;
    }
    if (sample.frames.frame_no != m.frame_no || sample.frames.draw_calls_last != 4 ||
        sample.resources.texture_bytes != m.texture_bytes ||
        sample.memory.collected_qpc != 90 || sample.memory.private_bytes != 100 || memcmp(&m, &before, sizeof m)) return 3;
    if (sample.pacing.fps_milli != 60000 || sample.pacing.collected_qpc != 95 ||
        sample.texture_cache.cache_bytes != UINT64_C(5000000000) || sample.texture_cache.upload_max_us != 1250 ||
        sample.texture_cache.collected_qpc != 95 || sample.overrides.load_max_us != 0 || sample.overrides.files != 9 ||
        sample.memory_trend.floor_slope_milli != -1536250 || sample.memory_trend.raw_slope_milli != 2000 ||
        sample.memory_trend.floor_rising != 1 || sample.memory_trend.collected_qpc != 90 ||
        sample.renderer.mip_mode != 2 || sample.renderer.depth_mode != 1 || sample.renderer.anisotropy_milli != 8000 ||
        sample.renderer.buffer_bytes != 64 || sample.renderer.collected_qpc != 100 ||
        sample.world.map_valid != 1 || sample.world.map != 9 || sample.world.avatar_x != -12 ||
        sample.world.avatar_y != 2000000 || sample.world.collected_qpc != 98) return 4;

    in.memory_qpc = 0; f.tick_qpc = 0; in.map = -1; in.avatar_valid = 0; in.anisotropy = -1.0;
    u9telemetry_metrics_copy(&in, &sample);
    if (sample.directory[2].flags || sample.directory[8].flags || !all_zero(&sample.memory, sizeof sample.memory) ||
        !all_zero(&sample.memory_trend, sizeof sample.memory_trend) ||
        sample.directory[3].flags || sample.directory[6].flags || sample.directory[7].flags ||
        !all_zero(&sample.pacing, sizeof sample.pacing) || !all_zero(&sample.texture_cache, sizeof sample.texture_cache) ||
        !all_zero(&sample.overrides, sizeof sample.overrides) ||
        sample.directory[0].flags != U9TM_GROUP_AVAILABLE || sample.directory[5].flags != U9TM_GROUP_AVAILABLE ||
        sample.world.map_valid || sample.world.avatar_valid || sample.world.avatar_x || sample.renderer.anisotropy_milli) return 5;

    in.world_qpc = 0;
    u9telemetry_metrics_copy(&in, &sample);
    if (sample.directory[5].flags || !all_zero(&sample.world, sizeof sample.world) ||
        sample.directory[4].flags != U9TM_GROUP_AVAILABLE) return 8;

    {
        U9MetricsHeavy h = {0};
        U9ObjStats area = {0};
        area.valid = 1; area.objects = 312; area.worstCell = 41; area.topType[2] = 777; area.topCount[2] = 9;
        area.meshUsers = 2318; area.nonfixedUsed = 3250000; area.sampleMs = 0.75;
        area.gridW = 128; area.gridH = 128; area.cellCount = 2; area.cellsOutside = 1;
        area.cell[0].x = 63; area.cell[0].y = 41; area.cell[0].objects = 212; area.cell[0].drawn = 180;
        area.cell[0].moving = 3; area.cell[0].flags = 7;
        area.cell[1].x = 127; area.cell[1].y = 0; area.cell[1].objects = 2045;
        area.meshTypes = 900; area.meshListCount = 1;
        area.meshList[0].appearance = 4711; area.meshList[0].users = 120; area.meshList[0].size = 65536; area.meshList[0].inPool = 1;
        {
            static U9TexListItem tex[2];
            tex[0].id = 5448; tex[0].id2 = 3; tex[0].w = 256; tex[0].h = 128; tex[0].lastUsed = 1000; tex[0].keyed = 1;
            tex[1].id = 8047; tex[1].id2 = 300; tex[1].w = 8192; tex[1].h = 64; tex[1].lastUsed = 10; tex[1].overridden = 1;
            h.tex_list_qpc = 94; h.tex_list = tex; h.tex_list_count = 2; h.tex_list_entries = 812; h.tex_list_frame = 1005;
        }
        h.area_qpc = 91; h.addr_qpc = 92; h.addr_largest_bytes = UINT64_C(222298112); h.addr_walk_ms = 1.5;
        h.vram_qpc = 93; h.vram_kind = 1; h.vram_dedicated_bytes = UINT64_C(8589934592); h.vram_local_use_bytes = 300000000; h.vram_query_ms = 0.05;
        h.ov_res_qpc = 0;
        in.world_qpc = 98; in.heavy = &h; in.area = &area; in.cell_slots_usable = 2045;
        u9telemetry_metrics_copy(&in, &sample);
        if (sample.directory[9].flags != U9TM_GROUP_AVAILABLE || sample.area.collected_qpc != 91 ||
            sample.area.objects != 312 || sample.area.worst_cell != 41 || sample.area.worst_cell_capacity != 2045 ||
            sample.area.top_type3 != 777 || sample.area.top_count3 != 9 || sample.area.mesh_users != 2318 ||
            sample.area.nonfixed_used != 3250000 || sample.area.sample_us != 750) return 9;
        if (sample.directory[14].flags != U9TM_GROUP_AVAILABLE || sample.area_cells.count != 2 ||
            sample.area_cells.cells_outside != 1 || sample.area_cells.slot_capacity != 2045 ||
            sample.area_cells.avatar_valid != 0 ||
            sample.area_cells.cell[0] != ((uint64_t)63 | (uint64_t)41 << 8 | (uint64_t)212 << 16 |
                                          (uint64_t)180 << 32 | (uint64_t)3 << 44 | (uint64_t)7 << 56) ||
            sample.area_cells.cell[1] != ((uint64_t)127 | (uint64_t)2045 << 16) || sample.area_cells.cell[2]) return 17;
        if (sample.directory[16].flags != U9TM_GROUP_AVAILABLE || sample.mesh_list.mesh_types != 900 || sample.mesh_list.count != 1 ||
            sample.mesh_list.item[0] != ((uint64_t)4711 | (uint64_t)120 << 16 | (uint64_t)65536 << 32 | (uint64_t)1 << 60)) return 18;
        if (sample.directory[15].flags != U9TM_GROUP_AVAILABLE || sample.texture_list.collected_qpc != 94 ||
            sample.texture_list.cache_entries != 812 || sample.texture_list.count != 2 ||
            sample.texture_list.item[0] != ((uint64_t)5448 | (uint64_t)3 << 16 | (uint64_t)256 << 24 | (uint64_t)128 << 36 |
                                            (uint64_t)5 << 48 | (uint64_t)1 << 61) ||
            sample.texture_list.item[1] != ((uint64_t)8047 | (uint64_t)255 << 16 | (uint64_t)4095 << 24 | (uint64_t)64 << 36 |
                                            (uint64_t)995 << 48 | (uint64_t)1 << 60)) return 19;
        if (sample.directory[10].flags != U9TM_GROUP_AVAILABLE || sample.address_space.collected_qpc != 92 ||
            sample.address_space.largest_free_bytes != UINT64_C(222298112) || sample.address_space.walk_us != 1500) return 10;
        if (sample.directory[11].flags != U9TM_GROUP_AVAILABLE || sample.directory[11].version != 2 || sample.vram.source != 1 ||
            sample.vram.dedicated_bytes != UINT64_C(8589934592) || sample.vram.local_usage_bytes != 300000000 ||
            sample.vram.query_us != 50) return 11;
        if (sample.directory[12].flags || !all_zero(&sample.override_resident, sizeof sample.override_resident)) return 12;
        area.valid = 0;
        u9telemetry_metrics_copy(&in, &sample);
        if (sample.directory[9].flags || !all_zero(&sample.area, sizeof sample.area) ||
            sample.directory[14].flags || !all_zero(&sample.area_cells, sizeof sample.area_cells)) return 13;
        if (sample.directory[13].flags) return 14;
        in.gpu_support = 1; in.gpu_have = 1; in.gpu_frame = 590; in.gpu_issued_qpc = 88; in.gpu_samples = 12;
        in.gpu_frame_ms = 9.875; in.gpu_region_ms[7] = 1.25; in.gpu_mask = 1u << 7;
        u9telemetry_metrics_copy(&in, &sample);
        if (sample.directory[13].flags != U9TM_GROUP_AVAILABLE || sample.gpu.collected_qpc != 88 ||
            sample.gpu.frame_no != 590 || sample.gpu.frame_us != 9875 || sample.gpu.water_us != 1250 ||
            sample.gpu.region_mask != (1u << 7) || sample.gpu.scene_us != 0) return 15;
        in.gpu_have = 0; in.gpu_support = 2;
        u9telemetry_metrics_copy(&in, &sample);
        if (sample.directory[13].flags != U9TM_GROUP_AVAILABLE || sample.gpu.support != 2 || sample.gpu.collected_qpc != 100) return 16;
        in.gpu_support = 0;
    }

    in.available = 0;
    u9telemetry_metrics_copy(&in, &sample);
    for (i = 0; i < U9TM_GAME_GROUPS; i++) if (sample.directory[i].flags) return 6;
    if (!all_zero(&sample.frames, sizeof sample - offsetof(U9TelemetryGameRecord, frames))) return 7;
    puts("PASS: metrics adapter preserves 64 bits, fixed-point and signed fields, per-group availability, directory rules and source cache.");
    return 0;
}
