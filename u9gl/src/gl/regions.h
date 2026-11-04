#ifndef U9GL_REGIONS_H
#define U9GL_REGIONS_H

#define U9REGION_SLOTS 240

void u9regions_load(void);

int         u9regions_exists(int map);
const char *u9regions_name(int map);

int u9regions_centre(int map, float *x, float *y, float *z, int *hasZ);

int u9regions_lava(int map, const int **cells);

#endif
