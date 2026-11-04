#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../addresses.h"
#include "../fix/fixmem.h"
#include "glsun.h"
#include "glshadow.h"
#include "glspaces.h"
#include "../fix/gold.h"

void u9glpost_sun_probe(void (*out)(const char *fmt, ...));

#define SUN_DEG_PER_SEC (1.0f / 240.0f)
#define SUN_OFFSET_DEG  90.0f
#define DEG2RAD         0.017453292f

static float rdf(uintptr_t p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

int u9sun_read(U9Sun *s)
{
    uintptr_t sky = U9_SKY;
    float a;
    int i;
    if (!readable4(sky + U9_SKY_TIME) || !readable4(sky + U9_SKY_STORM)) return 0;
    s->time        = (int)rd32(sky + U9_SKY_TIME);
    s->underground = (int)rd32(sky + U9_SKY_UNDERGROUND);
    s->moonMap     = (int)rd32(sky + U9_SKY_MOON_MAP);
    s->storm       = rdf(sky + U9_SKY_STORM);
    for (i = 0; i < 3; i++) s->stored[i] = rdf(sky + U9_SKY_SUN_DIR + 4u * (unsigned)i);
    a = ((float)s->time * SUN_DEG_PER_SEC - SUN_OFFSET_DEG) * DEG2RAD;
    s->dir[0] = cosf(a);
    s->dir[1] = 0.0f;
    s->dir[2] = sinf(a);
    s->elevation = asinf(s->dir[U9SUN_UP]) / DEG2RAD;
    return 1;
}

static void shadow_command(const char *v, void (*out)(const char *fmt, ...))
{
    while (*v == ' ') v++;
    if (!strcmp(v, "view")) {
        g_sunView = (g_sunView + 1) % 3;
        out(g_sunView == 1 ? "sun shadow view: the near map, top left - near the sun dark, nothing white (again: the far map)"
            : g_sunView == 2 ? "sun shadow view: the far map (again: off)" : "sun shadow view off");
    } else if (!strcmp(v, "debug")) {
        g_sunDebug = !g_sunDebug;
        out(g_sunDebug ? "sun shadow debug on: green lit, red in shadow, blue facing away from the sun"
                       : "sun shadow debug off");
    } else if (!strcmp(v, "on") || !strcmp(v, "preview")) {
        g_sunOn = 1;
        out("sun shadows on: shadowed ground darkened %d%%, rooms in shade except through open doors and windows",
            (int)(g_sunStrength * 100.0f + 0.5f));
    } else if (!strcmp(v, "off")) {
        g_sunOn = g_sunView = g_sunDebug = 0;
        out("sun shadows off, and their views");
    } else if (!strncmp(v, "strength ", 9)) {
        int pc = atoi(v + 9);
        if (pc < 0) pc = 0;
        if (pc > 100) pc = 100;
        g_sunStrength = (float)pc / 100.0f;
        out("sun shadow strength %d%%: how much of the light a shadow takes", pc);
    } else if (!strncmp(v, "range ", 6)) {
        g_sunRange = (float)atof(v + 6);
        if (g_sunRange < 500.0f) g_sunRange = 500.0f;
        out("sun shadows reach %.0f (the far map's half width)", (double)g_sunRange);
    } else if (!strcmp(v, "fade off")) {
        g_sunFadeFrom = g_sunFadeTo = 0.0f;
        out("sun shadows reach as far as the maps");
    } else if (!strncmp(v, "fade ", 5)) {
        float from = 0.0f, to = 0.0f;
        if (sscanf(v + 5, "%f %f", &from, &to) != 2 || to <= from) {
            out("sun shadow fade <from> <to>: gone by <to> units from the camera (fade off: no fade)");
        } else {
            g_sunFadeFrom = from;
            g_sunFadeTo = to;
            out("sun shadows fade out from %.0f to %.0f units from the camera", (double)from, (double)to);
        }
    } else if (!strcmp(v, "water on") || !strcmp(v, "water off")) {
        g_waterDepth = v[7] == 'n';
        out(g_waterDepth ? "water writes its surface's depth: shadows land on the water"
                         : "water writes no depth: shadows land on the bed under it (as before)");
    } else if (!strcmp(v, "probe")) {
        u9glpost_sun_probe(out);
        out("sun probe under the mouse (also in the log) - with the sun up and its shadows drawn:");
    } else if (!strncmp(v, "bias ", 5)) {
        g_sunBias = (float)atof(v + 5);
        if (g_sunBias < 0.0f) g_sunBias = 0.0f;
        out("sun shadow bias %.1f units (the lamps' is 'pool shadow bias')", (double)g_sunBias);
    } else if (!strcmp(v, "cull on") || !strcmp(v, "cull off")) {
        g_sunCull = v[6] == 'n';
        out(g_sunCull ? "sun shadow cull on: a caster outside a map's square, or too far for its shadow to show, is not drawn into it"
                      : "sun shadow cull off: every caster into every map (before 2026-10-30)");
    } else if (!strcmp(v, "walls on") || !strcmp(v, "walls off")) {
        g_sunWalls = v[7] == 'n';
        out(g_sunWalls ? "sun shadow walls on: a wall turned away from the sun fully shaded just past edge-on; one facing it shaded as squarely as it faces"
                       : "sun shadow walls off: walls graded as the ground, full only 20 degrees past edge-on (before 2026-10-30)");
    } else if (!strncmp(v, "step ", 5)) {
        g_sunStepMs = atoi(v + 5);
        if (g_sunStepMs < 0) g_sunStepMs = 0;
        if (g_sunStepMs) out("sun shadow step %d ms: the maps' direction moves that often, faded in over %d ms (%u steps so far)",
                             g_sunStepMs, g_sunXfadeMs < g_sunStepMs ? g_sunXfadeMs : g_sunStepMs, u9shadow_sun_steps());
        else out("sun shadow step 0: the direction every frame, no cross-fade (before 2026-10-30)");
    } else if (!strncmp(v, "xfade ", 6)) {
        g_sunXfadeMs = atoi(v + 6);
        if (g_sunXfadeMs < 0) g_sunXfadeMs = 0;
        if (g_sunStepMs) out("sun shadow xfade %d ms: each new direction fades in over that (at most the step, %d ms; 0 a cut)",
                             g_sunXfadeMs, g_sunStepMs);
        else out("sun shadow xfade %d ms, but the cross-fade is off (step 0) - 'sun shadow step 2000' turns it back on",
                 g_sunXfadeMs);
    } else if (!strcmp(v, "hold")) {
        g_sunHold = !g_sunHold;
        out(g_sunHold ? "sun shadow HOLD: the maps rebuilt from the sun's direction as it is now, held still (again: it moves)"
                      : "sun shadow direction follows the sun again");
    } else if (!strcmp(v, "smooth on") || !strcmp(v, "smooth off")) {
        g_sunSmooth = v[8] == 'n';
        out(g_sunSmooth ? "sun shadow smooth on: each sample blends the four texels round it - edges slide instead of flipping"
                        : "sun shadow smooth off: each sample the nearest texel (before 2026-10-27)");
    } else if (!strcmp(v, "freeze")) {
        g_sunFreeze = !g_sunFreeze;
        out(g_sunFreeze ? "sun shadow FROZEN: the last maps kept - if the twitching stops, the maps' contents were changing (again: live)"
                        : "sun shadow maps live again");
    } else if (!strncmp(v, "trace", 5) && (v[5] == ' ' || !v[5])) {
        unsigned follow = (unsigned)atoi(v + 5);
        u9shadow_sun_trace(follow);
        if (follow) out("sun shadow trace: 3 s of the sun's casters into the log (SUNTRACE), following gl tex %u - keep still", follow);
        else out("sun shadow trace: 3 s of the sun's casters per frame into the log (SUNTRACE) - keep still and watch the twitching");
    } else if (!strcmp(v, "steady on") || !strcmp(v, "steady off")) {
        g_sunSteady = v[8] == 'n';
        out(g_sunSteady ? "sun shadow steady on: the maps' grid held still while the sun moves, pivoting near the camera (taken again every %.0f deg; %u so far)"
                        : "sun shadow steady off: the grid turns with the sun (before 2026-10-27)", 3.0, u9shadow_sun_rebases());
    } else if (!strcmp(v, "slope on") || !strcmp(v, "slope off")) {
        g_sunSlopeOcc = v[7] == 'n';
        out(g_sunSlopeOcc ? "sun shadow slope on: a grazed surface's own slope across a texel is not taken for a blocker"
                          : "sun shadow slope off: the blocker distance as read (before 2026-10-27)");
    } else if (!strncmp(v, "near ", 5)) {
        g_sunNear = (float)atof(v + 5);
        if (g_sunNear < 250.0f) g_sunNear = 250.0f;
        out("the sharp near map is %.0f each way", (double)g_sunNear);
    } else if (!strcmp(v, "cascades on") || !strcmp(v, "cascades off")) {
        g_sunCascades = v[10] == 'n';
        out(g_sunCascades ? "sun shadow cascades on: a sharp near map and a far one"
                          : "sun shadow cascades off: one map out to the range");
    } else if (!strncmp(v, "ahead ", 6)) {
        g_sunAhead = (float)atof(v + 6) / 100.0f;
        if (g_sunAhead < 0.0f) g_sunAhead = 0.0f;
        if (g_sunAhead > 0.9f) g_sunAhead = 0.9f;
        out("each map centred %d%% of its half width ahead of the camera", (int)(g_sunAhead * 100.0f + 0.5f));
    } else if (!strncmp(v, "size ", 5)) {
        g_sunSize = atoi(v + 5);
        if (g_sunSize < 256) g_sunSize = 256;
        if (g_sunSize > 4096) g_sunSize = 4096;
        out("sun shadow map %d x %d", g_sunSize, g_sunSize);
    } else if (!strcmp(v, "memory")) {
        out("sun shadow memory is %s - memory on (the frame and the memory), off (the frame alone), only (the memory alone)",
            g_sunMemory == 2 ? "ONLY" : g_sunMemory ? "on" : "off");
    } else if (!strcmp(v, "memory only")) {
        g_sunMemory = 2;
        out("sun shadow memory only: the map drawn from remembered triangles alone - red in debug is the memory");
    } else if (!strcmp(v, "memory on") || !strcmp(v, "memory off")) {
        g_sunMemory = v[8] == 'n';
        if (!g_sunMemory) u9shadow_sun_forget();
        out(g_sunMemory ? "sun shadow memory on: roofs above the camera kept after they leave the screen"
                        : "sun shadow memory off: only what is on screen casts");
    } else if (!strncmp(v, "keep ", 5)) {
        g_sunKeepS = atoi(v + 5);
        if (g_sunKeepS < 1) g_sunKeepS = 1;
        out("when the memory fills, triangles unseen for %d s go first", g_sunKeepS);
    } else if (!strncmp(v, "above ", 6)) {
        g_sunAbove = (float)atof(v + 6);
        out("sun shadow memory takes triangles more than %.0f above the camera", (double)g_sunAbove);
    } else if (!strcmp(v, "forget")) {
        u9shadow_sun_forget();
        out("sun shadow memory cleared");
    } else if (!strncmp(v, "min ", 4)) {
        g_sunMinElevation = (float)atof(v + 4);
        out("no sun shadow below %.1f deg", (double)g_sunMinElevation);
    } else {
        out("sun shadow on | off | view | debug | strength <percent> | range <units> | size <texels> | min <degrees> |"
            " memory on|off|only | keep <s> | above <units> | forget | near <units> | cascades on|off | ahead <percent> |"
            " fade <from> <to> | fade off | bias <units> | cull on|off | walls on|off | step <ms> | xfade <ms> | slope on|off | steady on|off | freeze | hold | smooth on|off | trace [<gl tex>] | probe | water on|off");
    }
}

void u9sun_command(const char *arg, void (*out)(const char *fmt, ...))
{
    U9Sun s;
    float diff = 0.0f;
    int i, t;
    const char *where;
    while (arg && *arg == ' ') arg++;
    if (arg && !strncmp(arg, "shadow", 6)) { shadow_command(arg + 6, out); return; }
    if (!u9sun_read(&s)) { out("the sky state cannot be read (no world loaded?)"); return; }
    for (i = 0; i < 3; i++) {
        float d = fabsf(s.dir[i] - s.stored[i]);
        if (d > diff) diff = d;
    }
    t = ((s.time % 86400) + 86400) % 86400;
    where = s.elevation < -0.5f ? "below the horizon"
          : s.elevation > 85.0f ? "overhead"
          : s.dir[0] > 0.0f ? "toward +X (morning side)" : "toward -X (evening side)";
    out("sun at %02d:%02d (t %d s): direction (%.3f, %.3f, %.3f), %.1f deg above the horizon, %s",
        t / 3600, t / 60 % 60, s.time, (double)s.dir[0], (double)s.dir[1], (double)s.dir[2],
        (double)s.elevation, where);
    out("  the engine's copy (%.3f, %.3f, %.3f): %s", (double)s.stored[0], (double)s.stored[1], (double)s.stored[2],
        diff < 0.002f ? "matches" : "differs - stale while the sky is not drawn (underground, or between draws)");
    out("  storm %.2f, underground %s, moon map %s", (double)s.storm,
        s.underground ? "yes" : "no", s.moonMap ? "yes" : "no");
    out("  shadows %s, strength %d%%, bias %.1f, from %.0f deg; maps %d x %d: %s %.0f%s, %d%% ahead; %u triangles in the last of %u builds%s%s",
        g_sunOn ? "on" : "off (sun shadow on)", (int)(g_sunStrength * 100.0f + 0.5f), (double)g_sunBias,
        (double)g_sunMinElevation,
        g_sunSize, g_sunSize, g_sunCascades ? "near" : "one,", (double)(g_sunCascades ? g_sunNear : g_sunRange),
        g_sunCascades ? "" : " each way", (int)(g_sunAhead * 100.0f + 0.5f), g_sunTris, g_sunBuilds,
        g_sunView ? "; view on" : "", g_sunDebug ? "; debug on" : "");
    if (g_sunCascades) out("  far map %.0f each way", (double)g_sunRange);
    out("  caster runs drawn / culled: near map %u / %u, far map %u / %u (cull %s)",
        g_sunRunsDrawn[0], g_sunRunsCulled[0], g_sunRunsDrawn[1], g_sunRunsCulled[1], g_sunCull ? "on" : "off");
    if (g_sunStepMs > 0)
        out("  cross-fade: direction every %d ms, faded in over %d ms; %u steps so far%s", g_sunStepMs,
            g_sunXfadeMs < g_sunStepMs ? g_sunXfadeMs : g_sunStepMs, u9shadow_sun_steps(), g_sunHold ? " (held)" : "");
    else
        out("  cross-fade off: direction every frame");
    if (g_sunFadeTo > g_sunFadeFrom)
        out("  fading out from %.0f to %.0f from the camera", (double)g_sunFadeFrom, (double)g_sunFadeTo);
    out("  memory %s: %u triangles held, %u drawn from memory last frame, %u forgotten so far; keep %d s, above the camera + %.0f",
        g_sunMemory == 2 ? "only" : g_sunMemory ? "on" : "off", g_sunMemLive, g_sunMemDrawn, g_sunMemDropped, g_sunKeepS, (double)g_sunAbove);
    {
        int ax, ay, az, count = 0, j;
        float p[3];
        char name[33];
        if (u9avatar_position(&ax, &ay, &az)) {
            p[0] = (float)ax; p[1] = (float)ay; p[2] = (float)az + 40.0f;
            j = u9spaces_at(p, name, sizeof name, &count);
            if (j >= 0) {
                out("  the Avatar is in space '%s' (%d spaces on this map): shaded by the room test, sun in by open portals",
                    name, count);
                if (arg && !strcmp(arg, "room")) u9spaces_describe(j, out);
                else out("  'sun room' lists its planes and portals");
            }
            else out("  the Avatar is in no space (%d on this map): only the map shades here", count);
        }
    }
}
