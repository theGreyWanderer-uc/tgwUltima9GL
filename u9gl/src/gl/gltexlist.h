#ifndef U9GL_GLTEXLIST_H
#define U9GL_GLTEXLIST_H

#define U9GL_TEXLIST_MAX 256

typedef struct {
    int      id, id2;
    unsigned w, h;
    unsigned lastUsed;
    int      overridden;
    int      keyed;
} U9TexListItem;

unsigned u9gl_tex_recent(U9TexListItem *out, unsigned max, unsigned *entries, unsigned *frame);

#endif
