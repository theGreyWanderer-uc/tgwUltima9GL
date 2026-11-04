#include <windows.h>
#include <string.h>
#include <stdint.h>

#include "u9renderer.h"
#include "../../addresses.h"
#include "../common/patchmgr.h"
#include "../injection/config.h"
#include "log.h"

#define U9_RASTERIZER_GL   5u
#define U9_GL_TAIL_EXTRA   0x400u
#define U9_GL_TAIL_MAGIC   0x39554C47u

typedef void (U9CALL *FactoryFn)(void *owner, u32 rasterizer,
                                 u32 a1, u32 a2, u32 width, u32 height,
                                 u32 a5, u32 a6, u32 a7, u32 a8, u32 mode);

typedef void *(__cdecl  *OpNewFn)(u32 size);
typedef void *(U9CALL   *GlideCtorFn)(void *self);
typedef void *(U9CALL   *RendererCtorFn)(void *self);
typedef void  (U9CALL   *OwnerPreinitFn)(void *owner, u32 w, u32 h, u32 mode);
typedef void  (U9CALL   *BindOwnerFn)(void *renderer, void *owner);
typedef void  (U9CALL   *Slot1ArgFn)(void *self, u32 arg);

static FactoryFn o_factory;

static const uint8_t OWNER_SLOT_EXPECT[4] = { 0x80, 0xE7, 0x55, 0x00 };
static const U9PatchSpec OWNER_SLOT_SPEC = {
    "FACTORY_OWNER_SLOT0", U9PATCH_VTABLE, U9SYM_OWNER_FACTORY_SLOT,
    4, OWNER_SLOT_EXPECT, 4, U9PF_EXCLUSIVE
};

static void *g_glVtbl[U9_VTBL_SLOT_COUNT];
static int   g_vtblReady;

static void *g_r5Vtbl[U9_VTBL_SLOT_COUNT];

static void *g_glRenderer;
static void *g_r5Object;

void u9gl_probe_window(const char *when);
int  u9gl_context_create(void);
void u9gl_context_destroy(void);
void u9gl_bind_renderer(void *renderer);

#define VT(obj)  (*(const U9RendererVtbl **)(obj))
#define SLOTFN(obj, idx, type)  ((type)(*(void ***)(obj))[(idx)])
#define OWNER_U32(owner, off)  (*(u32 *)((char *)(owner) + (off)))

static int r5_glide_object(void) { return u9cfg_int("r5_glide_object", 0); }

static u32 U9CALL r5_slot08_open(void *self)
{
    (void)self;
    return 1u;
}

static int in_glide_range(uintptr_t a)
{
    return a >= U9_GLIDE_RANGE_LO && a < U9_GLIDE_RANGE_HI;
}

static int build_r5_vtable(void)
{
    const uintptr_t *glide = (const uintptr_t *)U9_VTBL_RENDERER_GLIDE;
    const uintptr_t *base  = (const uintptr_t *)U9_VTBL_RENDERER_BASE;
    unsigned i, ours = 0, shared = 0;

    for (i = 0; i < U9_VTBL_SLOT_COUNT; i++) {
        uintptr_t fn = glide[i];
        if (in_glide_range(fn)) {
            if (i == 8u) {
                fn = (uintptr_t)r5_slot08_open;
            } else if (i == U9_VTBL_SLOT_COUNT - 1u) {
                fn = 0u;
            } else {
                u9log_line("R5   REFUSED: slot %u still holds Glide code 0x%08X - "
                           "did its stage 5 hook install? (own_renderer_life, "
                           "gl_own_gamma)", i, (unsigned)fn);
                return 0;
            }
        } else if (fn == U9_PURECALL) {
            u9log_line("R5   REFUSED: slot %u is pure (0x%08X) and nothing "
                       "replaced it", i, (unsigned)fn);
            return 0;
        }
        g_r5Vtbl[i] = (void *)fn;
        if (fn && fn == base[i]) shared++;
        else if (fn)             ours++;
    }
    u9log_line("R5   vtable built at 0x%08X from stage 5's slot set: %u ours, "
               "%u shared base, slot 8 ours (returns 1), slot 54 null; no entry "
               "in 0x%08X..0x%08X", (unsigned)(uintptr_t)g_r5Vtbl, ours, shared,
               U9_GLIDE_RANGE_LO, U9_GLIDE_RANGE_HI);
    return 1;
}

static int r5_prepare(void)
{
    static int state;

    if (r5_glide_object()) return 1;
    if (!u9cfg_int("own_renderer_life", 0)) {
        u9log_line("R5   REFUSED: own_renderer_life is 0, so our slots would "
                   "call Glide's Initialize and open a Glide context");
        return 0;
    }
    if (!state) state = build_r5_vtable() ? 1 : -1;
    return state > 0;
}

static void *gl_construct(void *owner, u32 width, u32 height, u32 mode, u32 arg5)
{
    OpNewFn        opNew     = (OpNewFn)U9_OP_NEW;
    BindOwnerFn    bindOwner = (BindOwnerFn)U9_RENDERER_BIND_OWNER;
    int            glideObj  = r5_glide_object();
    void          *obj;
    u32            allocSize = U9_GLIDE_OBJECT_SIZE + U9_GL_TAIL_EXTRA;
    u8             initOk;

    if (glideObj && !g_vtblReady) {
        memcpy(g_glVtbl, (const void *)U9_VTBL_RENDERER_GLIDE, sizeof(g_glVtbl));
        g_vtblReady = 1;
        u9log_line("GL   vtable copied from 0x%08X, %u slots, ours at 0x%08X",
                   U9_VTBL_RENDERER_GLIDE, U9_VTBL_SLOT_COUNT,
                   (unsigned)(uintptr_t)g_glVtbl);
    }

    obj = opNew(allocSize);
    u9log_line("GL   operator new(0x%X) = 0x%08X  (Glide size 0x%X + 0x%X tail)",
               allocSize, (unsigned)(uintptr_t)obj,
               U9_GLIDE_OBJECT_SIZE, U9_GL_TAIL_EXTRA);
    if (!obj)
        return NULL;

    if (glideObj) {
        ((GlideCtorFn)U9_GLIDE_CTOR)(obj);
        u9log_line("GL   Glide_Renderer_Create done, vtbl was 0x%08X",
                   *(unsigned *)obj);
    } else {
        memset(obj, 0, allocSize);
        ((RendererCtorFn)U9_RENDERER_BASE_CTOR)(obj);
        *((u8 *)obj + U9_OFF_MIP_ENABLED)          = 1u;
        *(u32 *)((char *)obj + U9_OFF_MIP_LODBIAS) = 0u;
        *(u32 *)((char *)obj + U9_OFF_MIP_LEVEL)   = 1u;
        *((u8 *)obj + U9_OFF_DISABLED_GUARD)       = 0u;
        *(u32 *)((char *)obj + U9_OFF_LIST_HEAD)   = 0u;
        u9log_line("R5   Renderer_Base_ctor done on our object, no Glide "
                   "constructor ran (base vtbl 0x%08X)", *(unsigned *)obj);
        g_r5Object = obj;
    }

    *(u32 *)((char *)obj + U9_GLIDE_OBJECT_SIZE) = U9_GL_TAIL_MAGIC;

    *(const U9RendererVtbl **)obj =
        (const U9RendererVtbl *)(glideObj ? g_glVtbl : g_r5Vtbl);
    *(void **)((char *)owner + U9_OFF_OWNER_RENDERER) = obj;

    u9gl_probe_window("before Initialize");
    initOk = VT(obj)->Initialize(obj, width, height, mode);
    u9log_line("GL   Initialize(%u,%u,0x%X) = %u", width, height, mode, initOk);

    VT(obj)->Clear(obj, 0xFF, 0xFFFF);
    bindOwner(obj, owner);
    SLOTFN(obj, U9_SLOT_04, Slot1ArgFn)(obj, OWNER_U32(owner, U9_OFF_OWNER_16C));
    SLOTFN(obj, U9_SLOT_05, Slot1ArgFn)(obj, OWNER_U32(owner, U9_OFF_OWNER_170));
    SLOTFN(obj, U9_SLOT_06, Slot1ArgFn)(obj, OWNER_U32(owner, U9_OFF_OWNER_168));
    VT(obj)->SetDepthMode(obj, 2);
    VT(obj)->SetDepthFunc(obj, 3);
    VT(obj)->SetDepthWrite(obj, 1);

    OWNER_U32(owner, U9_OFF_OWNER_ARG5) = arg5;
    if (arg5 && OWNER_U32((void *)(uintptr_t)arg5, U9_OFF_ARG5_448))
        OWNER_U32(owner, U9_OFF_OWNER_154) =
            OWNER_U32((void *)(uintptr_t)arg5, U9_OFF_ARG5_448);
    else
        OWNER_U32(owner, U9_OFF_OWNER_154) = width;

    u9log_line("GL   owner+0x114=0x%08X owner+0x154=0x%08X  w=%u h=%u bpp=%u",
               OWNER_U32(owner, U9_OFF_OWNER_ARG5),
               OWNER_U32(owner, U9_OFF_OWNER_154),
               *(u32 *)((char *)obj + U9_OFF_WIDTH),
               *(u32 *)((char *)obj + U9_OFF_HEIGHT),
               *(u32 *)((char *)obj + U9_OFF_BPP));
    u9gl_probe_window("after init sequence");
    u9gl_bind_renderer(obj);

    u9log_line("GL   tail magic %s",
               *(u32 *)((char *)obj + U9_GLIDE_OBJECT_SIZE) == U9_GL_TAIL_MAGIC
                   ? "intact" : "CLOBBERED");
    return obj;
}

static void U9CALL h_createForRasterizer(void *owner, u32 rasterizer,
                                         u32 a1, u32 a2, u32 width, u32 height,
                                         u32 a5, u32 a6, u32 a7, u32 a8, u32 mode)
{
    void *renderer;

    u9log_line("FACTORY owner=0x%08X rasterizer=%u args=(%u,%u,w=%u,h=%u,0x%08X,%u,%u,%u,0x%08X)",
               (unsigned)(uintptr_t)owner, rasterizer,
               a1, a2, width, height, a5, a6, a7, a8, mode);

    if (rasterizer == 3u && u9cfg_int("rasterizer5", 0)) {
        u9log_line("FACTORY   rasterizer 3 -> 5 (rasterizer5=1; Options.ini "
                   "stays at 3, so u9.exe without the DLL is still safe)");
        rasterizer = U9_RASTERIZER_GL;
    }
    if (rasterizer == U9_RASTERIZER_GL && !r5_prepare()) {
        u9log_line("FACTORY   rasterizer 5 refused - falling back to 3");
        rasterizer = 3;
    }

    if (rasterizer == U9_RASTERIZER_GL) {
        OwnerPreinitFn preinit = (OwnerPreinitFn)U9_OWNER_PREINIT;

        u9log_line("FACTORY   rasterizer 5 -> building it here on %s; "
                   "0x%08X will NOT run",
                   r5_glide_object() ? "Glide's constructor (parked path)"
                                     : "Renderer_Base_ctor (R5-0)",
                   U9_FACTORY_ADDR);
        preinit(owner, width, height, mode);

        g_glRenderer = gl_construct(owner, width, height, mode, a5);
        if (g_glRenderer) {
            u9log_line("FACTORY   RendererGL live: owner+4 = 0x%08X vtbl = 0x%08X",
                       (unsigned)(uintptr_t)g_glRenderer,
                       *(unsigned *)g_glRenderer);
            return;
        }
        u9log_line("FACTORY   construction FAILED - falling back to Glide");
        rasterizer = 3;
    }

    o_factory(owner, rasterizer, a1, a2, width, height, a5, a6, a7, a8, mode);

    renderer = *(void **)((char *)owner + U9_OFF_OWNER_RENDERER);
    u9log_line("FACTORY   owner+4 = 0x%08X  vtbl = 0x%08X",
               (unsigned)(uintptr_t)renderer,
               renderer ? *(unsigned *)renderer : 0u);

    if (renderer)
        u9gl_bind_renderer(renderer);
}

static FactoryFn const g_factory_signature_guard = h_createForRasterizer;

int u9factory_tailIntact(void)
{
    if (!g_glRenderer) return 1;
    return *(u32 *)((char *)g_glRenderer + U9_GLIDE_OBJECT_SIZE) == U9_GL_TAIL_MAGIC;
}

int u9r5_is_ours(const void *renderer)
{
    return renderer && renderer == g_r5Object;
}

int u9factory_install(void)
{
    (void)g_factory_signature_guard;

    o_factory = (FactoryFn)u9patch_install(&OWNER_SLOT_SPEC,
                                           (void *)h_createForRasterizer);
    if (!o_factory) {
        u9log_line("FACTORY hook REFUSED by the patch manager - see the manifest");
        return 0;
    }
    u9log_line("FACTORY hook installed on vtbl_RendererOwner[0] (0x%08X) "
               "through the patch manager", U9_FACTORY_ADDR);
    return 1;
}

void u9factory_remove(void)
{
}
