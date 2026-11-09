#ifndef U9FIX_PLAYERSTATS_H
#define U9FIX_PLAYERSTATS_H

#include <stdint.h>

typedef struct U9PlayerStats {
    int recordValid;
    int strength, dexterity, intelligence;
    unsigned health, healthMax, healthBase;
    unsigned mana, manaMax, manaBase;
    unsigned breath, breathMax, breathBase;
    int      equip[7];
    int      handsClass;
    unsigned armour, armourBonus, magicResistBonus;
    uint32_t stateFlags;
    int weapon;
    int skill[5];
    int datumsValid;
    int classChosen, classId;
    unsigned shrines;
    uint64_t spellsLo, spellsHi;
    int gold, arrows;
    uint64_t activeSpells;
    uint32_t combatStates;
    int stunMs;
    int spellDiscount;
} U9PlayerStats;

int u9player_read(U9PlayerStats *out);

void u9player_purse(U9PlayerStats *out);

uint64_t u9player_spells_scan(void);

int u9player_combat_scan(int *state, int *stunMs);

#endif
