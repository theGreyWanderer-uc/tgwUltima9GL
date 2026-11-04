#ifndef U9GL_GPUPROF_H
#define U9GL_GPUPROF_H
#include <stdint.h>

#define U9GPU_MAX_REGIONS 16
#define U9GPU_RING        8

int  u9gpu_region(const char *name);
int  u9gpu_find(const char *name);

void u9gpu_frame_begin(uint64_t frame, int sample);
void u9gpu_begin(int region);
void u9gpu_end(int region);
void u9gpu_frame_end(void);

typedef struct U9GpuSample {
    uint64_t frame;
    uint64_t issued_qpc;
    uint64_t read_qpc;
    double   frame_ms;
    double   region_ms[U9GPU_MAX_REGIONS];
    uint32_t region_mask;
} U9GpuSample;

typedef struct U9GpuState {
    int      supported;
    uint64_t samples, skipped;
    int      have;
    U9GpuSample latest;
} U9GpuState;
const U9GpuState *u9gpu_state(void);
void u9gpu_report(void);

#endif
