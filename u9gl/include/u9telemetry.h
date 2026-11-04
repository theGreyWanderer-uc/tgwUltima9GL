#ifndef U9GL_TELEMETRY_H
#define U9GL_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#define U9TM_MAGIC          UINT32_C(0x4D543955)
#define U9TM_VERSION_MAJOR  2u
#define U9TM_VERSION_MINOR  0u
#define U9TM_HEADER_BYTES   160u
#define U9TM_ENTRY_BYTES    16u
#define U9TM_MAX_GROUPS     32u
#define U9TM_MAPPING_BYTES  65536u
#define U9TM_SEQUENCE_OFF   16u
#define U9TM_SIZE_OFF       12u
#define U9TM_PRODUCER_GAME  1u
#define U9TM_PRODUCER_SYNTHETIC 2u
#define U9TM_READ_ATTEMPTS  3u
#define U9TM_READ_BUDGET_MS 20u
#define U9TM_PUBLISH_HZ     10u
#define U9TM_STALE_MS       2000u

#define U9TM_GROUP_AVAILABLE 1u

#define U9TM_GROUP_FRAMES    1u
#define U9TM_GROUP_RESOURCES 2u
#define U9TM_GROUP_MEMORY    3u
#define U9TM_GROUP_PACING    4u
#define U9TM_GROUP_RENDERER  5u
#define U9TM_GROUP_WORLD     6u
#define U9TM_GROUP_TEXTURE_CACHE 7u
#define U9TM_GROUP_OVERRIDES 8u
#define U9TM_GROUP_MEMORY_TREND 9u
#define U9TM_GROUP_AREA 10u
#define U9TM_GROUP_ADDRESS_SPACE 11u
#define U9TM_GROUP_VRAM 12u
#define U9TM_GROUP_OVERRIDE_RESIDENT 13u
#define U9TM_GROUP_GPU 14u
#define U9TM_GROUP_AREA_CELLS 15u
#define U9TM_AREA_CELLS 256u
#define U9TM_GROUP_TEXTURE_LIST 16u
#define U9TM_GROUP_MESH_LIST 17u
#define U9TM_LIST_ITEMS 256u
#define U9TM_GROUP_BIT(id)   (UINT32_C(1) << (id))

#define U9TM_REQUEST_MAGIC  UINT32_C(0x52543955)
#define U9TM_REQUEST_BYTES  64u
#define U9TM_REQUEST_EXPIRY_MS 5000u

#pragma pack(push, 8)
typedef struct U9TelemetryHeader {
    uint32_t magic;
    uint16_t major, minor;
    uint32_t header_bytes;
    uint32_t snapshot_bytes;
    uint32_t sequence;
    uint32_t producer_pid;
    uint64_t process_created_filetime;
    uint8_t  session_id[16];
    uint64_t qpc_frequency;
    uint64_t session_start_qpc;
    uint64_t sample_qpc;
    uint64_t sample_utc_filetime;
    uint64_t sample_index;
    uint32_t producer_kind;
    uint16_t group_count, entry_bytes;
    char     producer_version[32];
    uint32_t mapping_bytes;
    uint32_t requested_groups;
    uint8_t  reserved[24];
} U9TelemetryHeader;

typedef struct U9TelemetryGroupEntry {
    uint16_t id, version;
    uint32_t offset;
    uint32_t bytes;
    uint32_t flags;
} U9TelemetryGroupEntry;

typedef struct U9TelemetryFrames {
    uint64_t collected_qpc;
    uint64_t frame_no, presents, present_failed;
    uint64_t draw_calls_last, vertices_last;
} U9TelemetryFrames;

typedef struct U9TelemetryResources {
    uint64_t collected_qpc;
    uint64_t textures_alive, texture_bytes;
    uint64_t textures_created, textures_destroyed;
} U9TelemetryResources;

typedef struct U9TelemetryMemory {
    uint64_t collected_qpc;
    uint64_t private_bytes, working_set, peak_working_set, pagefile;
} U9TelemetryMemory;

typedef struct U9TelemetryPacing {
    uint64_t collected_qpc;
    uint64_t fps_milli;
} U9TelemetryPacing;

typedef struct U9TelemetryRenderer {
    uint64_t collected_qpc;
    uint64_t mip_mode;
    uint64_t anisotropy_milli;
    uint64_t depth_mode;
    uint64_t buffers_alive, buffer_bytes, fbos_alive;
} U9TelemetryRenderer;

typedef struct U9TelemetryWorld {
    uint64_t collected_qpc;
    uint64_t map_valid, map;
    uint64_t avatar_valid;
    int64_t  avatar_x, avatar_y, avatar_z;
} U9TelemetryWorld;

typedef struct U9TelemetryTextureCache {
    uint64_t collected_qpc;
    uint64_t cached, cache_bytes, budget_bytes, pending;
    uint64_t uploads, duplicates, evictions;
    uint64_t uploads_per_sec, evictions_per_sec;
    uint64_t upload_max_us;
} U9TelemetryTextureCache;

typedef struct U9TelemetryOverrides {
    uint64_t collected_qpc;
    uint64_t enabled, files, used, rejected;
    uint64_t load_max_us;
} U9TelemetryOverrides;

typedef struct U9TelemetryMemoryTrend {
    uint64_t collected_qpc;
    int64_t  floor_slope_milli, raw_slope_milli;
    uint64_t span_frames, window_samples, window_capacity;
    uint64_t floor_rising;
} U9TelemetryMemoryTrend;

typedef struct U9TelemetryArea {
    uint64_t collected_qpc;
    uint64_t grid_w;
    uint64_t grid_h;
    uint64_t cells_drawn;
    uint64_t cells_active;
    uint64_t cells_collide;
    uint64_t cells_queued;
    uint64_t cells_with_slots;
    uint64_t slot_bytes;
    uint64_t lru_cells;
    uint64_t ring_near;
    uint64_t ring_mid;
    uint64_t ring_far;
    uint64_t terrain_meshes;
    uint64_t terrain_mesh_max;
    uint64_t terrain_a;
    uint64_t terrain_b;
    uint64_t queue_sorting;
    uint64_t queue_waiting;
    uint64_t queue_ready;
    uint64_t pending_meshes;
    uint64_t pending_instances;
    uint64_t objects;
    uint64_t worst_cell;
    uint64_t worst_cell_capacity;
    uint64_t drawn;
    uint64_t moving;
    uint64_t fixed;
    uint64_t nonfixed;
    uint64_t unresolved;
    uint64_t unique_types;
    uint64_t top_type1;
    uint64_t top_count1;
    uint64_t top_type2;
    uint64_t top_count2;
    uint64_t top_type3;
    uint64_t top_count3;
    uint64_t in_camera;
    uint64_t mesh_types;
    uint64_t mesh_in_use;
    uint64_t mesh_idle;
    uint64_t mesh_users;
    uint64_t mesh_max_users;
    uint64_t mesh_max_appearance;
    uint64_t mesh_bytes_pool;
    uint64_t mesh_bytes_outside;
    uint64_t pool_size;
    uint64_t pool_largest_free;
    uint64_t engine_mesh_types;
    uint64_t engine_mesh_bytes;
    uint64_t idle_keep;
    uint64_t fixed_used;
    uint64_t fixed_max;
    uint64_t nonfixed_used;
    uint64_t nonfixed_max;
    uint64_t sample_us;
} U9TelemetryArea;

typedef struct U9TelemetryAddressSpace {
    uint64_t collected_qpc;
    uint64_t free_bytes;
    uint64_t largest_free_bytes;
    uint64_t walk_us;
} U9TelemetryAddressSpace;

typedef struct U9TelemetryVram {
    uint64_t collected_qpc;
    uint64_t source;
    uint64_t dedicated_bytes;
    uint64_t local_usage_bytes, local_budget_bytes;
    uint64_t shared_usage_bytes, shared_budget_bytes;
    uint64_t query_us;
} U9TelemetryVram;

typedef struct U9TelemetryOverrideResident {
    uint64_t collected_qpc;
    uint64_t count;
    uint64_t bytes;
} U9TelemetryOverrideResident;

typedef struct U9TelemetryGpu {
    uint64_t collected_qpc;
    uint64_t frame_no;
    uint64_t samples, skipped;
    uint64_t support;
    uint64_t frame_us;
    uint64_t region_mask;
    uint64_t scene_us;
    uint64_t beam_us;
    uint64_t rays_us;
    uint64_t bloom_us;
    uint64_t ao_us;
    uint64_t pool_shadows_us;
    uint64_t sun_shadows_us;
    uint64_t water_us;
    uint64_t composite_us;
} U9TelemetryGpu;

typedef struct U9TelemetryAreaCells {
    uint64_t collected_qpc;
    uint64_t grid_w, grid_h;
    uint64_t avatar_valid, avatar_x, avatar_y;
    uint64_t slot_capacity;
    uint64_t cells_outside;
    uint64_t count;
    uint64_t cell[U9TM_AREA_CELLS];
} U9TelemetryAreaCells;

typedef struct U9TelemetryTextureList {
    uint64_t collected_qpc;
    uint64_t cache_entries;
    uint64_t count;
    uint64_t item[U9TM_LIST_ITEMS];
} U9TelemetryTextureList;

typedef struct U9TelemetryMeshList {
    uint64_t collected_qpc;
    uint64_t mesh_types;
    uint64_t count;
    uint64_t item[U9TM_LIST_ITEMS];
} U9TelemetryMeshList;

typedef struct U9TelemetryRequest {
    uint32_t magic;
    uint16_t major, minor;
    uint32_t bytes;
    uint32_t mask;
    uint32_t heartbeat;
    uint8_t  reserved[44];
} U9TelemetryRequest;
#pragma pack(pop)

#define U9TM_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
#define U9TM_FIELD(type, field, off) U9TM_ASSERT(u9tm_##type##_##field, offsetof(type, field) == (off))
U9TM_ASSERT(u9tm_header_size, sizeof(U9TelemetryHeader) == U9TM_HEADER_BYTES);
U9TM_ASSERT(u9tm_entry_size, sizeof(U9TelemetryGroupEntry) == U9TM_ENTRY_BYTES);
U9TM_ASSERT(u9tm_frames_size, sizeof(U9TelemetryFrames) == 48);
U9TM_ASSERT(u9tm_resources_size, sizeof(U9TelemetryResources) == 40);
U9TM_ASSERT(u9tm_memory_size, sizeof(U9TelemetryMemory) == 40);
U9TM_ASSERT(u9tm_pacing_size, sizeof(U9TelemetryPacing) == 16);
U9TM_ASSERT(u9tm_renderer_size, sizeof(U9TelemetryRenderer) == 56);
U9TM_ASSERT(u9tm_world_size, sizeof(U9TelemetryWorld) == 56);
U9TM_ASSERT(u9tm_texture_cache_size, sizeof(U9TelemetryTextureCache) == 88);
U9TM_ASSERT(u9tm_overrides_size, sizeof(U9TelemetryOverrides) == 48);
U9TM_ASSERT(u9tm_memory_trend_size, sizeof(U9TelemetryMemoryTrend) == 56);
U9TM_ASSERT(u9tm_area_size, sizeof(U9TelemetryArea) == 448);
U9TM_ASSERT(u9tm_addressspace_size, sizeof(U9TelemetryAddressSpace) == 32);
U9TM_ASSERT(u9tm_vram_size, sizeof(U9TelemetryVram) == 64);
U9TM_ASSERT(u9tm_overrideresident_size, sizeof(U9TelemetryOverrideResident) == 24);
U9TM_ASSERT(u9tm_gpu_size, sizeof(U9TelemetryGpu) == 128);
U9TM_ASSERT(u9tm_area_cells_size, sizeof(U9TelemetryAreaCells) == 8 * (9 + U9TM_AREA_CELLS));
U9TM_ASSERT(u9tm_texture_list_size, sizeof(U9TelemetryTextureList) == 8 * (3 + U9TM_LIST_ITEMS));
U9TM_ASSERT(u9tm_mesh_list_size, sizeof(U9TelemetryMeshList) == 8 * (3 + U9TM_LIST_ITEMS));
U9TM_ASSERT(u9tm_request_size, sizeof(U9TelemetryRequest) == U9TM_REQUEST_BYTES);
U9TM_FIELD(U9TelemetryHeader, magic, 0);
U9TM_FIELD(U9TelemetryHeader, major, 4);
U9TM_FIELD(U9TelemetryHeader, minor, 6);
U9TM_FIELD(U9TelemetryHeader, header_bytes, 8);
U9TM_FIELD(U9TelemetryHeader, snapshot_bytes, U9TM_SIZE_OFF);
U9TM_FIELD(U9TelemetryHeader, sequence, U9TM_SEQUENCE_OFF);
U9TM_FIELD(U9TelemetryHeader, producer_pid, 20);
U9TM_FIELD(U9TelemetryHeader, process_created_filetime, 24);
U9TM_FIELD(U9TelemetryHeader, session_id, 32);
U9TM_FIELD(U9TelemetryHeader, qpc_frequency, 48);
U9TM_FIELD(U9TelemetryHeader, session_start_qpc, 56);
U9TM_FIELD(U9TelemetryHeader, sample_qpc, 64);
U9TM_FIELD(U9TelemetryHeader, sample_utc_filetime, 72);
U9TM_FIELD(U9TelemetryHeader, sample_index, 80);
U9TM_FIELD(U9TelemetryHeader, producer_kind, 88);
U9TM_FIELD(U9TelemetryHeader, group_count, 92);
U9TM_FIELD(U9TelemetryHeader, entry_bytes, 94);
U9TM_FIELD(U9TelemetryHeader, producer_version, 96);
U9TM_FIELD(U9TelemetryHeader, mapping_bytes, 128);
U9TM_FIELD(U9TelemetryHeader, requested_groups, 132);
U9TM_FIELD(U9TelemetryHeader, reserved, 136);
U9TM_FIELD(U9TelemetryGroupEntry, id, 0);
U9TM_FIELD(U9TelemetryGroupEntry, version, 2);
U9TM_FIELD(U9TelemetryGroupEntry, offset, 4);
U9TM_FIELD(U9TelemetryGroupEntry, bytes, 8);
U9TM_FIELD(U9TelemetryGroupEntry, flags, 12);
U9TM_FIELD(U9TelemetryRequest, magic, 0);
U9TM_FIELD(U9TelemetryRequest, major, 4);
U9TM_FIELD(U9TelemetryRequest, minor, 6);
U9TM_FIELD(U9TelemetryRequest, bytes, 8);
U9TM_FIELD(U9TelemetryRequest, mask, 12);
U9TM_FIELD(U9TelemetryRequest, heartbeat, 16);
#undef U9TM_FIELD
#undef U9TM_ASSERT

#endif
