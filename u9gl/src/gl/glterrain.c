#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../diag/log.h"
#include "glterrain.h"

static const uint8_t TD_EXPECT[12] = {
    0x00, 0x00, 0x40, 0x45,
    0x00, 0x00, 0xA0, 0x45,
    0x00, 0x00, 0xC0, 0x45
};
static const U9PatchSpec TD_SPEC = {
    "GL_TERRAIN_DETAIL_DISTANCE", U9PATCH_STUB, U9SYM_TERRAIN_DETAIL_DISTS,
    12, TD_EXPECT, 12, U9PF_EXCLUSIVE
};

static int g_tdInstalled, g_tdDist;
static uintptr_t g_tdSite;

static void td_values(int d, uint8_t *b)
{
    float f[3];
    if (d <= 3072) { memcpy(b, TD_EXPECT, 12); return; }
    f[0] = (float)d;
    f[1] = (float)d * 5.0f / 3.0f;
    f[2] = (float)d * 2.0f;
    memcpy(b, f, 12);
}

static int td_apply(int d)
{
    uint8_t b[12];
    DWORD old;

    if (d < 0) d = 0;
    if (d > 100000) d = 100000;
    if (d <= 3072) d = 0;
    td_values(d, b);
    if (!g_tdInstalled) {
        if (!d) { g_tdDist = 0; return 1; }
        if (!u9patch_install_bytes(&TD_SPEC, b, sizeof b)) return 0;
        g_tdInstalled = 1;
        g_tdSite = u9sym(U9SYM_TERRAIN_DETAIL_DISTS);
    } else {
        if (!g_tdSite || !VirtualProtect((void *)g_tdSite, 12, PAGE_READWRITE, &old)) return 0;
        memcpy((void *)g_tdSite, b, 12);
        VirtualProtect((void *)g_tdSite, 12, old, &old);
    }
    g_tdDist = d;
    return 1;
}

void u9gl_terrain_detail_install(void)
{
    int d = u9cfg_int("gl_terrain_detail_distance", 0);

    if (d <= 3072) {
        u9log_line("GLTERRAIN land at full detail to 3072, as stock (gl_terrain_detail_distance %d);"
                   " post terrain <units> changes it", d);
        return;
    }
    if (!td_apply(d)) {
        u9log_line("GLTERRAIN: REFUSED (see the manifest) - land at full detail to 3072, as stock");
        return;
    }
    u9log_line("GLTERRAIN land at full detail to %d, coarser levels from %d and %d (stock 3072, 5120, 6144)",
               g_tdDist, g_tdDist * 5 / 3, g_tdDist * 2);
}

void u9gl_terrain_command(const char *arg, void (*out)(const char *fmt, ...))
{
    while (arg && *arg == ' ') arg++;
    if (arg && *arg) {
        char *e;
        long v = strtol(arg, &e, 10);
        if (e == arg) {
            out("post terrain [<units>]   how far the land keeps full detail; 0 = the game's 3072,"
                " 9216 = all of an 8000 view");
            return;
        }
        if (!td_apply((int)v)) {
            out("terrain: the patch was refused (see the log's manifest) - land stays at 3072");
            return;
        }
        u9log_line("GLTERRAIN console: land at full detail to %d", g_tdDist ? g_tdDist : 3072);
    }
    if (g_tdDist)
        out("land at full detail to %d, coarser from %d and %d (game: 3072, 5120, 6144);"
            " chunks follow as you move", g_tdDist, g_tdDist * 5 / 3, g_tdDist * 2);
    else
        out("land at full detail to 3072, the game's own; post terrain <units> to change it");
}
