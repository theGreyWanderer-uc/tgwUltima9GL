#ifndef U9_TEXOVERRIDE_H
#define U9_TEXOVERRIDE_H

typedef struct {
    unsigned       w, h;
    unsigned char *rgba;
} U9TexImage;

int u9tex_enabled(void);

int u9tex_override(int id, int id2,
                   const unsigned char *engineRgba,
                   unsigned engineW, unsigned engineH,
                   U9TexImage *out);

typedef enum {
    U9TEX_Q_OFF = 0,
    U9TEX_Q_COMPOSED,
    U9TEX_Q_NONE,
    U9TEX_Q_UNTRIED,
    U9TEX_Q_USED,
    U9TEX_Q_REJECTED
} U9TexQuery;
U9TexQuery u9tex_query(int id, int id2, char *file, unsigned fileSize);
const char *u9tex_folder(void);

typedef struct {
    unsigned files, used, rejected;
    double   loadMsMax;
} U9TexStats;
void u9tex_take_stats(U9TexStats *s);

#endif
