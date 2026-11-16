#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../../addresses.h"
#include "u9renderer.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../fix/fixmem.h"
#include "../diag/log.h"
#include "glbackface.h"

int      g_bfOn;
unsigned g_bfSent, g_bfSkipped;
static int g_bfInstalled;

const void *u9gl_current_renderer(void);
int  u9gl_is_bound(void *self);
volatile unsigned char *u9gl_back_face_flag(void);

int u9gl_back_face_installed(void) { return g_bfInstalled; }

#define BF_TALLY 32
static struct { int tex; uint32_t pf; unsigned n; } g_bfTally[BF_TALLY];
static int g_bfTallyN;

static void tally_skip(uintptr_t r, uint32_t pf)
{
    uintptr_t mat;
    int tex = -1, i;
    if (readable4(r + U9_OFF_CUR_MATERIAL) && (mat = rd32(r + U9_OFF_CUR_MATERIAL)) != 0 && readable4(mat + U9_MAT_TEXID))
        tex = *(const volatile int16_t *)(mat + U9_MAT_TEXID);
    for (i = 0; i < g_bfTallyN; i++)
        if (g_bfTally[i].tex == tex && g_bfTally[i].pf == pf) { g_bfTally[i].n++; return; }
    if (g_bfTallyN < BF_TALLY) {
        g_bfTally[g_bfTallyN].tex = tex;
        g_bfTally[g_bfTallyN].pf = pf;
        g_bfTally[g_bfTallyN].n = 1;
        g_bfTallyN++;
    }
}

void u9gl_back_face_held_report(void (*out)(const char *fmt, ...))
{
    int i, j, order[BF_TALLY];
    for (i = 0; i < g_bfTallyN; i++) order[i] = i;
    for (i = 1; i < g_bfTallyN; i++)
        for (j = i; j > 0 && g_bfTally[order[j]].n > g_bfTally[order[j - 1]].n; j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    if (!g_bfTallyN) { out("no back faces held back so far"); return; }
    for (i = 0; i < g_bfTallyN && i < 12; i++)
        out("held back: texture %d, %u faces, primitive flags 0x%02X (%s)", g_bfTally[order[i]].tex,
            g_bfTally[order[i]].n, (unsigned)g_bfTally[order[i]].pf,
            (g_bfTally[order[i]].pf & U9_PRIM_TRANSLUCENT) ? "translucent: the sort pool" : "second colour: slot 37's list");
}

static int __attribute__((cdecl, used)) back_face_wanted(void)
{
    uintptr_t r = (uintptr_t)u9gl_current_renderer();
    uint32_t pf;
    if (!g_bfOn || !r) return 0;
    if (!u9gl_is_bound((void *)r)) { g_bfSkipped++; return 0; }
    if (!readable4(r + U9_OFF_PRIM_FLAGS)) return 0;
    pf = rd32(r + U9_OFF_PRIM_FLAGS);
    if (pf & (U9_PRIM_TRANSLUCENT | U9_PRIM_SECONDARY)) {
        g_bfSkipped++;
        tally_skip(r, pf);
        return 0;
    }
    *u9gl_back_face_flag() = 1;
    g_bfSent++;
    return 1;
}

static const uint8_t BF_SKIP_EXPECT[11] = { 0xFF, 0x05, 0x70, 0xC7, 0x98, 0x00, 0xE9, 0x30, 0x03, 0x00, 0x00 };
static const uint8_t BF_NEXT_EXPECT[7]  = { 0x8B, 0x44, 0x24, 0x1C, 0x83, 0xC7, 0x60 };
static const U9PatchSpec BF_SKIP_SPEC = {
    "GL_BACK_FACE_CASTERS", U9PATCH_STUB, U9SYM_FACE_BACK_SKIP, 11, BF_SKIP_EXPECT, 11, U9PF_EXCLUSIVE
};
static const U9PatchSpec BF_NEXT_SPEC = {
    "GL_BACK_FACE_CLEAR", U9PATCH_STUB, U9SYM_FACE_LOOP_NEXT, 7, BF_NEXT_EXPECT, 7, U9PF_EXCLUSIVE
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

void u9gl_back_face_install(void)
{
    uintptr_t skip = u9sym(U9SYM_FACE_BACK_SKIP), next = u9sym(U9SYM_FACE_LOOP_NEXT);
    uintptr_t draw, resume, flag;
    uint8_t *st, jmp[11];
    unsigned n = 0, n2;

    g_bfOn = u9cfg_int("gl_back_face_casters", 1);
    if (!u9cfg_int("gl_back_face_patch", 1)) { u9log_line("GLBACKFACE: not installed (gl_back_face_patch 0)"); return; }
    if (!skip || !next) return;
    draw   = skip + 0x0B;
    resume = next + 7;
    flag   = (uintptr_t)u9gl_back_face_flag();
    st = (uint8_t *)VirtualAlloc(NULL, 96, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("GLBACKFACE: no memory for the stubs - not installed"); return; }

    st[n++] = 0xFF; st[n++] = 0x05; *(uint32_t *)(st + n) = 0x0098C770u; n += 4;
    st[n++] = 0x60;
    st[n++] = 0xE8; put_rel32(st + n, (uintptr_t)&back_face_wanted); n += 4;
    st[n++] = 0x85; st[n++] = 0xC0;
    st[n++] = 0x61;
    st[n++] = 0x0F; st[n++] = 0x85; put_rel32(st + n, draw); n += 4;
    st[n++] = 0xE9; put_rel32(st + n, next); n += 4;
    n2 = n = (n + 15u) & ~15u;
    st[n++] = 0xC6; st[n++] = 0x05; *(uint32_t *)(st + n) = (uint32_t)flag; n += 4; st[n++] = 0x00;
    st[n++] = 0x8B; st[n++] = 0x44; st[n++] = 0x24; st[n++] = 0x1C;
    st[n++] = 0x83; st[n++] = 0xC7; st[n++] = 0x60;
    st[n++] = 0xE9; put_rel32(st + n, resume); n += 4;
    FlushInstructionCache(GetCurrentProcess(), st, 96);

    site_jump(jmp, 7, next, (uintptr_t)(st + n2));
    if (!u9patch_install_bytes(&BF_NEXT_SPEC, jmp, 7)) {
        VirtualFree(st, 0, MEM_RELEASE);
        u9log_line("GLBACKFACE: REFUSED at the loop's end (see the manifest) - back faces stay dropped");
        return;
    }
    site_jump(jmp, 11, skip, (uintptr_t)st);
    if (!u9patch_install_bytes(&BF_SKIP_SPEC, jmp, 11)) {
        u9log_line("GLBACKFACE: REFUSED at the back face (see the manifest) - back faces stay dropped");
        return;
    }
    g_bfInstalled = 1;
    u9log_line("GLBACKFACE: installed (stubs %08X, %08X) - back faces as casters %s", (unsigned)(uintptr_t)st,
               (unsigned)(uintptr_t)(st + n2), g_bfOn ? "ON" : "off (console: pool shadow backfaces on)");
}

void u9gl_back_face_report(void)
{
    if (g_bfInstalled)
        u9log_line("GLBACKFACE: %u back faces sent as casters, %u left out (translucent, second colour, not bound)",
                   g_bfSent, g_bfSkipped);
}
