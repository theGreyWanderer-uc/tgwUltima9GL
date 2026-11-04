#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../injection/config.h"
#include "fixmem.h"
#include "npcprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define REC_SIZE        RECORD_STRIDE
#define REC_AUTHORED    352u
#define TABLE_BYTES     (ACTOR_COUNT * REC_SIZE)

#define REC_LINK        0x00u
#define REC_NAME        0x04u
#define REC_STATE       0x48u
#define REC_MAP         0x58u
#define REC_X           0x5Cu
#define REC_Y           0x60u
#define REC_Z           0x64u
#define REC_HEALTH      0x34u
#define REC_MANA        0x3Au
#define STATE_ARCHETYPE 0x800u

#define LINE_CAP        3000

typedef struct { unsigned off; const char *name; } Cell;

static const Cell CELLS[] = {
    { 0x28,  "might"   }, { 0x2C,  "agility" }, { 0x30,  "intel"   },
    { 0x124, "unarmed" }, { 0x128, "1h"      }, { 0x12C, "2h"      },
    { 0x130, "blunt"   }, { 0x134, "ranged"  },
};

typedef struct { unsigned id; const char *why; } Watched;
static const Watched WATCH[] = {
    {  39, "ambig"  }, {  65, "cells"  }, {  79, "health" }, {  99, "ambig"  },
    { 120, "health" }, { 123, "health" }, { 126, "cells"  }, { 150, "cells"  },
    { 186, "cells"  }, { 191, "cells"  }, { 192, "cells"  }, { 193, "cells"  },
    { 195, "cells"  }, { 196, "cells"  }, { 197, "cells+health" },
    { 203, "cells"  }, { 204, "cells"  }, { 206, "cells"  }, { 207, "cells"  },
    { 208, "cells+health" }, { 212, "cells" }, { 214, "health" },
    { 240, "cells"  }, { 255, "health" }, { 281, "cells"  }, { 290, "cells"  },
};
#define WATCH_COUNT (sizeof WATCH / sizeof WATCH[0])

static int      g_on;
static DWORD    g_t0, g_next, g_every = 500;
static int      g_have;
static unsigned g_lines, g_samples, g_changes, g_spawns, g_frees, g_unreadable;
static uint8_t  g_prev[TABLE_BYTES], g_cur[TABLE_BYTES];

static void npclog(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    if (g_lines >= LINE_CAP) return;
    if (++g_lines == LINE_CAP) {
        u9log_line("NPC %7lu line cap reached - further changes are counted,"
                   " not logged", (unsigned long)(u9time_now() - g_t0));
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u9log_line("NPC %7lu %s", (unsigned long)(u9time_now() - g_t0), buf);
}

static uint16_t w16(const uint8_t *r, unsigned off) { uint16_t v; memcpy(&v, r + off, 2); return v; }
static uint32_t d32(const uint8_t *r, unsigned off) { uint32_t v; memcpy(&v, r + off, 4); return v; }

static void rec_name(const uint8_t *r, char out[33])
{
    memcpy(out, r + REC_NAME, 32);
    out[32] = 0;
}

static const char *why_watched(unsigned id)
{
    unsigned i;
    for (i = 0; i < WATCH_COUNT; i++) if (WATCH[i].id == id) return WATCH[i].why;
    return 0;
}

static int clone_source(const uint8_t *tbl, const uint8_t *r)
{
    unsigned i;
    for (i = 1; i < REC_AUTHORED; i++) {
        const uint8_t *a = tbl + i * REC_SIZE;
        if ((d32(a, REC_STATE) & STATE_ARCHETYPE) &&
            memcmp(a + REC_NAME, r + REC_NAME, 32) == 0)
            return (int)i;
    }
    return 0;
}

static int cells_text(const uint8_t *r, char *out, size_t cap)
{
    size_t n = 0, i;
    out[0] = 0;
    for (i = 0; i < sizeof CELLS / sizeof CELLS[0] && n < cap; i++) {
        const uint8_t *c = r + CELLS[i].off;
        if (!c[0] && !c[1] && !c[2] && !c[3]) continue;
        n += (size_t)snprintf(out + n, cap - n, " %s %02X %02X %02X %02X",
                              CELLS[i].name, c[0], c[1], c[2], c[3]);
    }
    if (!out[0]) snprintf(out, cap, " (all level cells zero)");
    return (int)n;
}

static void describe(const char *tag, unsigned slot, const uint8_t *tbl)
{
    const uint8_t *r = tbl + slot * REC_SIZE;
    char name[33], cells[256];
    const char *why = why_watched(slot);
    int src = slot >= REC_AUTHORED ? clone_source(tbl, r) : 0;

    rec_name(r, name);
    cells_text(r, cells, sizeof cells);
    npclog("%s slot %u '%s'%s%s link %08X state %08X map %d at %d,%d,%d",
           tag, slot, name, why ? " watched:" : "", why ? why : "",
           (unsigned)d32(r, REC_LINK), (unsigned)d32(r, REC_STATE),
           (int)d32(r, REC_MAP), (int)d32(r, REC_X), (int)d32(r, REC_Y),
           (int)(int16_t)w16(r, REC_Z));
    if (src)
        npclog("    clone of record %u%s%s", (unsigned)src,
               why_watched((unsigned)src) ? " - watched: " : "",
               why_watched((unsigned)src) ? why_watched((unsigned)src) : "");
    npclog("    health %u/%u/%u  mana %u/%u/%u  (current/bonus max/base max)",
           w16(r, REC_HEALTH), w16(r, REC_HEALTH + 2), w16(r, REC_HEALTH + 4),
           w16(r, REC_MANA), w16(r, REC_MANA + 2), w16(r, REC_MANA + 4));
    npclog("   %s", cells);
}

static void write_table(const char *tag, const uint8_t *tbl)
{
    char dir[MAX_PATH], path[MAX_PATH + 48];
    DWORD n = GetTempPathA(sizeof dir, dir), wrote = 0;
    HANDLE h;
    int len;

    if (!n || n >= sizeof dir - 16) return;
    lstrcatA(dir, "u9gl_npc");
    CreateDirectoryA(dir, 0);
    len = snprintf(path, sizeof path, "%s\\npc_%lu_%s.bin", dir,
                   (unsigned long)GetCurrentProcessId(), tag);
    if (len < 0 || len >= MAX_PATH) return;
    h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        npclog("could not write %s (error %lu)", path, (unsigned long)GetLastError());
        return;
    }
    WriteFile(h, tbl, TABLE_BYTES, &wrote, 0);
    CloseHandle(h);
    npclog("wrote %s: %lu bytes, 512 records of 0x13C", path, (unsigned long)wrote);
}

static int take(uint8_t *out)
{
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t base;

    if (!readable4(ACTOR_RECORDS)) return 0;
    base = rd32(ACTOR_RECORDS);
    if (!base || !VirtualQuery((void *)base, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    if (base + TABLE_BYTES > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) return 0;
    memcpy(out, (const void *)base, TABLE_BYTES);
    return 1;
}

static void compare(void)
{
    unsigned s, i;

    for (s = 0; s < ACTOR_COUNT; s++) {
        const uint8_t *a = g_prev + s * REC_SIZE, *b = g_cur + s * REC_SIZE;
        char name[33];

        if (memcmp(a, b, REC_SIZE) == 0) continue;
        rec_name(b, name);

        if (s >= REC_AUTHORED && d32(a, REC_LINK) == 0 && d32(b, REC_LINK) != 0) {
            g_spawns++;
            describe("SPAWN", s, g_cur);
            continue;
        }
        if (s >= REC_AUTHORED && d32(a, REC_LINK) != 0 && d32(b, REC_LINK) != 0 &&
            memcmp(a + REC_NAME, b + REC_NAME, 32) != 0) {
            char was[33];
            rec_name(a, was);
            g_frees++;
            g_spawns++;
            npclog("FREE  slot %u '%s' (health was %u) - refilled within one sample",
                   s, was, w16(a, REC_HEALTH));
            describe("SPAWN", s, g_cur);
            continue;
        }
        if (s >= REC_AUTHORED && d32(a, REC_LINK) != 0 && d32(b, REC_LINK) == 0) {
            char was[33];
            rec_name(a, was);
            g_frees++;
            npclog("FREE  slot %u '%s' (health was %u)", s, was, w16(a, REC_HEALTH));
            continue;
        }

        if (d32(a, REC_LINK) != d32(b, REC_LINK)) {
            g_changes++;
            npclog("LINK  slot %u '%s' %08X -> %08X", s, name,
                   (unsigned)d32(a, REC_LINK), (unsigned)d32(b, REC_LINK));
        }
        for (i = 0; i < sizeof CELLS / sizeof CELLS[0]; i++) {
            const uint8_t *x = a + CELLS[i].off, *y = b + CELLS[i].off;
            if (memcmp(x, y, 4) == 0) continue;
            g_changes++;
            npclog("CELL  slot %u '%s' %s %02X %02X %02X %02X -> %02X %02X %02X %02X",
                   s, name, CELLS[i].name, x[0], x[1], x[2], x[3], y[0], y[1], y[2], y[3]);
        }
        if (memcmp(a + REC_HEALTH, b + REC_HEALTH, 6) != 0) {
            g_changes++;
            npclog("HP    slot %u '%s' %u/%u/%u -> %u/%u/%u", s, name,
                   w16(a, REC_HEALTH), w16(a, REC_HEALTH + 2), w16(a, REC_HEALTH + 4),
                   w16(b, REC_HEALTH), w16(b, REC_HEALTH + 2), w16(b, REC_HEALTH + 4));
        }
        if (memcmp(a + REC_MANA, b + REC_MANA, 6) != 0) {
            g_changes++;
            npclog("MANA  slot %u '%s' %u/%u/%u -> %u/%u/%u", s, name,
                   w16(a, REC_MANA), w16(a, REC_MANA + 2), w16(a, REC_MANA + 4),
                   w16(b, REC_MANA), w16(b, REC_MANA + 2), w16(b, REC_MANA + 4));
        }
    }
}

void u9npc_tick(void)
{
    DWORD now;
    unsigned i;

    if (!g_on) return;
    now = u9time_now();
    if ((LONG)(now - g_next) < 0) return;
    g_next = now + g_every;

    if (!take(g_cur)) { g_unreadable++; return; }
    g_samples++;

    if (!g_have) {
        g_have = 1;
        write_table("first", g_cur);
        npclog("first sample - the watched records as they stand:");
        for (i = 0; i < WATCH_COUNT; i++) describe("WATCH", WATCH[i].id, g_cur);
        npclog("clone slots in use now:");
        for (i = REC_AUTHORED; i < ACTOR_COUNT; i++)
            if (d32(g_cur + i * REC_SIZE, REC_LINK)) describe("LIVE ", i, g_cur);
    } else {
        compare();
    }
    memcpy(g_prev, g_cur, TABLE_BYTES);
}

void u9npc_install(void)
{
    int ms = u9cfg_int("diag_npc_watch_ms", 500);
    if (ms < 100) ms = 100;
    g_every = (DWORD)ms;
    g_t0 = g_next = u9time_now();
    g_on = 1;
    u9log_line("NPC watch on: the 512-record table sampled every %d ms on the"
               " actor tick; changes to level cells, health, mana and the link"
               " are logged per slot, clone spawns with their source record."
               " Table dumps in %%TEMP%%\\u9gl_npc", ms);
}

void u9npc_report(void)
{
    if (!g_on) return;
    if (g_have && take(g_cur)) {
        compare();
        write_table("exit", g_cur);
    } else if (g_have) {
        write_table("last", g_prev);
    }
    u9log_line("NPC watch: %u samples (%u unreadable), %u field changes, %u spawns,"
               " %u frees, %u lines", g_samples, g_unreadable, g_changes, g_spawns,
               g_frees, g_lines);
}
