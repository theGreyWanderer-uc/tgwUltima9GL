#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../addresses.h"
#include "../fix/fixmem.h"
#include "../injection/config.h"
#include "../diag/log.h"
#include "glsun.h"
#include "glshadow.h"

int u9console_current_map(void);

#define MOON_PI       3.1415927f
#define MOON_TWO_PI   6.2831855f
#define MOON_TILT     0.52359879f
#define MOON_NO_MAP   14

int   g_moonShadowOn = 1;
static float g_moonShadowStrength = 0.4f;
static int   g_moonShadowPct[5] = { 0, 25, 50, 75, 100 };

static float rdf(uintptr_t p) { uint32_t v = rd32(p); float f; memcpy(&f, &v, 4); return f; }

static void rot(float m[12], char axis, float a)
{
    float c = cosf(a), s = sinf(a), p, q, r;
    int i;
    if (axis == 'x') {
        for (i = 0; i < 12; i += 4) { p = m[i + 1]; q = m[i + 2]; m[i + 1] = p * c + s * q; m[i + 2] = c * q - p * s; }
    } else if (axis == 'y') {
        for (i = 0; i < 12; i += 4) { p = m[i]; q = m[i + 2]; m[i] = p * c - s * q; m[i + 2] = p * s + c * q; }
    } else {
        for (i = 0; i < 12; i += 4) { p = m[i]; r = m[i + 1]; m[i] = p * c + s * r; m[i + 1] = c * r - p * s; }
    }
}

static float wrap(float a)
{
    while (a < 0.0f) a += MOON_TWO_PI;
    while (a >= MOON_TWO_PI) a -= MOON_TWO_PI;
    return a;
}

static void one_moon(U9MoonOne *o, int offset, float night, float A, float tilt)
{
    float m[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    float n;
    o->theta = wrap((float)offset * 45.0f * 0.017453292f - night * -MOON_PI);
    o->index = (int)(o->theta * 57.295776f * 0.022222223f);
    if (o->index > 7) o->index = 7;
    o->frac = 1.0f - ((float)(o->index + 1) * 0.7853982f - o->theta) * 1.2732395f;
    if (o->frac < 0.0f) o->frac = 0.0f;
    if (o->frac > 1.0f) o->frac = 1.0f;
    o->lit = (1.0f - fabsf(o->theta - MOON_PI) * (1.0f / MOON_PI)) * A;
    rot(m, 'x', 1.5707964f);
    rot(m, 'z', tilt);
    rot(m, 'y', o->theta);
    rot(m, 'z', 0.7853982f);
    o->dir[0] = m[1]; o->dir[1] = m[5]; o->dir[2] = m[9];
    n = sqrtf(o->dir[0] * o->dir[0] + o->dir[1] * o->dir[1] + o->dir[2] * o->dir[2]);
    if (n > 0.0f) { o->dir[0] /= n; o->dir[1] /= n; o->dir[2] /= n; }
    o->elevation = asinf(o->dir[U9SUN_UP] < -1.0f ? -1.0f : o->dir[U9SUN_UP] > 1.0f ? 1.0f : o->dir[U9SUN_UP])
                   / 0.017453292f;
}

static void engine_copy(U9MoonOne *o, uintptr_t sky, unsigned idxOff, unsigned haloOff, const float cam[3], int camOk)
{
    uintptr_t h;
    o->storedIndex = readable4(sky + idxOff) ? (int)(int16_t)(rd32(sky + idxOff) & 0xFFFFu) : -1;
    o->haloAlpha = -1;
    o->placedOk = 0;
    if (!readable4(sky + haloOff)) return;
    h = (uintptr_t)rd32(sky + haloOff);
    if (!h || !readable4(h + U9_SKYOBJ_ALPHA) || !readable4(h + U9_SKYOBJ_POS + 8)) return;
    o->haloAlpha = (int)(rd32(h + U9_SKYOBJ_ALPHA) & 0xFFu);
    if (camOk) {
        float d[3], n;
        int i;
        for (i = 0; i < 3; i++) d[i] = rdf(h + U9_SKYOBJ_POS + 4u * (unsigned)i) - cam[i];
        n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 1.0f) {
            for (i = 0; i < 3; i++) o->placed[i] = d[i] / n;
            o->placedOk = 1;
        }
    }
}

int u9moon_read(U9Moons *ms)
{
    uintptr_t clock, sky = U9_SKY, world, camObj;
    unsigned t, day;
    float night, A, storm, cam[3] = { 0, 0, 0 };
    int d, camOk = 0, i, map;
    if (!readable4(U9_WORLDCLOCK_PTR)) return 0;
    clock = (uintptr_t)rd32(U9_WORLDCLOCK_PTR);
    if (!clock || !readable4(clock + U9_CLOCK_DAY) || !readable4(sky + U9_SKY_STORM)) return 0;
    t   = rd32(clock + U9_CLOCK_TICKS) / 150u;
    day = rd32(clock + U9_CLOCK_DAY);
    if (t < 63001u) { day -= 1u; night = (float)(t + 23400u); }
    else night = (float)t - 63000.0f;
    night *= 2.1367521e-5f;
    if (night < 0.0f) night = 0.0f;
    if (night > 1.0f) night = 1.0f;
    A = 0.0f;
    if (night > 0.05f && night < 0.95f) {
        if (night < 0.15f) A = (night - 0.05f) * 10.0f;
        else {
            A = 1.0f;
            if ((double)night > 0.85) A = (1.0f - (night + 0.05f)) * 10.0f;
        }
    }
    storm = rdf(sky + U9_SKY_STORM);
    A -= storm * 4.0f;
    if (A < 0.0f) A = 0.0f;
    ms->time = (int)t;
    ms->day = (int)day;
    ms->night = night;
    ms->alpha = A;
    d = (int)day % 8;
    one_moon(&ms->m[0], d, night, A, MOON_TILT);
    one_moon(&ms->m[1], d + ((int)day + 1) * 3, night, A, -MOON_TILT);
    if (readable4(U9_WORLD_PTR)) {
        world = (uintptr_t)rd32(U9_WORLD_PTR);
        if (world && readable4(world + U9_WORLD_CAMERA)) {
            camObj = (uintptr_t)rd32(world + U9_WORLD_CAMERA);
            if (camObj && readable4(camObj + U9_CAMERA_POS + 8)) {
                for (i = 0; i < 3; i++) cam[i] = rdf(camObj + U9_CAMERA_POS + 4u * (unsigned)i);
                camOk = 1;
            }
        }
    }
    engine_copy(&ms->m[0], sky, U9_SKY_MOON_INDEX_A, U9_SKY_MOON_HALO_A, cam, camOk);
    engine_copy(&ms->m[1], sky, U9_SKY_MOON_INDEX_B, U9_SKY_MOON_HALO_B, cam, camOk);
    map = u9console_current_map();
    ms->shown = A > 0.0f && map != MOON_NO_MAP &&
                !(readable4(sky + U9_SKY_UNDERGROUND) && rd32(sky + U9_SKY_UNDERGROUND)) &&
                !(readable4(sky + U9_SKY_MOON_MAP) && rd32(sky + U9_SKY_MOON_MAP));
    return 1;
}

static void pct_list(const char *s)
{
    int k;
    for (k = 0; k < 5 && s && *s; k++) {
        while (*s == ' ' || *s == ',') s++;
        if (*s < '0' || *s > '9') break;
        g_moonShadowPct[k] = atoi(s);
        if (g_moonShadowPct[k] > 100) g_moonShadowPct[k] = 100;
        while (*s >= '0' && *s <= '9') s++;
    }
}

void u9moon_init(void)
{
    int pc;
    g_moonShadowOn = u9cfg_int("gl_moon_shadows", 1);
    pc = u9cfg_int("gl_moon_shadow_strength", 40);
    g_moonShadowStrength = (float)(pc < 0 ? 0 : pc > 100 ? 100 : pc) / 100.0f;
    pct_list(u9cfg_str("gl_moon_shadow_phase_percent", "0,25,50,75,100"));
}

static int phase_class(int index)
{
    static const signed char CLS[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    return CLS[index & 7];
}

float u9moon_shadow_strength(const U9Moons *ms, int k)
{
    const U9MoonOne *o = &ms->m[k];
    float share, fade;
    if (!ms->shown) return 0.0f;
    share = ((1.0f - o->frac) * (float)g_moonShadowPct[phase_class(o->index)] +
             o->frac * (float)g_moonShadowPct[phase_class(o->index + 1)]) / 100.0f;
    fade = (o->elevation - g_sunMinElevation) / 10.0f;
    if (fade > 1.0f) fade = 1.0f;
    if (fade < 0.0f) fade = 0.0f;
    return g_moonShadowStrength * share * ms->alpha * fade;
}

int u9moon_caster(const U9Moons *ms, float *strength)
{
    static int last = -1;
    float s0 = u9moon_shadow_strength(ms, 0), s1 = u9moon_shadow_strength(ms, 1);
    int k;
    if (last == 0 && s1 <= s0 * 1.1f) k = 0;
    else if (last == 1 && s0 <= s1 * 1.1f) k = 1;
    else k = s1 > s0 ? 1 : 0;
    *strength = k ? s1 : s0;
    if (*strength <= 0.001f) { last = -1; return -1; }
    if (k != last) u9log_line("MOON shadows cast by the %s moon, strength %.0f%%", k ? "orange" : "cyan",
                              (double)(*strength * 100.0f));
    last = k;
    return k;
}

void u9moon_command(const char *arg, void (*out)(const char *fmt, ...))
{
    static const char *const NAME[2] = { "cyan", "orange" };
    static const char *const PHASE[8] = { "new", "crescent", "half", "gibbous", "full", "gibbous",
                                          "half", "crescent" };
    U9Moons ms;
    float s;
    int k, caster, tt;
    while (arg && *arg == ' ') arg++;
    if (arg && !strncmp(arg, "shadow", 6)) {
        const char *v = arg + 6;
        while (*v == ' ') v++;
        if (!strcmp(v, "on"))                      g_moonShadowOn = 1;
        else if (!strcmp(v, "off"))                g_moonShadowOn = 0;
        else if (!strncmp(v, "strength ", 9)) {
            int pc = atoi(v + 9);
            g_moonShadowStrength = (float)(pc < 0 ? 0 : pc > 100 ? 100 : pc) / 100.0f;
        } else if (!strncmp(v, "phase ", 6))      pct_list(v + 6);
        else if (*v) {
            out("moon shadow on|off | strength <percent> | phase <new> <crescent> <half> <gibbous> <full>");
            return;
        }
        if (*v) u9log_line("MOON shadow: '%s' - %s, strength %.0f%%, phase %d,%d,%d,%d,%d", arg,
                           g_moonShadowOn ? "on" : "off", (double)(g_moonShadowStrength * 100.0f),
                           g_moonShadowPct[0], g_moonShadowPct[1], g_moonShadowPct[2], g_moonShadowPct[3],
                           g_moonShadowPct[4]);
        out("moon shadows %s, strength %.0f%%, by phase: new %d%%, crescent %d%%, half %d%%, gibbous %d%%, full %d%%",
            g_moonShadowOn ? "on" : "off", (double)(g_moonShadowStrength * 100.0f), g_moonShadowPct[0],
            g_moonShadowPct[1], g_moonShadowPct[2], g_moonShadowPct[3], g_moonShadowPct[4]);
        return;
    }
    if (!u9moon_read(&ms)) { out("the world clock cannot be read (no world loaded?)"); return; }
    tt = ms.time % 86400;
    out("moons at %02d:%02d, day %d: night %.2f, fade %.2f - %s", tt / 3600, tt / 60 % 60, ms.day,
        (double)ms.night, (double)ms.alpha, ms.shown ? "drawn" : "not drawn now");
    for (k = 0; k < 2; k++) {
        const U9MoonOne *o = &ms.m[k];
        float dot = o->placedOk ? o->dir[0] * o->placed[0] + o->dir[1] * o->placed[1] + o->dir[2] * o->placed[2] : 0.0f;
        out("  %s: %s (phase %d, %.0f%% into the next), %.1f deg up, toward (%.3f, %.3f, %.3f); light %.0f%%",
            NAME[k], PHASE[o->index & 7], o->index, (double)(o->frac * 100.0f), (double)o->elevation,
            (double)o->dir[0], (double)o->dir[1], (double)o->dir[2], (double)(o->lit * 100.0f));
        out("    the engine's copy: phase %d, halo alpha %d (ours %d), placed %s", o->storedIndex, o->haloAlpha,
            (int)(o->lit * 255.0f + 0.5f),
            !o->placedOk ? "unknown" : dot > 0.9995f ? "the same way" : "ANOTHER WAY - stale, or the formula is off");
        if (o->placedOk && dot <= 0.9995f)
            out("      placed toward (%.3f, %.3f, %.3f), %.1f deg apart", (double)o->placed[0], (double)o->placed[1],
                (double)o->placed[2], (double)(acosf(dot > 1.0f ? 1.0f : dot < -1.0f ? -1.0f : dot) / 0.017453292f));
    }
    caster = u9moon_caster(&ms, &s);
    out("  shadows %s, strength %.0f%%: %s", g_moonShadowOn ? "on" : "off (moon shadow on)",
        (double)(g_moonShadowStrength * 100.0f),
        caster < 0 ? "no moon casts now" : caster ? "the orange moon casts" : "the cyan moon casts");
    if (caster >= 0) out("    at %.0f%% (strength x phase x fade x height)", (double)(s * 100.0f));
    out("  'moon shadow ...' to change them; the map itself is the sun's ('sun shadow view', 'debug')");
}
