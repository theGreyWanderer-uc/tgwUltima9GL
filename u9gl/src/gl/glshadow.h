#ifndef U9GL_GLSHADOW_H
#define U9GL_GLSHADOW_H

#include <windows.h>
#include <GL/gl.h>
#include "glpost.h"

typedef void (*U9CasterFn)(void *ctx, unsigned first, unsigned count, unsigned glTex, int cutout);
unsigned    u9gl_casters(U9CasterFn fn, void *ctx);
int         u9gl_reticle_centre(float *x, float *y);
void        u9gl_pick_report(void (*out)(const char *fmt, ...));
void        u9gl_pick_ties(void (*out)(const char *fmt, ...));
void        u9gl_beam_report(int texId, void (*out)(const char *fmt, ...));
unsigned    u9gl_runs_with_texture(int texId, U9CasterFn fn, void *ctx);
#define U9TEX_LAVA 43
void        u9shadow_report_near(const float pos[3], float within, void (*line)(const char *fmt, ...));
void        u9shadow_sun_probe_ray(const U9FrameView *v, const float P[3], const float N[3], const float l[3],
                                   void (*out)(const char *fmt, ...));
void        u9shadow_probe_ray(const U9FrameView *v, const float L[3], const float P[3], float range, int slot,
                               float weight, int fixture, float bias, float slope, void (*line)(const char *fmt, ...));
const void *u9gl_caster_vertices(int *stride);

#define U9TEX_BLOB_SHADOW 6708

#define U9TEX_WATER 49
int u9shadow_hide_blob(void);

void u9shadow_init(void);

void u9shadow_new_frame(const U9FrameView *v);

#define U9SHADOW_MAX 8

GLuint u9shadow_build(const U9FrameView *v, const float pos[3], float reach, int slot, int fixture);

void u9shadow_draw_view(const U9FrameView *v, int surfW, int surfH);

typedef struct {
    float  r[3], u[3], l[3];
    float  pr[3], pu[3], pl[3];
    float  c[3];
    float  half, dz, texel;
    int    size;
    GLuint tex;
} U9SunMap;
unsigned u9shadow_sun_rebases(void);
int  u9shadow_build_sun(const U9FrameView *v, const float dir[3], U9SunMap *nearMap, U9SunMap *farMap);
void u9shadow_draw_sun_view(int surfW, int surfH);
extern int      g_sunView, g_sunDebug, g_sunOn, g_sunSize;
extern float    g_sunRange, g_sunMinElevation, g_sunStrength, g_sunNear, g_sunAhead;
extern float    g_sunFadeFrom, g_sunFadeTo, g_sunBias;
extern int      g_sunSlopeOcc;
extern int      g_sunCull;
extern unsigned g_sunRunsDrawn[2], g_sunRunsCulled[2];
extern int      g_sunWalls;
extern int      g_sunSteady;
extern int      g_sunFreeze;
extern int      g_sunHold;
extern int      g_sunSmooth;
extern int      g_sunStepMs, g_sunXfadeMs;
int  u9shadow_sun_fading(U9SunMap *nearOld, U9SunMap *farOld, float *mix);
unsigned u9shadow_sun_steps(void);
void u9shadow_sun_trace(unsigned followTex);
extern int      g_waterDepth;
extern int      g_sunCascades;
extern unsigned g_sunBuilds, g_sunTris;
extern int      g_sunMemory, g_sunKeepS;
extern float    g_sunAbove;
extern float    g_sunMemMargin;
extern int      g_sunEvict;
extern unsigned g_memEvicted;
extern unsigned g_sunMemLive, g_sunMemDrawn, g_sunMemDropped, g_sunMemSkipped;
void u9shadow_sun_forget(void);

extern int   g_shadowOn;
extern int   g_shadowSize;
extern int   g_shadowCutouts;
extern int   g_shadowView;
extern int   g_shadowHideBlob;
extern int   g_shadowCount;
extern float g_shadowNear;
extern float g_shadowFixture;
extern float g_shadowColumn;
extern float g_shadowPart;
extern unsigned g_shadowBuilds, g_shadowTris, g_shadowFixtures;
extern int   g_shadowMem;
extern int   g_shadowMemAnimated;
extern int   g_lmFaces;
extern int   g_lmGrid;
extern int   g_lmRunBox;
extern int   g_lmKeep;
extern unsigned g_keepKept, g_keepMade, g_keepChecks, g_keepMismatch;
extern unsigned g_memRefused;
void u9shadow_mem_census(void (*out)(const char *fmt, ...), const char *tag, unsigned refused, unsigned sunMaps);
extern unsigned g_gridChoices, g_gridTested, g_gridListed, g_gridChecks, g_gridMismatch;
extern int   g_shadowFire;
extern unsigned g_shadowMemTris, g_shadowMemFix, g_shadowMemUs, g_shadowMemFrames;
extern unsigned g_shadowRunsDrawn, g_shadowRunsCulled;

#endif
