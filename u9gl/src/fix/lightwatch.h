#ifndef U9FIX_LIGHTWATCH_H
#define U9FIX_LIGHTWATCH_H

void u9light_install(void);

void u9light_tick(void);

typedef struct {
    float    pos[3];
    float    base[3];
    float    rgb[3];
    float    range;
    unsigned flags;
    unsigned rec;
    float    baseRgb[3];
    float    flickerDepth;
    int      stepped;
    int      type;
} U9LightSnap;

#define U9TYPE_FILL_LIGHT 1510

#define U9LIGHT_CORE 50.0f

void u9light_range_scales(float *pos, float *neg);
int u9light_snapshot(U9LightSnap *out, int max);

unsigned u9light_ranged_switch(void);
void     u9light_set_ranged_switch(unsigned v);

int  u9light_flicker_range(void);
void u9light_set_flicker_range(int v);

int u9light_ambient(int rgb[3]);

int u9light_book_open(void);

void u9light_report(void);

void u9light_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
