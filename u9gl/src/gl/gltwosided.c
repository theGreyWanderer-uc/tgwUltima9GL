#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../addresses.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../fix/fixmem.h"
#include "../diag/log.h"
#include "gltwosided.h"

#define TS_MAX 32
static int      g_tsTex[TS_MAX], g_nTs, g_tsOn = 1;
static unsigned g_tsPassed, g_tsLogged;
static int      g_tsSeen[TS_MAX];

const void *u9gl_current_renderer(void);

int u9gl_two_sided_tex(int texId)
{
    int i;
    if (!g_tsOn) return 0;
    for (i = 0; i < g_nTs; i++) if (g_tsTex[i] == texId) return 1;
    return 0;
}

static int __attribute__((cdecl, used)) two_sided_now(void)
{
    uintptr_t r = (uintptr_t)u9gl_current_renderer(), mat;
    int tex, i;
    if (!r || !g_tsOn || !readable4(r + U9_OFF_CUR_MATERIAL)) return 0;
    mat = rd32(r + U9_OFF_CUR_MATERIAL);
    if (!mat || !readable4(mat + U9_MAT_TEXID)) return 0;
    tex = *(const volatile int16_t *)(mat + U9_MAT_TEXID);
    for (i = 0; i < g_nTs; i++)
        if (g_tsTex[i] == tex) {
            g_tsPassed++;
            if (!g_tsSeen[i] && g_tsLogged < TS_MAX) {
                g_tsSeen[i] = 1;
                g_tsLogged++;
                u9log_line("GLTWOSIDED texture %d drawn from both sides (gl_two_sided_textures)", tex);
            }
            return 1;
        }
    return 0;
}

static const uint8_t TS_EXPECT[5] = { 0xF6, 0xC4, 0x04, 0x75, 0x28 };
static const U9PatchSpec TS_SPEC = {
    "GL_TWO_SIDED_FACES", U9PATCH_STUB, U9SYM_FACE_TWO_SIDED_TEST, 5, TS_EXPECT, 5, U9PF_EXCLUSIVE
};

static void put_rel32(uint8_t *at, uintptr_t target)
{
    *(int32_t *)at = (int32_t)(target - ((uintptr_t)at + 4));
}

void u9gl_two_sided_install(void)
{
    const char *s;
    uintptr_t site = u9sym(U9SYM_FACE_TWO_SIDED_TEST);
    uintptr_t draw, test;
    uint8_t *st, jmp[5];
    unsigned n = 0;

    g_tsOn = u9cfg_int("gl_two_sided", 0);
    s = u9cfg_str("gl_two_sided_textures", "6413,6414,3078,3079,6486,6487,3130");
    g_nTs = 0;
    while (s && *s && g_nTs < TS_MAX) {
        char *e;
        long v = strtol(s, &e, 10);
        if (e == s) { s++; continue; }
        g_tsTex[g_nTs++] = (int)v;
        s = e;
    }
    if (!g_tsOn) { u9log_line("GLTWOSIDED off by config - glows drawn from their front only, as stock"); return; }
    if (!site) return;
    draw = site + 0x2D;
    test = site + 5;
    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("GLTWOSIDED: no memory for the stub - not installed"); return; }

    st[n++] = 0xF6; st[n++] = 0xC4; st[n++] = 0x04;
    st[n++] = 0x0F; st[n++] = 0x85; put_rel32(st + n, draw); n += 4;
    st[n++] = 0x60;
    st[n++] = 0xE8; put_rel32(st + n, (uintptr_t)&two_sided_now); n += 4;
    st[n++] = 0x85; st[n++] = 0xC0;
    st[n++] = 0x61;
    st[n++] = 0x0F; st[n++] = 0x85; put_rel32(st + n, draw); n += 4;
    st[n++] = 0xE9; put_rel32(st + n, test); n += 4;
    FlushInstructionCache(GetCurrentProcess(), st, 64);

    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    if (!u9patch_install_bytes(&TS_SPEC, jmp, 5)) {
        VirtualFree(st, 0, MEM_RELEASE);
        u9log_line("GLTWOSIDED: REFUSED (see the manifest) - glows drawn from their front only");
        return;
    }
    u9log_line("GLTWOSIDED: installed - %d textures drawn from both sides (stub %08X)", g_nTs, (unsigned)(uintptr_t)st);
}

void u9gl_two_sided_report(void)
{
    if (g_tsPassed) u9log_line("GLTWOSIDED: %u faces drawn without the facing test", g_tsPassed);
}
