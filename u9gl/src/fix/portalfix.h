#ifndef U9FIX_PORTALFIX_H
#define U9FIX_PORTALFIX_H

#include <stdint.h>

void u9portal_install(void);

void u9portal_tick(void);

void u9portal_door_note(uintptr_t obj, unsigned handle, const char *when);

void u9portal_report(void);

#endif
