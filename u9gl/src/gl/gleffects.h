#ifndef U9GL_GLEFFECTS_H
#define U9GL_GLEFFECTS_H

#include "glpost.h"
#include "../fix/lightwatch.h"

typedef struct {
    int      texId, texId2;
    unsigned first, count;
    unsigned glTex, texFmt;
    int      additive;
} U9EffectRun;

unsigned u9gl_effect_runs(void (*fn)(void *ctx, const U9EffectRun *r), void *ctx);

void u9fx_frame(const U9FrameView *v);

void u9fx_command(const char *arg, void (*out)(const char *fmt, ...));

void u9fx_report(void);

#define U9FX_LIGHTS       24
#define U9TYPE_FIRE_LIGHT (-44)
#define U9LIGHT_SOFT      0x80000000u
int  u9fx_fire_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v, int dungeon);

#define U9FX_RUNE_LIGHTS  16
#define U9TYPE_RUNE_LIGHT (-45)
int      u9fx_is_rune(int texId);
unsigned u9fx_rune_mask(int texId, unsigned glTex);
int      u9fx_rune_tint(int texId, float tint[3]);
int      u9fx_rune_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v);
void     u9fx_rune_command(const char *arg, void (*out)(const char *fmt, ...));

int      u9fx_is_glyph(int texId);
unsigned u9fx_glyph_mask(int texId, unsigned glTex);
int      u9fx_glyph_tint(int texId, float tint[3]);

#define U9FX_PILLAR_LIGHTS  12
#define U9TYPE_PILLAR_LIGHT (-46)
int      u9fx_is_pillar(int texId);
float    u9fx_pillar_glow(void);
int      u9fx_pillar_lights(U9LightSnap *all, int n, int cap, const U9FrameView *v);
void     u9fx_pillar_command(const char *arg, void (*out)(const char *fmt, ...));

#define U9TYPE_GATE_LIGHT (-47)
int      u9fx_gate_tex(int texId);
int      u9fx_gate_here(const float p[3]);
float    u9fx_gate_glow(void);
int      u9fx_gate_lights(U9LightSnap *all, int n, int cap);
void     u9fx_gate_command(const char *arg, void (*out)(const char *fmt, ...));

void u9fx_fire_command(const char *arg, void (*out)(const char *fmt, ...));
void u9fx_glow_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
