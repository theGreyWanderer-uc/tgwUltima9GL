#include <windows.h>
#include <stdint.h>

#include "../gl/glpost.h"
#include "../../addresses.h"
#include "../diag/log.h"
#include "config.h"

int  u9hooks_install(void *vtblAddr);
void u9crash_install(void);
void u9watch_install(void);
void u9watch_remove(void);
void u9watch_report(void);
void u9crash_remove(void);
void u9hooks_remove(void);
int  u9factory_install(void);
void u9factory_remove(void);
void u9gl_context_destroy(void);
void u9gl_source_report(int force);
void u9gl_untextured_report(void);
void u9gl_alpha_report(void);
void u9feed_install(void);
void u9feed_report(void);

static int g_installed;

static int gate_check(void)
{
    HMODULE base = GetModuleHandleA(NULL);
    uint32_t *vtbl = (uint32_t *)U9_VTBL_RENDERER_GLIDE;
    int rasterizer;

    if ((uintptr_t)base != U9_IMAGE_BASE) {
        u9log_line("GATE FAIL image base = 0x%08X, expected 0x%08X",
                   (unsigned)(uintptr_t)base, U9_IMAGE_BASE);
        return 0;
    }
    if (IsBadReadPtr(vtbl, U9_VTBL_SLOT_COUNT * 4)) {
        u9log_line("GATE FAIL vtable at 0x%08X unreadable", U9_VTBL_RENDERER_GLIDE);
        return 0;
    }
    if (vtbl[U9_SLOT_DTOR] != U9_GLIDE_SLOT0_DTOR) {
        u9log_line("GATE FAIL vtbl[0] = 0x%08X, expected 0x%08X (different build?)",
                   vtbl[U9_SLOT_DTOR], U9_GLIDE_SLOT0_DTOR);
        return 0;
    }
    if (vtbl[U9_SLOT_DRAW_TRIANGLE] != U9_GLIDE_SLOT32_DRAWTRI) {
        u9log_line("GATE FAIL vtbl[32] = 0x%08X, expected 0x%08X",
                   vtbl[U9_SLOT_DRAW_TRIANGLE], U9_GLIDE_SLOT32_DRAWTRI);
        return 0;
    }
    rasterizer = *(int *)U9_RASTERIZER_VALUE;
    u9log_line("GATE OK  base=0x%08X vtbl=0x%08X rasterizer(at attach)=%d%s",
               (unsigned)(uintptr_t)base, U9_VTBL_RENDERER_GLIDE, rasterizer,
               rasterizer == 0 ? " (not yet parsed - an early attach, from the injector or the loader)" : "");
    return 1;
}

#include "../common/lifecycle.h"
#include "../common/patchmgr.h"
#include "../common/buildid.h"
#include "../common/u9time.h"
#include "../telemetry/publisher.h"
#include "../fix/fixes.h"
#include "../fix/gold.h"
#include "../gl/console.h"
#include "../fix/freeplace.h"
#include "../gl/metrics.h"
#include "../gl/glslots.h"
#include "../fix/probes.h"
#include "../fix/teleprobe.h"
#include "../diag/furniture.h"

#include "u9gl_version.h"
#include "../gl/gltwosided.h"
#include "../gl/glbackface.h"
#include "../gl/glanimdraw.h"
#include "../gl/glbackground.h"

static HINSTANCE g_self;

static void attach(void)
{
    u9log_open(NULL);
    u9log_line("u9log attached  pid=%lu  version %s", GetCurrentProcessId(), U9GL_VERSION_FULL);
    u9crash_install();
    u9life_push_at_exit("crash reporter", u9crash_remove);
    u9life_push_at_exit("gamma ramp", u9glslots_restore_gamma);
    u9cfg_load(g_self);
    u9metrics_init();
    u9glslots_init();
    u9glpost_init();

    u9build_detect();

    u9time_init();
    u9telemetry_install();
    u9probe_init();
    u9fix_set_tick_observer(u9probe_actor_tick);
    u9fix_set_entity_dumper(u9probe_dump_entity);
    u9fix_set_mover_query_observer(u9tele_query_observer());

    u9fix_install();
    u9gold_install();
    u9console_install();
    u9freeplace_install();
    u9glpost_install_hooks();
    u9gl_two_sided_install();
    u9gl_back_face_install();
    u9gl_anim_draw_install();
    u9gl_background_install();

    u9watch_install();

    if (!gate_check()) {
        u9log_line("no hooks installed; game runs unmodified");
        u9log_flush();
        return;
    }
    if (u9hooks_install((void *)U9_VTBL_RENDERER_GLIDE)) {
        g_installed = 1;
        u9log_line("hooks installed on slots 0,1,7,15,32,34,42,45,48");
    } else {
        u9log_line("u9hooks_install FAILED (VirtualProtect?)");
    }
    u9factory_install();
    u9patch_manifest();
    u9log_flush();
}

static void detach(int exiting)
{
    u9watch_report();
    u9fix_report();
    u9gold_report();
    u9probe_report();
    u9furn_report();

    u9gl_source_report(1);
    u9gl_untextured_report();
    u9gl_alpha_report();
    u9feed_report();
    u9glpost_report();
    u9gl_two_sided_report();
    u9gl_back_face_report();
    u9gl_anim_draw_report();

    u9log_line("");
    if (exiting) {
        u9log_line("teardown: the process is exiting - reports and display state only");
        u9life_unwind_at_exit();
        u9log_line("u9log detached at exit");
        u9log_close();
        return;
    }
    u9log_line("teardown:");
    u9probe_remove();
    u9watch_remove();
    u9gl_context_destroy();
    u9factory_remove();
    if (g_installed) { u9hooks_remove(); g_installed = 0; }
    u9life_unwind();
    u9log_line("u9log detached");
    u9log_close();
}

#include "../common/claim.h"

static int g_passive;

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(inst);
        g_self = inst;
        if (!u9claim_process()) { g_passive = 1; break; }
        attach();
        break;
    case DLL_PROCESS_DETACH:
        if (!g_passive) detach(reserved != NULL);
        break;
    default: break;
    }
    return TRUE;
}
