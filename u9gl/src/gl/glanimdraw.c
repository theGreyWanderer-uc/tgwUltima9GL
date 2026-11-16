#include <windows.h>
#include <stdint.h>
#include "../../addresses.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../fix/fixmem.h"
#include "../diag/log.h"
#include "glanimdraw.h"

volatile unsigned char *u9gl_anim_flag(void);

static int      g_adInstalled;
static unsigned g_adObjects, g_adLastObjects, g_adTotal;

int u9gl_anim_draw_installed(void) { return g_adInstalled; }

void u9gl_anim_draw_stats(unsigned *objects, unsigned *lastObjects)
{
    *objects = g_adObjects;
    *lastObjects = g_adLastObjects;
}

void u9gl_anim_draw_frame_end(void)
{
    g_adLastObjects = g_adObjects;
    g_adObjects = 0;
}

static void __attribute__((cdecl, used)) anim_begin(uintptr_t cam)
{
    uintptr_t obj;
    int anim = 0;
    if (cam && readable4(cam + 0xFC)) {
        obj = rd32(cam + 0xFC);
        if (obj && readable4(obj + 0x28) && rd32(obj + 0x28)) anim = 1;
    }
    *u9gl_anim_flag() = (unsigned char)anim;
    if (anim) { g_adObjects++; g_adTotal++; }
}

static const uint8_t AD_BEGIN_EXPECT[6] = { 0x8B, 0x8E, 0xFC, 0x00, 0x00, 0x00 };
static const uint8_t AD_END_EXPECT[6]   = { 0x8B, 0x86, 0xFC, 0x00, 0x00, 0x00 };
static const U9PatchSpec AD_BEGIN_SPEC = {
    "GL_ANIM_DRAW_BEGIN", U9PATCH_STUB, U9SYM_DRAWOBJ_PARTS_BEGIN, 6, AD_BEGIN_EXPECT, 6, U9PF_EXCLUSIVE
};
static const U9PatchSpec AD_END_SPEC = {
    "GL_ANIM_DRAW_END", U9PATCH_STUB, U9SYM_DRAWOBJ_PARTS_END, 6, AD_END_EXPECT, 6, U9PF_EXCLUSIVE
};

static void put_rel32(uint8_t *at, uintptr_t target)
{
    *(int32_t *)at = (int32_t)(target - ((uintptr_t)at + 4));
}

static void site_jump(uint8_t *out, unsigned len, uintptr_t site, uintptr_t to)
{
    unsigned i;
    out[0] = 0xE9;
    *(int32_t *)(out + 1) = (int32_t)(to - (site + 5));
    for (i = 5; i < len; i++) out[i] = 0x90;
}

void u9gl_anim_draw_install(void)
{
    uintptr_t begin = u9sym(U9SYM_DRAWOBJ_PARTS_BEGIN), end = u9sym(U9SYM_DRAWOBJ_PARTS_END);
    uintptr_t flag;
    uint8_t *st, jmp[6];
    unsigned n = 0, n2;

    if (!u9cfg_int("diag_anim_draw", 1)) { u9log_line("GLANIMDRAW: not installed (diag_anim_draw 0)"); return; }
    if (!begin || !end) return;
    flag = (uintptr_t)u9gl_anim_flag();
    st = (uint8_t *)VirtualAlloc(NULL, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("GLANIMDRAW: no memory for the stubs - not installed"); return; }

    st[n++] = 0x60;
    st[n++] = 0x56;
    st[n++] = 0xE8; put_rel32(st + n, (uintptr_t)&anim_begin); n += 4;
    st[n++] = 0x83; st[n++] = 0xC4; st[n++] = 0x04;
    st[n++] = 0x61;
    st[n++] = 0x8B; st[n++] = 0x8E; st[n++] = 0xFC; st[n++] = 0x00; st[n++] = 0x00; st[n++] = 0x00;
    st[n++] = 0xE9; put_rel32(st + n, begin + 6); n += 4;
    n2 = n = (n + 15u) & ~15u;
    st[n++] = 0xC6; st[n++] = 0x05; *(uint32_t *)(st + n) = (uint32_t)flag; n += 4; st[n++] = 0x00;
    st[n++] = 0x8B; st[n++] = 0x86; st[n++] = 0xFC; st[n++] = 0x00; st[n++] = 0x00; st[n++] = 0x00;
    st[n++] = 0xE9; put_rel32(st + n, end + 6); n += 4;
    FlushInstructionCache(GetCurrentProcess(), st, 96);

    site_jump(jmp, 6, end, (uintptr_t)(st + n2));
    if (!u9patch_install_bytes(&AD_END_SPEC, jmp, 6)) {
        VirtualFree(st, 0, MEM_RELEASE);
        u9log_line("GLANIMDRAW: REFUSED at the parts' end (see the manifest) - not installed");
        return;
    }
    site_jump(jmp, 6, begin, (uintptr_t)st);
    if (!u9patch_install_bytes(&AD_BEGIN_SPEC, jmp, 6)) {
        u9log_line("GLANIMDRAW: REFUSED at the parts' start (see the manifest) - the end stub only lowers a flag");
        return;
    }
    g_adInstalled = 1;
    u9log_line("GLANIMDRAW: installed (stubs %08X, %08X) - triangles from animated objects are marked, for counting only",
               (unsigned)(uintptr_t)st, (unsigned)(uintptr_t)(st + n2));
}

void u9gl_anim_draw_report(void)
{
    if (g_adInstalled) u9log_line("GLANIMDRAW: %u animated objects drawn in all", g_adTotal);
}
