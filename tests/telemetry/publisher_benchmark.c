#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "../../u9gl/src/telemetry/publisher.h"
#include "../../u9gl/src/telemetry/metrics_snapshot.h"
#include "../../u9gl/src/telemetry/transport.h"
#include "../../u9gl/src/common/perfclock.h"
#include "../../u9gl/src/gl/gpuprof.h"

static int g_enabled;
static U9GLMetrics g_metrics;
static void (*g_remove)(void);
static volatile uint64_t g_sink;
int u9cfg_int(const char *key, int fallback) { return !strcmp(key, "diag_telemetry") ? g_enabled : fallback; }
void u9life_push_at_exit(const char *name, void (*fn)(void)) { (void)name; g_remove = fn; }
void u9log_line(const char *fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); puts(""); }
const U9GLMetrics *u9metrics_current(void) { return &g_metrics; }
int u9metrics_telemetry_state(uint64_t *memory_qpc) { *memory_qpc = 0; return 1; }
void u9metrics_figures(U9MetricsFigures *out) { memset(out, 0, sizeof *out); }
void u9metrics_heavy(U9MetricsHeavy *out) { memset(out, 0, sizeof *out); }
static U9ObjStats g_area;
const U9ObjStats *u9objstats_get(void) { return &g_area; }
static U9GpuState g_gpu = { 0, 0, 0, 0, { 0 } };
const U9GpuState *u9gpu_state(void) { return &g_gpu; }
int u9gpu_find(const char *name) { (void)name; return -1; }
void u9gpu_report(void) {}
int u9gl_mip_filter_mode(void) { return 1; }
float u9gl_anisotropy_cached(void) { return 8.0f; }
int u9gl_depth_mode(void) { return 0; }
int u9console_current_map(void) { return 9; }
int u9avatar_position(int *x, int *y, int *z) { *x = 1; *y = 2; *z = 3; return 1; }

int main(int argc, char **argv)
{
    U9TelemetryGameRecord sample;
    U9MetricsFigures figures = {0};
    U9TelemetryInputs in = {0};
    U9TelemetryTransport transport = {0};
    uint64_t start, elapsed, frequency = u9perf_frequency(), i, iterations = 2000000;
    g_enabled = argc > 1 && !strcmp(argv[1], "on");
    in.metrics = &g_metrics; in.figures = &figures; in.available = 1; in.map = 9;
    if (!g_enabled) {
        start = u9perf_now();
        for (i = 0; i < iterations; i++) {
            g_metrics.frame_no = i;
            u9telemetry_metrics_copy(&in, &sample);
        }
        elapsed = u9perf_now() - start; g_sink = sample.frames.frame_no;
        printf("BENCH adapter_iterations=%" PRIu64 " mean_us=%.4f\n", iterations, elapsed * 1000000.0 / frequency / iterations);
        if (!u9telemetry_transport_open(&transport, U9TM_PRODUCER_SYNTHETIC, "benchmark")) return 1;
        for (i = 0; i < 200000; i++) {
            in.now = u9perf_now(); u9telemetry_metrics_copy(&in, &sample);
            u9telemetry_transport_publish(&transport, &sample.header);
        }
        printf("BENCH transport_samples=%" PRIu64 " mean_us=%.4f max_us=%.4f\n", transport.published,
               transport.publish_ticks * 1000000.0 / frequency / transport.published,
               transport.publish_max_ticks * 1000000.0 / frequency);
        u9telemetry_transport_close(&transport);
    }
    u9telemetry_install();
    start = u9perf_now();
    for (i = 0; i < iterations; i++) u9telemetry_frame_end();
    elapsed = u9perf_now() - start;
    printf("BENCH callback=%s iterations=%" PRIu64 " mean_us=%.4f\n", g_enabled ? "on" : "off", iterations,
           elapsed * 1000000.0 / frequency / iterations);
    if (g_remove) g_remove();
    return 0;
}
