#ifndef U9FIX_SHRINEWATCH_H
#define U9FIX_SHRINEWATCH_H

void u9shrine_install(void);

void u9shrine_report(void);

int  u9shrine_beam(int fadeMs, int delayMs, int *virtue, float base[3], float foot[3], float *level);
float u9shrine_beam_peak(int leadMs);
void u9shrine_beam_test(int v);

#endif
