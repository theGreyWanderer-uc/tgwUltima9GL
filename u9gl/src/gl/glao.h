#ifndef U9GL_GLAO_H
#define U9GL_GLAO_H

#include "glpost.h"

void u9ao_init(void);
unsigned u9ao_build(const U9FrameView *v, unsigned exactTex, int w, int h, int scale, const float *castDir,
                    float k[4]);
void u9ao_command(const char *arg, void (*out)(const char *fmt, ...));
void u9contact_command(const char *arg, void (*out)(const char *fmt, ...));
void u9ao_release(void);

#endif
