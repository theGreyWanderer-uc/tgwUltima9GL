#ifndef U9_OBJSTATS_H
#define U9_OBJSTATS_H

#include <stdint.h>

#define U9_OBJSTATS_CELLS 256
typedef struct { uint16_t x, y, objects, drawn, moving, flags; } U9ObjCell;

#define U9_OBJSTATS_MESHES 256
typedef struct { uint32_t appearance, users, size, inPool; } U9ObjMesh;

typedef struct {
    int      valid;

    unsigned gridW, gridH;
    unsigned cellsDrawn, cellsActive, cellsCollide, cellsQueued;
    unsigned cellsWithSlots;
    unsigned slotBytes;
    unsigned lruCells;
    unsigned ring[3];
    unsigned terrainMeshes, terrainMeshMax, terrainA, terrainB;
    unsigned queue[3];
    unsigned pendingMeshes, pendingInstances;

    unsigned objects, worstCell, drawn, moving, fixed, nonfixed, unresolved;
    unsigned uniqueTypes;
    unsigned topType[3], topCount[3];
    unsigned inCamera;

    unsigned meshTypes, meshInUse, meshIdle, meshUsers, meshMaxUsers, meshMaxAppearance;
    unsigned meshBytesPool, meshBytesOutside;
    unsigned poolSize, poolLargestFree;
    unsigned engineMeshTypes, engineMeshBytes;
    unsigned idleKeep;

    unsigned fixedUsed, fixedMax, nonfixedUsed, nonfixedMax;

    double   sampleMs;

    unsigned  cellCount, cellsOutside;
    U9ObjCell cell[U9_OBJSTATS_CELLS];
    unsigned  meshListCount;
    U9ObjMesh meshList[U9_OBJSTATS_MESHES];
} U9ObjStats;

void u9objstats_sample(void);

const U9ObjStats *u9objstats_get(void);

typedef void (*U9AreaObjectFn)(const unsigned char *obj, int fixed, int moving, void *ctx);
unsigned u9area_each_object(U9AreaObjectFn fn, void *ctx);

#endif
