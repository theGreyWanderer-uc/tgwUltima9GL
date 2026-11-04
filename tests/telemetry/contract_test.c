#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../u9gl/include/u9telemetry.h"

static unsigned char g_record[U9TM_MAPPING_BYTES];
static struct Field { char name[64]; uint64_t value; } g_fields[192];
static unsigned g_count;

static void add(const char *group, const char *field, uint64_t value)
{
    snprintf(g_fields[g_count].name, sizeof g_fields[g_count].name, "%s.%s", group, field);
    g_fields[g_count++].value = value;
}

int main(int argc, char **argv)
{
    U9TelemetryHeader h;
    FILE *binary, *expected;
    size_t length;
    unsigned i, skipped = 0;
    char key[96], value[96], session[33];
    if (argc != 3) return 2;
    binary = fopen(argv[1], "rb");
    if (!binary) return 2;
    length = fread(g_record, 1, sizeof g_record, binary);
    fclose(binary);
    if (length < sizeof h) return 3;
    memcpy(&h, g_record, sizeof h);
    if (h.snapshot_bytes != length || h.group_count > U9TM_MAX_GROUPS ||
        U9TM_HEADER_BYTES + h.group_count * U9TM_ENTRY_BYTES > length) return 3;

#define H(member) add("header", #member, h.member)
    H(magic); H(major); H(minor); H(header_bytes); H(snapshot_bytes); H(sequence);
    H(producer_pid); H(process_created_filetime); H(qpc_frequency); H(session_start_qpc);
    H(sample_qpc); H(sample_utc_filetime); H(sample_index); H(producer_kind);
    H(group_count); H(entry_bytes); H(mapping_bytes); H(requested_groups);
#undef H

    for (i = 0; i < h.group_count; i++) {
        U9TelemetryGroupEntry e;
        memcpy(&e, g_record + U9TM_HEADER_BYTES + i * U9TM_ENTRY_BYTES, sizeof e);
        if (e.offset + e.bytes > length) return 3;
#define PAYLOAD(type, var) type var; if (e.bytes < sizeof var) return 3; memcpy(&var, g_record + e.offset, sizeof var)
        if (e.id == U9TM_GROUP_FRAMES && e.version == 1) {
            PAYLOAD(U9TelemetryFrames, f);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("frames", "collected_qpc", f.collected_qpc); add("frames", "frame_no", f.frame_no);
            add("frames", "presents", f.presents); add("frames", "present_failed", f.present_failed);
            add("frames", "draw_calls_last", f.draw_calls_last); add("frames", "vertices_last", f.vertices_last);
        } else if (e.id == U9TM_GROUP_RESOURCES && e.version == 1) {
            PAYLOAD(U9TelemetryResources, r);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("resources", "collected_qpc", r.collected_qpc); add("resources", "textures_alive", r.textures_alive);
            add("resources", "texture_bytes", r.texture_bytes); add("resources", "textures_created", r.textures_created);
            add("resources", "textures_destroyed", r.textures_destroyed);
        } else if (e.id == U9TM_GROUP_MEMORY && e.version == 1) {
            PAYLOAD(U9TelemetryMemory, m);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("memory", "collected_qpc", m.collected_qpc); add("memory", "private_bytes", m.private_bytes);
            add("memory", "working_set", m.working_set); add("memory", "peak_working_set", m.peak_working_set);
            add("memory", "pagefile", m.pagefile);
        } else if (e.id == U9TM_GROUP_PACING && e.version == 1) {
            PAYLOAD(U9TelemetryPacing, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("pacing", "collected_qpc", g.collected_qpc); add("pacing", "fps_milli", g.fps_milli);
        } else if (e.id == U9TM_GROUP_RENDERER && e.version == 1) {
            PAYLOAD(U9TelemetryRenderer, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("renderer", "collected_qpc", g.collected_qpc); add("renderer", "mip_mode", g.mip_mode);
            add("renderer", "anisotropy_milli", g.anisotropy_milli); add("renderer", "depth_mode", g.depth_mode);
            add("renderer", "buffers_alive", g.buffers_alive); add("renderer", "buffer_bytes", g.buffer_bytes);
            add("renderer", "fbos_alive", g.fbos_alive);
        } else if (e.id == U9TM_GROUP_WORLD && e.version == 1) {
            PAYLOAD(U9TelemetryWorld, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("world", "collected_qpc", g.collected_qpc); add("world", "map_valid", g.map_valid);
            add("world", "map", g.map); add("world", "avatar_valid", g.avatar_valid);
            add("world", "avatar_x", (uint64_t)g.avatar_x); add("world", "avatar_y", (uint64_t)g.avatar_y);
            add("world", "avatar_z", (uint64_t)g.avatar_z);
        } else if (e.id == U9TM_GROUP_TEXTURE_CACHE && e.version == 1) {
            PAYLOAD(U9TelemetryTextureCache, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("textureCache", "collected_qpc", g.collected_qpc); add("textureCache", "cached", g.cached);
            add("textureCache", "cache_bytes", g.cache_bytes); add("textureCache", "budget_bytes", g.budget_bytes);
            add("textureCache", "pending", g.pending); add("textureCache", "uploads", g.uploads);
            add("textureCache", "duplicates", g.duplicates); add("textureCache", "evictions", g.evictions);
            add("textureCache", "uploads_per_sec", g.uploads_per_sec);
            add("textureCache", "evictions_per_sec", g.evictions_per_sec);
            add("textureCache", "upload_max_us", g.upload_max_us);
        } else if (e.id == U9TM_GROUP_OVERRIDES && e.version == 1) {
            PAYLOAD(U9TelemetryOverrides, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("overrides", "collected_qpc", g.collected_qpc); add("overrides", "enabled", g.enabled);
            add("overrides", "files", g.files); add("overrides", "used", g.used);
            add("overrides", "rejected", g.rejected); add("overrides", "load_max_us", g.load_max_us);
        } else if (e.id == U9TM_GROUP_MEMORY_TREND && e.version == 1) {
            PAYLOAD(U9TelemetryMemoryTrend, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("memoryTrend", "collected_qpc", g.collected_qpc);
            add("memoryTrend", "floor_slope_milli", (uint64_t)g.floor_slope_milli);
            add("memoryTrend", "raw_slope_milli", (uint64_t)g.raw_slope_milli);
            add("memoryTrend", "span_frames", g.span_frames); add("memoryTrend", "window_samples", g.window_samples);
            add("memoryTrend", "window_capacity", g.window_capacity); add("memoryTrend", "floor_rising", g.floor_rising);
        } else if (e.id == U9TM_GROUP_AREA && e.version == 1) {
            PAYLOAD(U9TelemetryArea, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("area", "collected_qpc", g.collected_qpc);
            add("area", "grid_w", g.grid_w);
            add("area", "grid_h", g.grid_h);
            add("area", "cells_drawn", g.cells_drawn);
            add("area", "cells_active", g.cells_active);
            add("area", "cells_collide", g.cells_collide);
            add("area", "cells_queued", g.cells_queued);
            add("area", "cells_with_slots", g.cells_with_slots);
            add("area", "slot_bytes", g.slot_bytes);
            add("area", "lru_cells", g.lru_cells);
            add("area", "ring_near", g.ring_near);
            add("area", "ring_mid", g.ring_mid);
            add("area", "ring_far", g.ring_far);
            add("area", "terrain_meshes", g.terrain_meshes);
            add("area", "terrain_mesh_max", g.terrain_mesh_max);
            add("area", "terrain_a", g.terrain_a);
            add("area", "terrain_b", g.terrain_b);
            add("area", "queue_sorting", g.queue_sorting);
            add("area", "queue_waiting", g.queue_waiting);
            add("area", "queue_ready", g.queue_ready);
            add("area", "pending_meshes", g.pending_meshes);
            add("area", "pending_instances", g.pending_instances);
            add("area", "objects", g.objects);
            add("area", "worst_cell", g.worst_cell);
            add("area", "worst_cell_capacity", g.worst_cell_capacity);
            add("area", "drawn", g.drawn);
            add("area", "moving", g.moving);
            add("area", "fixed", g.fixed);
            add("area", "nonfixed", g.nonfixed);
            add("area", "unresolved", g.unresolved);
            add("area", "unique_types", g.unique_types);
            add("area", "top_type1", g.top_type1);
            add("area", "top_count1", g.top_count1);
            add("area", "top_type2", g.top_type2);
            add("area", "top_count2", g.top_count2);
            add("area", "top_type3", g.top_type3);
            add("area", "top_count3", g.top_count3);
            add("area", "in_camera", g.in_camera);
            add("area", "mesh_types", g.mesh_types);
            add("area", "mesh_in_use", g.mesh_in_use);
            add("area", "mesh_idle", g.mesh_idle);
            add("area", "mesh_users", g.mesh_users);
            add("area", "mesh_max_users", g.mesh_max_users);
            add("area", "mesh_max_appearance", g.mesh_max_appearance);
            add("area", "mesh_bytes_pool", g.mesh_bytes_pool);
            add("area", "mesh_bytes_outside", g.mesh_bytes_outside);
            add("area", "pool_size", g.pool_size);
            add("area", "pool_largest_free", g.pool_largest_free);
            add("area", "engine_mesh_types", g.engine_mesh_types);
            add("area", "engine_mesh_bytes", g.engine_mesh_bytes);
            add("area", "idle_keep", g.idle_keep);
            add("area", "fixed_used", g.fixed_used);
            add("area", "fixed_max", g.fixed_max);
            add("area", "nonfixed_used", g.nonfixed_used);
            add("area", "nonfixed_max", g.nonfixed_max);
            add("area", "sample_us", g.sample_us);
        } else if (e.id == U9TM_GROUP_ADDRESS_SPACE && e.version == 1) {
            PAYLOAD(U9TelemetryAddressSpace, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("addressSpace", "collected_qpc", g.collected_qpc);
            add("addressSpace", "free_bytes", g.free_bytes);
            add("addressSpace", "largest_free_bytes", g.largest_free_bytes);
            add("addressSpace", "walk_us", g.walk_us);
        } else if (e.id == U9TM_GROUP_VRAM && e.version == 2) {
            PAYLOAD(U9TelemetryVram, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("vram", "collected_qpc", g.collected_qpc);
            add("vram", "source", g.source);
            add("vram", "dedicated_bytes", g.dedicated_bytes);
            add("vram", "local_usage_bytes", g.local_usage_bytes);
            add("vram", "local_budget_bytes", g.local_budget_bytes);
            add("vram", "shared_usage_bytes", g.shared_usage_bytes);
            add("vram", "shared_budget_bytes", g.shared_budget_bytes);
            add("vram", "query_us", g.query_us);
        } else if (e.id == U9TM_GROUP_OVERRIDE_RESIDENT && e.version == 1) {
            PAYLOAD(U9TelemetryOverrideResident, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("overrideResident", "collected_qpc", g.collected_qpc);
            add("overrideResident", "count", g.count);
            add("overrideResident", "bytes", g.bytes);
        } else if (e.id == U9TM_GROUP_GPU && e.version == 1) {
            PAYLOAD(U9TelemetryGpu, g);
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("gpu", "collected_qpc", g.collected_qpc);
            add("gpu", "frame_no", g.frame_no);
            add("gpu", "samples", g.samples);
            add("gpu", "skipped", g.skipped);
            add("gpu", "support", g.support);
            add("gpu", "frame_us", g.frame_us);
            add("gpu", "region_mask", g.region_mask);
            add("gpu", "scene_us", g.scene_us);
            add("gpu", "beam_us", g.beam_us);
            add("gpu", "rays_us", g.rays_us);
            add("gpu", "bloom_us", g.bloom_us);
            add("gpu", "ao_us", g.ao_us);
            add("gpu", "pool_shadows_us", g.pool_shadows_us);
            add("gpu", "sun_shadows_us", g.sun_shadows_us);
            add("gpu", "water_us", g.water_us);
            add("gpu", "composite_us", g.composite_us);
        } else if (e.id == U9TM_GROUP_AREA_CELLS && e.version == 1) {
            U9TelemetryAreaCells g;
            if (e.bytes < offsetof(U9TelemetryAreaCells, cell)) return 3;
            memset(&g, 0, sizeof g);
            memcpy(&g, g_record + e.offset, offsetof(U9TelemetryAreaCells, cell));
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add("areaCells", "collected_qpc", g.collected_qpc); add("areaCells", "grid_w", g.grid_w);
            add("areaCells", "grid_h", g.grid_h); add("areaCells", "avatar_valid", g.avatar_valid);
            add("areaCells", "avatar_x", g.avatar_x); add("areaCells", "avatar_y", g.avatar_y);
            add("areaCells", "slot_capacity", g.slot_capacity); add("areaCells", "cells_outside", g.cells_outside);
            add("areaCells", "count", g.count);
        } else if ((e.id == U9TM_GROUP_TEXTURE_LIST || e.id == U9TM_GROUP_MESH_LIST) && e.version == 1) {
            U9TelemetryTextureList g;
            const char *name = e.id == U9TM_GROUP_TEXTURE_LIST ? "textureList" : "meshList";
            if (e.bytes < offsetof(U9TelemetryTextureList, item)) return 3;
            memset(&g, 0, sizeof g);
            memcpy(&g, g_record + e.offset, offsetof(U9TelemetryTextureList, item));
            if (!(e.flags & U9TM_GROUP_AVAILABLE)) continue;
            add(name, "collected_qpc", g.collected_qpc);
            add(name, e.id == U9TM_GROUP_TEXTURE_LIST ? "cache_entries" : "mesh_types", g.cache_entries);
            add(name, "count", g.count);
        } else skipped++;
#undef PAYLOAD
    }

    expected = fopen(argv[2], "r");
    if (!expected) return 2;
    for (i = 0; i < g_count; i++) {
        char *end;
        uint64_t want;
        if (fscanf(expected, "%95s %95s", key, value) != 2 || strcmp(key, g_fields[i].name)) {
            fprintf(stderr, "expected %s, fixture has %s\n", g_fields[i].name, key);
            return 4;
        }
        want = strtoull(value, &end, 10);
        if (*end || g_fields[i].value != want) {
            fprintf(stderr, "%s: got %" PRIu64 ", expected %s\n", key, g_fields[i].value, value);
            return 5;
        }
    }
    for (i = 0; i < 16; i++) snprintf(session + i * 2, 3, "%02x", h.session_id[i]);
    if (fscanf(expected, "%95s %95s", key, value) != 2 || strcmp(key, "session_id") || strcmp(value, session)) return 6;
    if (fscanf(expected, "%95s %95s", key, value) != 2 || strcmp(key, "producer_version") ||
        !memchr(h.producer_version, 0, 32) || strcmp(value, h.producer_version)) return 7;
    if (fscanf(expected, "%95s %95s", key, value) != 2 || strcmp(key, "skipped") ||
        strtoul(value, NULL, 10) != skipped) return 8;
    fclose(expected);
    return 0;
}
