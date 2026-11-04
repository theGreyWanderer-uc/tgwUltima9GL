#ifndef U9GL_GLREFLECT_H
#define U9GL_GLREFLECT_H

#include "glpost.h"

void u9reflect_init(void);
unsigned u9reflect_build(const U9FrameView *v, int w, int h, int scale);
int  u9reflect_command(const char *arg, void (*out)(const char *fmt, ...));
void u9reflect_report(void (*out)(const char *fmt, ...));
void u9reflect_release(void);

#endif
