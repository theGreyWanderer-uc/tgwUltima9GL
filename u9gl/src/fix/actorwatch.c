#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../injection/config.h"
#include "../common/u9time.h"
#include "fixmem.h"
#include "actorwatch.h"

void u9log_line(const char *fmt, ...);

#define ACT_SELF      0x18u
#define ACT_TARGET    0x104u
#define REC_NAME      0x04u
#define REC_HP        0x34u
#define REC_HPMAX     0x36u
#define REC_FLAGS     0x48u
#define REC_NOAI      0x53u
#define REC_NOTICE    0x54u
#define REC_LISTBITS  0xC4u
#define ACT_DELAY     0x24u
#define WORLD_PTR     0x00850228u
#define SCAN_FROM     0x108u
#define SCAN_MAX      ((0x3A0u - SCAN_FROM) / 4u)
#define MIN_SIZE      0x214u
#define LEARN         6u
#define MUTE_AFTER    12u
#define RAW_PER_LINE  8

typedef struct { uint32_t vt, size; } ClassSize;
static const ClassSize kSizes[] = {
    {0x00741CA8u,0x2D0u},{0x00742820u,0x214u},{0x00742F00u,0x23Cu},{0x007432E8u,0x2A0u},{0x007437D0u,0x230u},
    {0x00743E88u,0x214u},{0x007445B0u,0x2A4u},{0x007449C0u,0x314u},{0x00744FC0u,0x284u},{0x00745990u,0x214u},
    {0x00745D58u,0x244u},{0x00745F38u,0x29Cu},{0x007462B0u,0x27Cu},{0x00746650u,0x298u},{0x007469F8u,0x224u},
    {0x00746B78u,0x258u},{0x00746F30u,0x2A0u},{0x007470D8u,0x234u},{0x00747460u,0x29Cu},{0x00747A50u,0x258u},
    {0x00747C50u,0x258u},{0x00747E28u,0x2C0u},{0x00748A40u,0x268u},{0x00748F08u,0x2C0u},{0x00749128u,0x25Cu},
    {0x00749840u,0x240u},{0x00749A20u,0x244u},{0x00749E58u,0x224u},{0x00749FC8u,0x218u},{0x0074A138u,0x220u},
    {0x0074A2A8u,0x294u},{0x0074A458u,0x298u},{0x0074A600u,0x290u},{0x0074A7A0u,0x280u},{0x0074A940u,0x27Cu},
    {0x0074AB10u,0x26Cu},{0x0074AC90u,0x218u},{0x0074AE08u,0x244u},{0x0074B240u,0x29Cu},{0x0074B420u,0x244u},
    {0x0074B800u,0x284u},{0x0074BC18u,0x244u},{0x0074BE30u,0x2D0u},{0x0074C610u,0x240u},{0x0074CC70u,0x3A0u},
    {0x0074CE68u,0x284u},{0x0074D210u,0x294u},{0x0074D5B0u,0x29Cu},{0x0074D838u,0x2A8u},{0x0074E600u,0x23Cu},
    {0x0074E8B8u,0x258u},{0x00750070u,0x2B8u},{0x007502A0u,0x2B8u},{0x007506A8u,0x260u},{0x00750890u,0x23Cu},
    {0x00750FB8u,0x240u},{0x00751170u,0x228u},{0x007512F0u,0x228u},{0x00751650u,0x228u},
};
#define NSIZES   ((int)(sizeof kSizes / sizeof kSizes[0]))
#define NBUCKET  (NSIZES + 1)

typedef struct { unsigned off; char kind; const char *name; } Field;
#define NFIELD 6
typedef struct { uint32_t vt; const char *label; Field f[NFIELD]; } ClassInfo;
static const ClassInfo kClasses[] = {
    { 0x00741CA8u, "avatar", { {0} } },
    { 0x00748F08u, "caster", { {0x220,'i',"state"}, {0x224,'i',"mode"}, {0x2B4,'x',"spell"},
                               {0x2B8,'i',"pause"}, {0x174,'i',"countdown"}, {0x178,'i',"reload"} } },
    { 0x00747E28u, "caster-blink", { {0x220,'i',"state"}, {0x224,'i',"mode"}, {0x2B4,'x',"spell"},
                               {0x2B8,'i',"pause"}, {0x174,'i',"countdown"}, {0x178,'i',"reload"} } },
    { 0x007469F8u, "spirit", { {0x214,'i',"step"}, {0x218,'i',"attack"}, {0x21C,'i',"timer"},
                               {0x220,'f',"hold"} } },
    { 0x0074AE08u, "creature", { {0} } },
};
#define NCLASSES ((int)(sizeof kClasses / sizeof kClasses[0]))

static const struct { const char *name; uint32_t bit; } kFlags[] = {
    { "cannot_die", 0x00000001u }, { "poisoned", 0x00000002u }, { "diseased", 0x00000004u },
    { "hexed", 0x00000008u }, { "ward_active", 0x00000010u }, { "life_depleted", 0x00000020u },
    { "sleeping", 0x00000040u }, { "power_boosted", 0x00000080u }, { "spawn_seated", 0x00000100u },
    { "hidden", 0x00000200u }, { "immobilized", 0x00000400u }, { "cloned_archetype", 0x00000800u },
    { "beguiled", 0x00001000u }, { "damage_reflecting", 0x00002000u }, { "full_damage_reflection", 0x00004000u },
    { "fleeing", 0x00008000u }, { "spellcasting_disabled", 0x00010000u }, { "magic_resistant", 0x00020000u },
    { "name_revealed", 0x00040000u }, { "player_recognized", 0x00080000u }, { "position_locked", 0x00100000u },
    { "fear_immune", 0x00200000u }, { "has_actor", 0x00400000u }, { "arrival_trigger_armed", 0x00800000u },
    { "travelling", 0x01000000u }, { "route_search_failed", 0x02000000u }, { "hostile_mode", 0x04000000u },
    { "primary_hostile_mode", 0x08000000u }, { "dialogue_active", 0x10000000u },
    { "departure_trigger_armed", 0x20000000u }, { "routine_active", 0x40000000u },
    { "route_search_blocked", 0x80000000u },
};

typedef struct {
    uintptr_t act;
    uint32_t  vt, size;
    int       bucket, cls;
    int       x, y, z, mx, my, mz;
    DWORD     movedAt;
    int32_t   target;
    int       noAI, delayed, offList, noticing;
    unsigned  gateLines;
    unsigned  notice;
    uint32_t  flags;
    unsigned  hp, hpMax;
    uint32_t  field[NFIELD];
    uint32_t  raw[SCAN_MAX];
    char      name[33];
} Watched;

static Watched  g_w[ACTOR_COUNT];
static uint16_t g_chg[NBUCKET][SCAN_MAX], g_samples[NBUCKET];
static uint8_t  g_muted[NBUCKET][SCAN_MAX], g_nonMono[NBUCKET][SCAN_MAX];
static int      g_seeable = -1;
static unsigned g_hidden, g_notices;
static unsigned g_gateQuiet;
static unsigned g_classSeen[NBUCKET];
static int      g_on = -1, g_ms, g_radius, g_moveMs;
static unsigned g_lines, g_cap, g_capped, g_seen, g_samplesTotal;
static DWORD    g_t0, g_last;

static void alog(const char *fmt, ...)
{
    char buf[900];
    va_list ap;
    if (g_lines >= g_cap) {
        if (!g_capped) { g_capped = 1; u9log_line("ACTR line cap (%u) reached - the watch goes quiet; diag_actor_watch_lines raises it", g_cap); }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("ACTR %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static int size_index(uint32_t vt)
{
    int i;
    for (i = 0; i < NSIZES; i++) if (kSizes[i].vt == vt) return i;
    return NSIZES;
}

static int class_index(uint32_t vt)
{
    int i;
    for (i = 0; i < NCLASSES; i++) if (kClasses[i].vt == vt) return i;
    return -1;
}

static const char *class_label(const Watched *w, char *buf, size_t n)
{
    if (w->cls >= 0) snprintf(buf, n, "%s", kClasses[w->cls].label);
    else snprintf(buf, n, "class %08X", (unsigned)w->vt);
    return buf;
}

static void rec_name(unsigned slot, char out[33])
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0, r;
    int i, end = 0;
    out[0] = 0;
    if (!recs) return;
    r = recs + slot * RECORD_STRIDE;
    if (!readable4(r + REC_NAME + 28)) return;
    memcpy(out, (const void *)(r + REC_NAME), 32);
    out[32] = 0;
    for (i = 0; i < 32 && out[i]; i++) {
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) out[i] = '?';
        if (out[i] != ' ') end = i + 1;
    }
    out[end] = 0;
}

static int actor_pos(uintptr_t act, int *x, int *y, int *z)
{
    uintptr_t et, obj, page;
    uint32_t sh, xy;
    if (!readable4(act + ACT_SELF) || !readable4(ENTITY_TABLE) || !(et = rd32(ENTITY_TABLE))) return 0;
    sh = rd32(act + ACT_SELF);
    if (sh > 0x100000u || !readable4(et + sh * 0x0Cu + 8)) return 0;
    obj = rd32(et + sh * 0x0Cu + 8);
    if (!obj || !readable4(obj + 0x14) || (rd32(obj + 0x14) & 0x200u)) return 0;
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(obj + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16) + (int32_t)rd32(page + 0x10);
    *z = (int16_t)(rd32(obj + 8) & 0xFFFFu);
    return 1;
}

static int dist3(int ax, int ay, int az, int bx, int by, int bz)
{
    double dx = ax - bx, dy = ay - by, dz = az - bz;
    return (int)sqrt(dx * dx + dy * dy + dz * dz);
}

static void flag_diff(uint32_t was, uint32_t now, char *out, size_t n)
{
    size_t len = 0;
    unsigned i;
    out[0] = 0;
    for (i = 0; i < sizeof kFlags / sizeof kFlags[0] && len + 32 < n; i++) {
        uint32_t b = kFlags[i].bit;
        if ((was ^ now) & b) len += (size_t)snprintf(out + len, n - len, " %c%s", (now & b) ? '+' : '-', kFlags[i].name);
    }
}

static void fmt_field(const Field *f, uint32_t v, char *out, size_t n)
{
    if (f->kind == 'f') { float fl; memcpy(&fl, &v, 4); snprintf(out, n, "%.2f", (double)fl); }
    else if (f->kind == 'x') snprintf(out, n, "%08X", (unsigned)v);
    else snprintf(out, n, "%d", (int)v);
}

static void target_text(int32_t t, char *out, size_t n)
{
    char nm[33];
    if (t < 0) { snprintf(out, n, "nobody"); return; }
    if ((unsigned)t >= ACTOR_COUNT) { snprintf(out, n, "%d (out of range)", (int)t); return; }
    rec_name((unsigned)t, nm);
    snprintf(out, n, "%d '%s'", (int)t, nm);
}

static int avatar_seeable(uintptr_t *objOut, uint32_t *flagOut)
{
    uintptr_t w = readable4(WORLD_PTR) ? rd32(WORLD_PTR) : 0, cam, o;
    *objOut = 0; *flagOut = 0;
    if (!w || !readable4(w + 0xC0) || !(cam = rd32(w + 0xC0)) || !readable4(cam + 0x1C)) return -1;
    o = rd32(cam + 0x1C);
    *objOut = o;
    if (!o) return 1;
    if (!readable4(o + 0xAC)) return -1;
    *flagOut = rd32(o + 0xAC);
    return *flagOut != 0;
}

static int named_field(const Watched *w, unsigned off)
{
    int i;
    if (w->cls < 0) return 0;
    for (i = 0; i < NFIELD && kClasses[w->cls].f[i].name; i++) if (kClasses[w->cls].f[i].off == off) return 1;
    return 0;
}

static void gates_text(const Watched *w, char *out, size_t n)
{
    size_t len = 0;
    out[0] = 0;
    if (w->flags & 0x20u) len += (size_t)snprintf(out + len, n - len, " life_depleted");
    if (w->noAI)          len += (size_t)snprintf(out + len, n - len, " record+0x53");
    if (w->delayed)       len += (size_t)snprintf(out + len, n - len, " delay+0x24");
    if (w->offList)       len += (size_t)snprintf(out + len, n - len, " off-the-active-list");
    if (w->flags & 0x10000000u) len += (size_t)snprintf(out + len, n - len, " dialogue_active");
    if (g_seeable == 0 && len + 24 < n) snprintf(out + len, n - len, " camera-hides-the-Avatar");
}

static void begin(Watched *w, unsigned slot, uintptr_t act, uint32_t vt, uintptr_t rec, int ax, int ay, int az)
{
    char cl[48], tg[64];
    int i;
    memset(w, 0, sizeof *w);
    w->act = act;
    w->vt = vt;
    w->bucket = size_index(vt);
    w->size = w->bucket < NSIZES ? kSizes[w->bucket].size : MIN_SIZE;
    w->cls = class_index(vt);
    rec_name(slot, w->name);
    actor_pos(act, &w->x, &w->y, &w->z);
    w->mx = w->x; w->my = w->y; w->mz = w->z;
    w->movedAt = u9time_now();
    w->target = (int32_t)rd32(act + ACT_TARGET);
    w->flags = rd32(rec + REC_FLAGS);
    w->hp = *(const volatile uint16_t *)(rec + REC_HP);
    w->hpMax = *(const volatile uint16_t *)(rec + REC_HPMAX);
    w->noAI = *(const volatile uint8_t *)(rec + REC_NOAI) != 0;
    w->notice = *(const volatile uint16_t *)(rec + REC_NOTICE);
    w->offList = (rd32(rec + REC_LISTBITS) & 0x80u) != 0;
    w->delayed = (int32_t)rd32(act + ACT_DELAY) > 0;
    if (w->cls >= 0)
        for (i = 0; i < NFIELD && kClasses[w->cls].f[i].name; i++)
            if (kClasses[w->cls].f[i].off + 4 <= w->size) w->field[i] = rd32(act + kClasses[w->cls].f[i].off);
    for (i = 0; i < (int)SCAN_MAX && SCAN_FROM + 4u * (unsigned)i + 4 <= w->size; i++)
        w->raw[i] = rd32(act + SCAN_FROM + 4u * (unsigned)i);
    g_seen++;
    g_classSeen[w->bucket]++;
    target_text(w->target, tg, sizeof tg);
    {
        char gates[160];
        gates_text(w, gates, sizeof gates);
        alog("SEEN  slot %u '%s' %s (vt %08X, size 0x%X%s) at %d,%d,%d, %d from the Avatar; hp %u/%u; flags %08X;"
             " target %s; notices within %u; held by:%s",
             slot, w->name, class_label(w, cl, sizeof cl), (unsigned)vt, (unsigned)w->size,
             w->bucket < NSIZES ? "" : ", not built by Actor_Create - only its first 0x214 bytes are read",
             w->x, w->y, w->z, dist3(w->x, w->y, w->z, ax, ay, az), w->hp, w->hpMax, (unsigned)w->flags, tg,
             w->notice, *gates ? gates : " nothing");
    }
}

static void sample(Watched *w, unsigned slot, uintptr_t act, uintptr_t rec, int ax, int ay, int az)
{
    char buf[700], tg[64], tg2[64];
    int32_t target = (int32_t)rd32(act + ACT_TARGET);
    uint32_t flags = rd32(rec + REC_FLAGS);
    unsigned hp = *(const volatile uint16_t *)(rec + REC_HP), hpMax = *(const volatile uint16_t *)(rec + REC_HPMAX);
    int i, n, b = w->bucket, learning;
    size_t len;
    DWORD now = u9time_now();

    actor_pos(act, &w->x, &w->y, &w->z);
    if (target != w->target) {
        target_text(w->target, tg, sizeof tg);
        target_text(target, tg2, sizeof tg2);
        alog("TARGET slot %u '%s' %s -> %s, at %d,%d,%d", slot, w->name, tg, tg2, w->x, w->y, w->z);
        w->target = target;
    }
    if (flags != w->flags) {
        flag_diff(w->flags, flags, buf, sizeof buf);
        alog("FLAGS slot %u '%s' %08X -> %08X:%s", slot, w->name, (unsigned)w->flags, (unsigned)flags, buf);
        w->flags = flags;
    }
    if (hp != w->hp || hpMax != w->hpMax) {
        alog("HP    slot %u '%s' %u/%u -> %u/%u", slot, w->name, w->hp, w->hpMax, hp, hpMax);
        w->hp = hp; w->hpMax = hpMax;
    }
    {
        int noAI = *(const volatile uint8_t *)(rec + REC_NOAI) != 0;
        int offList = (rd32(rec + REC_LISTBITS) & 0x80u) != 0;
        int32_t delay = (int32_t)rd32(act + ACT_DELAY);
        int delayed = delay > 0, d, noticing;
        if (noAI != w->noAI || offList != w->offList || delayed != w->delayed) {
            if ((w->flags & 0x04000000u) || w->gateLines < 2) {
                w->gateLines++;
                alog("GATE  slot %u '%s' record+0x53 %d -> %d, delay+0x24 %s -> %s (%d), off the active list %d -> %d%s",
                     slot, w->name, w->noAI, noAI, w->delayed ? "running" : "none", delayed ? "running" : "none",
                     (int)delay, w->offList, offList,
                     (!(w->flags & 0x04000000u) && w->gateLines == 2) ? " (not hostile: further changes only counted)" : "");
            } else g_gateQuiet++;
            w->noAI = noAI; w->offList = offList; w->delayed = delayed;
        }
        d = dist3(w->x, w->y, w->z, ax, ay, az);
        noticing = slot != 0 && (w->flags & 0x04000000u) && w->target < 0 && w->notice && d <= (int)w->notice;
        if (noticing != w->noticing) {
            char gates[160];
            gates_text(w, gates, sizeof gates);
            if (noticing) {
                g_notices++;
                alog("NOTICE slot %u '%s' is hostile, has no target and is %d from the Avatar, within its %u - it should"
                     " perceive it; held by:%s", slot, w->name, d, w->notice, *gates ? gates : " nothing found");
            } else {
                char tg[64];
                target_text(w->target, tg, sizeof tg);
                alog("NOTICE slot %u '%s' ends: %d from the Avatar, target %s", slot, w->name, d, tg);
            }
            w->noticing = noticing;
        }
    }
    if (w->target >= 0 && (unsigned)w->target < ACTOR_COUNT && now - w->movedAt >= (DWORD)g_moveMs &&
        dist3(w->x, w->y, w->z, w->mx, w->my, w->mz) >= 16) {
        int tx = 0, ty = 0, tz = 0, have;
        uintptr_t tbl = rd32(ACTOR_TABLE), ta = readable4(tbl + 4u * (unsigned)w->target) ? rd32(tbl + 4u * (unsigned)w->target) : 0;
        have = ta && readable4(ta) && actor_pos(ta, &tx, &ty, &tz);
        if (have)
            alog("MOVE  slot %u '%s' at %d,%d,%d: %d from its target, %d from the Avatar", slot, w->name,
                 w->x, w->y, w->z, dist3(w->x, w->y, w->z, tx, ty, tz), dist3(w->x, w->y, w->z, ax, ay, az));
        else
            alog("MOVE  slot %u '%s' at %d,%d,%d: its target's position unreadable, %d from the Avatar", slot, w->name,
                 w->x, w->y, w->z, dist3(w->x, w->y, w->z, ax, ay, az));
        w->mx = w->x; w->my = w->y; w->mz = w->z;
        w->movedAt = now;
    }
    if (w->cls >= 0) {
        len = 0; buf[0] = 0;
        for (i = 0; i < NFIELD && kClasses[w->cls].f[i].name; i++) {
            const Field *f = &kClasses[w->cls].f[i];
            uint32_t v;
            char a[24], c[24];
            if (f->off + 4 > w->size) continue;
            v = rd32(act + f->off);
            if (v == w->field[i]) continue;
            fmt_field(f, w->field[i], a, sizeof a);
            fmt_field(f, v, c, sizeof c);
            if (len + 64 < sizeof buf) len += (size_t)snprintf(buf + len, sizeof buf - len, " %s %s -> %s", f->name, a, c);
            w->field[i] = v;
        }
        if (len) alog("FIELD slot %u '%s':%s", slot, w->name, buf);
    }
    if (slot == 0) return;
    learning = g_samples[b] < LEARN;
    if (g_samples[b] < 0xFFFFu) g_samples[b]++;
    len = 0; n = 0; buf[0] = 0;
    for (i = 0; i < (int)SCAN_MAX && SCAN_FROM + 4u * (unsigned)i + 4 <= w->size; i++) {
        uint32_t v = rd32(act + SCAN_FROM + 4u * (unsigned)i);
        if (v == w->raw[i]) continue;
        if (named_field(w, SCAN_FROM + 4u * (unsigned)i)) { w->raw[i] = v; continue; }
        if (g_chg[b][i] < 0xFFFFu) g_chg[b][i]++;
        if (v < w->raw[i]) g_nonMono[b][i] = 1;
        if (!g_muted[b][i] && g_samples[b] >= MUTE_AFTER &&
            (4u * g_chg[b][i] >= 3u * g_samples[b] || (!g_nonMono[b][i] && 2u * g_chg[b][i] >= g_samples[b]))) {
            char cl[48];
            g_muted[b][i] = 1;
            alog("MUTE  %s +0x%X changes on %u of %u samples%s - no longer listed", class_label(w, cl, sizeof cl),
                 SCAN_FROM + 4u * (unsigned)i, g_chg[b][i], g_samples[b],
                 g_nonMono[b][i] ? ", a timer" : ", only climbing - a clock");
        }
        if (!learning && !g_muted[b][i] && n < RAW_PER_LINE && len + 40 < sizeof buf) {
            len += (size_t)snprintf(buf + len, sizeof buf - len, " +0x%X %08X->%08X", SCAN_FROM + 4u * (unsigned)i,
                                    (unsigned)w->raw[i], (unsigned)v);
            n++;
        }
        w->raw[i] = v;
    }
    if (n) alog("RAW   slot %u '%s':%s", slot, w->name, buf);
}

void u9actor_tick(void)
{
    uintptr_t tbl, recs, av;
    int ax = 0, ay = 0, az = 0;
    unsigned i;
    DWORD now;

    if (g_on < 0) {
        g_on = u9cfg_int("diag_actor_watch", 0);
        g_ms = u9cfg_int("diag_actor_watch_ms", 500);
        g_radius = u9cfg_int("diag_actor_watch_radius", 6000);
        g_moveMs = u9cfg_int("diag_actor_watch_move_ms", 1000);
        g_cap = (unsigned)u9cfg_int("diag_actor_watch_lines", 30000);
        if (g_ms < 100) g_ms = 100;
        g_t0 = u9time_now();
        if (g_on)
            u9log_line("ACTR watch on: every %d ms, actors within %d of the Avatar (0 = all), MOVE every %d ms while"
                       " engaged, at most %u lines; %d classes sized, %d named", g_ms, g_radius, g_moveMs, g_cap,
                       NSIZES, NCLASSES);
    }
    if (!g_on) return;
    now = u9time_now();
    if (now - g_last < (DWORD)g_ms) return;
    g_last = now;
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (!tbl || !recs || !readable4(tbl + 4u * (ACTOR_COUNT - 1))) return;
    av = rd32(tbl);
    if (!av || !readable4(av) || !actor_pos(av, &ax, &ay, &az)) return;
    g_samplesTotal++;
    {
        uintptr_t o;
        uint32_t flag;
        int seeable = avatar_seeable(&o, &flag);
        if (seeable == 0) g_hidden++;
        if (seeable != g_seeable) {
            alog("SEEABLE the Avatar %s (camera +0x1C %08X%s, its +0xAC %08X%s)",
                 seeable > 0 ? "can be noticed by monsters" : seeable == 0 ? "CANNOT be noticed by monsters" :
                 "- the camera condition is unreadable", (unsigned)o,
                 o ? "" : " - none", (unsigned)flag,
                 (o && readable4(o)) ? "" : "");
            if (o && readable4(o)) alog("SEEABLE   that object's vtable %08X", (unsigned)rd32(o));
            g_seeable = seeable;
        }
    }

    for (i = 0; i < ACTOR_COUNT; i++) {
        Watched *w = &g_w[i];
        uintptr_t act = rd32(tbl + 4u * i), rec = recs + i * RECORD_STRIDE;
        uint32_t vt;
        int x, y, z, inRange, size;
        if (act && w->act && act != w->act) {
            alog("GONE  slot %u '%s' (its slot now holds another actor)", i, w->name);
            w->act = 0;
        }
        if (!act || !readable4(act) || !readable4(rec + REC_FLAGS)) {
            if (w->act) { alog("GONE  slot %u '%s' (removed)", i, w->name); w->act = 0; }
            continue;
        }
        vt = rd32(act);
        if (vt < IMAGE_LO || vt >= IMAGE_HI) continue;
        size = size_index(vt) < NSIZES ? (int)kSizes[size_index(vt)].size : (int)MIN_SIZE;
        if (!readable4(act + (unsigned)size - 4)) continue;
        if (!actor_pos(act, &x, &y, &z)) {
            if (w->act) { alog("GONE  slot %u '%s' (no position: its object is unloaded or hidden)", i, w->name); w->act = 0; }
            continue;
        }
        inRange = g_radius <= 0 || dist3(x, y, z, ax, ay, az) <= (w->act ? g_radius + g_radius / 10 : g_radius);
        if (!inRange) {
            if (w->act) { alog("GONE  slot %u '%s' (out of range, at %d,%d,%d)", i, w->name, x, y, z); w->act = 0; }
            continue;
        }
        if (!w->act) begin(w, i, act, vt, rec, ax, ay, az);
        else sample(w, i, act, rec, ax, ay, az);
    }
}

void u9actor_report(void)
{
    int b, i, muted;
    if (g_on <= 0) return;
    u9log_line("ACTR report: %u samples, %u actors seen, %u lines%s; the camera hid the Avatar from monsters on %u"
               " samples; %u NOTICE lines (hostile, in range, no target); %u GATE changes of peaceful actors not"
               " logged", g_samplesTotal, g_seen, g_lines,
               g_capped ? " (the cap was reached)" : "", g_hidden, g_notices, g_gateQuiet);
    for (b = 0; b < NBUCKET; b++) {
        char offs[400];
        size_t len = 0;
        if (!g_classSeen[b]) continue;
        muted = 0; offs[0] = 0;
        for (i = 0; i < (int)SCAN_MAX; i++)
            if (g_muted[b][i]) {
                muted++;
                if (len + 10 < sizeof offs) len += (size_t)snprintf(offs + len, sizeof offs - len, " +0x%X", SCAN_FROM + 4u * (unsigned)i);
            }
        if (b < NSIZES) {
            int c = class_index(kSizes[b].vt);
            u9log_line("ACTR   %s (vt %08X, size 0x%X): %u seen, %u samples, %d timers muted%s%s",
                       c >= 0 ? kClasses[c].label : "class", (unsigned)kSizes[b].vt, (unsigned)kSizes[b].size,
                       g_classSeen[b], g_samples[b], muted, muted ? ":" : "", offs);
        } else {
            u9log_line("ACTR   classes not built by Actor_Create: %u seen", g_classSeen[b]);
        }
    }
}
