#ifndef U9GL_BACKGROUND_H
#define U9GL_BACKGROUND_H

#include <stdint.h>

void u9gl_background_install(void);

void u9gl_background_set_owner(int ours);

void u9gl_background_tick(void);

int u9gl_background_on(void);

int u9gl_background_keep_running(uintptr_t caller);

#endif
