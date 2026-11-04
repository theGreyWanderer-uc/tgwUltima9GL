#ifndef U9GL_TELEMETRY_METRICS_SNAPSHOT_H
#define U9GL_TELEMETRY_METRICS_SNAPSHOT_H
#include "../../include/u9telemetry.h"
#include "../gl/metrics.h"
#include "../diag/objstats.h"
#include "../gl/gltexlist.h"

#define U9TM_GAME_GROUPS 17u
typedef struct U9TelemetryGameRecord {
    U9TelemetryHeader header;
    U9TelemetryGroupEntry directory[U9TM_GAME_GROUPS];
    U9TelemetryFrames frames;
    U9TelemetryResources resources;
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
} U9TelemetryGameRecord;

typedef struct U9TelemetryInputs {
    const U9GLMetrics *metrics;
    const U9MetricsFigures *figures;
    int available;
    uint64_t memory_qpc;
    uint64_t now;
    int mip_mode, depth_mode;
    double anisotropy;
    uint64_t world_qpc;
    int map;
    int avatar_valid, avatar_x, avatar_y, avatar_z;
    const U9MetricsHeavy *heavy;
    const U9ObjStats *area;
    unsigned cell_slots_usable;
    int gpu_support;
    int gpu_have;
    uint64_t gpu_frame, gpu_issued_qpc, gpu_samples, gpu_skipped;
    double gpu_frame_ms, gpu_region_ms[9];
    uint32_t gpu_mask;
} U9TelemetryInputs;

void u9telemetry_game_record_init(U9TelemetryGameRecord *record);
void u9telemetry_metrics_copy(const U9TelemetryInputs *in, U9TelemetryGameRecord *out);
#endif
