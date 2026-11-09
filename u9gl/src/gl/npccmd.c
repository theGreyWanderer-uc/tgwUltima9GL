#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/symbols.h"
#include "../fix/fixmem.h"
#include "../fix/gold.h"
#include "npcscale.h"
#include "highway.h"
#include "npccmd.h"

#define WORLD           0x00850228u
#define REC_STATE       0x48u
#define ACT_MOVER       0x14u
#define ACT_TARGET      0x104u
#define MOVER_YAW       0x1A8u
#define VT_GETYAW       0xCCu
#define VT_PLACENEAR    0xE8u

typedef int  (U9CALL *PlaceNearFn)(void *mover, const float *dest, float facing, int testOnly);
typedef void (U9CALL *SetTargetFn)(void *actorTable, void *source, void *target);

static const struct { const char *name; uint32_t bit; } g_flags[] = {
    { "cannot_die", 0x00000001u },       { "poisoned", 0x00000002u },
    { "diseased", 0x00000004u },         { "hexed", 0x00000008u },
    { "ward_active", 0x00000010u },      { "life_depleted", 0x00000020u },
    { "sleeping", 0x00000040u },         { "power_boosted", 0x00000080u },
    { "spawn_seated", 0x00000100u },     { "hidden", 0x00000200u },
    { "immobilized", 0x00000400u },      { "cloned_archetype", 0x00000800u },
    { "beguiled", 0x00001000u },         { "damage_reflecting", 0x00002000u },
    { "full_damage_reflection", 0x00004000u }, { "fleeing", 0x00008000u },
    { "spellcasting_disabled", 0x00010000u },  { "magic_resistant", 0x00020000u },
    { "name_revealed", 0x00040000u },    { "player_recognized", 0x00080000u },
    { "position_locked", 0x00100000u },  { "fear_immune", 0x00200000u },
    { "has_actor", 0x00400000u }, { "arrival_trigger_armed", 0x00800000u },
    { "travelling", 0x01000000u },       { "route_search_failed", 0x02000000u },
    { "hostile_mode", 0x04000000u },     { "primary_hostile_mode", 0x08000000u },
    { "dialogue_active", 0x10000000u },  { "departure_trigger_armed", 0x20000000u },
    { "routine_active", 0x40000000u },   { "route_search_blocked", 0x80000000u },
};
#define NFLAGS ((int)(sizeof g_flags / sizeof g_flags[0]))

static const struct { const char *alias, *name; } g_alias[] = {
    { "actor", "has_actor" },          { "hostile", "hostile_mode" },
    { "routine", "routine_active" },   { "seated", "spawn_seated" },
};

static int current_map(void)
{
    uintptr_t w = readable4(WORLD) ? rd32(WORLD) : 0, m;
    m = (w && readable4(w + 0xCC)) ? rd32(w + 0xCC) : 0;
    return (m && readable4(m + 0x20)) ? (int)rd32(m + 0x20) : -1;
}

static int flag_index(const char *want, U9ConsoleOut out)
{
    int i;
    if (!_stricmp(want, "combat") || !_stricmp(want, "engaged_in_combat")) {
        out("(0x00400000 is not a combat flag: the engine sets it when an NPC's actor is"
            " created and clears it when the actor is destroyed - shown as has_actor)");
        want = "has_actor";
    }
    for (i = 0; i < (int)(sizeof g_alias / sizeof g_alias[0]); i++)
        if (!_stricmp(want, g_alias[i].alias)) { want = g_alias[i].name; break; }
    for (i = 0; i < NFLAGS; i++)
        if (!_stricmp(want, g_flags[i].name)) return i;
    return -1;
}

static uintptr_t actor_of(unsigned npc)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0, act, vt;
    if (!tbl || npc >= ACTOR_COUNT || !readable4(tbl + npc * 4)) return 0;
    act = rd32(tbl + npc * 4);
    if (!act || !readable4(act) || !readable4(act + ACT_TARGET)) return 0;
    vt = rd32(act);
    return (vt >= IMAGE_LO && vt < IMAGE_HI) ? act : 0;
}

static void no_actor(const U9NpcPlace *p, U9ConsoleOut out)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t act = (tbl && readable4(tbl + p->npc * 4)) ? rd32(tbl + p->npc * 4) : 0;
    if (!act) out("%u %s has no actor (its actor-table slot is empty)", p->npc, p->name);
    else      out("%u %s: actor %08X does not look like one", p->npc, p->name, (unsigned)act);
}

static uintptr_t record_of(unsigned npc)
{
    uintptr_t recs = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    return (recs && npc < ACTOR_COUNT && readable4(recs + npc * RECORD_STRIDE + REC_STATE))
           ? recs + npc * RECORD_STRIDE : 0;
}

static void split(const char *arg, char *name, size_t cap, const char **rest)
{
    size_t n = 0;
    while (*arg == ' ') arg++;
    while (*arg && *arg != ' ' && n + 1 < cap) name[n++] = *arg++;
    name[n] = 0;
    while (*arg && *arg != ' ') arg++;
    while (*arg == ' ') arg++;
    *rest = arg;
}

static void get_pos(const U9NpcPlace *p, U9ConsoleOut out)
{
    int ax, ay, az;
    if (u9avatar_position(&ax, &ay, &az))
        out("%u %s: map %d at %d,%d,%d  (%d away, %+d up from you)", p->npc, p->name, p->map,
            p->x, p->y, p->z, (int)hypotf((float)(p->x - ax), (float)(p->y - ay)), p->z - az);
    else
        out("%u %s: map %d at %d,%d,%d", p->npc, p->name, p->map, p->x, p->y, p->z);
}

static void set_pos(const U9NpcPlace *p, const char *val, U9ConsoleOut out)
{
    uintptr_t act = actor_of(p->npc), mover, vt;
    float dest[3], facing = 0.0f;
    char *end;
    int ok;

    if (!_stricmp(val, "here")) {
        int x, y, z;
        if (!u9avatar_position(&x, &y, &z)) { out("your position is not readable"); return; }
        dest[0] = (float)x; dest[1] = (float)y; dest[2] = (float)z;
    } else {
        dest[0] = (float)strtod(val, &end);
        if (end == val || (*end != ',' && *end != ' ')) { out("usage: set npc pos <name> <x,y[,z]|here>"); return; }
        val = end + 1;
        dest[1] = (float)strtod(val, &end);
        if (end == val) { out("usage: set npc pos <name> <x,y[,z]|here>"); return; }
        while (*end == ' ' || *end == ',') end++;
        if (*end) {
            val = end;
            dest[2] = (float)strtod(val, &end);
            if (end == val || *end) { out("usage: set npc pos <name> <x,y[,z]|here>"); return; }
        } else {
            dest[2] = (float)p->z;
        }
    }
    if (!act || !readable4(act + ACT_MOVER) || !(mover = rd32(act + ACT_MOVER)) || !readable4(mover)) {
        out("%u %s has no mover of a known kind", p->npc, p->name);
        return;
    }
    vt = rd32(mover);
    if (!readable4(vt + VT_PLACENEAR) || rd32(vt + VT_PLACENEAR) != u9sym(U9SYM_MOVER_PLACENEAR)) {
        out("%u %s's mover places itself some other way (vtable %08X) - not moved",
            p->npc, p->name, (unsigned)vt);
        return;
    }
    if (readable4(vt + VT_GETYAW) && rd32(vt + VT_GETYAW) == u9sym(U9SYM_MOVER_GETYAW) &&
        readable4(mover + MOVER_YAW)) {
        uint32_t v = rd32(mover + MOVER_YAW);
        memcpy(&facing, &v, 4);
        if (!(facing > -100.0f && facing < 100.0f)) facing = 0.0f;
    }
    ok = ((PlaceNearFn)u9sym(U9SYM_MOVER_PLACENEAR))((void *)mover, dest, facing, 0);
    {
        U9NpcPlace now;
        if (u9npc_live_place(p->npc, current_map(), &now))
            out("%u %s: asked for %.0f,%.0f,%.0f; now at %d,%d,%d%s", p->npc, p->name,
                dest[0], dest[1], dest[2], now.x, now.y, now.z,
                ok ? "" : " - no legal spot nearby, so put there regardless");
        else
            out("%u %s: asked for %.0f,%.0f,%.0f; %s", p->npc, p->name, dest[0], dest[1], dest[2],
                ok ? "placed" : "no legal spot nearby, put there regardless");
    }
}

static void get_target(const U9NpcPlace *p, U9ConsoleOut out)
{
    uintptr_t act = actor_of(p->npc), rec;
    int t;
    if (!act) { no_actor(p, out); return; }
    t = (int32_t)rd32(act + ACT_TARGET);
    if (t < 0) { out("%u %s: no target (%d); actor %08X, vtable %08X", p->npc, p->name, t,
                     (unsigned)act, (unsigned)rd32(act)); return; }
    rec = record_of((unsigned)t);
    if (t < (int)ACTOR_COUNT && rec) {
        char n[33];
        memcpy(n, (const void *)(rec + 4), 32);
        n[32] = 0;
        out("%u %s: target %d %s; actor %08X, vtable %08X", p->npc, p->name, t, n[0] ? n : "(no name)",
            (unsigned)act, (unsigned)rd32(act));
    } else {
        out("%u %s: target %d (outside the table)", p->npc, p->name, t);
    }
}

static void set_target(const U9NpcPlace *p, const char *who, int map, U9ConsoleOut out)
{
    uintptr_t act = actor_of(p->npc), src, dst, tbl;
    U9NpcPlace t;
    if (!act) { no_actor(p, out); return; }
    if (!*who) { out("usage: set npc target <name> <who|none>"); return; }
    if (!_stricmp(who, "none")) {
        *(volatile int32_t *)(act + ACT_TARGET) = -1;
        out("%u %s: target cleared (-1)", p->npc, p->name);
        return;
    }
    if (!u9npc_locate(who, map, &t, out)) return;
    src = u9npc_object(p->npc, map);
    dst = u9npc_object(t.npc, map);
    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    if (!src || !dst || !tbl) { out("both must stand on this map"); return; }
    ((SetTargetFn)u9sym(U9SYM_ACTOR_SET_COMBAT_TARGET))((void *)tbl, (void *)src, (void *)dst);
    if ((int32_t)rd32(act + ACT_TARGET) == (int)t.npc)
        out("%u %s: target %u %s (and hostile mode set)", p->npc, p->name, t.npc, t.name);
    else
        out("%u %s: the engine refused target %u %s (a friendly-to-you NPC cannot be your target)",
            p->npc, p->name, t.npc, t.name);
}

static void get_flag(const U9NpcPlace *p, const char *which, U9ConsoleOut out)
{
    uintptr_t rec = record_of(p->npc);
    uint32_t st;
    int i;
    if (!rec) { out("%u %s: record unreadable", p->npc, p->name); return; }
    st = rd32(rec + REC_STATE);
    if (*which) {
        if ((i = flag_index(which, out)) < 0) { out("no flag '%s' - get npc flag <name> lists them", which); return; }
        out("%u %s: %s = %d", p->npc, p->name, g_flags[i].name, (st & g_flags[i].bit) != 0);
        return;
    }
    out("%u %s: state %08X", p->npc, p->name, (unsigned)st);
    for (i = 0; i < NFLAGS; i++)
        if (st & g_flags[i].bit) out("  %08X %s", (unsigned)g_flags[i].bit, g_flags[i].name);
}

static void set_flag(const U9NpcPlace *p, const char *rest, U9ConsoleOut out)
{
    uintptr_t rec = record_of(p->npc);
    char which[40];
    const char *val;
    uint32_t before;
    int i;
    split(rest, which, sizeof which, &val);
    if (!which[0] || (strcmp(val, "0") && strcmp(val, "1"))) { out("usage: set npc flag <name> <flag> <0|1>"); return; }
    if ((i = flag_index(which, out)) < 0) { out("no flag '%s' - get npc flag <name> lists them", which); return; }
    if (!rec) { out("%u %s: record unreadable", p->npc, p->name); return; }
    before = rd32(rec + REC_STATE);
    *(volatile uint32_t *)(rec + REC_STATE) = val[0] == '1' ? before | g_flags[i].bit : before & ~g_flags[i].bit;
    out("%u %s: %s %d -> %c (state %08X -> %08X)", p->npc, p->name, g_flags[i].name,
        (before & g_flags[i].bit) != 0, val[0], (unsigned)before, (unsigned)rd32(rec + REC_STATE));
}

static void get_highway(const char *who, int map, U9ConsoleOut out)
{
    U9HighwayUse use[48];
    char name[40];
    int npc, n, i;

    if ((npc = u9npc_find_any(who, name, sizeof name, out)) < 0) return;
    n = u9highway_npc_points((unsigned)npc, use, 48);
    if (n < 0) { out("activity.flx was not read"); return; }
    if (n == 0) { out("%d %s: its schedule names no highway points", npc, name); return; }
    out("%d %s: %d highway point%s in its schedule - tppos highway <link> goes to one",
        npc, name, n, n == 1 ? "" : "s");
    for (i = 0; i < n; i++) {
        const U9HighwayPoint *p = u9highway_point(use[i].link);
        char maps[64];
        size_t m = 0;
        int k;
        maps[0] = 0;
        for (k = 0; p && k < p->nmaps && m + 8 < sizeof maps; k++)
            m += (size_t)snprintf(maps + m, sizeof maps - m, "%s%d", k ? "," : "", p->maps[k]);
        if (!p)
            out("  %5u  (not in highway.dat)  %s", use[i].link, use[i].record);
        else
            out("  %5u  %d,%d,%d  map %s%s  %s%s", use[i].link, p->x, p->y, p->z,
                maps[0] ? maps : "?", u9highway_pick_map(p, -1, map) == map ? " (here)" : "",
                use[i].record, use[i].relocate ? "  [relocate]" : "");
    }
}

static int npc_args(const char *arg, const char **what, char *name, size_t cap, const char **rest,
                    U9NpcPlace *p, int *map, U9ConsoleOut out, int set)
{
    static char word[16];
    const char *after;
    split(arg, word, sizeof word, &after);
    *what = word;
    if (strcmp(word, "pos") && strcmp(word, "target") && strcmp(word, "flag")) {
        out(set ? "usage: set npc pos|target|flag <name> ..."
                : "usage: get npc pos|target|flag|highway|size <name> ...");
        return 0;
    }
    split(after, name, cap, rest);
    if (!name[0]) { out("usage: %s npc %s <name> ...", set ? "set" : "get", word); return 0; }
    if ((*map = current_map()) < 0) { out("no world is loaded"); return 0; }
    return u9npc_locate(name, *map, p, out);
}

void u9npc_get_command(const char *arg, U9ConsoleOut out)
{
    const char *what, *rest;
    char name[40];
    U9NpcPlace p;
    int map;
    while (*arg == ' ') arg++;
    if (!strncmp(arg, "highway", 7) && (arg[7] == ' ' || !arg[7])) {
        const char *who = arg + 7;
        while (*who == ' ') who++;
        if (!*who) { out("usage: get npc highway <name>"); return; }
        get_highway(who, current_map(), out);
        return;
    }
    if (!strncmp(arg, "size", 4) && (arg[4] == ' ' || !arg[4])) {
        const char *who = arg + 4;
        while (*who == ' ') who++;
        u9npcscale_command(who, out);
        return;
    }
    if (!npc_args(arg, &what, name, sizeof name, &rest, &p, &map, out, 0)) return;
    if (!strcmp(what, "pos"))         get_pos(&p, out);
    else if (!strcmp(what, "target")) get_target(&p, out);
    else                              get_flag(&p, rest, out);
}

void u9npc_set_command(const char *arg, U9ConsoleOut out)
{
    const char *what, *rest;
    char name[40];
    U9NpcPlace p;
    int map;
    if (!npc_args(arg, &what, name, sizeof name, &rest, &p, &map, out, 1)) return;
    if (!strcmp(what, "pos"))         set_pos(&p, rest, out);
    else if (!strcmp(what, "target")) set_target(&p, rest, map, out);
    else                              set_flag(&p, rest, out);
}
