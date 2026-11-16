#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../injection/config.h"
#include "../diag/log.h"
#include "../common/u9time.h"
#include "glfog.h"

int u9glpost_running(void);

static int   g_volume;
static char  g_list[128];
static char  g_greenList[64];
static int   g_greenDensity = 50;
static int   g_green;
static char  g_fallList[64];
static int   g_fall;
static int   g_fallDensity = 100;
static int   g_fallHeight = 0;
static int   g_fallSoft = 20;
static int   g_fallPad = 100;
static int   g_fallEdge = 100;
static int   g_density = 400;
static int   g_soft = 40;
static int   g_height = 25;
static int   g_noise = 100;
static int   g_wisp = 100;
static int   g_reach = 3000;
static int   g_keepS = 10;

static int      g_have;
static unsigned g_seenAt;
static float    g_lo[3], g_hi[3], g_meanZ, g_rgba[4];
static float    g_topZ;
static unsigned g_tris;
#define FOOT_GAP 4000.0f
#define FOOT_MAX 20000.0f
static float    g_ulo[2], g_uhi[2];
static unsigned g_footResets;

static void foot_add(const float lo[3], const float hi[3])
{
    int k, fresh = !g_have;
    for (k = 0; k < 2 && !fresh; k++)
        if (lo[k] > g_uhi[k] + FOOT_GAP || hi[k] < g_ulo[k] - FOOT_GAP ||
            (hi[k] > g_uhi[k] ? hi[k] : g_uhi[k]) - (lo[k] < g_ulo[k] ? lo[k] : g_ulo[k]) > FOOT_MAX)
            fresh = 1;
    if (fresh) {
        if (g_have) g_footResets++;
        for (k = 0; k < 2; k++) { g_ulo[k] = lo[k]; g_uhi[k] = hi[k]; }
        g_topZ = hi[2];
        return;
    }
    if (hi[2] > g_topZ) g_topZ = hi[2];
    for (k = 0; k < 2; k++) {
        if (lo[k] < g_ulo[k]) g_ulo[k] = lo[k];
        if (hi[k] > g_uhi[k]) g_uhi[k] = hi[k];
    }
}
static unsigned g_frames, g_framesSeen;

static void list_read(const char *s, char *g_list, size_t cap)
{
    size_t i, n = 1;
    g_list[0] = ',';
    for (i = 0; s[i] && n < cap - 2; i++) {
        char c = s[i] == ' ' ? ',' : s[i];
        if (c != ',' || g_list[n - 1] != ',') g_list[n++] = c;
    }
    if (n > 1 && g_list[n - 1] != ',') g_list[n++] = ',';
    g_list[n] = 0;
}

static int in_list(const char *list, int texId)
{
    char key[16];
    snprintf(key, sizeof key, ",%d,", texId);
    return list[1] && strstr(list, key) != NULL;
}

static int listed_fall(int texId) { return in_list(g_fallList, texId); }

static int listed(int texId)
{
    return in_list(g_list, texId) || in_list(g_fallList, texId);
}

static int listed_green(int texId)
{
    char key[16];
    snprintf(key, sizeof key, ",%d,", texId);
    return g_greenList[1] && strstr(g_greenList, key) != NULL;
}

void u9fog_init(void)
{
    g_volume  = u9cfg_int("gl_fog_volume", 1);
    list_read(u9cfg_str("gl_fog_volume_textures", "4948,4949,6415,4428"), g_list, sizeof g_list);
    list_read(u9cfg_str("gl_fog_volume_green_textures", "6415"), g_greenList, sizeof g_greenList);
    g_density = u9cfg_int("gl_fog_volume_density", 400);
    g_greenDensity = u9cfg_int("gl_fog_volume_green_density", 50);
    list_read(u9cfg_str("gl_fog_volume_fall_textures", ""), g_fallList, sizeof g_fallList);
    g_fallDensity = u9cfg_int("gl_fog_volume_fall_density", 100);
    g_fallHeight  = u9cfg_int("gl_fog_volume_fall_height", 0);
    g_fallSoft    = u9cfg_int("gl_fog_volume_fall_soft", 20);
    g_fallPad     = u9cfg_int("gl_fog_volume_fall_pad", 100);
    g_fallEdge    = u9cfg_int("gl_fog_volume_fall_edge", 100);
    g_soft    = u9cfg_int("gl_fog_volume_soft", 40);
    g_height  = u9cfg_int("gl_fog_volume_height", 25);
    g_noise   = u9cfg_int("gl_fog_volume_noise", 100);
    g_wisp    = u9cfg_int("gl_fog_volume_wisp", 100);
    g_reach   = u9cfg_int("gl_fog_volume_reach", 3000);
    g_keepS   = u9cfg_int("gl_fog_volume_keep", 10);
}

int u9fog_hides(int texId)
{
    return g_volume && listed(texId) && u9glpost_running();
}

void u9fog_frame(U9FogParams *o)
{
    float lo[3], hi[3], mz, rgba[4];
    unsigned tris, now = u9time_now();
    memset(o, 0, sizeof *o);
    g_frames++;
    if (u9gl_sheet_measure(listed, lo, hi, &mz, rgba, &tris)) {
        if (!g_have) u9log_line("FOG sheet seen: %u triangles, height %.0f..%.0f, %.0f x %.0f, colour %.0f,%.0f,%.0f alpha %.0f",
                                tris, (double)lo[2], (double)hi[2], (double)(hi[0] - lo[0]), (double)(hi[1] - lo[1]),
                                (double)(rgba[0] * 255.0f), (double)(rgba[1] * 255.0f), (double)(rgba[2] * 255.0f),
                                (double)(rgba[3] * 255.0f));
        foot_add(lo, hi);
        memcpy(g_lo, lo, sizeof lo);
        memcpy(g_hi, hi, sizeof hi);
        memcpy(g_rgba, rgba, sizeof rgba);
        {
            float glo[3], ghi[3], gz, grgba[4];
            unsigned gt;
            g_green = u9gl_sheet_measure(listed_green, glo, ghi, &gz, grgba, &gt);
            g_fall  = u9gl_sheet_measure(listed_fall, glo, ghi, &gz, grgba, &gt);
        }
        g_meanZ = mz;
        g_tris = tris;
        g_seenAt = now;
        g_have = 1;
        g_framesSeen++;
    } else if (g_have && (LONG)(now - g_seenAt) > (LONG)g_keepS * 1000) {
        g_have = 0;
        u9log_line("FOG sheet forgotten: not seen for %d s", g_keepS);
    }
    if (!g_volume || !g_have) return;
    o->on      = 1;
    if (g_fall) {
        float pad = (float)(g_fallPad < 0 ? 0 : g_fallPad);
        o->top     = g_topZ + (float)g_fallHeight;
        o->density = (float)g_fallDensity / 100.0f / 1000.0f;
        o->soft    = g_fallSoft > 1 ? (float)g_fallSoft : 1.0f;
        o->edge    = g_fallEdge > 1 ? (float)g_fallEdge : 1.0f;
        o->box[0]  = g_ulo[0] - pad; o->box[1] = g_ulo[1] - pad;
        o->box[2]  = g_uhi[0] + pad; o->box[3] = g_uhi[1] + pad;
    } else {
        o->top     = g_topZ + (float)g_height;
        o->density = (float)(g_green ? g_greenDensity : g_density) / 100.0f / 1000.0f;
        o->soft    = g_soft > 1 ? (float)g_soft : 1.0f;
        o->edge    = 300.0f;
        o->box[0]  = g_ulo[0] - 400.0f; o->box[1] = g_ulo[1] - 400.0f;
        o->box[2]  = g_uhi[0] + 400.0f; o->box[3] = g_uhi[1] + 400.0f;
    }
    o->rgb[0]  = g_rgba[0]; o->rgb[1] = g_rgba[1]; o->rgb[2] = g_rgba[2];
    o->noise   = (float)(g_noise < 0 ? 0 : g_noise > 100 ? 100 : g_noise) / 100.0f;
    o->scale   = 1.0f / (float)(g_wisp > 10 ? g_wisp : 10);
    o->time    = (float)(now % 3600000u) / 1000.0f;
    o->reach   = (float)(g_reach > 100 ? g_reach : 100);
}

void u9fog_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v = arg ? arg : "";
    unsigned now = u9time_now();
    while (*v == ' ') v++;
    if (!strncmp(v, "volume", 6)) {
        v += 6;
        while (*v == ' ') v++;
        if (!strcmp(v, "on")) g_volume = 1;
        else if (!strcmp(v, "off")) g_volume = 0;
        out("volume fog %s%s", g_volume ? "on: the mist sheet hidden, the layer under it fogged" : "off: the game's flat sheet",
            g_volume && !u9glpost_running() ? " - needs the post pass (gl_post = 1)" : "");
        u9log_line("FOG volume %s", g_volume ? "on" : "off");
        return;
    }
    if (!strncmp(v, "fall ", 5)) {
        const char *w = v + 5;
        if (!strncmp(w, "textures ", 9)) { list_read(w + 9, g_fallList, sizeof g_fallList); g_have = 0; }
        else if (!strncmp(w, "height ", 7)) g_fallHeight = atoi(w + 7);
        else if (!strncmp(w, "soft ", 5))   g_fallSoft = atoi(w + 5);
        else if (!strncmp(w, "pad ", 4))    g_fallPad = atoi(w + 4);
        else if (!strncmp(w, "edge ", 5))   g_fallEdge = atoi(w + 5);
        else if (*w >= '0' && *w <= '9')    g_fallDensity = atoi(w);
        else {
            out("fog fall <percent> | fall height <units> | fall soft <units> | fall pad <units> |"
                " fall edge <units> | fall textures <ids ...>");
            return;
        }
        u9log_line("FOG '%s' - fall (%s): density %d%%, height %+d, soft %d, pad %d, edge %d", v,
                   g_fallList[1] ? g_fallList : "none", g_fallDensity, g_fallHeight, g_fallSoft, g_fallPad, g_fallEdge);
    }
    else if (!strncmp(v, "density ", 8)) g_density = atoi(v + 8);
    else if (!strncmp(v, "green textures ", 15)) list_read(v + 15, g_greenList, sizeof g_greenList);
    else if (!strncmp(v, "green ", 6))  g_greenDensity = atoi(v + 6);
    else if (!strncmp(v, "soft ", 5))   g_soft = atoi(v + 5);
    else if (!strncmp(v, "height ", 7)) g_height = atoi(v + 7);
    else if (!strncmp(v, "noise ", 6))  g_noise = atoi(v + 6);
    else if (!strncmp(v, "wisp ", 5))   g_wisp = atoi(v + 5);
    else if (!strncmp(v, "reach ", 6))  g_reach = atoi(v + 6);
    else if (!strncmp(v, "textures ", 9)) {
        list_read(v + 9, g_list, sizeof g_list);
        g_have = 0;
    } else if (*v) {
        out("fog | fog volume on|off | density <percent> | green <percent> | soft <units> | height <units> |"
            " noise <percent> | wisp <units> | reach <units> | textures <ids ...> | green textures <ids ...> |"
            " fall <percent> | fall height|soft|pad|edge <units> | fall textures <ids ...>");
        return;
    }
    if (*v && strncmp(v, "fall ", 5)) u9log_line("FOG '%s' - density %d%%, green %d%%, soft %d, height %+d, noise %d%%, wisp %d, reach %d,"
                       " textures %s, green %s", v, g_density, g_greenDensity, g_soft, g_height, g_noise, g_wisp,
                       g_reach, g_list[1] ? g_list : "none", g_greenList[1] ? g_greenList : "none");
    if (g_have)
        out("mist sheet (%s): %u triangles, height %.0f..%.0f (mean %.0f - %s), %.0f x %.0f at %.0f,%.0f; colour %.0f,%.0f,%.0f alpha %.0f; last seen %u ms ago",
            g_list[1] ? g_list : "none", g_tris, (double)g_lo[2], (double)g_hi[2], (double)g_meanZ,
            g_hi[2] - g_lo[2] < 2.0f ? "flat" : "NOT flat", (double)(g_hi[0] - g_lo[0]), (double)(g_hi[1] - g_lo[1]),
            (double)((g_lo[0] + g_hi[0]) * 0.5f), (double)((g_lo[1] + g_hi[1]) * 0.5f),
            (double)(g_rgba[0] * 255.0f), (double)(g_rgba[1] * 255.0f), (double)(g_rgba[2] * 255.0f),
            (double)(g_rgba[3] * 255.0f), now - g_seenAt);
    else if (!strncmp(v, "textures ", 9))
        out("textures now %s: measured from the next frame - 'fog' again to see it", g_list[1] ? g_list : "none");
    else
        out("no mist sheet seen (%s) in the last %d s", g_list[1] ? g_list : "none", g_keepS);
    if (g_have)
        out("footprint so far %.0f x %.0f, %.0f..%.0f by %.0f..%.0f, top %.0f; started again %u times",
            (double)(g_uhi[0] - g_ulo[0]), (double)(g_uhi[1] - g_ulo[1]), (double)g_ulo[0], (double)g_uhi[0],
            (double)g_ulo[1], (double)g_uhi[1], (double)g_topZ, g_footResets);
    out("volume fog %s: density %d%%, green mist (%s) %d%%%s, soft %d, height %+d, noise %d%%, wisp %d, reach %d;"
        " sheet in %u of %u frames", g_volume ? "on" : "off (fog volume on)", g_density,
        g_greenList[1] ? g_greenList : "none", g_greenDensity, g_have && g_green ? " - in use now" : "", g_soft,
        g_height, g_noise, g_wisp, g_reach, g_framesSeen, g_frames);
    out("waterfall mist (%s)%s: density %d%%, height %+d, soft %d, pad %d, edge %d - in place of the above",
        g_fallList[1] ? g_fallList : "none", g_have && g_fall ? " - in use now" : "", g_fallDensity,
        g_fallHeight, g_fallSoft, g_fallPad, g_fallEdge);
}
