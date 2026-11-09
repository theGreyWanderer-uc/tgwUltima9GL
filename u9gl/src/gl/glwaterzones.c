#include <windows.h>
#include <stdio.h>
#include <GL/gl.h>
#include "glpost.h"
#include "metrics.h"

int u9console_current_map(void);
int u9avatar_position(int *x, int *y, int *z);

typedef struct {
    short map;
    int x, y, z;
    unsigned char sx, sy, sz;
    unsigned char shape;
    unsigned short link;
} WaterZone;

static const WaterZone ZONES[] = {
    {   9,  29057,  45152,  1414,  80,  20,  80, 0x0D,   2 },
    {   9,  48181,  55744,  2500,  35,  35, 200, 0x05,   1 },
    {   9,  36127,  71839,  1529, 100, 100, 255, 0x09,  16 },
    {   9,  36073,  71902,  1439, 100, 100, 255, 0x05,  17 },
    {   9,  36161,  71910,   907, 150, 150, 255, 0x09,  18 },
    {   9,  36111,  73872,   460, 255, 255,  80, 0x35,  12 },
    {   9,  93263,  82317,  1233, 100, 200, 125, 0x0D,   2 },
    {   9,  84563,  89646,  1489,  50,  30, 100, 0x09,   1 },
    {  19,  11269,   7730,   466,  50,  50, 130, 0x09,   6 },
    {  19,  10751,   6577,   426,  50,  50, 130, 0x09,   7 },
    {  19,  10778,   5794,   667,  50,  50, 130, 0x09,   6 },
    {  19,   4524,  10816,   251,  50,  50, 220, 0x0D,   4 },
    {  19,   5030,  11389,   626, 120,  25, 130, 0x09,   5 },
    {  50,  10343,  23009,   302, 100,  25, 130, 0x09,   1 },
    { 164,  84577,  89528,  1413,  25,  25, 100, 0x08,   1 },
    { 168,  84577,  89528,  1413,  25,  25, 100, 0x08,   1 },
    { 169,  84577,  89528,  1413,  25,  25, 100, 0x08,   1 },
    { 230,  84577,  89528,  1413,  25,  25, 100, 0x08,   1 },
};
#define NZONES (sizeof ZONES / sizeof ZONES[0])

static void zone_box(const WaterZone *z, float lo[3], float hi[3])
{
    float k = z->shape >> 4 ? 7.0f : 1.0f;
    float hx = (float)z->sx * k, hy = (float)z->sy * k, h = 2.0f * (float)z->sz * k;
    lo[0] = (float)z->x - hx; hi[0] = (float)z->x + hx;
    lo[1] = (float)z->y - hy; hi[1] = (float)z->y + hy;
    lo[2] = (float)z->z;      hi[2] = (float)z->z + h;
}

static void to_camera(const U9FrameView *v, const float p[3], float cs[3])
{
    float d[3];
    int k;
    for (k = 0; k < 3; k++) d[k] = p[k] - v->m[k * 4 + 3];
    for (k = 0; k < 3; k++) cs[k] = v->m[k] * d[0] + v->m[4 + k] * d[1] + v->m[8 + k] * d[2];
}

static void to_window(const U9FrameView *v, const float cs[3], int scale, float *px, float *py)
{
    float ex = v->cx + cs[0] * v->sx / cs[1];
    float ey = v->cy - cs[2] * v->sy / cs[1];
    *px = ((float)v->vx + ex * (float)v->vw / v->engineW) / (float)scale;
    *py = ((float)v->vy + (float)v->vh - 1.0f - ey * (float)v->vh / v->engineH) / (float)scale;
}

static void edge(const U9FrameView *v, const float a[3], const float b[3], int scale)
{
    const float nearY = 8.0f;
    float ca[3], cb[3], t, ax, ay, bx, by;
    int k;
    to_camera(v, a, ca);
    to_camera(v, b, cb);
    if (ca[1] < nearY && cb[1] < nearY) return;
    if (ca[1] < nearY || cb[1] < nearY) {
        t = (nearY - ca[1]) / (cb[1] - ca[1]);
        if (ca[1] < nearY) for (k = 0; k < 3; k++) ca[k] = ca[k] + (cb[k] - ca[k]) * t;
        else               for (k = 0; k < 3; k++) cb[k] = ca[k] + (cb[k] - ca[k]) * t;
    }
    to_window(v, ca, scale, &ax, &ay);
    to_window(v, cb, scale, &bx, &by);
    glVertex2f(ax, ay);
    glVertex2f(bx, by);
}

static void draw_box(const U9FrameView *v, const float lo[3], const float hi[3], int scale)
{
    static const int E[12][2] = { {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4}, {0,4},{1,5},{2,6},{3,7} };
    float c[8][3];
    int k;
    for (k = 0; k < 8; k++) {
        c[k][0] = (k & 1) ? hi[0] : lo[0];
        c[k][1] = (k & 2) ? hi[1] : lo[1];
        c[k][2] = (k & 4) ? hi[2] : lo[2];
    }
    for (k = 0; k < 12; k++) edge(v, c[E[k][0]], c[E[k][1]], scale);
}

void u9wz_draw(const U9FrameView *v, int surfW, int surfH, int scale)
{
    int map = u9console_current_map(), ax = 0, ay = 0, az = 0, haveAv;
    unsigned i, inside[NZONES], nin = 0;
    char line[160];
    size_t n;
    if (!v || !v->valid || map < 0) return;
    haveAv = u9avatar_position(&ax, &ay, &az);
    for (i = 0; i < NZONES; i++) {
        float lo[3], hi[3];
        if (ZONES[i].map != map || !haveAv) continue;
        zone_box(&ZONES[i], lo, hi);
        if (ax >= lo[0] && ax <= hi[0] && ay >= lo[1] && ay <= hi[1] && az >= lo[2] && az <= hi[2]) inside[nin++] = i;
    }

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_VIEWPORT_BIT | GL_LINE_BIT | GL_TRANSFORM_BIT | GL_COLOR_BUFFER_BIT);
    glViewport(0, 0, surfW, surfH);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)surfW, 0.0, (double)surfH, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_FOG); glDisable(GL_LIGHTING);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (i = 0; i < NZONES; i++) {
        float lo[3], hi[3], ctr[3], a[3], b[3];
        unsigned j;
        int in = 0, k;
        if (ZONES[i].map != map) continue;
        for (j = 0; j < nin; j++) if (inside[j] == i) in = 1;
        zone_box(&ZONES[i], lo, hi);
        glLineWidth(in ? 3.0f : 1.5f);
        glBegin(GL_LINES);
        if (in) glColor4f(0.6f, 0.95f, 1.0f, 1.0f);
        else    glColor4f(0.35f, 0.7f, 1.0f, 0.8f);
        draw_box(v, lo, hi, scale);
        glColor4f(0.1f, 0.3f, 1.0f, 0.9f);
        ctr[0] = (float)ZONES[i].x; ctr[1] = (float)ZONES[i].y; ctr[2] = (float)ZONES[i].z;
        for (k = 0; k < 3; k++) {
            a[0] = ctr[0]; a[1] = ctr[1]; a[2] = ctr[2];
            b[0] = ctr[0]; b[1] = ctr[1]; b[2] = ctr[2];
            a[k] -= 20.0f; b[k] += 20.0f;
            edge(v, a, b, scale);
        }
        glEnd();
    }
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();

    if (u9metrics_text_ready() || u9metrics_text_init()) {
        n = (size_t)snprintf(line, sizeof line, "WATER ZONES (map %d) - inside:", map);
        if (!nin) snprintf(line + n, sizeof line - n, " none by the boxes");
        for (i = 0; i < nin && n < sizeof line - 12; i++) {
            n += (size_t)snprintf(line + n, sizeof line - n, " %u", (unsigned)ZONES[inside[i]].link);
        }
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0.0, (double)surfW, (double)surfH, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glColor4f(0.6f, 0.95f, 1.0f, 1.0f);
        u9metrics_text(12.0f, 24.0f, line);
        glColor4f(0.75f, 0.8f, 0.9f, 1.0f);
        u9metrics_text(12.0f, 42.0f, "the game keeps one switch: the last zone crossed decides, even inside another box");
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
    }
    glPopAttrib();
}
