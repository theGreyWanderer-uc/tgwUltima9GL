#ifndef U9GL_SLOTS_H
#define U9GL_SLOTS_H

#define U9_SLOT_DESTROY 0

#include "u9renderer.h"

void u9glslots_init(void);
void u9glslots_restore_gamma(void);

void U9CALL u9gl_slot_set_gamma(void *self, float gamma);
void U9CALL u9gl_slot_get_gamma(void *self, unsigned *r, unsigned *g, unsigned *b);

void U9CALL u9gl_slot_set_depth_mode(void *self, unsigned mode);
void U9CALL u9gl_slot_disable_depth(void *self);
void U9CALL u9gl_slot_set_depth_write(void *self, unsigned enable);
void U9CALL u9gl_slot_set_depth_func(void *self, unsigned func);

void     U9CALL u9gl_slot04(void *self, float a);
unsigned U9CALL u9gl_slot13(void *self, unsigned a, unsigned b);
void     U9CALL u9gl_slot14(void *self, unsigned a);
void     U9CALL u9gl_slot_set_mipmap_level(void *self, unsigned level);
void     U9CALL u9gl_slot_draw_line(void *self, const void *v0, const void *v1,
                                    float alpha);
void     U9CALL u9gl_slot_flush_lines(void *self);
void     U9CALL u9gl_slot_screenshot(void *self, const char *path);
void    *U9CALL u9gl_slot_capture(void *self, unsigned toBuffer);
void     U9CALL u9gl_slot_suspend(void *self);
void     U9CALL u9gl_slot_restore_state(void *self);

#endif
