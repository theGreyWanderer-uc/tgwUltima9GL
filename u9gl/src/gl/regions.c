#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regions.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define NAME_W          0x80
#define T_NAME          0x08
#define T_TILES         0x98
#define T_CHUNK         1024
#define POINT_UNITS     128
#define HEIGHT_UNITS    4
#define F_WIDTH         0x10
#define F_TABLE         0x1C
#define PG_BASE_X       0x0C
#define PG_LISTS        0x20
#define OBJ_SIZE        10

#define LAVA_TEX        43
#define LAVA_GRID       320
#define LAVA_MAPS       100

typedef struct {
    int  has, centreOk, hasZ;
    int  x, y, z;
    char name[NAME_W];
    int *lava, nLava;
} Region;

static Region g_reg[U9REGION_SLOTS];

typedef struct { int x, y, z; } Obj;

static unsigned char *read_all(const char *path, DWORD *len)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char *b = NULL;
    DWORD size, got = 0;
    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    if (size != INVALID_FILE_SIZE && size > 0 && size < 64u * 1024 * 1024 && (b = malloc(size)) != NULL &&
        !(ReadFile(f, b, size, &got, NULL) && got == size)) {
        free(b);
        b = NULL;
    }
    CloseHandle(f);
    *len = got;
    return b;
}

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int      les16(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }

static Obj *fixed_objects(const char *dir, int map, int *count)
{
    char path[MAX_PATH + 32];
    DWORD len;
    unsigned char *b;
    uint32_t w, h, heap, i;
    Obj *o = NULL;
    int n = 0, cap = 0;

    *count = 0;
    snprintf(path, sizeof path, "%sfixed.%d", dir, map);
    if (!(b = read_all(path, &len))) return NULL;
    if (len < F_TABLE) { free(b); return NULL; }
    w = le32(b + F_WIDTH);
    h = le32(b + F_WIDTH + 4);
    heap = 0x20 + 4 * w * h;
    if (w > 64 || h > 64 || heap > len) { free(b); return NULL; }
    for (i = 0; i < w * h; i++) {
        uint32_t v = le32(b + F_TABLE + 4 * i), pg, k;
        int bx, by;
        if (!v) continue;
        pg = heap + v - 1;
        if (pg + PG_LISTS + 64 > len) continue;
        bx = (int)le32(b + pg + PG_BASE_X);
        by = (int)le32(b + pg + PG_BASE_X + 4);
        for (k = 0; k < 16; k++) {
            uint32_t off = le32(b + pg + PG_LISTS + 4 * k), guard = 0;
            while (off && heap + off + OBJ_SIZE <= len && guard++ < 4096) {
                const unsigned char *p = b + heap + off;
                if (n == cap) {
                    Obj *g = realloc(o, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *o);
                    if (!g) { free(o); free(b); return NULL; }
                    o = g;
                }
                o[n].x = bx + les16(p + 4);
                o[n].y = by + les16(p + 6);
                o[n].z = les16(p + 8);
                n++;
                off = le32(p);
            }
        }
    }
    free(b);
    *count = n;
    return o;
}

static int ground_at(HANDLE f, uint32_t w, uint32_t h, const uint16_t *tiles, int x, int y,
                     int *z, int *hole)
{
    uint32_t px, py, tw = w / 16, c, v;
    DWORD got = 0;
    unsigned char p[4];
    if (x < 0 || y < 0) return 0;
    px = (uint32_t)x / POINT_UNITS; py = (uint32_t)y / POINT_UNITS;
    if (px >= w || py >= h) return 0;
    c = tiles[(py / 16) * tw + px / 16];
    if (SetFilePointer(f, (LONG)(T_TILES + 2 * tw * (h / 16) + c * T_CHUNK + 4 * ((py % 16) * 16 + px % 16)),
                       NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return 0;
    if (!ReadFile(f, p, 4, &got, NULL) || got != 4) return 0;
    v = le32(p);
    *z = (int)(v & 0xFFFu) * HEIGHT_UNITS;
    *hole = (int)((v >> 12) & 1u);
    return 1;
}

static void centre_from_terrain(Region *r, uint32_t w, uint32_t h, const uint16_t *tiles)
{
    uint32_t tw = w / 16, th = h / 16, n = tw * th, i, best = 0, filler = 0, fillN = 0, cnt = 0;
    double sx = 0, sy = 0, bestD = 1e300;
    for (i = 0; i < n; i++) {
        uint32_t j, k = 0;
        if (i && tiles[i] == tiles[i - 1]) continue;
        for (j = 0; j < n; j++) k += tiles[j] == tiles[i];
        if (k > fillN) { fillN = k; filler = tiles[i]; }
    }
    for (i = 0; i < n; i++)
        if (fillN < 2 || tiles[i] != filler) { sx += i % tw; sy += i / tw; cnt++; }
    if (!cnt) { r->x = (int)(w * POINT_UNITS / 2); r->y = (int)(h * POINT_UNITS / 2); r->centreOk = 1; return; }
    sx /= cnt; sy /= cnt;
    for (i = 0; i < n; i++) {
        double dx, dy, d;
        if (fillN >= 2 && tiles[i] == filler) continue;
        dx = (double)(i % tw) - sx; dy = (double)(i / tw) - sy; d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = i; }
    }
    r->x = (int)(((best % tw) * 16 + 8) * POINT_UNITS);
    r->y = (int)(((best / tw) * 16 + 8) * POINT_UNITS);
    r->centreOk = 1;
}

static void centre_from_objects(Region *r, const Obj *o, int n, HANDLE f, uint32_t w, uint32_t h,
                                const uint16_t *tiles)
{
    double sx = 0, sy = 0, bestD = 1e300;
    int i, nearest = 0, pick = -1, floorZ, g, hole;
    static int hist[4096];
    int top = 0, topN = -1;

    for (i = 0; i < n; i++) { sx += o[i].x; sy += o[i].y; }
    sx /= n; sy /= n;
#define D2(k) (((double)o[k].x - sx) * ((double)o[k].x - sx) + ((double)o[k].y - sy) * ((double)o[k].y - sy))
    for (i = 0; i < n; i++) if (D2(i) < bestD) { bestD = D2(i); nearest = i; }
    memset(hist, 0, sizeof hist);
    for (i = 0; i < n; i++)
        if (abs(o[i].x - o[nearest].x) <= 2048 && abs(o[i].y - o[nearest].y) <= 2048)
            hist[(o[i].z >> 5) + 2048]++;
    for (i = 0; i < 4096; i++)
        if (hist[i] > topN) { topN = hist[i]; top = i; }
    floorZ = (top - 2048) * 32 + 16;
    bestD = 1e300;
    for (i = 0; i < n; i++)
        if (abs(o[i].x - o[nearest].x) <= 2048 && abs(o[i].y - o[nearest].y) <= 2048 &&
            abs(o[i].z - floorZ) <= 32 && D2(i) < bestD) { bestD = D2(i); pick = i; }
#undef D2
    if (pick < 0) pick = nearest;
    r->x = o[pick].x;
    r->y = o[pick].y;
    r->centreOk = 1;
    if (ground_at(f, w, h, tiles, r->x, r->y, &g, &hole) && !hole && floorZ <= g + 128) {
        r->hasZ = 0;
    } else {
        r->hasZ = 1;
        r->z = floorZ + 32;
    }
}

static void load_region(const char *dir, int map)
{
    Region *r = &g_reg[map];
    char path[MAX_PATH + 32];
    unsigned char head[T_TILES];
    uint16_t *tiles = NULL;
    uint32_t w, h, n;
    DWORD got = 0;
    HANDLE f;
    Obj *o;
    int count;

    snprintf(path, sizeof path, "%sterrain.%d", dir, map);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD a = GetFileAttributesA(path);
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
            r->has = 1;
            snprintf(r->name, NAME_W, "(in use)");
        }
        return;
    }
    r->has = 1;
    if (!ReadFile(f, head, sizeof head, &got, NULL) || got != sizeof head) goto done;
    memcpy(r->name, head + T_NAME, NAME_W);
    r->name[NAME_W - 1] = 0;
    w = le32(head); h = le32(head + 4);
    if (!w || !h || w > 4096 || h > 4096) goto done;
    n = (w / 16) * (h / 16);
    if (!(tiles = malloc(n * sizeof *tiles))) goto done;
    if (!ReadFile(f, tiles, n * sizeof *tiles, &got, NULL) || got != n * sizeof *tiles) goto done;

    o = fixed_objects(dir, map, &count);
    if (o && count) centre_from_objects(r, o, count, f, w, h, tiles);
    else            centre_from_terrain(r, w, h, tiles);
    free(o);
done:
    if (!r->name[0]) snprintf(r->name, NAME_W, "(unnamed)");
    free(tiles);
    CloseHandle(f);
}

static void lava_scan(const char *dir, int map)
{
    char path[MAX_PATH + 32];
    DWORD len = 0;
    unsigned char *b;
    uint32_t w, h, tw, cc, x, y;
    Region *r = &g_reg[map];
    int *hash = NULL, cap = 0;
    enum { HASH = 8192 };
    snprintf(path, sizeof path, "%sterrain.%d", dir, map);
    if (!(b = read_all(path, &len))) return;
    if (len < T_TILES) goto done;
    w = le32(b); h = le32(b + 4); cc = le32(b + 0x94);
    if (!w || !h || w > 4096 || h > 4096 || T_TILES + 2 * (w / 16) * (h / 16) > len) goto done;
    tw = w / 16;
    if (!(hash = calloc(HASH, sizeof *hash))) goto done;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const unsigned char *t = b + T_TILES + 2 * ((x / 16) + (y / 16) * tw);
            uint32_t c = (uint32_t)t[0] | (uint32_t)t[1] << 8, v;
            size_t off = T_TILES + 2 * (size_t)tw * (h / 16) + (size_t)c * T_CHUNK + 4 * ((x % 16) + (y % 16) * 16);
            int cx, cy, z, k;
            unsigned s;
            if (c >= cc || off + 4 > len) continue;
            v = le32(b + off);
            if ((v >> 22) != LAVA_TEX) continue;
            cx = (int)(x * POINT_UNITS) / LAVA_GRID;
            cy = (int)(y * POINT_UNITS) / LAVA_GRID;
            z = (int)(v & 0xFFFu) * HEIGHT_UNITS;
            for (s = ((unsigned)cx * 73856093u ^ (unsigned)cy * 19349663u) & (HASH - 1); hash[s]; s = (s + 1) & (HASH - 1)) {
                k = hash[s] - 1;
                if (r->lava[3 * k] == cx && r->lava[3 * k + 1] == cy) break;
            }
            if (hash[s]) {
                k = hash[s] - 1;
                if (z > r->lava[3 * k + 2]) r->lava[3 * k + 2] = z;
                continue;
            }
            if (r->nLava >= HASH * 3 / 4) continue;
            if (r->nLava == cap) {
                int *p = realloc(r->lava, (size_t)(cap ? cap * 2 : 64) * 3 * sizeof *p);
                if (!p) continue;
                r->lava = p;
                cap = cap ? cap * 2 : 64;
            }
            r->lava[3 * r->nLava] = cx; r->lava[3 * r->nLava + 1] = cy; r->lava[3 * r->nLava + 2] = z;
            hash[s] = ++r->nLava;
        }
done:
    free(hash);
    free(b);
}

int u9regions_lava(int map, const int **cells)
{
    if (map < 0 || map >= U9REGION_SLOTS || !g_reg[map].nLava) return 0;
    *cells = g_reg[map].lava;
    return g_reg[map].nLava;
}

void u9regions_load(void)
{
    static int done;
    char dir[MAX_PATH], *slash;
    DWORD t0 = u9time_now();
    int m, regions = 0, centres = 0, lavaMaps = 0, lavaCells = 0;

    if (done) return;
    done = 1;
    GetModuleFileNameA(NULL, dir, sizeof dir);
    slash = strrchr(dir, '\\');
    if (!slash) return;
    snprintf(slash + 1, (size_t)(dir + sizeof dir - slash - 1), "static\\");
    for (m = 0; m < U9REGION_SLOTS; m++) {
        load_region(dir, m);
        regions += g_reg[m].has;
        centres += g_reg[m].centreOk;
        if (m < LAVA_MAPS && g_reg[m].has) {
            lava_scan(dir, m);
            if (g_reg[m].nLava) { lavaMaps++; lavaCells += g_reg[m].nLava; }
        }
    }
    u9log_line("REGIONS %d regions, %d with a centre, %d with lava ground (%d cells), read in %lu ms", regions,
               centres, lavaMaps, lavaCells, (unsigned long)(u9time_now() - t0));
}

int u9regions_exists(int map)
{
    return map >= 0 && map < U9REGION_SLOTS && g_reg[map].has;
}

const char *u9regions_name(int map)
{
    return u9regions_exists(map) ? g_reg[map].name : "";
}

int u9regions_centre(int map, float *x, float *y, float *z, int *hasZ)
{
    const Region *r;
    if (!u9regions_exists(map) || !g_reg[map].centreOk) return 0;
    r = &g_reg[map];
    *x = (float)r->x; *y = (float)r->y; *z = (float)r->z; *hasZ = r->hasZ;
    return 1;
}
