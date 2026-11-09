#ifndef U9GL_GLPOST_H
#define U9GL_GLPOST_H

typedef struct {
    int   valid;
    int   depthMode;
    float zRef, zFar;
    int   vx, vy, vw, vh;
    float engineW, engineH;
    float sx, sy, cx, cy;
    float m[12];
} U9FrameView;

int u9gl_frame_view(U9FrameView *out);

void u9wz_draw(const U9FrameView *v, int surfW, int surfH, int scale);

int u9glpost_point_covered(const U9FrameView *v, const float p[3], float margin);

unsigned u9glpost_exact_tex(void);

void u9glpost_init(void);

void u9glpost_install_hooks(void);

void u9glpost_begin(int surfW, int surfH, int *frameW, int *frameH);

void u9glpost_end(int surfW, int surfH);

int u9glpost_owns_gamma(void);

void u9glpost_release(void);

void u9glpost_albedo_targets(int on);

void u9glpost_pool_command(const char *arg, void (*out)(const char *fmt, ...));

void u9glpost_command(const char *arg, void (*out)(const char *fmt, ...));

void u9glpost_report(void);

#endif
