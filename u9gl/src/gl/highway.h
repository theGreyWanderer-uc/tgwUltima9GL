#ifndef U9GL_HIGHWAY_H
#define U9GL_HIGHWAY_H

#define U9HIGHWAY_MAPS 8

typedef struct {
    unsigned link;
    int      x, y, z;
    int      nmaps;
    int      maps[U9HIGHWAY_MAPS];
} U9HighwayPoint;

typedef struct {
    unsigned link;
    int      relocate;
    int      map;
    char     record[16];
} U9HighwayUse;

void u9highway_load(void);

const U9HighwayPoint *u9highway_point(unsigned link);

int u9highway_pick_map(const U9HighwayPoint *p, int want, int current);

int u9highway_npc_points(unsigned npc, U9HighwayUse *out, int cap);

#endif
