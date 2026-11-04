#ifndef U9_DIAG_H
#define U9_DIAG_H

#include <stdint.h>

void u9diag_line(const char *fmt, ...);

int u9diag_read(const void *addr, void *out, unsigned n);

uint32_t u9diag_u32(uintptr_t addr);

uint32_t u9diag_vtable(uintptr_t addr);

void u9diag_dump_object(const char *label, uintptr_t addr, unsigned dwords);



uintptr_t u9call_patch(uintptr_t site, void *replacement);
void      u9call_revert_all(void);

void     *u9inline_hook(uintptr_t target, void *replacement, unsigned stolen,
                        const unsigned char *expect);
void      u9inline_remove_all(void);

void      u9watch_revive(void);
void      u9watch_stream(int frame);
void      u9watch_dump_all(void);

void      u9hw_arm(uintptr_t obj);
void      u9hw_arm_at(uintptr_t obj, const unsigned *offs, unsigned n);
void      u9hw_disarm(void);
void      u9hw_follow(uintptr_t candidate);
int       u9hw_is_armed(void);
uintptr_t u9hw_owner(void);
int       u9hw_handle(void *ctx);
void      u9hw_arm_counter(uintptr_t addr, unsigned limit);
int       u9hw_counter_done(void);
void      u9hw_counter_report(const char *why);

#endif
