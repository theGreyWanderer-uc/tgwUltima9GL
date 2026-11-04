#include <windows.h>
#include "perfclock.h"

uint64_t u9perf_now(void)
{
    LARGE_INTEGER value;
    return QueryPerformanceCounter(&value) && value.QuadPart > 0 ? (uint64_t)value.QuadPart : 0;
}

uint64_t u9perf_frequency(void)
{
    LARGE_INTEGER value;
    return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? (uint64_t)value.QuadPart : 0;
}
