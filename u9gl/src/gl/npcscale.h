#ifndef U9GL_NPCSCALE_H
#define U9GL_NPCSCALE_H

#include <stddef.h>
#include <stdint.h>

typedef void (*U9ConsoleOut)(const char *fmt, ...);

void u9npcscale_command(const char *arg, U9ConsoleOut out);

typedef struct {
    unsigned npc;
    char     name[33];
    int      map, x, y, z;
    int      live;
} U9NpcPlace;

int u9npc_locate(const char *arg, int curMap, U9NpcPlace *found, U9ConsoleOut out);

int u9npc_live_place(unsigned npc, int curMap, U9NpcPlace *p);

uintptr_t u9npc_object(unsigned npc, int curMap);

int u9npc_find_any(const char *arg, char *name, size_t cap, U9ConsoleOut out);

void u9npc_combat_text(unsigned npc, char *buf, size_t cap);

void u9npc_list_near(int curMap, int x, int y, int z, int rxy, int rz, unsigned max, U9ConsoleOut out);

#endif
