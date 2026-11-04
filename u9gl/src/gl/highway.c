#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "highway.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define MARKER_TYPE     1134u
#define MAX_POINTS      2048
#define REGIONS         240

static U9HighwayPoint g_pt[MAX_POINTS];
static int            g_n;
static unsigned char *g_act;
static DWORD          g_actLen;

static unsigned char *read_all(const char *path, DWORD *len)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    unsigned char *b = NULL;
    DWORD size, got = 0;
    *len = 0;
    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    if (size != INVALID_FILE_SIZE && size > 0 && size < 64u * 1024 * 1024 && (b = malloc(size)) != NULL &&
        !(ReadFile(f, b, size, &got, NULL) && got == size)) {
        free(b);
        b = NULL;
    }
    CloseHandle(f);
    *len = b ? got : 0;
    return b;
}

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const unsigned char *p) { return (unsigned)(p[0] | p[1] << 8); }
static int      les16(const unsigned char *p) { return (int16_t)(p[0] | p[1] << 8); }

static U9HighwayPoint *point_slot(unsigned link)
{
    int i;
    for (i = 0; i < g_n; i++)
        if (g_pt[i].link == link) return &g_pt[i];
    return NULL;
}

static void scan_region(const char *dir, int map)
{
    char path[MAX_PATH + 32];
    DWORD len;
    unsigned char *b;
    uint32_t w, h, H, c;

    snprintf(path, sizeof path, "%sruntime%cnonfixed.%d", dir, 92, map);
    if (!(b = read_all(path, &len))) return;
    if (len < 0x24) { free(b); return; }
    w = le32(b + 0x14);
    h = le32(b + 0x18);
    H = 36 + 4 * w * h;
    if (w > 64 || h > 64 || H > len) { free(b); return; }
    for (c = 0; c < w * h; c++) {
        uint32_t v = le32(b + 0x20 + 4 * c), pg, k;
        int bx, by;
        if (!v) continue;
        pg = H + v - 1;
        if (pg + 0x60 > len) continue;
        bx = (int)le32(b + pg + 0x0C);
        by = (int)le32(b + pg + 0x10);
        for (k = 0; k < 16; k++) {
            uint32_t off = le32(b + pg + 0x20 + 4 * k), guard = 0;
            while (off && H + off + 32 <= len && guard++ < 4096) {
                const unsigned char *e = b + H + off;
                U9HighwayPoint *p;
                if (le16(e + 0x0A) == MARKER_TYPE && (p = point_slot(le16(e + 0x1A))) != NULL &&
                    p->x == bx + les16(e + 4) && p->y == by + les16(e + 6)) {
                    if (!p->nmaps) p->z = les16(e + 8);
                    if (p->nmaps < U9HIGHWAY_MAPS) p->maps[p->nmaps++] = map;
                }
                off = le32(e);
            }
        }
    }
    free(b);
}

void u9highway_load(void)
{
    char dir[MAX_PATH], path[MAX_PATH + 32], *slash;
    DWORD len;
    unsigned char *b;
    uint32_t n, i;
    int map, placed = 0;
    DWORD t0 = u9time_now();

    GetModuleFileNameA(NULL, dir, sizeof dir);
    slash = strrchr(dir, 92);
    if (!slash) return;
    slash[1] = 0;

    snprintf(path, sizeof path, "%sstatic%chighway.dat", dir, 92);
    if (!(b = read_all(path, &len)) || len < 8) { free(b); u9log_line("HIGHWAY no highway.dat"); return; }
    n = le32(b);
    for (i = 0; i < n && g_n < MAX_POINTS && 8 + (i + 1) * 12 <= len; i++) {
        g_pt[g_n].link = le32(b + 8 + i * 12);
        g_pt[g_n].x = (int32_t)le32(b + 12 + i * 12);
        g_pt[g_n].y = (int32_t)le32(b + 16 + i * 12);
        g_n++;
    }
    free(b);
    for (map = 0; map < REGIONS; map++) scan_region(dir, map);
    for (i = 0; i < (uint32_t)g_n; i++) if (g_pt[i].nmaps) placed++;

    snprintf(path, sizeof path, "%sstatic%cactivity.flx", dir, 92);
    g_act = read_all(path, &g_actLen);

    u9log_line("HIGHWAY %d points, %d with a marker in some region; activity.flx %lu bytes; %lu ms",
               g_n, placed, (unsigned long)g_actLen, (unsigned long)(u9time_now() - t0));
}

const U9HighwayPoint *u9highway_point(unsigned link)
{
    return point_slot(link);
}

int u9highway_pick_map(const U9HighwayPoint *p, int want, int current)
{
    int i;
    if (!p || !p->nmaps) return -1;
    if (want >= 0) {
        for (i = 0; i < p->nmaps; i++) if (p->maps[i] == want) return want;
        return -1;
    }
    for (i = 0; i < p->nmaps; i++) if (p->maps[i] == current) return current;
    return p->maps[0];
}

int u9highway_npc_points(unsigned npc, U9HighwayUse *out, int cap)
{
    uint32_t off, len, count, r;
    const unsigned char *e, *p, *end;
    int n = 0;

    if (!g_act || g_actLen < 0x88 || 0x80 + npc * 8 + 8 > g_actLen) return -1;
    off = le32(g_act + 0x80 + npc * 8);
    len = le32(g_act + 0x84 + npc * 8);
    if (!len || off + len > g_actLen || len < 8) return 0;
    e = g_act + off;
    end = e + len;
    count = le32(e);
    p = e + 8;
    for (r = 0; r < count && p + 16 <= end; r++) {
        char name[16];
        memcpy(name, p + 1, 15);
        name[15] = 0;
        p += 16;
        while (p + 9 <= end) {
            unsigned op = p[0], a = le16(p + 1), b2 = le16(p + 3), links[2], nl = 0, k;
            int map = -1;
            p += 9;
            if (op == 0xFF) break;
            if (op == 0x01 || op == 0x02) { links[0] = a; links[1] = b2; nl = 2; }
            else if (op == 0x03)          { links[0] = a; nl = 1; map = (int)b2; }
            for (k = 0; k < nl; k++) {
                int j, seen = 0;
                for (j = 0; j < n; j++) if (out[j].link == links[k]) seen = 1;
                if (seen || n >= cap) continue;
                out[n].link = links[k];
                out[n].relocate = op == 0x03;
                out[n].map = map;
                memcpy(out[n].record, name, sizeof name);
                n++;
            }
        }
    }
    return n;
}
