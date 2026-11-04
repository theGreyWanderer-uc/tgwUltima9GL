#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "gold.h"

void u9log_line(const char *fmt, ...);

#define WIDGET_CACHE     0x80u
#define WIDGET_FORCE     0x84u
#define ACT_MOVER        0x14u
#define ACT_SELF         0x18u
#define MOVER_YAW        0x1A8u
#define GOLD_PILE_TYPE   0x502u
#define PROP_QUANTITY    0x11
#define STACK_MAX        9999
#define REAGENT_FIRST    2192
#define REAGENT_COUNT    8

typedef int        (U9CALL *RedrawFn)(void *self);
typedef int        (*GoldTotalFn)(void);
typedef uint32_t * (__attribute__((fastcall)) *ItemCreateFn)(uint32_t *out,
                                        int type, void *loc, int flags);
typedef void       (U9CALL *SetPropFn)(uint32_t *handle, int prop, int value);
typedef void       (__attribute__((stdcall)) *HandleRefFn)(uint32_t h);
typedef void       (__attribute__((stdcall)) *LayOutFn)(uint32_t h, float x,
                                        float y, float z);
typedef float      (U9CALL *GetYawFn)(void *mover);

static RedrawFn g_origRedraw;
static int      g_cap = 9999;
static uint32_t g_lastTotal = 0xFFFFFFFFu;
static unsigned g_totalLogged, g_piles;

static int obj_pos(uintptr_t obj, int *x, int *y, int *z)
{
    uintptr_t page;
    uint32_t xy;

    if (!obj || !readable4(obj + 0x14) || (rd32(obj + 0x14) & 0x200u)) return 0;
    page = obj & ~(uintptr_t)0xFFFu;
    if (!readable4(page + 0x10)) return 0;
    xy = rd32(obj + 4);
    *x = (int16_t)(xy & 0xFFFFu) + (int32_t)rd32(page + 0x0C);
    *y = (int16_t)(xy >> 16)     + (int32_t)rd32(page + 0x10);
    *z = (int16_t)(rd32(obj + 8) & 0xFFFFu);
    return 1;
}

static uintptr_t avatar_actor(void)
{
    uintptr_t tbl = readable4(ACTOR_TABLE) ? rd32(ACTOR_TABLE) : 0;
    return (tbl && readable4(tbl)) ? rd32(tbl) : 0;
}

static uintptr_t actor_object(uintptr_t act)
{
    uintptr_t et;
    uint32_t sh;

    if (!act || !readable4(act + ACT_SELF) || !readable4(ENTITY_TABLE)) return 0;
    et = rd32(ENTITY_TABLE);
    sh = rd32(act + ACT_SELF);
    return (et && readable4(et + sh * 0x0Cu + 8)) ? rd32(et + sh * 0x0Cu + 8) : 0;
}

int u9avatar_position(int *x, int *y, int *z)
{
    return obj_pos(actor_object(avatar_actor()), x, y, z);
}

static int drop_stack(int type, int amount, float ahead, float side)
{
    uintptr_t act, obj, mover, vt, getYaw;
    uint32_t h = 0;
    int x, y, z;
    float yaw = 0.0f, px, py;
    const char *how = "no mover - straight at +y";
    struct { int32_t x, y; int16_t z, pad; } loc;

    act = avatar_actor();
    obj = actor_object(act);
    if (!obj_pos(obj, &x, &y, &z)) {
        u9log_line("GIVE: the Avatar's position is not readable - nothing dropped");
        return 0;
    }

    mover = readable4(act + ACT_MOVER) ? rd32(act + ACT_MOVER) : 0;
    if (mover && readable4(mover) && (vt = rd32(mover)) && readable4(vt + 0xCC)) {
        getYaw = rd32(vt + 0xCC);
        if (getYaw == u9sym(U9SYM_MOVER_GETYAW) && readable4(mover + MOVER_YAW)) {
            uint32_t v = rd32(mover + MOVER_YAW);
            memcpy(&yaw, &v, 4);
            how = "the mover's yaw at +0x1A8";
        } else {
            yaw = ((GetYawFn)getYaw)((void *)mover);
            how = "the mover's own slot +0xCC";
        }
        if (!(yaw > -100.0f && yaw < 100.0f)) { yaw = 0.0f; how = "an unusable yaw - straight at +y"; }
    }
    px = (float)x - ahead * sinf(yaw) + side * cosf(yaw);
    py = (float)y + ahead * cosf(yaw) + side * sinf(yaw);

    memset(&loc, 0, sizeof loc);
    loc.x = (int32_t)px;
    loc.y = (int32_t)py;
    loc.z = (int16_t)z;
    ((ItemCreateFn)u9sym(U9SYM_ITEM_CREATE))(&h, type, &loc, 0);
    if (!h) {
        u9log_line("GIVE: Item_Create gave no handle for type %d - nothing dropped", type);
        return 0;
    }
    if (amount > 0)
        ((SetPropFn)u9sym(U9SYM_HANDLE_SETPROP))(&h, PROP_QUANTITY, amount);
    ((HandleRefFn)u9sym(U9SYM_HANDLE_ADDREF))(h);
    ((LayOutFn)u9sym(U9SYM_LOOT_LAYOUT_DROP))(h, px, py, (float)z);
    ((HandleRefFn)u9sym(U9SYM_HANDLE_RELEASE))(h);
    g_piles++;
    u9log_line("GIVE: type %d x%d dropped at %d,%d,%d - the Avatar at %d,%d,%d, facing %d deg (%s)",
               type, amount, (int)px, (int)py, z, x, y, z, (int)(yaw * 57.29578f), how);
    return 1;
}

void u9give_command(const char *arg, void (*out)(const char *fmt, ...))
{
    const char *v;
    int n, made = 0, k;
    while (arg && *arg == ' ') arg++;
    if (!arg || !*arg) {
        out("give gold <n>   piles of gold in front of you (9,999 a pile)");
        out("give regs <n>   a stack of each of the 8 reagents, <n> in each (up to 9,999)");
        out("give item <type>   one item of that type (TYPES.DAT index) in front of you");
        out("give lenses     the Codex lenses: the Blue (human, 5519) and the Red (gargoyle, 5520)");
        return;
    }
    v = arg;
    while (*v && *v != ' ') v++;
    while (*v == ' ') v++;
    n = atoi(v);
    if (!strncmp(arg, "gold", 4) && (arg[4] == ' ' || !arg[4])) {
        int left, piles = 0;
        if (n < 1) { out("give gold <n>, n at least 1"); return; }
        if (n > 999999) n = 999999;
        for (left = n; left > 0 && piles < 101; left -= STACK_MAX, piles++)
            made += drop_stack(GOLD_PILE_TYPE, left > STACK_MAX ? STACK_MAX : left,
                               100.0f + 30.0f * (float)(piles / 5), 40.0f * (float)(piles % 5 - 2));
        if (made) out("%d gold dropped in front of you, in %d pile%s", n, made, made == 1 ? "" : "s");
        else out("no gold dropped - is a world loaded? (see GIVE in the log)");
        return;
    }
    if (!strncmp(arg, "regs", 4) && (arg[4] == ' ' || !arg[4])) {
        if (n < 1) { out("give regs <n>, n at least 1"); return; }
        if (n > STACK_MAX) n = STACK_MAX;
        for (k = 0; k < REAGENT_COUNT; k++)
            made += drop_stack(REAGENT_FIRST + k, n, 100.0f, 30.0f * ((float)k - 3.5f));
        if (made) out("%d of each reagent dropped in front of you (%d stacks)", n, made);
        else out("no reagents dropped - is a world loaded? (see GIVE in the log)");
        return;
    }
    if (!strncmp(arg, "item", 4) && (arg[4] == ' ' || !arg[4])) {
        if (n < 1 || n > 0xFFFF) { out("give item <type>, a TYPES.DAT index 1-65535"); return; }
        if (drop_stack(n, 0, 100.0f, 0.0f)) out("type %d dropped in front of you", n);
        else out("type %d not made - is a world loaded? (see GIVE in the log)", n);
        return;
    }
    if (!strncmp(arg, "lenses", 6) && (arg[6] == ' ' || !arg[6])) {
        made = drop_stack(5519, 0, 100.0f, -30.0f) + drop_stack(5520, 0, 100.0f, 30.0f);
        if (made == 2) out("the Blue lens (5519) and the Red lens (5520) dropped in front of you");
        else out("%d of the 2 lenses dropped - see GIVE in the log", made);
        return;
    }
    out("give gold <n> | give regs <n> | give item <type> | give lenses");
}

static int U9CALL h_redraw(void *self)
{
    uintptr_t w = (uintptr_t)self;
    uint32_t total, shown;
    int changed = 0;

    total = (uint32_t)((GoldTotalFn)u9sym(U9SYM_GOLD_TOTAL))();
    shown = total > 9999u ? total / 1000u : total;
    if (*(volatile uint16_t *)(w + WIDGET_CACHE) != (uint16_t)shown) {
        *(volatile uint16_t *)(w + WIDGET_CACHE) = (uint16_t)shown;
        changed = 1;
    }
    if (total != g_lastTotal) {
        if (g_totalLogged < 200) {
            g_totalLogged++;
            u9log_line("GOLD total %u, the backpack shows %u%s", (unsigned)total,
                       (unsigned)shown, total > 9999u ? " (thousands)" : "");
        }
        g_lastTotal = total;
    }
    return (*(volatile uint32_t *)(w + WIDGET_FORCE) != 0) | changed;
}

static const uint8_t CAP_A_EXPECT[5] = { 0xB9, 0x0F, 0x27, 0x00, 0x00 };
static const uint8_t CAP_B_EXPECT[5] = { 0xBF, 0x0F, 0x27, 0x00, 0x00 };
static const uint8_t REDRAW_EXPECT[4] = { 0x80, 0xC6, 0x5C, 0x00 };

static const U9PatchSpec CAP_A_SPEC = {
    "GOLD_CAP_HEADROOM", U9PATCH_STUB, U9SYM_GOLD_CAP_HEADROOM,
    5, CAP_A_EXPECT, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec CAP_B_SPEC = {
    "GOLD_CAP_PICKUP", U9PATCH_STUB, U9SYM_GOLD_CAP_PICKUP,
    5, CAP_B_EXPECT, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec REDRAW_SPEC = {
    "GOLD_WIDGET_REDRAW", U9PATCH_VTABLE, U9SYM_GOLDWIDGET_REDRAW_SLOT,
    4, REDRAW_EXPECT, 4, U9PF_EXCLUSIVE
};

void u9gold_install(void)
{
    g_cap    = u9cfg_int("gold_cap", 9999);
    if (g_cap < 9999) g_cap = 9999;
    if (g_cap > 9999999) g_cap = 9999999;

    if (g_cap > 9999) {
        uint8_t a[5], b[5];
        uint32_t cap = (uint32_t)g_cap;
        memcpy(a, CAP_A_EXPECT, 5);
        memcpy(b, CAP_B_EXPECT, 5);
        memcpy(a + 1, &cap, 4);
        memcpy(b + 1, &cap, 4);
        if (u9patch_install_bytes(&CAP_A_SPEC, a, 5) &&
            u9patch_install_bytes(&CAP_B_SPEC, b, 5))
            u9log_line("GOLD cap raised to %d", g_cap);
        else
            u9log_line("GOLD cap NOT raised - a site was refused (see the manifest)");
    }
    if (g_cap > 9999) {
        g_origRedraw = (RedrawFn)u9patch_install(&REDRAW_SPEC, (void *)h_redraw);
        if (g_origRedraw)
            u9log_line("GOLD backpack count: above 9,999 it shows thousands (10,000"
                       " reads 10)");
        else
            u9log_line("GOLD backpack count NOT replaced - refused (see the manifest)");
    }
}
