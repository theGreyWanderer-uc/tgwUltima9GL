#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/symbols.h"
#include "fixmem.h"
#include "gold.h"
#include "playerstats.h"

#define DATUM_STORE   0x01333E64u
#define DATUM_COUNT   0x400u
#define DATUM_READING 69u
#define DATUM_CLASS   480u

static const uint16_t kShrineDatum[8] = { 49, 2, 123, 249, 236, 117, 222, 164 };

static int32_t  s32at(uintptr_t p) { return *(const volatile int32_t *)p; }
static unsigned u16at(uintptr_t p) { return *(const volatile uint16_t *)p; }
static unsigned u8at(uintptr_t p)  { return *(const volatile uint8_t *)p; }

#define ARMOR_BLOCK      0x00837EF0u
#define ARMOR_TABLE      0x00780C74u
#define ARMOR_TABLE_END  0x007810F8u
#define ARMOR_SLOTS      6u
#define WEAPON_ROWS      0x0079F268u
#define WEAPON_ROWS_END  0x0079F984u
#define WEAPON_ROW       0x1Cu
#define SHIELD_SLOT      6u
#define MANAGER_ALT      0x008515E8u
#define MANAGER_HOLDER   0x0091F0E0u

static unsigned armour_worn(void)
{
    unsigned s, total = 0;
    uintptr_t row;
    if (!readable4(ARMOR_BLOCK + 4 * (ARMOR_SLOTS - 1))) return 0;
    for (s = 0; s < ARMOR_SLOTS; s++) {
        uint32_t level = rd32(ARMOR_BLOCK + 4 * s);
        for (row = ARMOR_TABLE; row < ARMOR_TABLE_END; row += 16)
            if (rd32(row) == s && rd32(row + 4) == level) { total += u8at(row + 9); break; }
    }
    return total;
}

static unsigned slot_type(uintptr_t rec, unsigned slot)
{
    uintptr_t mgr, item;
    uint32_t off;
    off = rd32(rec + 0x70 + 4 * slot);
    if (!off) return 0;
    if (rd32(rec + 0x48) & 0x800u) mgr = readable4(MANAGER_ALT) ? rd32(MANAGER_ALT) : 0;
    else {
        uintptr_t holder = readable4(MANAGER_HOLDER) ? rd32(MANAGER_HOLDER) : 0;
        mgr = (holder && readable4(holder + 0xD0)) ? rd32(holder + 0xD0) : 0;
    }
    if (!mgr || !readable4(mgr + 0x34)) return 0;
    item = rd32(mgr + 0x34) + off;
    if (!readable4(item + 0x08)) return 0;
    return u16at(item + 0x0A);
}

static int weapon_class(unsigned type)
{
    uintptr_t row;
    if (!type) return 0;
    for (row = WEAPON_ROWS; row < WEAPON_ROWS_END; row += WEAPON_ROW)
        if (u16at(row) == type) return s32at(row + 4);
    return 0;
}

static unsigned armour_shield(uintptr_t rec)
{
    uintptr_t row;
    unsigned type = slot_type(rec, SHIELD_SLOT);
    if (!type) return 0;
    for (row = WEAPON_ROWS; row < WEAPON_ROWS_END; row += WEAPON_ROW)
        if (u16at(row) == type) return (unsigned)(int)((float)s32at(row + 0x0C) * 0.1f);
    return 0;
}

int u9player_read(U9PlayerStats *out)
{
    uintptr_t rec, store;
    unsigned i;

    memset(out, 0, sizeof *out);
    out->weapon = -1;
    out->gold = out->arrows = -1;

    rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    if (rec && readable4(rec) && readable4(rec + RECORD_STRIDE - 4)) {
        out->recordValid = 1;
        out->armourBonus = armour_shield(rec);
        for (i = 0; i < 7; i++) out->equip[i] = (int)slot_type(rec, i);
        out->handsClass = weapon_class((unsigned)out->equip[1]);
        if (!out->handsClass) out->handsClass = weapon_class((unsigned)out->equip[0]);
        out->armour = armour_worn() + out->armourBonus;
        out->spellDiscount = readable4(ARMOR_BLOCK + 12) && rd32(ARMOR_BLOCK + 12) == 7;
        out->strength = s32at(rec + 0x28);
        out->dexterity = s32at(rec + 0x2C);
        out->intelligence = s32at(rec + 0x30);
        out->health = u16at(rec + 0x34);
        out->healthMax = u16at(rec + 0x36);
        out->healthBase = u16at(rec + 0x38);
        out->mana = u16at(rec + 0x3A);
        out->manaMax = u16at(rec + 0x3C);
        out->manaBase = u16at(rec + 0x3E);
        out->stateFlags = rd32(rec + 0x48);
        out->magicResistBonus = u8at(rec + 0x52);
        out->weapon = s32at(rec + 0xA8);
        out->breath = u16at(rec + 0xB4);
        out->breathMax = u16at(rec + 0xB6);
        out->breathBase = u16at(rec + 0xB8);
        for (i = 0; i < 12; i++) {
            uint64_t b = u8at(rec + 0x118 + i);
            if (i < 8) out->spellsLo |= b << (8 * i);
            else out->spellsHi |= b << (8 * (i - 8));
        }
        for (i = 0; i < 5; i++) out->skill[i] = s32at(rec + 0x124 + 4 * i);
    }

    store = readable4(DATUM_STORE) ? rd32(DATUM_STORE) : 0;
    if (store && readable4(store) && readable4(store + DATUM_COUNT - 4)) {
        out->datumsValid = 1;
        out->classChosen = u8at(store + DATUM_READING) != 0;
        out->classId = (int)u8at(store + DATUM_CLASS);
        for (i = 0; i < 8; i++)
            if (u8at(store + kShrineDatum[i])) out->shrines |= 1u << i;
    }
    return out->recordValid;
}

#define AVATAR_ITEMS     0x0098F320u
typedef int (*TotalFn)(void);

static int avatar_item_ok(void)
{
    uintptr_t rec, tbl, item, holder, mgr;
    rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    tbl = readable4(AVATAR_ITEMS) ? rd32(AVATAR_ITEMS) : 0;
    if (!rec || !readable4(rec) || !tbl || !readable4(tbl + 4)) return 0;
    item = rd32(tbl + 4);
    if (!item) {
        if (!rd32(rec)) return 0;
        holder = readable4(MANAGER_HOLDER) ? rd32(MANAGER_HOLDER) : 0;
        mgr = (holder && readable4(holder + 0xD0)) ? rd32(holder + 0xD0) : 0;
        if (!mgr || !readable4(mgr + 0x34)) return 0;
        item = rd32(mgr + 0x34) + rd32(rec);
    }
    return item && readable4(item + 0x14);
}

#define PROC_COUNT   0x01329950u
#define PROC_ORDER   0x01329958u
#define PROC_SLOTS   0x0132E780u
#define PROC_MAX     5000
#define SPELL_TYPE0  106u

uint64_t u9player_spells_scan(void)
{
    uintptr_t tbl, avatar, p;
    uint32_t me, type, slot;
    uint64_t bits = 0;
    int n, i;

    tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    avatar = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    if (!avatar || !readable4(avatar + 0x18)) return 0;
    me = rd32(avatar + 0x18);
    if (!me) return 0;
    n = readable4(PROC_COUNT) ? (int)rd32(PROC_COUNT) : 0;
    if (n <= 0 || n > PROC_MAX || !readable4(PROC_ORDER + 4u * (unsigned)(n - 1))) return 0;
    for (i = 0; i < n; i++) {
        slot = rd32(PROC_ORDER + 4u * (unsigned)i);
        if (slot >= PROC_MAX || !readable4(PROC_SLOTS + 4u * slot)) continue;
        p = rd32(PROC_SLOTS + 4u * slot);
        if (!p || !readable4(p + 0x18) || !readable4(p + 0xF8)) continue;
        if (u8at(p + 0x1C) & 1) continue;
        type = rd32(p + 0x18);
        if (type <= SPELL_TYPE0 || type > SPELL_TYPE0 + 45) continue;
        if (rd32(p + 0xF0) != me && rd32(p + 0xF8) != me) continue;
        bits |= (uint64_t)1 << (type - SPELL_TYPE0);
    }
    return bits;
}

int u9player_combat_scan(int *state, int *stunMs)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    uintptr_t a = (tbl && readable4(tbl)) ? rd32(tbl) : 0;
    *state = -1; *stunMs = 0;
    if (!a || !readable4(a + 0x24) || !readable4(a + 0x224)) return 0;
    *state = (int)rd32(a + 0x220);
    *stunMs = (int)rd32(a + 0x24);
    if (*stunMs < 0) *stunMs = 0;
    return 1;
}

void u9player_purse(U9PlayerStats *out)
{
    uintptr_t g = u9sym(U9SYM_GOLD_TOTAL), a = u9sym(U9SYM_ARROWS_TOTAL);
    out->gold = out->arrows = -1;
    if (!out->recordValid || !g || !a || !u9avatar_object() || !avatar_item_ok()) return;
    out->gold = ((TotalFn)g)();
    out->arrows = ((TotalFn)a)();
}
