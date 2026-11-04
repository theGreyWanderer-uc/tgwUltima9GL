#ifndef U9GL_GLSUN_H
#define U9GL_GLSUN_H

typedef struct {
    int   time;
    float dir[3];
    float stored[3];
    float elevation;
    float storm;
    int   underground;
    int   moonMap;
} U9Sun;

#define U9SUN_UP 2

int u9sun_read(U9Sun *s);

void u9sun_command(const char *arg, void (*out)(const char *fmt, ...));

typedef struct {
    float dir[3];
    float elevation;
    float theta;
    int   index;
    float frac;
    float lit;
    int   storedIndex;
    int   haloAlpha;
    float placed[3];
    int   placedOk;
} U9MoonOne;

typedef struct {
    int   time, day;
    float night;
    float alpha;
    int   shown;
    U9MoonOne m[2];
} U9Moons;

int u9moon_read(U9Moons *ms);

void  u9moon_init(void);
float u9moon_shadow_strength(const U9Moons *ms, int k);
int   u9moon_caster(const U9Moons *ms, float *strength);
extern int g_moonShadowOn;

void u9moon_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
