#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../../addresses.h"
#include "log.h"
#include "objstats.h"

#define CELLS_MAX      65536u
#define LIST_MAX       4096u
#define OBJECTS_MAX    65536u

static U9ObjStats g_s;
static int        g_logged;

static uint16_t   g_typeCount[65536];
static uint16_t   g_touched[OBJECTS_MAX];
static unsigned   g_touchedN;

static int readable(const void *p, unsigned bytes)
{
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char *at = (const unsigned char *)p, *end = at + bytes;
    if (!p) return 0;
    while (at < end) {
        if (!VirtualQuery(at, &mbi, sizeof mbi)) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        at = (const unsigned char *)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static uint32_t rd(const void *p) { return *(const uint32_t *)p; }

static unsigned list_len(const unsigned char *list, unsigned *meshes, unsigned *insts)
{
    const unsigned char *node;
    unsigned n = 0;
    if (!readable(list, 0x0C)) return 0;
    node = (const unsigned char *)(uintptr_t)rd(list + U9_LIST_HEAD);
    while (node && n < LIST_MAX) {
        if (!readable(node, 0x24)) break;
        if (meshes) {
            int m = (int)rd(node + U9_QLINK_MESHES), i = (int)rd(node + U9_QLINK_INSTANCES);
            if (m > 0 && m < 100000) *meshes += (unsigned)m;
            if (i > 0 && i < 100000) *insts  += (unsigned)i;
        }
        n++;
        node = (const unsigned char *)(uintptr_t)rd(node + U9_LINK_NEXT);
    }
    return n;
}

static void count_type(unsigned id)
{
    if (g_typeCount[id] == 0) {
        if (g_touchedN >= OBJECTS_MAX) return;
        g_touched[g_touchedN++] = (uint16_t)id;
    }
    if (g_typeCount[id] != 0xFFFF) g_typeCount[id]++;
}

static void sample_area(U9ObjStats *s)
{
    const unsigned char *area, *cells, *cell, *mgr = NULL;
    const unsigned char *fixBase = NULL, *nonBase = NULL;
    unsigned fixUsed = 0, nonUsed = 0, i, n;

    if (!readable((const void *)(uintptr_t)U9_AREA_LIST_OWNER, 4)) return;
    area = (const unsigned char *)(uintptr_t)rd((const void *)(uintptr_t)U9_AREA_LIST_OWNER);
    if (!area || !readable(area, 0xDC)) return;
    s->valid = 1;

    s->gridW = rd(area + U9_AREA_GRID_W);
    s->gridH = rd(area + U9_AREA_GRID_H);
    s->lruCells = rd(area + U9_AREA_LRU_COUNT);
    s->terrainA = rd(area + U9_AREA_TMESH_COUNT_A);
    s->terrainB = rd(area + U9_AREA_TMESH_COUNT_B);
    s->terrainMeshes  = s->terrainA + s->terrainB;
    s->terrainMeshMax = rd(area + U9_AREA_TMESH_MAX);
    for (i = 0; i < 3; i++) {
        s->ring[i]  = list_len(area + U9_AREA_RINGS + i * U9_AREA_RING_STRIDE, NULL, NULL);
        s->queue[i] = list_len(area + U9_AREA_QUEUES + i * U9_AREA_RING_STRIDE,
                               &s->pendingMeshes, &s->pendingInstances);
    }

    cells = (const unsigned char *)(uintptr_t)rd(area + U9_AREA_CELLS);
    n = s->gridW * s->gridH;
    if (cells && s->gridW && s->gridH && n <= CELLS_MAX &&
        readable(cells, n * U9_AREACELL_STRIDE)) {
        for (i = 0; i < n; i++) {
            uint32_t f = rd(cells + i * U9_AREACELL_STRIDE + U9_AREACELL_FLAGS);
            if (f & 1u) s->cellsDrawn++;
            if (f & 2u) s->cellsActive++;
            if (f & 4u) s->cellsCollide++;
            if (f & 8u) s->cellsQueued++;
        }
    }

    if (readable((const void *)(uintptr_t)U9_CUR_ITEM_MGR, 4))
        mgr = (const unsigned char *)(uintptr_t)rd((const void *)(uintptr_t)U9_CUR_ITEM_MGR);
    if (mgr && readable(mgr, U9_ITEMMGR_NONFIXED + 0x0C)) {
        fixBase = (const unsigned char *)(uintptr_t)rd(mgr + U9_ITEMMGR_FIXED);
        fixUsed = rd(mgr + U9_ITEMMGR_FIXED + 4);
        s->fixedUsed = fixUsed;
        s->fixedMax  = rd(mgr + U9_ITEMMGR_FIXED + 8);
        nonBase = (const unsigned char *)(uintptr_t)rd(mgr + U9_ITEMMGR_NONFIXED);
        nonUsed = rd(mgr + U9_ITEMMGR_NONFIXED + 4);
        s->nonfixedUsed = nonUsed;
        s->nonfixedMax  = rd(mgr + U9_ITEMMGR_NONFIXED + 8);
        if (fixBase && !readable(fixBase, fixUsed)) fixBase = NULL;
        if (nonBase && !readable(nonBase, nonUsed)) nonBase = NULL;
    }

    cell = (const unsigned char *)(uintptr_t)rd(area + U9_AREA_LIST_HEAD);
    while (cell && s->cellsWithSlots < CELLS_MAX) {
        static uint8_t freeMark[U9_AREA_SLOTS_ALLOC];
        const unsigned char *slots;
        unsigned idx, walked = 0, live, k, cellDrawn, cellMoving;

        if (!readable(cell, U9_AREACELL_STRIDE)) break;
        s->cellsWithSlots++;
        live = rd(cell + U9_AREACELL_COUNT);
        cellDrawn = s->drawn; cellMoving = s->moving;
        if (live <= U9_AREA_SLOTS_USABLE) {
            s->objects += live;
            if (live > s->worstCell) s->worstCell = live;
        }
        slots = (const unsigned char *)(uintptr_t)rd(cell + U9_AREACELL_SLOTS);
        if (slots && readable(slots, U9_AREA_SLOTS_ALLOC * U9_AREA_SLOT_STRIDE)) {
            memset(freeMark, 0, sizeof freeMark);
            idx = rd(cell + U9_AREACELL_FREEHEAD);
            while (idx >= 1 && idx <= U9_AREA_SLOTS_USABLE && walked <= U9_AREA_SLOTS_USABLE) {
                freeMark[idx] = 1;
                walked++;
                idx = rd(slots + idx * U9_AREA_SLOT_STRIDE);
            }
            for (k = 1; k <= U9_AREA_SLOTS_USABLE; k++) {
                const unsigned char *slot = slots + k * U9_AREA_SLOT_STRIDE;
                int32_t off;
                const unsigned char *obj = NULL;
                if (freeMark[k]) continue;
                if (rd(slot + U9_SLOT_INSTANCE)) s->drawn++;
                if (rd(slot + U9_SLOT_MOVER))    s->moving++;
                off = (int32_t)rd(slot + U9_SLOT_OBJECT);
                if (off < 0) {
                    s->fixed++;
                    if (fixBase && (uint32_t)(-off) + U9_OBJ_TYPE + 2 <= fixUsed)
                        obj = fixBase + (uint32_t)(-off);
                } else {
                    s->nonfixed++;
                    if (nonBase && (uint32_t)off + U9_OBJ_TYPE + 2 <= nonUsed)
                        obj = nonBase + (uint32_t)off;
                }
                if (obj) count_type(*(const uint16_t *)(obj + U9_OBJ_TYPE));
                else     s->unresolved++;
            }
        }
        if (cells && n && cell >= cells && cell < cells + n * U9_AREACELL_STRIDE &&
            (unsigned)(cell - cells) % U9_AREACELL_STRIDE == 0) {
            if (s->cellCount < U9_OBJSTATS_CELLS) {
                U9ObjCell *c = &s->cell[s->cellCount++];
                unsigned at = (unsigned)(cell - cells) / U9_AREACELL_STRIDE;
                c->x = (uint16_t)(at % s->gridW);
                c->y = (uint16_t)(at / s->gridW);
                c->objects = (uint16_t)(live <= U9_AREA_SLOTS_USABLE ? live : 0);
                c->drawn = (uint16_t)(s->drawn - cellDrawn);
                c->moving = (uint16_t)(s->moving - cellMoving);
                c->flags = (uint16_t)(rd(cell + U9_AREACELL_FLAGS) & 0xFFu);
            }
        } else {
            s->cellsOutside++;
        }
        cell = (const unsigned char *)(uintptr_t)rd(cell + U9_AREACELL_NEXT);
    }
    s->slotBytes = s->cellsWithSlots * U9_AREA_SLOTS_ALLOC * U9_AREA_SLOT_STRIDE;

    s->uniqueTypes = g_touchedN;
    for (i = 0; i < g_touchedN; i++) {
        unsigned id = g_touched[i], c = g_typeCount[id], j;
        for (j = 0; j < 3; j++) {
            if (c > s->topCount[j]) {
                unsigned m;
                for (m = 2; m > j; m--) { s->topCount[m] = s->topCount[m-1]; s->topType[m] = s->topType[m-1]; }
                s->topCount[j] = c; s->topType[j] = id;
                break;
            }
        }
        g_typeCount[id] = 0;
    }
    g_touchedN = 0;
}

static void sample_meshes(U9ObjStats *s)
{
    const unsigned char *table = (const unsigned char *)(uintptr_t)U9_MESH_TABLE;
    const uint32_t *sizes = (const uint32_t *)(uintptr_t)U9_MESH_SIZES;
    const unsigned char *pool = NULL;
    uint32_t poolBase = 0, poolSize = 0;
    unsigned i;

    if (!readable(table, U9_MESH_ENTRIES * U9_MESH_ENTRY) ||
        !readable(sizes, U9_MESH_ENTRIES * 4)) return;

    if (readable((const void *)(uintptr_t)U9_MESH_POOL, 4))
        pool = (const unsigned char *)(uintptr_t)rd((const void *)(uintptr_t)U9_MESH_POOL);
    if (pool && readable(pool, 0x24)) {
        poolBase = rd(pool);
        poolSize = rd(pool + 4);
        s->poolSize = poolSize;
        s->poolLargestFree = rd(pool + 0x20);
    }

    for (i = 0; i < U9_MESH_ENTRIES; i++) {
        const unsigned char *e = table + i * U9_MESH_ENTRY;
        uint32_t ptr = rd(e), size;
        int32_t users;
        if (!ptr) continue;
        users = (int32_t)rd(e + 4);
        size = sizes[i];
        s->meshTypes++;
        if (users >= 1) {
            s->meshInUse++;
            s->meshUsers += (unsigned)users;
            if ((unsigned)users > s->meshMaxUsers) { s->meshMaxUsers = (unsigned)users; s->meshMaxAppearance = i; }
        } else {
            s->meshIdle++;
        }
        {
            int inPool = poolSize && ptr >= poolBase && ptr - poolBase < poolSize;
            if (inPool) s->meshBytesPool += size;
            else        s->meshBytesOutside += size;
            if (users >= 1) {
                unsigned n = s->meshListCount, k;
                if (n < U9_OBJSTATS_MESHES || (unsigned)users > s->meshList[n - 1].users ||
                    ((unsigned)users == s->meshList[n - 1].users && size > s->meshList[n - 1].size)) {
                    k = n < U9_OBJSTATS_MESHES ? s->meshListCount++ : U9_OBJSTATS_MESHES - 1;
                    while (k > 0 && (s->meshList[k - 1].users < (unsigned)users ||
                           (s->meshList[k - 1].users == (unsigned)users && s->meshList[k - 1].size < size))) {
                        s->meshList[k] = s->meshList[k - 1]; k--;
                    }
                    s->meshList[k].appearance = i; s->meshList[k].users = (unsigned)users;
                    s->meshList[k].size = size; s->meshList[k].inPool = (unsigned)inPool;
                }
            }
        }
    }

    s->engineMeshTypes = rd((const void *)(uintptr_t)U9_STAT_MESH_TYPES);
    s->engineMeshBytes = rd((const void *)(uintptr_t)U9_STAT_MESH_BYTES);
    s->idleKeep        = rd((const void *)(uintptr_t)U9_MESH_IDLE_KEEP);
    s->inCamera        = rd((const void *)(uintptr_t)U9_STAT_IN_CAMERA);
}

void u9objstats_sample(void)
{
    LARGE_INTEGER f, a, b;
    U9ObjStats s;

    memset(&s, 0, sizeof s);
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    sample_area(&s);
    sample_meshes(&s);
    QueryPerformanceCounter(&b);
    s.sampleMs = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    g_s = s;

    if (!g_logged && s.valid && s.objects) {
        g_logged = 1;
        u9log_line("OBJSTATS first sample (%.2f ms): grid %ux%u, cells drawn/active/collide/queued "
                   "%u/%u/%u/%u, %u with slots, lru %u; rings %u/%u/%u; terrain meshes %u+%u=%u/%u; "
                   "queues %u/%u/%u pending meshes %u instances %u",
                   s.sampleMs, s.gridW, s.gridH, s.cellsDrawn, s.cellsActive, s.cellsCollide,
                   s.cellsQueued, s.cellsWithSlots, s.lruCells, s.ring[0], s.ring[1], s.ring[2],
                   s.terrainA, s.terrainB, s.terrainMeshes, s.terrainMeshMax, s.queue[0], s.queue[1], s.queue[2],
                   s.pendingMeshes, s.pendingInstances);
        u9log_line("OBJSTATS objects %u (cells' own counts) vs %u slots walked (%u fixed + %u nonfixed), "
                   "%u unresolved; drawn %u, moving %u; %u unique types, top %u x%u, %u x%u, %u x%u; "
                   "in camera %u",
                   s.objects, s.fixed + s.nonfixed, s.fixed, s.nonfixed, s.unresolved, s.drawn,
                   s.moving, s.uniqueTypes, s.topType[0], s.topCount[0], s.topType[1],
                   s.topCount[1], s.topType[2], s.topCount[2], s.inCamera);
        u9log_line("OBJSTATS meshes %u (engine says %u), %u in use, %u idle, %u users, most shared "
                   "appearance %u x%u; bytes pool %u + outside %u = %u (engine says %u); pool %u, "
                   "largest free %u; idle keep %u; items fixed %u/%u nonfixed %u/%u",
                   s.meshTypes, s.engineMeshTypes, s.meshInUse, s.meshIdle, s.meshUsers,
                   s.meshMaxAppearance, s.meshMaxUsers, s.meshBytesPool, s.meshBytesOutside,
                   s.meshBytesPool + s.meshBytesOutside, s.engineMeshBytes, s.poolSize,
                   s.poolLargestFree, s.idleKeep, s.fixedUsed, s.fixedMax, s.nonfixedUsed,
                   s.nonfixedMax);
    }
}

const U9ObjStats *u9objstats_get(void) { return &g_s; }

unsigned u9area_each_object(U9AreaObjectFn fn, void *ctx)
{
    const unsigned char *area, *cell, *mgr;
    const unsigned char *fixBase = NULL, *nonBase = NULL;
    unsigned fixUsed = 0, nonUsed = 0, cells = 0, seen = 0;

    if (!readable((const void *)(uintptr_t)U9_AREA_LIST_OWNER, 4)) return 0;
    area = (const unsigned char *)(uintptr_t)rd((const void *)(uintptr_t)U9_AREA_LIST_OWNER);
    if (!area || !readable(area, 0xDC)) return 0;
    if (!readable((const void *)(uintptr_t)U9_CUR_ITEM_MGR, 4)) return 0;
    mgr = (const unsigned char *)(uintptr_t)rd((const void *)(uintptr_t)U9_CUR_ITEM_MGR);
    if (!mgr || !readable(mgr, U9_ITEMMGR_NONFIXED + 0x0C)) return 0;
    fixBase = (const unsigned char *)(uintptr_t)rd(mgr + U9_ITEMMGR_FIXED);
    fixUsed = rd(mgr + U9_ITEMMGR_FIXED + 4);
    nonBase = (const unsigned char *)(uintptr_t)rd(mgr + U9_ITEMMGR_NONFIXED);
    nonUsed = rd(mgr + U9_ITEMMGR_NONFIXED + 4);
    if (fixBase && !readable(fixBase, fixUsed)) fixBase = NULL;
    if (nonBase && !readable(nonBase, nonUsed)) nonBase = NULL;

    cell = (const unsigned char *)(uintptr_t)rd(area + U9_AREA_LIST_HEAD);
    while (cell && cells++ < CELLS_MAX) {
        static uint8_t freeMark[U9_AREA_SLOTS_ALLOC];
        const unsigned char *slots;
        unsigned idx, walked = 0, k;

        if (!readable(cell, U9_AREACELL_STRIDE)) break;
        slots = (const unsigned char *)(uintptr_t)rd(cell + U9_AREACELL_SLOTS);
        if (slots && readable(slots, U9_AREA_SLOTS_ALLOC * U9_AREA_SLOT_STRIDE)) {
            memset(freeMark, 0, sizeof freeMark);
            idx = rd(cell + U9_AREACELL_FREEHEAD);
            while (idx >= 1 && idx <= U9_AREA_SLOTS_USABLE && walked <= U9_AREA_SLOTS_USABLE) {
                freeMark[idx] = 1;
                walked++;
                idx = rd(slots + idx * U9_AREA_SLOT_STRIDE);
            }
            for (k = 1; k <= U9_AREA_SLOTS_USABLE; k++) {
                const unsigned char *slot = slots + k * U9_AREA_SLOT_STRIDE, *obj = NULL;
                int32_t off;
                if (freeMark[k]) continue;
                off = (int32_t)rd(slot + U9_SLOT_OBJECT);
                if (off < 0) {
                    if (fixBase && (uint32_t)(-off) + 0x18u <= fixUsed) obj = fixBase + (uint32_t)(-off);
                } else {
                    if (nonBase && (uint32_t)off + 0x18u <= nonUsed) obj = nonBase + (uint32_t)off;
                }
                if (!obj) continue;
                seen++;
                fn(obj, off < 0, rd(slot + U9_SLOT_MOVER) != 0, ctx);
            }
        }
        cell = (const unsigned char *)(uintptr_t)rd(cell + U9_AREACELL_NEXT);
    }
    return seen;
}
