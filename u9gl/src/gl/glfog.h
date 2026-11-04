#ifndef U9GL_GLFOG_H
#define U9GL_GLFOG_H

typedef struct {
    int   on;
    float top;
    float density;
    float soft;
    float box[4];
    float rgb[3];
    float noise;
    float scale;
    float time;
    float reach;
} U9FogParams;

void u9fog_init(void);
void u9fog_frame(U9FogParams *out);
int  u9fog_hides(int texId);
void u9fog_command(const char *arg, void (*out)(const char *fmt, ...));

int u9gl_sheet_measure(int (*listed)(int texId), float lo[3], float hi[3], float *meanZ,
                       float rgba[4], unsigned *tris);

#endif
