#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/symbols.h"
#include "../fix/fixmem.h"
#include "../fix/gold.h"
#include "questcmd.h"

void u9log_line(const char *fmt, ...);

#define WORLD            0x00850228u
#define WORLD_MAP_MGR    0xCCu
#define DATUM_STORE      0x01333E64u
#define TRIGGER_SYSTEM   0x00AE7C6Cu
#define DATUM_COUNT      0x400
#define REC_MAP          0x58u

#define OBJ_TYPE         0x0Au
#define OBJ_FLAGS        0x14u
#define OBJ_LINK         0x1Au
#define HIDDEN_BITS      0x40100u

#define GATE_MAP         14
#define GATE_LINK        430
#define GATE_DATUM       69
#define GATE_X           8112
#define GATE_Y           7508
#define GATE_Z           1684
#define GATE_REACH       400
#define TYPE_GATE        641
#define TYPE_CLOUD       3746
#define TYPE_202         202
#define TYPE_OPENER      1094
#define SHOW_PLACE       0x4000u
#define TYPE_MOONGATE    4870
#define MOONGATE_REACH   600
#define FIND_BOTH_EXACT  7u
#define KIND_DELETE      11u

typedef void     (U9CALL *DatumSetFn)(void *store, int index, uint32_t value);
typedef uint8_t  (U9CALL *DatumGetFn)(void *store, int index);
typedef uint32_t (U9CALL *ExecFn)(void *sys, uintptr_t obj, uint32_t phase, uint32_t label);
typedef void     (U9CALL *ShowFn)(void *sys, uint32_t kind, uint32_t arg2, uintptr_t obj);
typedef void     (U9CALL *HideFn)(void *sys, uint32_t kind, uint32_t arg2, uintptr_t obj, uint32_t deferred);
typedef int      (U9CALL *CollectFn)(void *iter, uintptr_t *out, int max, void *mgr,
                                     const int32_t *pos, int halfExtent, uint32_t link);
typedef int      (U9CALL *FindTypeFn)(void *iter, uintptr_t *out, int max, void *mgr,
                                      const int32_t *pos, int halfExtent, uint32_t type, uint32_t flags);
typedef uint32_t (__attribute__((stdcall)) *HandleAllocFn)(uintptr_t obj);
typedef void     (__attribute__((stdcall)) *HandleFn)(uint32_t handle);
typedef uint8_t  (__attribute__((stdcall)) *EffectEndFn)(uint32_t handle);
typedef void     (U9CALL *PlaceAtFn)(uint32_t *handle, const void *loc, int zero);

typedef void (*OutFn)(const char *fmt, ...);

static void *datum_store(void)
{
    return readable4(DATUM_STORE) ? (void *)rd32(DATUM_STORE) : NULL;
}

static int datum_get(int index)
{
    DatumGetFn get = (DatumGetFn)u9sym(U9SYM_SHARED_DATUM_GET);
    void *store = datum_store();
    if (!get || !store || index < 0 || index >= DATUM_COUNT) return -1;
    return get(store, index);
}

static int datum_set(int index, int value)
{
    DatumSetFn set = (DatumSetFn)u9sym(U9SYM_SHARED_DATUM_SET);
    void *store = datum_store();
    if (!set || !store || index < 0 || index >= DATUM_COUNT) return 0;
    set(store, index, (uint32_t)value & 0xFFu);
    return 1;
}

void u9quest_datum_command(const char *arg, OutFn out)
{
    char *end;
    long index = strtol(arg, &end, 10), value;
    int before;

    if (end == arg || index < 0 || index >= DATUM_COUNT) { out("usage: datum <0-1023> [<0-255>]"); return; }
    before = datum_get((int)index);
    if (before < 0) { out("the datum store is not there yet - load a game first"); return; }
    while (*end == ' ') end++;
    if (!*end) { out("datum %ld = %d", index, before); return; }
    value = strtol(end, NULL, 10);
    if (value < 0 || value > 255) { out("a datum holds 0-255"); return; }
    datum_set((int)index, (int)value);
    out("datum %ld: %d -> %d", index, before, datum_get((int)index));
    u9log_line("QUEST datum %ld: %d -> %ld (console)", index, before, value);
}

static int avatar_map(void)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    return (rec && readable4(rec + REC_MAP)) ? (int)rd32(rec + REC_MAP) : -1;
}

static void *map_manager(void)
{
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0;
    return (world && readable4(world + WORLD_MAP_MGR)) ? (void *)rd32(world + WORLD_MAP_MGR) : NULL;
}

static const int32_t g_gatePos[3] = { GATE_X, GATE_Y, GATE_Z };

static int gate_objects(uintptr_t *out, int max)
{
    CollectFn collect = (CollectFn)u9sym(U9SYM_WORLD_COLLECT_BY_LINK);
    void *mgr = map_manager();
    uint32_t iter[32];

    out[0] = 0;
    if (!collect || !mgr) return 0;
    memset(iter, 0, sizeof iter);
    return collect(iter, out, max, mgr, g_gatePos, GATE_REACH, GATE_LINK);
}

static int moongates(uintptr_t *out, int max)
{
    FindTypeFn find = (FindTypeFn)u9sym(U9SYM_WORLD_FIND_BY_TYPE);
    void *mgr = map_manager();
    uint32_t iter[32];

    out[0] = 0;
    if (!find || !mgr) return 0;
    memset(iter, 0, sizeof iter);
    return find(iter, out, max, mgr, g_gatePos, MOONGATE_REACH, TYPE_MOONGATE, FIND_BOTH_EXACT);
}

static int obj_type(uintptr_t o) { return *(const volatile uint16_t *)(o + OBJ_TYPE); }
static int obj_hidden(uintptr_t o) { return (rd32(o + OBJ_FLAGS) & HIDDEN_BITS) == HIDDEN_BITS; }

static const char *type_name(int type)
{
    switch (type) {
    case TYPE_GATE:   return "the gate";
    case TYPE_CLOUD:  return "the cloud";
    case TYPE_202:    return "type 202";
    case TYPE_OPENER: return "trigger 910's object";
    case 1134:        return "the marker";
    default:          return "other";
    }
}

static void gate_status(OutFn out)
{
    uintptr_t objs[64];
    int i, n = gate_objects(objs, 64);

    uintptr_t gates[16];
    int g = moongates(gates, 16);

    out("datum %d = %d (the circle opens at 1)", GATE_DATUM, datum_get(GATE_DATUM));
    out("standing moongates (type %d): %d%s", TYPE_MOONGATE, g, g ? " - walking in goes to map 41" : "");
    if (!n) { out("no object with link %d near the gate - is this map 14?", GATE_LINK); return; }
    for (i = 0; i < n; i++) {
        int t = obj_type(objs[i]);
        out("  type %-5d %-22s %s  (flags %08X)", t, type_name(t),
            obj_hidden(objs[i]) ? "hidden" : "showing", (unsigned)rd32(objs[i] + OBJ_FLAGS));
    }
}

static uintptr_t find_type(const uintptr_t *objs, int type)
{
    for (; *objs; objs++) if (obj_type(*objs) == type) return *objs;
    return 0;
}

static void gate_on(OutFn out)
{
    uintptr_t objs[64], gates[16], opener, gate;
    void *sys = readable4(TRIGGER_SYSTEM) ? (void *)rd32(TRIGGER_SYSTEM) : NULL;
    int standing;

    gate_objects(objs, 64);
    opener = find_type(objs, TYPE_OPENER);
    gate = find_type(objs, TYPE_GATE);
    if (!gate || !sys) { out("the gate is not on this map (map 14, by the Gypsy's wagon)"); return; }
    datum_set(GATE_DATUM, 1);
    if (!moongates(gates, 16) && opener) {
        ((ExecFn)u9sym(U9SYM_TRIGGER_EXECUTE))(sys, opener, 0, 0);
        u9log_line("QUEST gate on: datum 69 = 1, trigger 910 run on %08X", (unsigned)opener);
    }
    if (obj_hidden(gate)) {
        ((ShowFn)u9sym(U9SYM_TRIGGER_SHOW_OBJECT))(sys, 10, SHOW_PLACE, gate);
        u9log_line("QUEST gate on: the gate shown directly");
    }
    standing = moongates(gates, 16);
    u9log_line("QUEST gate on: %d moongates standing, the 641 %s", standing, obj_hidden(gate) ? "hidden" : "showing");
    if (standing)
        out("gate open: %d moongate%s standing; datum %d = %d",
            standing, standing == 1 ? "" : "s", GATE_DATUM, datum_get(GATE_DATUM));
    else
        out("gate rising - the moongate stands once the effect ends (gate shows it); datum %d = %d",
            GATE_DATUM, datum_get(GATE_DATUM));
}

static int end_effects(uintptr_t obj)
{
    HandleAllocFn alloc = (HandleAllocFn)u9sym(U9SYM_HANDLE_ALLOC);
    HandleFn addref = (HandleFn)u9sym(U9SYM_HANDLE_ADDREF), release = (HandleFn)u9sym(U9SYM_HANDLE_RELEASE);
    EffectEndFn end = (EffectEndFn)u9sym(U9SYM_EFFECT_END_FOR_OBJECT);
    uint32_t h;
    int ended;

    if (!alloc || !addref || !release || !end) return 0;
    h = alloc(obj);
    if (!h) return 0;
    addref(h);
    ended = end(h) != 0;
    release(h);
    return ended;
}

static void gate_off(OutFn out)
{
    static const int closes[] = { TYPE_GATE, TYPE_CLOUD, TYPE_202 };
    uintptr_t objs[64], gates[16];
    void *sys = readable4(TRIGGER_SYSTEM) ? (void *)rd32(TRIGGER_SYSTEM) : NULL;
    unsigned i, hid = 0, ended = 0, removed = 0;

    gate_objects(objs, 64);
    if (!find_type(objs, TYPE_GATE) || !sys) { out("the gate is not on this map (map 14, by the Gypsy's wagon)"); return; }
    datum_set(GATE_DATUM, 0);
    moongates(gates, 16);
    for (i = 0; gates[i]; i++) {
        end_effects(gates[i]);
        ((HideFn)u9sym(U9SYM_TRIGGER_HIDE_OBJECT))(sys, KIND_DELETE, 0, gates[i], 0);
        removed++;
    }
    for (i = 0; objs[i]; i++)
        if (end_effects(objs[i])) {
            ended++;
            u9log_line("QUEST gate off: effect ended on type %d (%08X)", obj_type(objs[i]), (unsigned)objs[i]);
        }
    for (i = 0; i < sizeof closes / sizeof closes[0]; i++) {
        uintptr_t o = find_type(objs, closes[i]);
        if (!o || obj_hidden(o)) continue;
        ((HideFn)u9sym(U9SYM_TRIGGER_HIDE_OBJECT))(sys, 9, 0, o, 0);
        hid++;
    }
    u9log_line("QUEST gate off: datum 69 = 0, %u moongates removed, %u effects ended, %u objects hidden",
               removed, ended, hid);
    out("gate closed: %u moongate%s removed, %u effects ended, %u objects hidden; datum %d = %d",
        removed, removed == 1 ? "" : "s", ended, hid, GATE_DATUM, datum_get(GATE_DATUM));
}

static int obj_where(uintptr_t o, int p[3])
{
    uintptr_t page;
    uint32_t xy;
    if (!o || !readable4(o + 0x14) || (rd32(o + 0x14) & 0x200u)) return 0;
    page = o & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(o + 4);
    p[0] = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    p[1] = (int16_t)(xy >> 16) + (int32_t)rd32(page + 0x10);
    p[2] = (int16_t)(rd32(o + 8) & 0xFFFFu);
    return 1;
}

static int shrine_reward(uintptr_t o)
{
    static const uint16_t links[] = { 1034, 1280, 1300, 1310, 1320, 1330, 1340, 1350 };
    unsigned i, link;
    if (rd32(o + OBJ_FLAGS) & 1u) return 0;
    link = *(const volatile uint16_t *)(o + OBJ_LINK);
    for (i = 0; i < sizeof links / sizeof links[0]; i++) if (link == links[i]) return 1;
    return 0;
}

static int show_nearest(void *sys, void *mgr, const int32_t at[3], int type, int reach, int got[3],
                        int *already, int *kept)
{
    FindTypeFn find = (FindTypeFn)u9sym(U9SYM_WORLD_FIND_BY_TYPE);
    uintptr_t objs[64], best = 0;
    uint32_t iter[32];
    double bestD = 1e30;
    int n, i, p[3];
    *already = *kept = 0;
    memset(iter, 0, sizeof iter);
    n = find(iter, objs, 64, mgr, at, reach, (uint32_t)type, FIND_BOTH_EXACT);
    for (i = 0; i < n; i++) {
        double dx, dy, dz, d;
        if (!objs[i] || !readable4(objs[i] + OBJ_FLAGS)) continue;
        if (!obj_hidden(objs[i])) { (*already)++; continue; }
        if (shrine_reward(objs[i])) { (*kept)++; continue; }
        if (!obj_where(objs[i], p)) continue;
        dx = p[0] - at[0]; dy = p[1] - at[1]; dz = p[2] - at[2];
        d = dx * dx + dy * dy + dz * dz;
        if (d < bestD) { bestD = d; best = objs[i]; got[0] = p[0]; got[1] = p[1]; got[2] = p[2]; }
    }
    if (!best) return 0;
    ((ShowFn)u9sym(U9SYM_TRIGGER_SHOW_OBJECT))(sys, 10, SHOW_PLACE, best);
    u9log_line("QUEST show: type %d object %08X at %d,%d,%d shown (flags now %08X)", type, (unsigned)best,
               got[0], got[1], got[2], (unsigned)rd32(best + OBJ_FLAGS));
    return 1;
}

static const struct { const char *name; int rune, glyph, item; const char *itemName; } VIRTUES[] = {
    { "compassion",   2940, 4883, 4956, "Heart" },
    { "honesty",      2941, 4889, 4938, "Writ" },
    { "honor",        2942, 4884, 5472, "Chalice" },
    { "humility",     2943, 4885, 5496, "Crook" },
    { "justice",      2944, 4886, 5508, "Quill" },
    { "sacrifice",    2945, 4887, 5468, "Tear" },
    { "spirituality", 2946, 4888, 5291, "Ankh" },
    { "valor",        2947, 4890, 5288, "Dagger" },
};

static int show_virtue(const char *arg, OutFn out)
{
    void *sys = readable4(TRIGGER_SYSTEM) ? (void *)rd32(TRIGGER_SYSTEM) : NULL;
    void *mgr = map_manager();
    size_t len = 0;
    const char *which;
    int v, x, y, z, k;
    int32_t at[3];
    while (arg[len] && arg[len] != ' ') len++;
    for (v = 0; v < (int)(sizeof VIRTUES / sizeof VIRTUES[0]); v++)
        if (len >= 4 && !_strnicmp(arg, VIRTUES[v].name, len)) break;
    if (v == (int)(sizeof VIRTUES / sizeof VIRTUES[0])) return 0;
    which = arg + len;
    while (*which == ' ') which++;
    if (!u9sym(U9SYM_WORLD_FIND_BY_TYPE) || !sys || !mgr || !u9avatar_position(&x, &y, &z)) {
        out("no world loaded");
        return 1;
    }
    at[0] = x; at[1] = y; at[2] = z;
    for (k = 0; k < 3; k++) {
        static const char *const part[3] = { "item", "glyph", "rune" };
        int type = k == 0 ? VIRTUES[v].item : k == 1 ? VIRTUES[v].glyph : VIRTUES[v].rune, got[3], already, kept;
        char label[32];
        if (*which && _stricmp(which, part[k])) continue;
        snprintf(label, sizeof label, "%s", k == 0 ? VIRTUES[v].itemName : k == 1 ? "Glyph" : "Rune");
        if (show_nearest(sys, mgr, at, type, 3000, got, &already, &kept))
            out("%s of %s shown at %d,%d,%d", label, VIRTUES[v].name, got[0], got[1], got[2]);
        else if (kept)
            out("%s of %s: only the shrine's own copy is hidden here - left for the cleansing to reveal",
                label, VIRTUES[v].name);
        else if (already)
            out("%s of %s: none hidden within 3000 (%d already showing)", label, VIRTUES[v].name, already);
        else
            out("%s of %s: none within 3000 - stand at its shrine", label, VIRTUES[v].name);
    }
    if (!*which)
        out("the shrine needs the glyph and the %s; the cleansing makes the rune and its own %s",
            VIRTUES[v].itemName, VIRTUES[v].itemName);
    return 1;
}

static void bring_down(uintptr_t o, int x, int y, int z)
{
    HandleAllocFn alloc = (HandleAllocFn)u9sym(U9SYM_HANDLE_ALLOC);
    HandleFn addref = (HandleFn)u9sym(U9SYM_HANDLE_ADDREF), release = (HandleFn)u9sym(U9SYM_HANDLE_RELEASE);
    struct { int32_t x, y; int16_t z, pad; } loc;
    uint32_t h;

    if (!alloc || !addref || !release || !u9sym(U9SYM_WORLDOBJ_PLACE_AT) || !(h = alloc(o))) return;
    memset(&loc, 0, sizeof loc);
    loc.x = x + 60; loc.y = y; loc.z = (int16_t)(z + 40);
    addref(h);
    ((PlaceAtFn)u9sym(U9SYM_WORLDOBJ_PLACE_AT))(&h, &loc, 0);
    release(h);
    u9log_line("QUEST show: shrine copy %08X placed beside the Avatar at %d,%d,%d", (unsigned)o, loc.x, loc.y, loc.z);
}

void u9quest_show_command(const char *arg, OutFn out)
{
    FindTypeFn find = (FindTypeFn)u9sym(U9SYM_WORLD_FIND_BY_TYPE);
    void *sys = readable4(TRIGGER_SYSTEM) ? (void *)rd32(TRIGGER_SYSTEM) : NULL;
    void *mgr = map_manager();
    uintptr_t objs[64];
    uint32_t iter[32];
    int32_t pos[3];
    int x, y, z, n, i, shown = 0, already = 0, kept = 0, rewards = 0;
    char *end;
    long type = strtol(arg, &end, 10), reach;

    if (end == arg && show_virtue(arg, out)) return;
    if (end == arg || type <= 0 || type > 0xFFFF) {
        out("usage: show <virtue> [rune|glyph|item] - e.g. show justice; or show <type> [<reach>] [shrine]");
        return;
    }
    rewards = strstr(end, "shrine") != NULL;
    reach = strtol(end, NULL, 10);
    if (reach <= 0) reach = 2000;
    if (reach > 20000) reach = 20000;
    if (!find || !sys || !mgr || !u9avatar_position(&x, &y, &z)) { out("no world loaded"); return; }
    pos[0] = x; pos[1] = y; pos[2] = z;
    memset(iter, 0, sizeof iter);
    n = find(iter, objs, 64, mgr, pos, (int)reach, (uint32_t)type, FIND_BOTH_EXACT);
    if (n <= 0) { out("no type %ld within %ld of you", type, reach); return; }
    for (i = 0; i < n; i++) {
        uintptr_t o = objs[i];
        if (!o || !readable4(o + OBJ_FLAGS)) continue;
        if (!obj_hidden(o)) {
            already++;
            if (rewards && shrine_reward(o)) {
                bring_down(o, x, y, z);
                out("type %ld object %08X: a shrine's own copy, already showing - put beside you", type, (unsigned)o);
            }
            continue;
        }
        if (shrine_reward(o) && !rewards) { kept++; continue; }
        ((ShowFn)u9sym(U9SYM_TRIGGER_SHOW_OBJECT))(sys, 10, SHOW_PLACE, o);
        shown++;
        if (shrine_reward(o)) {
            bring_down(o, x, y, z);
            out("type %ld object %08X: a shrine's own copy, shown on request and put beside you - without "
                "fix_shrine_reveal its cleansing crashes (F-18)", type, (unsigned)o);
        }
        u9log_line("QUEST show: type %ld object %08X shown (flags now %08X)", type, (unsigned)o,
                   (unsigned)rd32(o + OBJ_FLAGS));
    }
    out("type %ld within %ld: %d shown, %d already showing", type, reach, shown, already);
    if (kept) out("%d left hidden: a shrine's own copies, which its cleansing reveals (F-18)", kept);
}

void u9quest_gate_command(const char *arg, OutFn out)
{
    if (avatar_map() != GATE_MAP) {
        out("the gate is on map %d (Earth, by the Gypsy's wagon); you are on map %d", GATE_MAP, avatar_map());
        return;
    }
    if (!*arg || !strcmp(arg, "status")) gate_status(out);
    else if (!strcmp(arg, "on"))         gate_on(out);
    else if (!strcmp(arg, "off"))        gate_off(out);
    else out("usage: gate [on|off|status]");
}
