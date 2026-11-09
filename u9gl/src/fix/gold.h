#ifndef U9_GOLD_H
#define U9_GOLD_H

void u9gold_install(void);

void u9gold_report(void);

int u9avatar_position(int *x, int *y, int *z);

#include <stdint.h>
uintptr_t u9avatar_object(void);

void u9give_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
