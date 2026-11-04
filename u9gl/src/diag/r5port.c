#include <windows.h>
#include <string.h>
#include <stdint.h>

#include "u9renderer.h"
#include "../../addresses.h"
#include "../injection/config.h"
#include "log.h"

int u9r5_is_ours(const void *renderer);

#define R5_U8(p, off)   (*(u8      *)((char *)(p) + (off)))
#define R5_U32(p, off)  (*(u32     *)((char *)(p) + (off)))
#define R5_S16(p, off)  (*(int16_t *)((char *)(p) + (off)))
#define R5_F(p, off)    (*(float   *)((char *)(p) + (off)))
#define R5_PTR(p, off)  (*(void   **)((char *)(p) + (off)))
#define R5_STAT(addr)   (*(u32 *)(uintptr_t)(addr))
#define R5_VSLOT(self, i)  ((*(void ***)(self))[(i)])

#define R5_SLOT_SORTED_INSERT 41

typedef void (U9CALL *R5BindMaterialFn)(void *self, void *mat);
typedef void (U9CALL *R5Slot48Fn)(void *self, u32 mode);
typedef void (U9CALL *R5Slot34Fn)(void *self, u32 count, void *verts);
typedef void (U9CALL *R5SortedInsertFn)(void *self, void **root, void *node);

int u9r5_ports(const void *self)
{
    static int enabled = -1;

    if (!u9r5_is_ours(self)) return 0;
    if (enabled < 0) {
        enabled = u9cfg_int("r5_ports", 1) ? 1 : 0;
        u9log_line("R5   ports %s: slots 11, 12, 33 and 37 and the fog class %s",
                   enabled ? "ON" : "off (r5_ports=0)",
                   enabled ? "run our code, not Glide's"
                           : "still call Glide's originals");
    }
    return enabled;
}

static void texsize_fixup(void *self)
{
    void *m = R5_PTR(self, U9_OFF_CUR_MATERIAL);

    if (!m) return;
    if (R5_U32(m, U9_MAT_TEXW) != 0u && R5_U32(m, U9_MAT_TEXH) != 0u) return;

    if (R5_S16(m, U9_MAT_LOG2H) < R5_S16(m, U9_MAT_LOG2W)) {
        int d = (signed char)R5_S16(m, U9_MAT_LOG2H) -
                (signed char)R5_S16(m, U9_MAT_LOG2W);
        R5_U32(m, U9_MAT_TEXW) = 0x100u;
        R5_U32(m, U9_MAT_TEXH) = 1u << (((unsigned)d + 8u) & 0x1Fu);
    } else {
        int d = (signed char)R5_S16(m, U9_MAT_LOG2W) -
                (signed char)R5_S16(m, U9_MAT_LOG2H);
        R5_U32(m, U9_MAT_TEXH) = 0x100u;
        R5_U32(m, U9_MAT_TEXW) = 1u << (((unsigned)d + 8u) & 0x1Fu);
    }
}

void r5_set_material_pooled(void *self, void *mat)
{
    ((R5BindMaterialFn)(uintptr_t)U9_RENDERER_BIND_MATERIAL)(self, mat);
    texsize_fixup(self);
}

void r5_set_material_immediate(void *self, void *mat)
{
    ((R5BindMaterialFn)(uintptr_t)U9_RENDERER_SET_MATERIAL)(self, mat);
    texsize_fixup(self);
}

static void apply_state_once(void *self)
{
    if (!R5_U8(self, U9_OFF_DRAW_LATCH)) {
        ((R5Slot48Fn)R5_VSLOT(self, U9_SLOT_SET_RENDER_STATE))(self, 0u);
        R5_U8(self, U9_OFF_DRAW_LATCH) = 1u;
    }
}

void r5_draw_polygon_list(void *self, u32 count, U9Vertex *verts, float alpha,
                          u32 project)
{
    u32    prim   = R5_U32(self, U9_OFF_PRIM_FLAGS);
    u32    mode   = R5_U32(self, U9_OFF_DRAW_MODE);
    int    n      = (int)count;
    int    pooled = (mode == 0u) && (prim & 0x0Eu) && !(prim & 0x40u);
    float *dst;
    float  zMin;
    int    i;

    if (R5_U8(self, U9_OFF_DISABLED_GUARD)) return;

    if (pooled) {
        u32 used = R5_U32(self, U9_OFF_POOL_VERTS);
        if ((int)R5_U32(self, U9_OFF_BATCH_COUNT) >= U9_R5_MAX_BATCH_RECORDS) return;
        if ((int)(used + count) >= U9_R5_MAX_POOL_VERTS) return;
        R5_U32(self, U9_OFF_POOL_VERTS) = used + count;
        dst = (float *)((char *)self + U9_OFF_VERTEX_POOL + used * 0x3Cu);
    } else {
        static int warned;
        dst = (float *)((char *)self + U9_OFF_SCRATCH_VERTS);
        if (n > 17 && !warned) {
            warned = 1;
            u9log_line("R5   slot 33: an immediate polygon of %d vertices overruns "
                       "the 17-vertex scratch buffer at +0x450150, as Glide's does",
                       n);
        }
    }

    zMin = verts[0].z;
    for (i = 0; i < n; i++) {
        U9Vertex *v   = &verts[i];
        float    *g   = dst + i * GRV_FLOATS;
        double    inv = 1.0 / (double)v->z;
        float     oow;

        if ((u8)project) {
            const char *view = (const char *)R5_PTR(self, U9_OFF_VIEW);
            v->x = (float)((double)*(const float *)(view + 0x08) * inv * v->x +
                           *(const float *)(view + 0xA4));
            v->y = (float)((double)*(const float *)(view + 0xA8) -
                           (double)*(const float *)(view + 0x0C) * inv * v->y);
        }
        g[GRV_X] = (float)(int)(v->x * 16.0f) * 0.0625f;
        g[GRV_Y] = (float)(int)(v->y * 16.0f) * 0.0625f;
        g[GRV_Z] = v->z;
        if (v->z < zMin) zMin = v->z;
        oow = (float)(inv * R5_F(self, U9_OFF_WSCALE));
        g[GRV_OOW] = oow;
        if (prim & 0x01u) {
            g[GRV_R] = v->r;
            g[GRV_G] = v->g;
            g[GRV_B] = v->b;
        }
        g[GRV_A] = (alpha >= 0.0f) ? alpha : v->a;
        if (R5_U8(self, U9_OFF_FOG_ENABLE) && g[GRV_Z] > R5_F(self, U9_OFF_FOG_NEAR)) {
            if (!(g[GRV_Z] >= R5_F(self, U9_OFF_FOG_FAR)))
                g[GRV_A] = ((R5_F(self, U9_OFF_FOG_FAR) - g[GRV_Z]) /
                            R5_F(self, U9_OFF_FOG_RANGE)) * g[GRV_A];
            else
                g[GRV_A] = 0.0f;
        }
        if (prim & 0x20u) {
            const char *m = (const char *)R5_PTR(self, U9_OFF_CUR_MATERIAL);
            int tmus = (int)R5_U32(self, U9_OFF_TMU_COUNT), t;
            if (m) {
                float sow = (float)R5_U32(m, U9_MAT_TEXW) * v->s * oow;
                float tow = (float)R5_U32(m, U9_MAT_TEXH) * v->t * oow;
                for (t = 0; t < tmus; t++) {
                    g[GRV_TMU0_SOW + 3 * t] = sow;
                    g[GRV_TMU0_TOW + 3 * t] = tow;
                    g[GRV_TMU0_OOW + 3 * t] = oow;
                }
            }
        }
    }

    if (mode != 0u) {
        apply_state_once(self);
        if (mode == 2u && n > 0) {
            u32 c = R5_U32(self, U9_OFF_LINE_COLOUR);
            dst[GRV_R] = (float)((c >> 16) & 0xFFu);
            dst[GRV_G] = (float)((c >>  8) & 0xFFu);
            dst[GRV_B] = (float)( c        & 0xFFu);
            for (i = 1; i < n; i++) {
                float *p = dst + (i - 1) * GRV_FLOATS, *q = dst + i * GRV_FLOATS;
                q[GRV_R] = p[GRV_R];
                q[GRV_G] = p[GRV_G];
                q[GRV_B] = p[GRV_B];
            }
        }
        R5_STAT(U9_STAT_VERTICES)  += count;
        R5_STAT(U9_STAT_TRIANGLES) += 1u;
        return;
    }

    if (pooled) {
        int   bucket = (int)(R5_F(self, U9_OFF_BUCKET_SCALE) * zMin);
        u32   recIndex, used;
        u32  *rec;
        void *copy;

        if (bucket >= 32) bucket = 31;
        else if (bucket < 0) bucket = 0;
        R5_U32(self, U9_OFF_BUCKET_INDEX) = (u32)bucket;

        recIndex = R5_U32(self, U9_OFF_BATCH_COUNT);
        R5_U32(self, U9_OFF_BATCH_COUNT) = recIndex + 1u;
        rec = (u32 *)((char *)self + U9_OFF_BATCH_RECORDS + recIndex * 0x18u);
        rec[1] = 0u;
        rec[0] = 0u;
        used   = R5_U32(self, U9_OFF_POOL_VERTS);
        copy   = (char *)self + U9_OFF_VERTEX_POOL + used * 0x3Cu;
        rec[4] = count;
        rec[5] = (u32)(uintptr_t)copy;
        memcpy(copy, dst, count * 0x3Cu);
        memcpy(&rec[2], &zMin, sizeof zMin);
        ((R5SortedInsertFn)R5_VSLOT(self, R5_SLOT_SORTED_INSERT))(
            self, (void **)((char *)self + U9_OFF_BUCKETS + (u32)bucket * 4u), rec);
        rec[3] = R5_U32(self, U9_OFF_CUR_MATERIAL);
        R5_U32(self, U9_OFF_POOL_VERTS) = used + count;
        R5_STAT(U9_STAT_VERTICES) += count;
        return;
    }

    apply_state_once(self);
    R5_STAT(U9_STAT_VERTICES)  += count;
    R5_STAT(U9_STAT_TRIANGLES) += 1u;

    if ((prim & 0x10u) &&
        (int)R5_U32(self, U9_OFF_LIST_COUNT) < U9_R5_MAX_LIST_NODES &&
        (int)(count + R5_U32(self, U9_OFF_POOL_VERTS)) < U9_R5_MAX_POOL_VERTS) {
        u32   k = R5_U32(self, U9_OFF_LIST_COUNT), used;
        u32  *node;
        void *copy;

        for (i = 0; i < n; i++) {
            float *g = dst + i * GRV_FLOATS;
            g[GRV_R] = verts[i].r2;
            g[GRV_G] = verts[i].g2;
            g[GRV_B] = verts[i].b2;
        }
        node = (u32 *)((char *)self + U9_DTOR_ARRAY_OFF + k * 0x10u);
        R5_U32(self, U9_OFF_LIST_COUNT) = k + 1u;
        node[1] = R5_U32(self, U9_OFF_LIST_HEAD);
        R5_U32(self, U9_OFF_LIST_HEAD) = (u32)(uintptr_t)node;
        node[2] = count;
        used    = R5_U32(self, U9_OFF_POOL_VERTS);
        copy    = (char *)self + U9_OFF_VERTEX_POOL + used * 0x3Cu;
        node[3] = (u32)(uintptr_t)copy;
        memcpy(copy, dst, count * 0x3Cu);
        R5_U32(self, U9_OFF_POOL_VERTS) = used + count;
        R5_STAT(U9_STAT_VERTICES) += count;
    }
}

void r5_flush_list_pool(void *self)
{
    u32 *node = (u32 *)(uintptr_t)R5_U32(self, U9_OFF_LIST_HEAD);

    if (node) {
        ((R5Slot48Fn)R5_VSLOT(self, U9_SLOT_SET_RENDER_STATE))(self, 1u);
        do {
            ((R5Slot34Fn)R5_VSLOT(self, U9_SLOT_DRAW_POLYGON))(
                self, node[2], (void *)(uintptr_t)node[3]);
            R5_STAT(U9_STAT_TRIANGLES) += 1u;
            node = (u32 *)(uintptr_t)node[1];
        } while (node);
    }
    R5_U32(self, U9_OFF_LIST_HEAD) = 0u;
}

static void U9CALL r5_fog_set_enabled(void *fog, u32 on)
{
    if (R5_U8(fog, U9_FOGOBJ_SUSPENDED)) return;
    if (on == 0u) {
        if (R5_U32(fog, U9_FOGOBJ_ENABLED) != 0u)
            R5_U32(fog, U9_FOGOBJ_ENABLED) = 0u;
    } else if (R5_U32(fog, U9_FOGOBJ_ENABLED) == 0u) {
        R5_U32(fog, U9_FOGOBJ_ENABLED) = 1u;
    }
}

static void U9CALL r5_fog_disable(void *fog)
{
    if (!R5_U8(fog, U9_FOGOBJ_SUSPENDED) && R5_U32(fog, U9_FOGOBJ_ENABLED) != 0u)
        R5_U32(fog, U9_FOGOBJ_ENABLED) = 0u;
}

static void U9CALL r5_fog_update(void *fog, u32 colour, u32 table)
{
    (void)fog; (void)colour; (void)table;
}

static void U9CALL r5_fog_set_mode(void *fog, u32 what)
{
    R5_U32(fog, U9_FOGOBJ_MODE) = what;
}

static void *g_fogVtbl[U9_FOG_VTBL_SLOTS];

void r5_fog_adopt(void *fog)
{
    static int built;

    if (!fog) return;
    if (!built) {
        memcpy(g_fogVtbl, (const void *)(uintptr_t)U9_VTBL_FOG_GLIDE, sizeof g_fogVtbl);
        g_fogVtbl[6] = (void *)r5_fog_set_enabled;
        g_fogVtbl[7] = (void *)r5_fog_disable;
        g_fogVtbl[8] = (void *)r5_fog_update;
        g_fogVtbl[9] = (void *)r5_fog_set_mode;
        built = 1;
    }
    *(void ***)fog = g_fogVtbl;
    u9log_line("R5   fog vtable ours: object 0x%08X on 0x%08X, a copy of "
               "vtbl_Fog_Glide 0x%08X with slots 6-9 state-only",
               (unsigned)(uintptr_t)fog, (unsigned)(uintptr_t)g_fogVtbl,
               U9_VTBL_FOG_GLIDE);
}
