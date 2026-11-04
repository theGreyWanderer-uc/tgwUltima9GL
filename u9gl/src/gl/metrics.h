#ifndef U9GL_METRICS_H
#define U9GL_METRICS_H

#include <stdint.h>

typedef struct U9GLMetrics {
    uint64_t frame_no;
    uint64_t presents;
    uint64_t present_failed;

    uint64_t draw_calls;
    uint64_t vertices;
    uint64_t draw_calls_last;
    uint64_t vertices_last;

    uint64_t textures_alive;
    uint64_t texture_bytes;
    uint64_t textures_created;
    uint64_t textures_destroyed;

    uint64_t buffers_alive;
    uint64_t buffer_bytes;
    uint64_t fbos_alive;

    uint64_t private_bytes;
    uint64_t working_set;
    uint64_t peak_working_set;
    uint64_t pagefile;
} U9GLMetrics;

void u9metrics_init(void);

void u9metrics_engine_frame(void);

void u9metrics_present(int ok);

void u9metrics_draw(unsigned vertices);

void u9metrics_tex_created(unsigned bytes);
void u9metrics_tex_destroyed(unsigned bytes);
void u9metrics_buffer_created(unsigned bytes);
void u9metrics_buffer_destroyed(unsigned bytes);

void u9metrics_frame_end(void);

void u9metrics_overlay(int surfW, int surfH);

void u9metrics_text(float x, float y, const char *s);
int  u9metrics_text_ready(void);
int  u9metrics_text_init(void);
void u9metrics_char_size(int *w, int *h);

void u9metrics_toggle_poll(void);

const U9GLMetrics *u9metrics_current(void);

int u9metrics_telemetry_state(uint64_t *memory_qpc);

typedef struct U9MetricsFigures {
    uint64_t tick_qpc;
    double   fps;
    unsigned tex_cached, tex_pending, tex_uploads, tex_dupes, tex_evictions;
    uint64_t tex_bytes, tex_budget_bytes;
    unsigned tex_uploads_per_sec, tex_evictions_per_sec;
    double   upload_ms_max;
    int      ov_enabled;
    unsigned ov_files, ov_used, ov_rejected;
    double   ov_load_ms_max;

    double   floor_slope, raw_slope;
    uint64_t slope_span_frames;
    unsigned window_samples, window_capacity;
    int      floor_rising;
} U9MetricsFigures;
void u9metrics_figures(U9MetricsFigures *out);

typedef struct U9MetricsSamplerCost { uint64_t count; double mean_us, max_us; } U9MetricsSamplerCost;
typedef struct U9MetricsHeavy {
    uint64_t area_qpc;
    uint64_t addr_qpc, addr_free_bytes, addr_largest_bytes;
    double   addr_walk_ms;
    uint64_t vram_qpc;
    int      vram_kind;
    uint64_t vram_dedicated_bytes;
    uint64_t vram_local_use_bytes, vram_local_budget_bytes;
    uint64_t vram_shared_use_bytes, vram_shared_budget_bytes;
    double   vram_query_ms;
    uint64_t ov_res_qpc, ov_res_count, ov_res_bytes;
    U9MetricsSamplerCost area_cost, addr_cost, vram_cost, ov_cost;
    uint64_t tex_list_qpc;
    const void *tex_list;
    unsigned tex_list_count, tex_list_entries, tex_list_frame;
    U9MetricsSamplerCost tex_list_cost;
} U9MetricsHeavy;
void u9metrics_heavy(U9MetricsHeavy *out);

#endif
