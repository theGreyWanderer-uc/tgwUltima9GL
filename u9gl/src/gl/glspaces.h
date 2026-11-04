#ifndef U9GL_GLSPACES_H
#define U9GL_GLSPACES_H

#define U9SP_TEX_W        1024
#define U9SP_TEX_H        16
#define U9SP_MAX_SPACES   256
#define U9SP_MAX_PLANES   4096
#define U9SP_MAX_PORTALS  2048
#define U9SP_PLANE0       (U9SP_MAX_SPACES * 2)
#define U9SP_PORTAL0      (U9SP_PLANE0 + U9SP_MAX_PLANES)

typedef struct {
    int   nSpaces, nPlanes, nPortals;
    int   roomFull, nearFull;
    float tex[U9SP_TEX_W * U9SP_TEX_H * 4];
    char  name[U9SP_MAX_SPACES][33];
} U9SpaceSet;

#define U9SP_LINKED       4

int u9spaces_for_lights(const float (*base)[3], const float (*pos)[3], const float *reach, int n,
                        int keepOut, const float cam[3], int (*lightRooms)[U9SP_LINKED], U9SpaceSet *set,
                        float camReach);

int u9spaces_at(const float p[3], char *name, int cap, int *count);

void u9spaces_describe(int j, void (*out)(const char *fmt, ...));

#endif
