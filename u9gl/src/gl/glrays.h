#ifndef U9GL_GLRAYS_H
#define U9GL_GLRAYS_H

#include "glpost.h"

void u9rays_init(void);
unsigned u9rays_build(const U9FrameView *v, unsigned colourTex, unsigned depthTex, unsigned exactTex,
                      unsigned bloomTex, unsigned skyTex, int w, int h, int scale, float k[4]);
int  u9rays_sun_on(void);
int  u9rays_air(void);
int  u9rays_glare(void);
void u9rays_command(const char *arg, void (*out)(const char *fmt, ...));
void u9rays_release(void);

#endif
