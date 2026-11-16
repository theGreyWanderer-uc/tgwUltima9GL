#define U9GL_TEXCACHE_REPORT 4096
int u9cfg_int(const char *key, int def);
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include <windows.h>
#include <stdint.h>

#include "u9renderer.h"
#include "../../addresses.h"
#include "../injection/patch.h"
#include "log.h"
#include "../gl/metrics.h"
#include "../gl/glslots.h"
#include "../gl/glbackground.h"
#include "../fix/npcprobe.h"
#include "../fix/actorwatch.h"
#include "../fix/playerwatch.h"
#include "../fix/meshprobe.h"
#include "../fix/meshstat.h"
#include "../fix/focusprobe.h"
#include "../fix/birdprobe.h"
#include "../fix/portalfix.h"
#include "../fix/lightwatch.h"
#include "../fix/fixes.h"
#include "../fix/flyerfix.h"
#include "furniture.h"
#include "../common/lifecycle.h"
#include "diag.h"

#define FIELD_U32(p, off)  (*(uint32_t *)((char *)(p) + (off)))
#define FIELD_U8(p, off)   (*(uint8_t  *)((char *)(p) + (off)))
#define FIELD_S16(p, off)  (*(int16_t  *)((char *)(p) + (off)))

static void (U9CALL *o_setMipMap)(U9Renderer *, u32);
static void (U9CALL *o_setMatPooled)(U9Renderer *, void *);
static void (U9CALL *o_setMatImmediate)(U9Renderer *, void *);
static void *(U9CALL *o_destroy)(U9Renderer *, u32);
static u8   (U9CALL *o_initialize)(U9Renderer *, u32, u32, u32);
static void (U9CALL *o_createDevice)(U9Renderer *, u32, u32, u32);
static void (U9CALL *o_clear)(U9Renderer *, u32, u32);
static void (U9CALL *o_drawTriangle)(U9Renderer *, const U9Vertex *,
                                     const U9Vertex *, const U9Vertex *, float);
static void (U9CALL *o_drawPolygon)(U9Renderer *, u32, GrVertexU9 *);
static u32  (U9CALL *o_testAndDraw)(U9Renderer *, const U9Vertex *, const U9Vertex *, const U9Vertex *, u32);
static void (U9CALL *o_present)(U9Renderer *);
static void (U9CALL *o_setDepthWrite)(U9Renderer *, u32);
static void (U9CALL *o_setRenderState)(U9Renderer *, u32);
static void (U9CALL *o_setDepthMode)(U9Renderer *, u32);
static void (U9CALL *o_disableDepth)(U9Renderer *);
static void (U9CALL *o_drawLine)(U9Renderer *, const U9Vertex *, const U9Vertex *, u32);
static void (U9CALL *o_drawPolyList)(U9Renderer *, u32, U9Vertex *, float, u32);
static void (U9CALL *o_flushSelected)(U9Renderer *, u32);
static void (U9CALL *o_flushBucketA)(U9Renderer *);
static void (U9CALL *o_flushListPool)(U9Renderer *);
static void (U9CALL *o_flushSorted)(U9Renderer *);
static void (U9CALL *o_flushBucketB)(U9Renderer *);
static void (U9CALL *o_flushLines)(U9Renderer *);

static uint32_t n32, n34, n42, n45, n48, n15;
static uint32_t nFlush;
static uint32_t n31, n33;
static int      g_inFlush;
static int      g_inListPool;
static int      g_flushSlot;
static uint32_t n34InFlush, n32InFlush;
static uint32_t totalTri;
static int      sawAlphaSentinel;
static U9Patch  g_patch;

int  u9factory_tailIntact(void);
void u9gl_probe_all_windows(const char *when);
int  u9gl_context_create(void);
int  u9gl_own_present(void *self);
int  u9gl_is_bound(void *self);
void u9gl_raster_stats(unsigned *tris, unsigned *runs, unsigned *dropped,
                       float *zMin, float *zMax);
void u9gl_tri(const void *renderer,
              const U9Vertex *v0, const U9Vertex *v1, const U9Vertex *v2,
              float alpha, const void *mat);
void u9gl_set_depth_write(int enable);
void u9gl_set_depth_test(int enable);
void u9gl_flush_epoch(void);
void u9gl_set_render_state(unsigned shadow);
void u9gl_poly(const void *renderer, unsigned count, const void *verts,
               const void *mat);
void u9gl_list_pool_pass(int on);
void u9gl_flush_census(const void *renderer, int slot, unsigned count,
                       const void *verts, const void *mat);
void u9gl_note_shadow(unsigned shadow);
void u9gl_probe_texture(const void *renderer, const void *mat);
unsigned u9gl_format_for_material(const void *mat);
unsigned u9gl_format_selector(const void *mat);
void u9gl_prefetch(const void *renderer, const void *mat);
void u9feed_install(void);
void u9gl_untextured_report(void);
void u9gl_offered_report(void);
void u9gl_source(int slot);

int  u9r5_is_ours(const void *renderer);
int  u9r5_ports(const void *self);
void r5_set_material_pooled(void *self, void *mat);
void r5_set_material_immediate(void *self, void *mat);
void r5_draw_polygon_list(void *self, u32 count, U9Vertex *verts, float alpha,
                          u32 project);
void r5_flush_list_pool(void *self);
void r5_fog_adopt(void *fog);


typedef void *(__cdecl  *U9NewFn)(unsigned);
typedef void  (__cdecl  *U9FreeFn)(void *);
typedef void *(U9CALL   *U9FogCtorFn)(void *fog, unsigned arg);
typedef void  (U9CALL   *U9FogDtorFn)(void *fog);
typedef void  (__stdcall *U9ArrayDestroyFn)(void *base, unsigned stride,
                                            unsigned count, void *elemDtor);

static int g_meshHwArmed, g_meshHwReported;

static void mesh_hw_exit(void)
{
    if (!g_meshHwReported) u9hw_counter_report("at exit");
    g_meshHwReported = 1;
    u9hw_disarm();
}

static void mesh_hw_poll(void)
{
    int app;
    if (g_meshHwReported) return;
    if (!g_meshHwArmed) {
        if (u9log_frame() < 600) return;
        g_meshHwArmed = 1;
        app = u9cfg_int("diag_mesh_watch", -1);
        if (app < 0 || app >= 8000 || !u9cfg_int("diag_mesh_watch_hw", 0)) return;
        u9hw_arm_counter(0x00926DF0u + (uintptr_t)app * 12u + 4u,
                         (unsigned)u9cfg_int("diag_mesh_watch_hw_hits", 20000));
        if (u9hw_is_armed()) u9life_push("mesh handle write count", mesh_hw_exit);
        return;
    }
    if (u9hw_counter_done()) {
        g_meshHwReported = 1;
        u9hw_counter_report("limit reached");
    }
}

static int u9own_life(void)   { return u9cfg_int("own_renderer_life", 0); }

static int u9glide_body(const void *fn)
{
    uintptr_t a = (uintptr_t)fn;
    return a >= U9_GLIDE_BODY_LO && a < U9_GLIDE_BODY_HI;
}
#define U9_MAY_CALL(fn) (!(u9own_life() && u9glide_body((const void *)(fn))))

static void own_zero(void *self, unsigned off, unsigned dwords)
{
    unsigned *p = (unsigned *)((char *)self + off);
    unsigned i;
    for (i = 0; i < dwords; i++) p[i] = 0u;
}

static u8 U9CALL own_initialize(U9Renderer *self, u32 a, u32 b, u32 c)
{
    void *fog;

    u9log_line("STAGE5 Initialize: ours. Zeroing %u + %u dwords, no Glide calls.",
               U9_INIT_ZERO_A_DWORDS, U9_INIT_ZERO_B_DWORDS);
    own_zero(self, U9_INIT_ZERO_A_OFF, U9_INIT_ZERO_A_DWORDS);
    own_zero(self, U9_INIT_ZERO_B_OFF, U9_INIT_ZERO_B_DWORDS);

    *((unsigned char *)self + U9_OFF_DISABLED_GUARD) = 0u;

    {
        void **vt = *(void ***)self;
        typedef void (U9CALL *Slot7Fn)(U9Renderer *, u32, u32, u32);
        ((Slot7Fn)vt[7])(self, a, b, c);
    }

    fog = ((U9NewFn)(uintptr_t)U9_OP_NEW)(U9_FOG_ALLOC_BYTES);
    if (!fog) {
        u9log_line("STAGE5 Initialize FAILED: no memory for the fog object");
        return 0;
    }
    ((U9FogCtorFn)(uintptr_t)U9_GLIDE_FOG_CTOR)(fog, 0);
    *(void **)((char *)self + U9_OFF_FOG_OBJECT) = fog;
    if (u9r5_ports(self)) r5_fog_adopt(fog);

    *(void **)((char *)self + U9_OFF_TEXMGR) = 0;

    {
        int count = ShowCursor(FALSE);
        while (count >= 0) count = ShowCursor(FALSE);
        u9log_line("STAGE5 Windows cursor hidden (display count %d), as the DX6"
                   " backend's Initialize does", count);
    }

    u9log_line("STAGE5 Initialize done: fog=0x%08X texmgr=NULL w=%u h=%u bpp=%u",
               (unsigned)(uintptr_t)fog,
               FIELD_U32(self, U9_OFF_WIDTH), FIELD_U32(self, U9_OFF_HEIGHT),
               FIELD_U32(self, U9_OFF_BPP));
    return 1;
}

static void own_teardown(U9Renderer *self)
{
    void *fog = *(void **)((char *)self + U9_OFF_FOG_OBJECT);
    void *mgr = *(void **)((char *)self + U9_OFF_TEXMGR);

    u9log_line("STAGE5 teardown: ours. fog=0x%08X texmgr=0x%08X",
               (unsigned)(uintptr_t)fog, (unsigned)(uintptr_t)mgr);
    u9log_flush();

    if (mgr) {
        typedef void (U9CALL *TexTeardownFn)(void *);
        ((TexTeardownFn)(uintptr_t)U9_GLIDE_TEXCACHE_TEARDOWN)(mgr);
        ((U9FreeFn)(uintptr_t)U9_ENGINE_FREE)(mgr);
        *(void **)((char *)self + U9_OFF_TEXMGR) = 0;
    }
    if (fog) {
        ((U9FogDtorFn)(uintptr_t)U9_GLIDE_FOG_DTOR)(fog);
        ((U9FreeFn)(uintptr_t)U9_ENGINE_FREE)(fog);
        *(void **)((char *)self + U9_OFF_FOG_OBJECT) = 0;
    }
    if (u9r5_is_ours(self))
        u9log_line("R5   teardown: the +0x401F48 elements were never "
                   "constructed on our object, so they are not destroyed");
    else
        ((U9ArrayDestroyFn)(uintptr_t)U9_ARRAY_DESTROY)(
            (char *)self + U9_DTOR_ARRAY_OFF, U9_DTOR_ARRAY_STRIDE,
            U9_DTOR_ARRAY_COUNT, (void *)(uintptr_t)U9_ARRAY_ELEM_DTOR);
    *(void **)self = (void *)(uintptr_t)U9_VTBL_RENDERER_BASE;
    ShowCursor(TRUE);
    u9log_line("STAGE5 teardown: complete, returning to the engine");
    u9log_flush();
}

static void u9stage5_report(void)
{
    HMODULE h = GetModuleHandleA("glide2x.dll");
    if (!u9own_life()) return;
    if (h) u9log_line("STAGE5 glide2x is loaded at 0x%08X - expected, a C++ "
                      "static initialiser does that before DllMain. What "
                      "matters is that we never opened a context: watch the "
                      "METRICS floor slope.", (unsigned)(uintptr_t)h);
    else   u9log_line("STAGE5 glide2x is not even loaded.");
}

static void *U9CALL h_destroy(U9Renderer *self, u32 flags)
{
    if (u9own_life()) {
        own_teardown(self);
        if (flags & 1u) ((U9FreeFn)(uintptr_t)U9_ENGINE_FREE)(self);
        return self;
    }
    return o_destroy(self, flags);
}

static u8 U9CALL h_initialize(U9Renderer *self, u32 a, u32 b, u32 c)
{
    u8 r;

    u9feed_install();

    u9log_line("F %06u slot01 Initialize this=0x%08X a0=0x%08X a1=0x%08X a2=0x%08X",
               u9log_frame(), (unsigned)(uintptr_t)self, a, b, c);
    r = u9own_life() ? own_initialize(self, a, b, c)
                     : o_initialize(self, a, b, c);
    if (u9own_life()) u9stage5_report();
    u9log_line("F %06u slot01 ret=%u  w=%u h=%u bpp=%u", u9log_frame(), (unsigned)r,
               FIELD_U32(self, U9_OFF_WIDTH), FIELD_U32(self, U9_OFF_HEIGHT),
               FIELD_U32(self, U9_OFF_BPP));
    return r;
}

void u9gl_display_open(void);
void u9glslots_gamma_from_options(void *self);

static void U9CALL h_createDevice(U9Renderer *self, u32 w, u32 h, u32 third)
{
    u9log_line("F %06u slot07 CreateDevice this=0x%08X a0=%u a1=%u a2=0x%08X",
               u9log_frame(), (unsigned)(uintptr_t)self, w, h, third);
    if (u9own_life()) {
        FIELD_U32(self, U9_OFF_WIDTH)  = w;
        FIELD_U32(self, U9_OFF_HEIGHT) = h;
        FIELD_U32(self, U9_OFF_BPP)    = U9_CREATEDEVICE_BPP;
        FIELD_U32(self, U9_OFF_DEPTH_WRITE)  = 0u;
        FIELD_U32(self, U9_OFF_STATE_SHADOW) = 0u;
        *((unsigned char *)self + U9_OFF_GLIDE_STATE2) = 0u;
        {
            void **vt = *(void ***)self;
            typedef void (U9CALL *Slot45Fn)(U9Renderer *, u32);
            ((Slot45Fn)vt[45])(self, 1u);
        }
        u9log_line("STAGE5 CreateDevice: ours, %ux%u bpp=%u, depth-write reset"
                   " and SetDepthWrite(1) driven through the vtable",
                   w, h, U9_CREATEDEVICE_BPP);
        u9glslots_gamma_from_options(self);
        u9gl_display_open();
    } else {
        o_createDevice(self, w, h, third);
    }
    u9log_line("F %06u slot07 after  w=%u h=%u bpp=%u", u9log_frame(),
               FIELD_U32(self, U9_OFF_WIDTH), FIELD_U32(self, U9_OFF_HEIGHT),
               FIELD_U32(self, U9_OFF_BPP));
}

static void U9CALL h_clear(U9Renderer *self, u32 alpha, u32 depth)
{
    if (u9log_want_detail(n15))
        u9log_line("F %06u slot15 Clear this=0x%08X a0=0x%08X a1=0x%08X",
                   u9log_frame(), (unsigned)(uintptr_t)self, alpha, depth);
    n15++;
    if (U9_MAY_CALL(o_clear)) o_clear(self, alpha, depth);
}

static void U9CALL h_drawTriangle(U9Renderer *self, const U9Vertex *v0,
                                  const U9Vertex *v1, const U9Vertex *v2,
                                  float alpha)
{
    if (alpha == U9_ALPHA_PER_VERTEX) sawAlphaSentinel = 1;

    if (u9log_want_detail(n32)) {
        u9log_line("F %06u slot32 this=0x%08X v0=0x%08X v1=0x%08X v2=0x%08X alpha=%.3f",
                   u9log_frame(), (unsigned)(uintptr_t)self,
                   (unsigned)(uintptr_t)v0, (unsigned)(uintptr_t)v1,
                   (unsigned)(uintptr_t)v2, alpha);
        u9log_line("F %06u slot32   v0 x=%.3f z=%.3f y=%.3f  rgb=%.2f,%.2f,%.2f a=%.2f s=%.4f t=%.4f",
                   u9log_frame(), v0->x, v0->z, v0->y,
                   v0->r, v0->g, v0->b, v0->a, v0->s, v0->t);
    }
    if (g_inFlush) n32InFlush++;
    n32++; totalTri++;
    if (u9gl_is_bound(self) &&
        FIELD_U32(self, U9_OFF_BATCH_COUNT) <= U9_MAX_BATCH_RECORDS - 1 &&
        FIELD_U32(self, U9_OFF_POOL_VERTS) + 3 <= U9_MAX_POOL_VERTS) {
        u9gl_source(32);
        u9gl_tri(self, v0, v1, v2, alpha,
                 (const void *)FIELD_U32(self, U9_OFF_CUR_MATERIAL));
    }
    if (U9_MAY_CALL(o_drawTriangle)) o_drawTriangle(self, v0, v1, v2, alpha);
}

void u9gl_guard_tri(const void *renderer, const U9Vertex *v0, const U9Vertex *v1, const U9Vertex *v2, unsigned flags);
static u32 U9CALL h_testAndDraw(U9Renderer *self, const U9Vertex *v0, const U9Vertex *v1,
                                const U9Vertex *v2, u32 flags)
{
    if (v0 && v1 && v2 && u9gl_is_bound(self)) u9gl_guard_tri(self, v0, v1, v2, flags & 0xFFu);
    return o_testAndDraw(self, v0, v1, v2, flags);
}

static void U9CALL h_drawPolygon(U9Renderer *self, u32 count, GrVertexU9 *verts)
{
    if (u9log_want_detail(n34))
        u9log_line("F %06u slot34 DrawPolygon this=0x%08X count=%u verts=0x%08X",
                   u9log_frame(), (unsigned)(uintptr_t)self, count,
                   (unsigned)(uintptr_t)verts);
    if (g_inFlush) {
        n34InFlush++;
        if (u9gl_is_bound(self))
            u9gl_flush_census(self, g_flushSlot, count, verts,
                              (const void *)FIELD_U32(self, U9_OFF_CUR_MATERIAL));
    }
    n34++;
    if (u9gl_is_bound(self) && (!g_inFlush || g_inListPool)) {
        u9gl_source(34);
        u9gl_poly(self, count, verts,
                  (const void *)FIELD_U32(self, U9_OFF_CUR_MATERIAL));
    }
    if (U9_MAY_CALL(o_drawPolygon)) o_drawPolygon(self, count, verts);
}

static void U9CALL h_setDepthWrite(U9Renderer *self, u32 enable)
{
    uint32_t before = FIELD_U32(self, U9_OFF_DEPTH_WRITE);
    if (U9_MAY_CALL(o_setDepthWrite)) o_setDepthWrite(self, enable);
    if (u9log_want_detail(n45))
        u9log_line("F %06u slot45 SetDepthWrite this=0x%08X a0=%u  +0x49CE0 %u->%u",
                   u9log_frame(), (unsigned)(uintptr_t)self, enable,
                   before, FIELD_U32(self, U9_OFF_DEPTH_WRITE));
    if (u9gl_is_bound(self))
        u9gl_set_depth_write((int)FIELD_U32(self, U9_OFF_DEPTH_WRITE));
    n45++;
}

static void U9CALL h_setRenderState(U9Renderer *self, u32 mode)
{
    uint32_t stateBefore = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    void    *mat         = (void *)FIELD_U32(self, U9_OFF_CUR_MATERIAL);
    int      detail      = u9log_want_detail(n48);

    if (detail) {
        if (mat)
            u9log_line("F %06u slot48 this=0x%08X a0=%u mat=0x%08X tex=%d flags=0x%08X key=0x%08X state=0x%08X",
                       u9log_frame(), (unsigned)(uintptr_t)self, mode,
                       (unsigned)(uintptr_t)mat,
                       (int)FIELD_S16(mat, U9_MAT_TEXID),
                       FIELD_U32(mat, U9_MAT_FLAGS),
                       FIELD_U32(mat, U9_MAT_CHROMAKEY), stateBefore);
        else
            u9log_line("F %06u slot48 this=0x%08X a0=%u mat=NULL state=0x%08X",
                       u9log_frame(), (unsigned)(uintptr_t)self, mode, stateBefore);
    }
    n48++;
    if (U9_MAY_CALL(o_setRenderState)) {
        o_setRenderState(self, mode);
    } else {
        void *fog = *(void **)((char *)self + U9_OFF_FOG_OBJECT);
        if (fog) {
            void **vt = *(void ***)fog;
            typedef void (U9CALL *FogSlot9Fn)(void *fog, unsigned what);
            ((FogSlot9Fn)vt[9])(fog, mode ? 2u : 1u);
        }
    }
    if (detail)
        u9log_line("F %06u slot48   state 0x%08X->0x%08X", u9log_frame(),
                   stateBefore, FIELD_U32(self, U9_OFF_STATE_SHADOW));
    if (u9gl_is_bound(self)) {
        u9gl_set_render_state(FIELD_U32(self, U9_OFF_STATE_SHADOW));
        u9gl_note_shadow(FIELD_U32(self, U9_OFF_STATE_SHADOW));
        u9gl_probe_texture(self, mat);
        u9gl_prefetch(self, mat);
        if (mat) {
            const char *mgr = *(const char *const *)
                              ((const char *)self + U9_OFF_TEXMGR);
            if (mgr) {
                const char *rec = *(const char *const *)(mgr + U9_TEXMGR_CURRENT);
                if (rec && FIELD_S16(mat, U9_MAT_TEXID) >= 0) {
                    unsigned actual = *(const unsigned *)(rec + U9_TEXREC_FORMAT);
                    {
                        static unsigned disagree;
                        unsigned derived = u9gl_format_for_material(mat);
                        unsigned mflags  = FIELD_U32(mat, U9_MAT_FLAGS);
                        if ((mflags & 0x40u) && derived != actual && disagree < 8) {
                            disagree++;
                            u9log_line("GLTEX *** DERIVATION WRONG mat=0x%08X tex=%d "
                                       "flags=0x%08X selector=%d derived=0x%02X "
                                       "actual=0x%02X  (selector -> actual is the "
                                       "mapping we want)",
                                       (unsigned)(uintptr_t)mat,
                                       (int)FIELD_S16(mat, U9_MAT_TEXID),
                                       FIELD_U32(mat, U9_MAT_FLAGS),
                                       (int)u9gl_format_selector(mat),
                                       derived, actual);
                        }
                    }
                }
            }
        }
    }
}

#define FLUSH_TRACE_FRAMES 6

static int flush_trace(void) { return u9log_frame() < FLUSH_TRACE_FRAMES; }

static void flush_log(const char *name, U9Renderer *self, uint32_t before,
                      uint32_t arg, int hasArg)
{
    uint32_t after = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    const char *note = (before != after) ? "  *** STATE CHANGED INSIDE FLUSH ***" : "";
    if (hasArg)
        u9log_line("F %06u FLUSH %s(0x%X) n32=%u n48=%u state 0x%08X->0x%08X%s",
                   u9log_frame(), name, arg, n32, n48, before, after, note);
    else
        u9log_line("F %06u FLUSH %s n32=%u n48=%u state 0x%08X->0x%08X%s",
                   u9log_frame(), name, n32, n48, before, after, note);
}

static void U9CALL h_flushSelected(U9Renderer *self, u32 mask)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int prevSlot = g_flushSlot;
        g_flushSlot = 35;
        if (U9_MAY_CALL(o_flushSelected)) o_flushSelected(self, mask);
        g_flushSlot = prevSlot;
    }
    if (u9gl_is_bound(self)) u9gl_flush_epoch();
    g_inFlush--;
    nFlush++;
    if (flush_trace()) {
        flush_log("FlushSelected", self, before, mask, 1);
        u9log_line("F %06u FLUSH FlushSelected returns to 0x%08X",
                   u9log_frame(), (unsigned)(uintptr_t)__builtin_return_address(0));
    }
}

static void U9CALL h_flushBucketA(U9Renderer *self)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int prevSlot = g_flushSlot;
        g_flushSlot = 36;
        if (U9_MAY_CALL(o_flushBucketA)) o_flushBucketA(self);
        g_flushSlot = prevSlot;
    }
    g_inFlush--;
    nFlush++;
    if (flush_trace()) flush_log("FlushBucketPoolA", self, before, 0, 0);
}

static void U9CALL h_flushListPool(U9Renderer *self)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int pass = u9gl_is_bound(self) && u9cfg_int("list_pool_pass", 1);
        int prevSlot = g_flushSlot;
        if (pass) { g_inListPool = 1; u9gl_list_pool_pass(1); }
        g_flushSlot = 37;
        if (u9r5_ports(self)) r5_flush_list_pool(self);
        else                  o_flushListPool(self);
        g_flushSlot = prevSlot;
        if (pass) { g_inListPool = 0; u9gl_list_pool_pass(0); }
    }
    g_inFlush--;
    nFlush++;
    if (flush_trace()) flush_log("FlushListPool", self, before, 0, 0);
}

static void U9CALL h_flushSorted(U9Renderer *self)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int prevSlot = g_flushSlot;
        g_flushSlot = 38;
        if (U9_MAY_CALL(o_flushSorted)) o_flushSorted(self);
        g_flushSlot = prevSlot;
    }
    g_inFlush--;
    nFlush++;
    if (flush_trace()) flush_log("FlushSortedBatches", self, before, 0, 0);
}

static void U9CALL h_flushBucketB(U9Renderer *self)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int prevSlot = g_flushSlot;
        g_flushSlot = 39;
        if (U9_MAY_CALL(o_flushBucketB)) o_flushBucketB(self);
        g_flushSlot = prevSlot;
    }
    g_inFlush--;
    nFlush++;
    if (flush_trace()) flush_log("FlushBucketPoolB", self, before, 0, 0);
}

static void U9CALL h_flushLines(U9Renderer *self)
{
    uint32_t before = FIELD_U32(self, U9_OFF_STATE_SHADOW);
    g_inFlush++;
    {
        int prevSlot = g_flushSlot;
        g_flushSlot = 40;
        if (U9_MAY_CALL(o_flushLines)) o_flushLines(self);
        g_flushSlot = prevSlot;
    }
    g_inFlush--;
    nFlush++;
    if (flush_trace()) flush_log("FlushLines", self, before, 0, 0);
}

static void U9CALL h_setDepthMode(U9Renderer *self, u32 mode)
{
    static unsigned n;
    if (n < 8) { n++; u9log_line("F %06u slot43 SetDepthMode(%u)", u9log_frame(), mode); }
    if (U9_MAY_CALL(o_setDepthMode)) o_setDepthMode(self, mode);
    if (u9gl_is_bound(self)) u9gl_set_depth_test(mode != 0);
}

static void U9CALL h_disableDepth(U9Renderer *self)
{
    static unsigned n;
    if (n < 8) { n++; u9log_line("F %06u slot44 DisableDepthBuffer", u9log_frame()); }
    if (U9_MAY_CALL(o_disableDepth)) o_disableDepth(self);
    if (u9gl_is_bound(self)) u9gl_set_depth_test(0);
}

static void U9CALL h_drawLine(U9Renderer *self, const U9Vertex *v0,
                              const U9Vertex *v1, u32 packedColour)
{
    n31++;
    if (U9_MAY_CALL(o_drawLine)) o_drawLine(self, v0, v1, packedColour);
}

static void U9CALL h_drawPolyList(U9Renderer *self, u32 count, U9Vertex *verts,
                                  float alpha, u32 project)
{
    {
        static unsigned logged33;
        if (logged33 < 8) {
            logged33++;
            u9log_line("F %06u slot33 DrawPolygonList count=%u verts=0x%08X "
                       "alpha=%.3f project=%u",
                       u9log_frame(), count, (unsigned)(uintptr_t)verts,
                       alpha, project);
        }
    }
    n33++;

    {
        static unsigned logged;
        float bx = 0.0f, by = 0.0f;
        int   look = verts && count > 0 && logged < 10;
        if (look) { bx = verts[0].x; by = verts[0].y; }

        if (u9r5_ports(self))
            r5_draw_polygon_list(self, count, verts, alpha, project);
        else
            o_drawPolyList(self, count, verts, alpha, project);

        if (look) {
            logged++;
            u9log_line("F %06u slot33 v0 before (%.1f, %.1f) -> after (%.1f, %.1f) "
                       "project=%u  %s",
                       u9log_frame(), bx, by, verts[0].x, verts[0].y, project,
                       (verts[0].x < -100.0f || verts[0].x > 4000.0f)
                           ? "*** OFF-SCREEN ***" : "");
        }
    }

    if (u9gl_is_bound(self) && verts && count >= 3 &&
        FIELD_U32(self, U9_OFF_BATCH_COUNT) <= U9_MAX_BATCH_RECORDS - 1 &&
        FIELD_U32(self, U9_OFF_POOL_VERTS) + count <= U9_MAX_POOL_VERTS) {
        const U9Vertex *v   = verts;
        const void     *mat = (const void *)FIELD_U32(self, U9_OFF_CUR_MATERIAL);
        u32 i;
        u9gl_source(33);
        for (i = 1; i + 1 < count; i++)
            u9gl_tri(self, &v[0], &v[i], &v[i + 1], alpha, mat);
    }
}

void u9gl_tex_stats(unsigned *cacheCount, unsigned *uploads, unsigned *dupes,
                    unsigned *pending, unsigned *mb, unsigned *evictions);
void u9crash_report_tally(void);


static int addr_readable(const void *p, unsigned bytes)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return ((const unsigned char *)p + bytes) <=
           ((const unsigned char *)mbi.BaseAddress + mbi.RegionSize);
}

#define U9_HANDLE_SCAN_MAX 65536u

typedef struct { unsigned short type; unsigned n, w0, lo, hi; } HandleGroup;

typedef struct { const unsigned char *lo, *hi; int ok; unsigned queries; } ScanRegion;

static int scan_readable(ScanRegion *c, const void *p, unsigned bytes)
{
    uintptr_t a = (uintptr_t)p, end = a + bytes;
    MEMORY_BASIC_INFORMATION mbi;

    if (!a || end < a) return 0;
    if (c->hi && a >= (uintptr_t)c->lo && end <= (uintptr_t)c->hi) return c->ok;
    c->queries++;
    if (!VirtualQuery(p, &mbi, sizeof mbi)) { c->lo = c->hi = NULL; return 0; }
    c->lo = (const unsigned char *)mbi.BaseAddress;
    c->hi = c->lo + mbi.RegionSize;
    c->ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    return c->ok && end <= (uintptr_t)c->hi;
}

#define SCAN_MB_SLOTS 4096u
static ScanRegion g_scanByMB[SCAN_MB_SLOTS];

static int scan_readable_mb(const void *p, unsigned bytes, unsigned *queries)
{
    ScanRegion *r = &g_scanByMB[((uintptr_t)p >> 20) & (SCAN_MB_SLOTS - 1u)];
    unsigned before = r->queries;
    int ok = scan_readable(r, p, bytes);

    *queries += r->queries - before;
    return ok;
}

static void log_handle_table(void)
{
    const unsigned char *base;
    unsigned i, live = 0, dead = 0, unreadable = 0;
    HandleGroup seen[64];
    unsigned nseen = 0, other = 0;
    ScanRegion tabRegion = {0};
    unsigned objQueries = 0;
    LARGE_INTEGER t0, t1, freq;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    ZeroMemory(g_scanByMB, sizeof g_scanByMB);

    if (!addr_readable((const void *)(uintptr_t)U9_HANDLE_TABLE, 4)) {
        u9log_line("HANDLES table pointer at 0x%08X is not readable",
                   U9_HANDLE_TABLE);
        return;
    }
    base = *(const unsigned char *const *)(uintptr_t)U9_HANDLE_TABLE;
    if (!base) { u9log_line("HANDLES table is NULL - not built yet"); return; }

    for (i = 0; i < U9_HANDLE_SCAN_MAX; i++) {
        const unsigned char *e = base + (size_t)i * U9_HANDLE_STRIDE;
        const unsigned char *obj;
        if (!scan_readable(&tabRegion, e, U9_HANDLE_STRIDE)) { unreadable = i; break; }
        obj = *(const unsigned char *const *)(e + U9_HANDLE_OBJ);
        if (!obj) { dead++; continue; }
        if (!scan_readable_mb(obj, 0x18, &objQueries)) { dead++; continue; }
        live++;
        {
            unsigned w0 = *(const unsigned *)obj;
            unsigned fl = *(const unsigned *)(obj + 0x14);
            unsigned short ty = *(const unsigned short *)(obj + U9_OBJ_TYPEID);
            unsigned k;
            for (k = 0; k < nseen; k++) if (seen[k].type == ty) break;
            if (k < nseen) {
                seen[k].n++;
                if (fl < seen[k].lo) seen[k].lo = fl;
                if (fl > seen[k].hi) seen[k].hi = fl;
            } else if (nseen < (unsigned)(sizeof seen / sizeof seen[0])) {
                seen[nseen].type = ty; seen[nseen].n = 1; seen[nseen].w0 = w0;
                seen[nseen].lo = fl; seen[nseen].hi = fl;
                nseen++;
            } else {
                other++;
            }
        }
    }

    QueryPerformanceCounter(&t1);
    u9log_line("HANDLES table at 0x%08X: %u live, %u empty, scan stopped at %u "
               "(cap %u) - stride 0x%X, object at +0x%X; took %.2f ms, %u VirtualQuery",
               (unsigned)(uintptr_t)base, live, dead,
               unreadable ? unreadable : U9_HANDLE_SCAN_MAX,
               U9_HANDLE_SCAN_MAX, U9_HANDLE_STRIDE, U9_HANDLE_OBJ,
               (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart,
               tabRegion.queries + objQueries);
    {
        unsigned a, b;
        for (a = 0; a + 1 < nseen; a++)
            for (b = a + 1; b < nseen; b++)
                if (seen[b].n > seen[a].n) {
                    HandleGroup t = seen[a];
                    seen[a] = seen[b]; seen[b] = t;
                }
    }
    for (i = 0; i < nseen && i < 24u; i++)
        u9log_line("HANDLES   type 0x%04X (%5u)  x%-4u  first word 0x%08X  "
                   "+0x14 fastref %u..%u%s",
                   seen[i].type, seen[i].type, seen[i].n, seen[i].w0,
                   seen[i].lo >> 21, seen[i].hi >> 21,
                   (seen[i].lo >> 21) >= 0x7FEu ? "   <- RELEASED fast reference "
                                                  "(0x7FE/0x7FF), the F-7 population"
                 : seen[i].type == 0x6F6 ? "   <- the id FUN_005F22C0 tests for"
                                         : "");
    if (other)
        u9log_line("HANDLES   ... and %u more records whose type id did not fit "
                   "the %u tracked groups", other,
                   (unsigned)(sizeof seen / sizeof seen[0]));
}


#define U9_MOVIE_CURRENT        0x00AEA248u
#define U9_APP_OBJECT           0x00850228u
#define U9_MOVIE_STOPPED        0xA4u
#define U9_MOVIE_CALLBACK       0xA8u
#define U9_MOVIE_IN_CALLBACK    0xACu
#define U9_MOVIE_SKIP_RESTORE   0xB0u
#define U9_MOVIE_DDRAW          0xB4u

typedef int           (__fastcall *U9PlayMovieFn)(int index, void *callback);
typedef unsigned char (U9CALL     *U9MoviePlayNextFn)(void *movie, int index);
typedef void          (U9CALL     *U9MovieFullStopFn)(void *movie);

static U9PlayMovieFn     o_playMovie;
static U9MoviePlayNextFn o_moviePlayNext;
static U9MovieFullStopFn o_movieFullStop;

static int movie_guard(void)
{
    const char *app = *(const char *const *)(uintptr_t)U9_APP_OBJECT;
    const char *owner, *rend;
    if (!app) return -1;
    owner = *(const char *const *)(app + 0xBC);
    if (!owner) return -1;
    rend = *(const char *const *)(owner + 4);
    if (!rend) return -1;
    return *(const unsigned char *)(rend + U9_OFF_DISABLED_GUARD);
}

static void movie_state(const char *what, const void *m, void *from)
{
    if (!m) {
        u9log_line("MOVIE %s movie=NULL from 0x%08X guard=%d frame %u", what,
                   (unsigned)(uintptr_t)from, movie_guard(), u9log_frame());
        return;
    }
    u9log_line("MOVIE %s movie=0x%08X from 0x%08X stopped=%u callback=0x%08X"
               " in_callback=%u skip_restore=%u ddraw=0x%08X guard=%d frame %u",
               what, (unsigned)(uintptr_t)m, (unsigned)(uintptr_t)from,
               FIELD_U32(m, U9_MOVIE_STOPPED), FIELD_U32(m, U9_MOVIE_CALLBACK),
               FIELD_U32(m, U9_MOVIE_IN_CALLBACK), FIELD_U32(m, U9_MOVIE_SKIP_RESTORE),
               FIELD_U32(m, U9_MOVIE_DDRAW), movie_guard(), u9log_frame());
}

static int __fastcall h_playMovie(int index, void *callback)
{
    void *from = __builtin_return_address(0);
    void *cur  = *(void **)(uintptr_t)U9_MOVIE_CURRENT;
    int r;

    u9log_line("MOVIE PlayMovie(index %d, callback 0x%08X) from 0x%08X",
               index, (unsigned)(uintptr_t)callback, (unsigned)(uintptr_t)from);
    movie_state("  current before", cur, from);
    r = o_playMovie(index, callback);
    cur = *(void **)(uintptr_t)U9_MOVIE_CURRENT;
    u9log_line("MOVIE PlayMovie -> %d", r);
    movie_state("  current after", cur, from);
    u9log_flush();
    return r;
}

static unsigned char U9CALL h_moviePlayNext(void *movie, int index)
{
    void *from = __builtin_return_address(0);
    unsigned char r;

    u9log_line("MOVIE PlayNext(index %d) from 0x%08X", index,
               (unsigned)(uintptr_t)from);
    movie_state("  entry", movie, from);
    r = o_moviePlayNext(movie, index);
    u9log_line("MOVIE PlayNext -> %u (0 = the clip failed to start)", r);
    u9log_flush();
    return r;
}

static void U9CALL h_movieFullStop(void *movie)
{
    void *from = __builtin_return_address(0);

    movie_state("FullStop entry", movie, from);
    u9log_flush();
    o_movieFullStop(movie);
    u9log_line("MOVIE FullStop returned: current movie 0x%08X guard=%d frame %u",
               (unsigned)(uintptr_t)*(void **)(uintptr_t)U9_MOVIE_CURRENT,
               movie_guard(), u9log_frame());
    u9log_flush();
}

static void movie_watch_install(void)
{
    static const uint8_t CALL_E8[1] = { 0xE8 };
    static const struct { U9Symbol sym; int kind; } SITES[] = {
        { U9SYM_MOVIE_PLAY_CALL_A, 0 }, { U9SYM_MOVIE_PLAY_CALL_B, 0 },
        { U9SYM_MOVIE_PLAY_CALL_C, 0 }, { U9SYM_MOVIE_PLAY_CALL_D, 0 },
        { U9SYM_MOVIE_NEXT_CALL_A, 1 }, { U9SYM_MOVIE_NEXT_CALL_B, 1 },
        { U9SYM_MOVIE_NEXT_CALL_C, 1 },
        { U9SYM_MOVIE_STOP_CALL_A, 2 }, { U9SYM_MOVIE_STOP_CALL_B, 2 },
        { U9SYM_MOVIE_STOP_CALL_KEY, 2 }, { U9SYM_MOVIE_STOP_CALL_EVENT, 2 },
    };
    static U9PatchSpec specs[sizeof SITES / sizeof SITES[0]];
    unsigned i, ok = 0, n = (unsigned)(sizeof SITES / sizeof SITES[0]);

    if (!u9cfg_int("watch_movie", 1)) return;
    for (i = 0; i < n; i++) {
        U9PatchSpec *spec = &specs[i];
        void *prev;
        spec->name        = u9sym_name(SITES[i].sym);
        spec->mechanism   = U9PATCH_CALLSITE;
        spec->target      = SITES[i].sym;
        spec->length      = 5;
        spec->expected    = CALL_E8;
        spec->expectedLen = 1;
        spec->flags       = U9PF_EXCLUSIVE;
        if (SITES[i].kind == 0) {
            prev = u9patch_install(spec, (void *)h_playMovie);
            if (prev && !o_playMovie) o_playMovie = (U9PlayMovieFn)prev;
        } else if (SITES[i].kind == 1) {
            prev = u9patch_install(spec, (void *)h_moviePlayNext);
            if (prev && !o_moviePlayNext) o_moviePlayNext = (U9MoviePlayNextFn)prev;
        } else {
            prev = u9patch_install(spec, (void *)h_movieFullStop);
            if (prev && !o_movieFullStop) o_movieFullStop = (U9MovieFullStopFn)prev;
        }
        if (prev) ok++;
    }
    u9log_line("MOVIE watch: %u of %u call sites redirected, observe only"
               " (PlayMovie 0x%08X, PlayNext 0x%08X, FullStop 0x%08X)", ok, n,
               (unsigned)(uintptr_t)o_playMovie, (unsigned)(uintptr_t)o_moviePlayNext,
               (unsigned)(uintptr_t)o_movieFullStop);
}

typedef void (U9CALL *RefsInvFn)(void *self, int a1, int a2, int a3, int a4, int a5);
typedef void (U9CALL *RefsCallerFn)(void *self);
static RefsInvFn    o_refsInv;
static RefsCallerFn o_refsCaller;
static void        *g_lastRefsCaller;
static unsigned     g_refsNullSeen;

static unsigned handle_obj(unsigned h)
{
    const unsigned char *base;
    const unsigned char *e;
    if (!addr_readable((const void *)(uintptr_t)U9_HANDLE_TABLE, 4)) return 0;
    base = *(const unsigned char *const *)(uintptr_t)U9_HANDLE_TABLE;
    if (!base) return 0;
    e = base + (size_t)h * U9_HANDLE_STRIDE;
    if (!addr_readable(e, U9_HANDLE_STRIDE)) return 0;
    return *(const unsigned *)(e + U9_HANDLE_OBJ);
}

static void describe_handle(const char *what, unsigned h)
{
    unsigned obj = handle_obj(h);
    if (!obj || !addr_readable((const void *)(uintptr_t)obj, 0x18)) {
        u9log_line("F7    %s handle %u -> %s", what, h,
                   obj ? "object not readable" : "no object");
        return;
    }
    u9log_line("F7    %s handle %u -> obj 0x%08X  type 0x%04X  first word "
               "0x%08X  fastref %u", what, h, obj,
               *(const unsigned short *)(uintptr_t)(obj + U9_OBJ_TYPEID),
               *(const unsigned *)(uintptr_t)obj,
               *(const unsigned *)(uintptr_t)(obj + 0x14) >> 21);
}

static void U9CALL h_refsCaller(void *self)
{
    g_lastRefsCaller = self;
    o_refsCaller(self);
}

static void U9CALL h_refsInv(void *self, int a1, int a2, int a3, int a4, int a5)
{
    if (self) { o_refsInv(self, a1, a2, a3, a4, a5); return; }

    g_refsNullSeen++;
    if (g_refsNullSeen <= 8) {
        const unsigned char *c = (const unsigned char *)g_lastRefsCaller;
        u9log_line("F7 *** NULL `this` into the reference invalidator "
                   "(occurrence %u). Skipped - it has nothing to do for a null "
                   "object. Caller object 0x%08X",
                   g_refsNullSeen, (unsigned)(uintptr_t)c);
        if (c && addr_readable(c, 0x108)) {
            u9log_line("F7    caller +0xE0 0x%08X  +0xE4 0x%08X  +0x100 0x%08X"
                       "  callback +0x104 0x%08X",
                       *(const unsigned *)(c + 0xE0), *(const unsigned *)(c + 0xE4),
                       *(const unsigned *)(c + 0x100), *(const unsigned *)(c + 0x104));
            describe_handle("+0xF0", *(const unsigned *)(c + 0xF0));
            describe_handle("+0xF4", *(const unsigned *)(c + 0xF4));
            describe_handle("+0xF8", *(const unsigned *)(c + 0xF8));
            u9log_line("F7    the callback at +0x104 is real code - decompile it "
                       "to name the subsystem, rather than inferring from offsets");
        } else {
            u9log_line("F7    caller object not readable; the invalidator may "
                       "have been reached from somewhere else entirely");
        }
    }
}

static void f7_watch_install(void)
{
    static const uint8_t expInv[6]    = { 0x8B, 0x91, 0x60, 0x01, 0x00, 0x00 };
    static const uint8_t expCaller[6] = { 0x51, 0x53, 0x56, 0x8B, 0xF1, 0x57 };
    static const U9PatchSpec specInv = {
        "F7_REFS_INVALIDATE", U9PATCH_INLINE, U9SYM_REFS_INVALIDATE,
        6, expInv, 6, 0
    };
    static const U9PatchSpec specCaller = {
        "F7_REFS_CALLER", U9PATCH_INLINE, U9SYM_REFS_INVALIDATE_CALLER,
        6, expCaller, 6, 0
    };
    if (!u9cfg_int("watch_f7", 1)) return;
    o_refsCaller = (RefsCallerFn)u9patch_install(&specCaller, (void *)h_refsCaller);
    o_refsInv    = (RefsInvFn)   u9patch_install(&specInv,    (void *)h_refsInv);
    if (!o_refsInv || !o_refsCaller) {
        u9log_line("F7 watch NOT installed (caller %s, invalidator %s) - leaving "
                   "the fault to crash rather than half-watching it",
                   o_refsCaller ? "ok" : "refused", o_refsInv ? "ok" : "refused");
        o_refsInv = 0; o_refsCaller = 0;
    } else {
        u9log_line("F7 watch installed: NULL `this` into 0x%08X will be logged "
                   "and skipped, with its caller named from 0x%08X",
                   (unsigned)u9sym(U9SYM_REFS_INVALIDATE),
                   (unsigned)u9sym(U9SYM_REFS_INVALIDATE_CALLER));
    }
}


#define U9_AREA_CELLS_MAX 4096u

static unsigned g_areaCells, g_areaUsedTotal, g_areaUsedMax, g_areaSuspect;
static unsigned g_areaCountField;

static void area_scan(void)
{
    const unsigned char *owner;
    const unsigned char *cell;
    unsigned cells = 0, total = 0, worst = 0, suspect = 0, countField = 0;

    g_areaCells = g_areaUsedTotal = g_areaUsedMax = 0;
    if (!addr_readable((const void *)(uintptr_t)U9_AREA_LIST_OWNER, 4)) return;
    owner = *(const unsigned char *const *)(uintptr_t)U9_AREA_LIST_OWNER;
    if (!owner || !addr_readable(owner, U9_AREA_LIST_HEAD + 4)) return;

    cell = *(const unsigned char *const *)(owner + U9_AREA_LIST_HEAD);
    while (cell && cells < U9_AREA_CELLS_MAX) {
        const unsigned char *slots;
        unsigned free = 0, idx;

        if (!addr_readable(cell, 0x28)) break;
        slots = *(const unsigned char *const *)(cell + U9_AREACELL_SLOTS);
        idx   = *(const unsigned *)(cell + U9_AREACELL_FREEHEAD);
        countField += *(const unsigned *)(cell + U9_AREACELL_COUNT);
        cells++;

        if (slots && addr_readable(slots, U9_AREA_SLOTS_ALLOC * U9_AREA_SLOT_STRIDE)) {
            while (free <= U9_AREA_SLOTS_USABLE) {
                if (idx == 0xFFFFFFFFu || idx == 0u) break;
                if (idx > U9_AREA_SLOTS_USABLE) { suspect++; break; }
                free++;
                idx = *(const unsigned *)(slots + (size_t)idx * U9_AREA_SLOT_STRIDE);
            }
            {
                unsigned used = (free <= U9_AREA_SLOTS_USABLE)
                              ? (U9_AREA_SLOTS_USABLE - free) : 0;
                total += used;
                if (used > worst) worst = used;
            }
        }
        cell = *(const unsigned char *const *)(cell + U9_AREACELL_NEXT);
    }
    g_areaCells = cells; g_areaUsedTotal = total; g_areaUsedMax = worst;
    g_areaSuspect = suspect; g_areaCountField = countField;
}

void u9area_occupancy(unsigned *cells, unsigned *worst, unsigned *total,
                      unsigned *cap)
{
    if (cells) *cells = g_areaCells;
    if (worst) *worst = g_areaUsedMax;
    if (total) *total = g_areaUsedTotal;
    if (cap)   *cap   = U9_AREA_SLOTS_USABLE;
}

static void log_address_space(void)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *p;
    SIZE_T committed = 0, freeTotal = 0, largestFree = 0, reserved = 0;

    GetSystemInfo(&si);
    p = (unsigned char *)si.lpMinimumApplicationAddress;
    while (p < (unsigned char *)si.lpMaximumApplicationAddress) {
        if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
        if (mbi.State == MEM_COMMIT) committed += mbi.RegionSize;
        else if (mbi.State == MEM_RESERVE) reserved += mbi.RegionSize;
        else if (mbi.State == MEM_FREE) {
            freeTotal += mbi.RegionSize;
            if (mbi.RegionSize > largestFree) largestFree = mbi.RegionSize;
        }
        if (mbi.RegionSize == 0) break;
        p += mbi.RegionSize;
    }
    {
        unsigned cc, up, du, pe, mb, ev;
        u9gl_tex_stats(&cc, &up, &du, &pe, &mb, &ev);
        u9log_line("GLTEXSTAT cache %u entries / %u MB, %u uploads, %u evictions, "
                   "%u duplicate-identity, %u pending", cc, mb, up, ev, du, pe);
    }
    u9log_line("GLMEM committed %u MB, reserved %u MB, free %u MB, "
               "largest free block %u MB",
               (unsigned)(committed / (1024u * 1024u)),
               (unsigned)(reserved  / (1024u * 1024u)),
               (unsigned)(freeTotal / (1024u * 1024u)),
               (unsigned)(largestFree / (1024u * 1024u)));

    {
        SIZE_T img = 0, mapped = 0, priv = 0;
        struct { SIZE_T bytes; char name[64]; } top[6];
        unsigned n = 0, i2, j;
        memset(top, 0, sizeof(top));

        p = (unsigned char *)si.lpMinimumApplicationAddress;
        while (p < (unsigned char *)si.lpMaximumApplicationAddress) {
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            if (mbi.State == MEM_COMMIT) {
                if      (mbi.Type == MEM_IMAGE)   img    += mbi.RegionSize;
                else if (mbi.Type == MEM_MAPPED)  mapped += mbi.RegionSize;
                else                              priv   += mbi.RegionSize;

                if (mbi.RegionSize > top[5].bytes) {
                    char nm[MAX_PATH]; const char *base = "(private)";
                    if (mbi.Type == MEM_IMAGE &&
                        GetModuleFileNameA((HMODULE)mbi.AllocationBase, nm, sizeof(nm))) {
                        const char *slash = nm;
                        const char *q;
                        for (q = nm; *q; q++) if (*q == 92 || *q == '/') slash = q + 1;
                        base = slash;
                    } else if (mbi.Type == MEM_MAPPED) base = "(mapped file)";
                    top[5].bytes = mbi.RegionSize;
                    lstrcpynA(top[5].name, base, sizeof(top[5].name));
                    for (i2 = 5; i2 > 0 && top[i2].bytes > top[i2-1].bytes; i2--) {
                        SIZE_T tb = top[i2].bytes; char tn[64];
                        lstrcpynA(tn, top[i2].name, sizeof(tn));
                        top[i2].bytes = top[i2-1].bytes;
                        lstrcpynA(top[i2].name, top[i2-1].name, sizeof(top[i2].name));
                        top[i2-1].bytes = tb;
                        lstrcpynA(top[i2-1].name, tn, sizeof(top[i2-1].name));
                    }
                }
            }
            if (mbi.RegionSize == 0) break;
            p += mbi.RegionSize;
        }
        u9log_line("GLMEM   by type: image %u MB, mapped %u MB, private %u MB",
                   (unsigned)(img / (1024u*1024u)), (unsigned)(mapped / (1024u*1024u)),
                   (unsigned)(priv / (1024u*1024u)));
        for (j = 0, n = 0; j < 6; j++)
            if (top[j].bytes >= 8u * 1024u * 1024u) {
                u9log_line("GLMEM   largest #%u: %4u MB  %s", ++n,
                           (unsigned)(top[j].bytes / (1024u*1024u)), top[j].name);
            }
    }
}

static void U9CALL h_setMaterialPooled(U9Renderer *self, void *mat)
{
    static unsigned n;
    if (u9r5_ports(self)) r5_set_material_pooled(self, mat);
    else                  o_setMatPooled(self, mat);
    if (mat && n < 40u) {
        int id = (int)FIELD_S16(mat, U9_MAT_TEXID);
        n++;
        u9log_line("F %06u slot11 SetMaterialPooled mat=0x%08X tex=%d flags=0x%08X",
                   u9log_frame(), (unsigned)(uintptr_t)mat, id,
                   FIELD_U32(mat, U9_MAT_FLAGS));
    }
}

static void U9CALL h_setMaterialImmediate(U9Renderer *self, void *mat)
{
    static unsigned n;
    if (u9r5_ports(self)) r5_set_material_immediate(self, mat);
    else                  o_setMatImmediate(self, mat);
    if (mat && n < 40u) {
        int id = (int)FIELD_S16(mat, U9_MAT_TEXID);
        n++;
        u9log_line("F %06u slot12 SetMaterialImmediate mat=0x%08X tex=%d flags=0x%08X",
                   u9log_frame(), (unsigned)(uintptr_t)mat, id,
                   FIELD_U32(mat, U9_MAT_FLAGS));
    }
}

static void U9CALL h_setMipMapLevel(U9Renderer *self, u32 level)
{
    static unsigned calls;
    uintptr_t before = FIELD_U32(self, U9_OFF_TEXMGR);

    if (U9_MAY_CALL(o_setMipMap)) o_setMipMap(self, level);

    {
        uintptr_t after = FIELD_U32(self, U9_OFF_TEXMGR);
        calls++;
        if (calls <= 16 || after != before)
            u9log_line("GLMIP  slot47 SetMipMapLevel(%u) call %u: texture manager "
                       "0x%08X -> 0x%08X%s  (+0x64=%u +0x5C=%u +0x60=%u)",
                       level, calls, (unsigned)before, (unsigned)after,
                       after != before ? "  *** REALLOCATED - every cached GL "
                                         "texture was decoded through the old one"
                                       : "",
                       FIELD_U32(self, 0x64), FIELD_U8(self, 0x5C),
                       FIELD_U32(self, 0x60));
    }
}

typedef void (U9CALL *U9DrainFn)(void *archive);
static unsigned g_drainPresents, g_drainIds, g_drainMax;

static void own_present_drain(void)
{
    void *arc = *(void **)(uintptr_t)U9_TEX_ARCHIVE;
    int pending;

    if (!arc) return;
    pending = *(const int *)((char *)arc + U9_TEXUI_PENDING_COUNT);
    if (pending > 0) {
        if (!g_drainPresents)
            u9log_line("STAGE5 composed pool: first drain, %d pending ids reclaimed"
                       " at Present (Glide slot 42's shared work) frame %u",
                       pending, u9log_frame());
        g_drainPresents++;
        g_drainIds += (unsigned)pending;
        if ((unsigned)pending > g_drainMax) g_drainMax = (unsigned)pending;
    }
    ((U9DrainFn)(uintptr_t)U9_TEX_COMPOSED_DRAIN)(arc);
    if ((u9log_frame() % 300) == 0)
        u9log_line("STAGE5 composed pool: %u ids reclaimed over %u draining"
                   " presents, largest single drain %u", g_drainIds,
                   g_drainPresents, g_drainMax);
}

static void present_original(U9Renderer *self)
{
    if (U9_MAY_CALL(o_present)) { o_present(self); return; }
    own_present_drain();
}

static void U9CALL h_present(U9Renderer *self)
{
    static int announced;

    u9metrics_engine_frame();
    u9metrics_toggle_poll();
    u9npc_tick();
    u9player_watch_tick();
    u9actor_tick();
    u9mesh_tick();
    u9meshstat_tick();
    u9focus_tick();
    u9gl_background_tick();
    u9bird_tick();
    u9portal_tick();
    u9light_tick();
    u9flyer_tick();
    u9furn_tick();
    u9linehit_tick();
    u9fix_los_clamp_tick();
    mesh_hw_poll();

    uint32_t tri  = *(uint32_t *)U9_STAT_TRIANGLES;

    if (!announced) {
        announced = 1;
        u9log_line("FIRST PRESENT rasterizer=%d  this=0x%08X  vtbl=0x%08X  w=%u h=%u bpp=%u",
                   *(int *)U9_RASTERIZER_VALUE, (unsigned)(uintptr_t)self,
                   *(unsigned *)self,
                   FIELD_U32(self, U9_OFF_WIDTH), FIELD_U32(self, U9_OFF_HEIGHT),
                   FIELD_U32(self, U9_OFF_BPP));
        u9gl_probe_all_windows("first Present");
        u9gl_context_create();
        u9gl_probe_all_windows("after GL context");
    }

    uint32_t vtx  = *(uint32_t *)U9_STAT_VERTICES;
    uint32_t clip = *(uint32_t *)U9_STAT_CLIPPED;

    if ((u9log_frame() % 300) == 0) { log_address_space(); u9crash_report_tally(); }
    if (u9cfg_int("area_occupancy", 1) && (u9log_frame() % 30) == 0) {
        area_scan();
        if ((u9log_frame() % 600) == 0)
            u9log_line("AREA  %u cells loaded, worst cell %u/%u slots used, "
                       "%u used across all cells%s  [cell+0x18 sums to %u - "
                       "equal to the total means it IS the live count]",
                       g_areaCells, g_areaUsedMax, U9_AREA_SLOTS_USABLE,
                       g_areaUsedTotal,
                       g_areaSuspect ? "  *** a free list left the valid range" : "",
                       g_areaCountField);
    }
    if (u9cfg_int("probe_handles", 1) && u9log_frame() >= 600 &&
        (u9log_frame() % 1200) == 0) log_handle_table();

    if (u9log_frame() < 120 || (u9log_frame() % 300) == 0)
    u9log_line("F %06u PRESENT this=0x%08X tri=%u vtx=%u clip=%u guard=%u "
               "ourTri=%u sentinel=%d tail=%d calls{15:%u,32:%u,34:%u,45:%u,48:%u,fl:%u,31:%u,33:%u} inFlush{32:%u,34:%u}",
               u9log_frame(), (unsigned)(uintptr_t)self, tri, vtx, clip,
               (unsigned)FIELD_U8(self, U9_OFF_DISABLED_GUARD),
               totalTri, sawAlphaSentinel, u9factory_tailIntact(),
               n15, n32, n34, n45, n48, nFlush, n31, n33,
               n32InFlush, n34InFlush);

    {
        static uint32_t prevTri;
        static int      announced;
        uint32_t glideTris = tri - prevTri;
        unsigned ourTris, runs, dropped;
        float zmin, zmax;
        prevTri = tri;
        u9gl_raster_stats(&ourTris, &runs, &dropped, &zmin, &zmax);
        if (u9log_frame() > 30 && announced < 6 && glideTris) {
            announced++;
            u9log_line("GLOVERDRAW glide drew %u tris this frame, we drew %u "
                       "(ratio %.2f)", glideTris, ourTris,
                       (double)ourTris / (double)glideTris);
        }
    }
    n15 = n32 = n34 = n45 = n48 = nFlush = n31 = n33 = 0;
    n32InFlush = n34InFlush = 0;
    totalTri = 0;
    u9log_frame_advance();
    if ((u9log_frame() % 300) == 0) {
        u9gl_untextured_report();
        u9gl_offered_report();
        u9log_flush();
    }
    n42++;
    u9feed_install();

    {
        static int said;
        if (!said) {
            said = 1;
            u9log_line("GLPRESENT glide_present=%d present_glide_first=%d "
                       "feed_glide=%d",
                       u9cfg_int("glide_present", 1),
                       u9cfg_int("present_glide_first", 1),
                       u9cfg_int("feed_glide", 1));
        }
    }

    if (u9cfg_int("glide_present", 1)) {
        if (u9cfg_int("present_glide_first", 1)) {
            present_original(self);
            u9gl_own_present(self);
        } else {
            u9gl_own_present(self);
            present_original(self);
        }
        return;
    }
    if (u9gl_own_present(self))
        return;
    present_original(self);
}

__attribute__((used))
static const U9RendererVtbl g_signature_guard = {
    .Initialize     = h_initialize,
    .CreateDevice   = h_createDevice,
    .Clear          = h_clear,
    .DrawTriangle   = h_drawTriangle,
    .DrawPolygon    = h_drawPolygon,
    .Present        = h_present,
    .SetDepthWrite  = h_setDepthWrite,
    .SetRenderState = h_setRenderState,
    .SetDepthMode       = h_setDepthMode,
    .DisableDepthBuffer = h_disableDepth,
    .DrawLine           = h_drawLine,
    .DrawPolygonList    = h_drawPolyList,
    .FlushSelected      = h_flushSelected,
    .FlushBucketPoolA   = h_flushBucketA,
    .FlushListPool      = h_flushListPool,
    .FlushSortedBatches = h_flushSorted,
    .FlushBucketPoolB   = h_flushBucketB,
    .FlushLines         = h_flushLines,
};

int u9hooks_install(void *vtblAddr)
{
    f7_watch_install();

    if (!u9patch_begin(&g_patch, vtblAddr, U9_VTBL_SLOT_COUNT))
        return 0;

    o_destroy        = (void *(U9CALL *)(U9Renderer *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_DESTROY,         &h_destroy);
    o_initialize     = (u8   (U9CALL *)(U9Renderer *, u32, u32, u32))
                       u9patch_slot(&g_patch, U9_SLOT_INITIALIZE,      &h_initialize);
    o_createDevice   = (void (U9CALL *)(U9Renderer *, u32, u32, u32))
                       u9patch_slot(&g_patch, U9_SLOT_CREATE_DEVICE,   &h_createDevice);
    o_clear          = (void (U9CALL *)(U9Renderer *, u32, u32))
                       u9patch_slot(&g_patch, U9_SLOT_CLEAR,           &h_clear);
    o_drawTriangle   = (void (U9CALL *)(U9Renderer *, const U9Vertex *,
                                        const U9Vertex *, const U9Vertex *, float))
                       u9patch_slot(&g_patch, U9_SLOT_DRAW_TRIANGLE,   &h_drawTriangle);
    o_drawPolygon    = (void (U9CALL *)(U9Renderer *, u32, GrVertexU9 *))
                       u9patch_slot(&g_patch, U9_SLOT_DRAW_POLYGON,    &h_drawPolygon);
    if (u9cfg_int("gl_shadow_guard_hook", 1))
        o_testAndDraw = (u32 (U9CALL *)(U9Renderer *, const U9Vertex *, const U9Vertex *, const U9Vertex *, u32))
                        u9patch_slot(&g_patch, U9_SLOT_TEST_AND_DRAW,   &h_testAndDraw);
    o_present        = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_PRESENT,         &h_present);
    o_setDepthWrite  = (void (U9CALL *)(U9Renderer *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_SET_DEPTH_WRITE, &h_setDepthWrite);
    o_setRenderState = (void (U9CALL *)(U9Renderer *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_SET_RENDER_STATE,&h_setRenderState);
    o_setDepthMode   = (void (U9CALL *)(U9Renderer *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_SET_DEPTH_MODE, &h_setDepthMode);
    o_disableDepth   = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, 44,                    &h_disableDepth);
    o_drawLine       = (void (U9CALL *)(U9Renderer *, const U9Vertex *,
                                        const U9Vertex *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_DRAW_LINE,      &h_drawLine);
    o_drawPolyList   = (void (U9CALL *)(U9Renderer *, u32, U9Vertex *, float, u32))
                       u9patch_slot(&g_patch, U9_SLOT_DRAW_POLY_LIST, &h_drawPolyList);
    o_flushSelected  = (void (U9CALL *)(U9Renderer *, u32))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_SELECTED,  &h_flushSelected);
    o_flushBucketA   = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_BUCKET_A,  &h_flushBucketA);
    o_flushListPool  = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_LIST_POOL, &h_flushListPool);
    o_flushSorted    = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_SORTED,    &h_flushSorted);
    o_flushBucketB   = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_BUCKET_B,  &h_flushBucketB);
    o_flushLines     = (void (U9CALL *)(U9Renderer *))
                       u9patch_slot(&g_patch, U9_SLOT_FLUSH_LINES,     &h_flushLines);

    o_setMipMap = (void (U9CALL *)(U9Renderer *, u32))
                  u9patch_slot(&g_patch, U9_SLOT_SET_MIPMAP, &h_setMipMapLevel);

    o_setMatPooled = (void (U9CALL *)(U9Renderer *, void *))
        u9patch_slot(&g_patch, U9_SLOT_SET_MATERIAL_P, &h_setMaterialPooled);
    o_setMatImmediate = (void (U9CALL *)(U9Renderer *, void *))
        u9patch_slot(&g_patch, U9_SLOT_SET_MATERIAL_I, &h_setMaterialImmediate);

    if (u9cfg_int("gl_own_depth", 0) || u9own_life()) {
        u9patch_slot(&g_patch, U9_SLOT_SET_DEPTH_MODE,  &u9gl_slot_set_depth_mode);
        u9patch_slot(&g_patch, 44,                      &u9gl_slot_disable_depth);
        u9patch_slot(&g_patch, U9_SLOT_SET_DEPTH_WRITE, &u9gl_slot_set_depth_write);
        u9patch_slot(&g_patch, U9_SLOT_SET_DEPTH_FUNC,  &u9gl_slot_set_depth_func);
        u9log_line("GLSLOT depth cluster 43,44,45,46 is OURS - no Glide code runs"
                   " for them (gl_own_depth=0 hands them back)");
    }

    if (u9own_life()) {
        u9patch_slot(&g_patch,  4, &u9gl_slot04);
        u9patch_slot(&g_patch, 13, &u9gl_slot13);
        u9patch_slot(&g_patch, 14, &u9gl_slot14);
        u9patch_slot(&g_patch, 31, &u9gl_slot_draw_line);
        u9patch_slot(&g_patch, 40, &u9gl_slot_flush_lines);
        u9patch_slot(&g_patch, 47, &u9gl_slot_set_mipmap_level);
        u9patch_slot(&g_patch, 50, &u9gl_slot_screenshot);
        u9patch_slot(&g_patch, 51, &u9gl_slot_capture);
        u9patch_slot(&g_patch, 52, &u9gl_slot_suspend);
        u9patch_slot(&g_patch, 53, &u9gl_slot_restore_state);
        u9gl_background_set_owner(1);
        u9log_line("STAGE5 slots 4,13,14,31,40,47,50,51,52,53 are OURS -"
                   " nothing left in the vtable enters Glide");
    }

    if (u9cfg_int("gl_own_gamma", 1)) {
        u9patch_slot(&g_patch, U9_SLOT_SET_GAMMA, &u9gl_slot_set_gamma);
        u9patch_slot(&g_patch, U9_SLOT_GET_GAMMA, &u9gl_slot_get_gamma);
        u9log_line("GLSLOT gamma slots 9 and 10 are OURS - no Glide code runs"
                   " for them");
    }

    movie_watch_install();

    u9patch_end(&g_patch, U9_VTBL_SLOT_COUNT);
    return 1;
}

void u9hooks_remove(void)
{
    u9patch_revert(&g_patch, U9_VTBL_SLOT_COUNT);
}
