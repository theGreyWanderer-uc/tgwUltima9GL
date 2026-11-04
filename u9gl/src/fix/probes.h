#ifndef U9FIX_PROBES_H
#define U9FIX_PROBES_H

#include <stdint.h>

void u9probe_init(void);

void u9probe_actor_tick(void *self);

void u9probe_report(void);

void u9probe_remove(void);

void u9probe_dump_entity(const char *what, unsigned id, uintptr_t ent);

#endif
