#ifndef U9GL_GLUNDER_H
#define U9GL_GLUNDER_H

void u9under_init(void);
void u9under_frame(void);
int  u9under_state(float *depth);
typedef struct {
    int   on;
    float surface;
    float density;
    float dark;
    float reach;
    float absorb[3];
    float rgb[3];
    float caustic;
    float cscale;
    float cdepth;
    float cblur;
    float shaft;
    float sscale;
    float sreach;
    float sdir[3];
    float time;
    float glowFree;
    float glowHaze;
    float glowReach;
} U9UnderFog;
void u9under_fog(U9UnderFog *out);
void u9under_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
