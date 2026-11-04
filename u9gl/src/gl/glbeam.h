#ifndef U9GL_GLBEAM_H
#define U9GL_GLBEAM_H

#include "glpost.h"

void u9beam_init(void);
int  u9beam_hides(int texId);
unsigned u9beam_build(const U9FrameView *v, unsigned depthTex, unsigned exactTex, int w, int h,
                      unsigned *inside, float k[4]);
int  u9beam_soft(void);
void u9beam_command(const char *arg, void (*out)(const char *fmt, ...));
void u9beam_release(void);

#endif
