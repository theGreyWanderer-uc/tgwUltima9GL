#ifndef U9FIX_TELEPROBE_H
#define U9FIX_TELEPROBE_H

#include <stdint.h>

void u9tele_install(int everything);

void u9tele_actor_tick(void *actor);

typedef void (*U9TeleQueryFn)(uint32_t *regs);
U9TeleQueryFn u9tele_query_observer(void);

void u9tele_report(void);

#endif
