#ifndef U9GL_SHADOWCOST_H
#define U9GL_SHADOWCOST_H
#include <stdint.h>
#include <x86intrin.h>

extern int      g_scostOn;
extern uint64_t g_scostGuardTsc;
extern unsigned g_scostGuardCalls;

enum { U9SC_CUBES, U9SC_SUN, U9SC_N };

void u9scost_init(void);
static inline uint64_t u9scost_tsc(void) { return __rdtsc(); }
void u9scost_add(int part, uint64_t qpcTicks);
void u9scost_lights(const float cam[3], const float *pos, int strideFloats, int n);
void u9scost_cube(int slot, const float pos[3], float reach, int type, unsigned frameTris, unsigned memTris,
                  uint64_t qpcTicks);
enum { U9SC_IN_SETUP, U9SC_IN_FIXTURE, U9SC_IN_SELECT, U9SC_IN_SWITCH, U9SC_IN_RUNS, U9SC_IN_MEM,
       U9SC_IN_FINISH, U9SC_IN_N };
void u9scost_inside(const uint64_t t[U9SC_IN_N], unsigned runs, unsigned drawn, unsigned culled);
void u9scost_frame_end(void);
void u9scost_report(void (*out)(const char *fmt, ...));

#endif
