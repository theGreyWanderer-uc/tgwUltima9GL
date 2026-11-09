#ifndef U9_FIXES_H
#define U9_FIXES_H

void u9fix_install(void);

void u9fix_report(void);

void u9linehit_tick(void);

void u9fix_los_clamp_tick(void);

void u9fix_set_tick_observer(void (*fn)(void *self));

void u9fix_set_entity_dumper(void (*fn)(const char *what, unsigned id,
                                        uintptr_t ent));

void u9fix_set_mover_query_observer(void (*fn)(uint32_t *regs));

#endif
