#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fixmem.h"
#include "playerset.h"
#include "../common/symbols.h"

void u9log_line(const char *fmt, ...);

typedef struct { const char *name, *alt; uint16_t off; } Cell;

static const Cell STATS[3] = {
    { "str", "strength", 0x28 }, { "dex", "dexterity", 0x2C }, { "int", "intelligence", 0x30 },
};
static const Cell SKILLS[5] = {
    { "fists", "unarmed", 0x124 }, { "1h", "1-handed", 0x128 }, { "2h", "2-handed", 0x12C },
    { "staff", "blunt", 0x130 }, { "ranged", "bow", 0x134 },
};
static const char *const SKILL_WORD[4] = { "Novice", "Apprentice", "Journeyman", "Master" };

static uintptr_t record0(void)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    return (rec && readable4(rec) && readable4(rec + RECORD_STRIDE - 4)) ? rec : 0;
}

static const Cell *find(const Cell *t, int n, const char *w, size_t len)
{
    int i;
    for (i = 0; i < n; i++)
        if ((strlen(t[i].name) == len && !_strnicmp(t[i].name, w, len)) ||
            (strlen(t[i].alt) == len && !_strnicmp(t[i].alt, w, len)))
            return &t[i];
    return NULL;
}

static void list(const char *what, const Cell *t, int n, int words, uintptr_t rec,
                 void (*out)(const char *fmt, ...))
{
    char line[160];
    int i, k = 0;
    k += wsprintfA(line, "%s:", what);
    for (i = 0; i < n; i++) {
        int v = (int)rd32(rec + t[i].off);
        k += wsprintfA(line + k, "  %s %d", t[i].name, v);
        if (words && v >= 0 && v < 4) k += wsprintfA(line + k, " (%s)", SKILL_WORD[v]);
    }
    out("%s", line);
}

static void usage(int words, int write, void (*out)(const char *fmt, ...))
{
    if (write) out(words ? "set skill fists|1h|2h|staff|ranged <0-3>" : "set stat str|dex|int <0-3>");
    else       out(words ? "get skill [fists|1h|2h|staff|ranged]   bare: all five" : "get stat [str|dex|int]   bare: all three");
}

static void command(const char *verb, const Cell *t, int n, int words, int write, const char *arg,
                    void (*out)(const char *fmt, ...))
{
    uintptr_t rec = record0();
    const char *w = arg ? arg : "", *e, *v;
    const Cell *c;
    long val;
    char *end;
    int before;

    if (!rec) { out("%s %s: no Avatar record - is a game loaded?", write ? "set" : "get", verb); return; }
    while (*w == ' ') w++;
    if (!*w) {
        if (write) usage(words, 1, out);
        else list(verb, t, n, words, rec, out);
        return;
    }
    for (e = w; *e && *e != ' '; e++) {}
    c = find(t, n, w, (size_t)(e - w));
    if (!c) { usage(words, write, out); return; }
    for (v = e; *v == ' '; v++) {}
    before = (int)rd32(rec + c->off);
    if (!write) {
        if (*v) { usage(words, 0, out); return; }
        if (words && before >= 0 && before < 4) out("%s %d (%s)", c->name, before, SKILL_WORD[before]);
        else out("%s %d", c->name, before);
        return;
    }
    val = strtol(v, &end, 10);
    if (!*v || end == v || val < 0 || val > 3) { out("set %s %s: a level from 0 to 3", verb, c->name); return; }
    *(volatile int32_t *)(rec + c->off) = (int32_t)val;
    u9log_line("PLAYERSET %s %s %d -> %ld (console)", verb, c->name, before, val);
    if (words) out("%s %d -> %ld (%s)", c->name, before, val, SKILL_WORD[val]);
    else out("%s %d -> %ld", c->name, before, val);
}

#define STATE_FLAGS 0x48
static const struct { const char *name, *alt; uint32_t bit; const char *word; int live; } EFFECTS[] = {
    { "poison", "poisoned", 0x2, "Poisoned", 1 },        { "invisible", "invis", 0x200, "Invisible", 1 },
    { "sleep", "asleep", 0x40, "Asleep", 1 },        { "paralyse", "paralyze", 0x400, "Paralysed", 1 },
    { "lock", "freeze", 0x100000, "Locked in place", 1 }, { "beguile", "charm", 0x1000, "Beguiled", 1 },
    { "disease", "plague", 0x4, "Diseased", 0 },     { "hex", "curse", 0x8, "Hexed", 0 },
    { "ward", "protect", 0x10, "Ward", 0 },          { "boost", "power", 0x80, "Power boosted", 0 },
    { "reflect", "mirror", 0x2000, "Reflecting damage", 0 },
    { "reflectall", "mirrorall", 0x4000, "Reflecting all damage", 0 },
    { "nocast", "silence", 0x10000, "Spellcasting disabled", 0 }, { "resist", "resistant", 0x20000, "Magic resistant", 0 },
};
#define N_EFFECTS ((int)(sizeof EFFECTS / sizeof EFFECTS[0]))

const char *u9playerset_effect_name(int i) { return i >= 0 && i < N_EFFECTS ? EFFECTS[i].name : NULL; }

static void effect_list(uint32_t flags, void (*out)(const char *fmt, ...))
{
    char line[400];
    int i, k = 0, any = 0;
    k += wsprintfA(line, "effects (0x%08X):", (unsigned)flags);
    for (i = 0; i < N_EFFECTS; i++)
        if (flags & EFFECTS[i].bit) { k += wsprintfA(line + k, EFFECTS[i].live ? "  %s" : "  %s (no effect)", EFFECTS[i].name); any = 1; }
    if (!any) k += wsprintfA(line + k, "  none");
    out("%s", line);
}

static void effect_names(void (*out)(const char *fmt, ...))
{
    char line[400];
    int i, k = 0;
    k += wsprintfA(line, "acted on:");
    for (i = 0; i < N_EFFECTS; i++) if (EFFECTS[i].live) k += wsprintfA(line + k, " %s", EFFECTS[i].name);
    out("%s", line);
    k = wsprintfA(line, "names only, no effect:");
    for (i = 0; i < N_EFFECTS; i++) if (!EFFECTS[i].live) k += wsprintfA(line + k, " %s", EFFECTS[i].name);
    out("%s", line);
}

static uintptr_t avatar_actor(uintptr_t *table)
{
    uintptr_t t = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t a = (t && readable4(t)) ? rd32(t) : 0;
    if (table) *table = t;
    return (a && readable4(a + 0x74)) ? a : 0;
}

typedef void (U9CALL *AddPoisonFn)(void *actors, void *actor, int hits);

static int poison_avatar(int hits, void (*out)(const char *fmt, ...))
{
    uintptr_t table, actor = avatar_actor(&table);
    uintptr_t fn = u9sym(U9SYM_ACTOR_ADD_POISON);
    int before;
    if (!actor || !fn) { out("set effect poison: no Avatar actor - is a game loaded?"); return 0; }
    before = (int)rd32(actor + 0x70);
    ((AddPoisonFn)fn)((void *)table, (void *)actor, hits);
    u9log_line("PLAYERSET poison +%d hits: %d -> %d (console)", hits, before, (int)rd32(actor + 0x70));
    out("poisoned: %d hit(s) left, 1 HP each every 8 s (was %d)", (int)rd32(actor + 0x70), before);
    return 1;
}

static void effect_command(int write, int on, const char *arg, void (*out)(const char *fmt, ...))
{
    uintptr_t rec = record0();
    const char *w = arg ? arg : "", *e;
    volatile uint32_t *flags;
    uint32_t before, after;
    size_t len;
    int i;

    if (!rec) { out("%s effect: no Avatar record - is a game loaded?", !write ? "get" : on ? "set" : "unset"); return; }
    flags = (volatile uint32_t *)(rec + STATE_FLAGS);
    before = *flags;
    while (*w == ' ') w++;
    if (!write) { effect_list(before, out); return; }
    if (!*w) {
        out(on ? "set effect <name>" : "unset effect <name>|all");
        effect_names(out);
        effect_list(before, out);
        return;
    }
    for (e = w; *e && *e != ' '; e++) {}
    len = (size_t)(e - w);
    if (!on && len == 3 && !_strnicmp(w, "all", 3)) {
        uint32_t mask = 0;
        uintptr_t actor = avatar_actor(NULL);
        if (actor) { *(volatile int32_t *)(actor + 0x70) = 0; *(volatile int32_t *)(actor + 0x74) = 0; }
        for (i = 0; i < N_EFFECTS; i++) mask |= EFFECTS[i].bit;
        after = before & ~mask;
    } else {
        for (i = 0; i < N_EFFECTS; i++)
            if ((strlen(EFFECTS[i].name) == len && !_strnicmp(EFFECTS[i].name, w, len)) ||
                (strlen(EFFECTS[i].alt) == len && !_strnicmp(EFFECTS[i].alt, w, len)))
                break;
        if (i == N_EFFECTS) { out("%s effect: no effect '%.*s'", on ? "set" : "unset", (int)len, w); effect_names(out); return; }
        if (EFFECTS[i].bit == 0x2) {
            if (on) {
                const char *h = e;
                long hits;
                while (*h == ' ') h++;
                hits = *h ? strtol(h, NULL, 10) : 10;
                if (hits < 1 || hits > 1000) { out("set effect poison [hits]: 1 to 1000, 10 by default"); return; }
                if (poison_avatar((int)hits, out)) effect_list(*flags, out);
                return;
            } else {
                uintptr_t actor = avatar_actor(NULL);
                if (actor) { *(volatile int32_t *)(actor + 0x70) = 0; *(volatile int32_t *)(actor + 0x74) = 0; }
            }
        }
        after = on ? before | EFFECTS[i].bit : before & ~EFFECTS[i].bit;
    }
    *flags = after;
    u9log_line("PLAYERSET effect 0x%08X -> 0x%08X (console)", (unsigned)before, (unsigned)after);
    effect_list(after, out);
}

void u9playerset_get_effect(const char *arg, void (*out)(const char *fmt, ...))   { effect_command(0, 0, arg, out); }
void u9playerset_set_effect(const char *arg, void (*out)(const char *fmt, ...))   { effect_command(1, 1, arg, out); }
void u9playerset_unset_effect(const char *arg, void (*out)(const char *fmt, ...)) { effect_command(1, 0, arg, out); }

void u9playerset_get_stat(const char *arg, void (*out)(const char *fmt, ...))   { command("stat", STATS, 3, 0, 0, arg, out); }
void u9playerset_set_stat(const char *arg, void (*out)(const char *fmt, ...))   { command("stat", STATS, 3, 0, 1, arg, out); }
void u9playerset_get_skill(const char *arg, void (*out)(const char *fmt, ...))  { command("skill", SKILLS, 5, 1, 0, arg, out); }
void u9playerset_set_skill(const char *arg, void (*out)(const char *fmt, ...))  { command("skill", SKILLS, 5, 1, 1, arg, out); }
