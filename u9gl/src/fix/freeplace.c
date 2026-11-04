#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "freeplace.h"

void u9log_line(const char *fmt, ...);

#define W_LEFT          0x2Cu
#define W_TOP           0x30u
#define W_DRAGGING      0x4Du
#define W_ARMED         0x6Cu
#define ICON_FRAME      0x88u
#define ICON_PRESSED    0x8Du

#define EV_LUP          0x2u
#define EV_LDOWN        0x4u
#define EV_RUP          0x800u
#define EV_RDOWN        0x1000u
#define EV_MOVE         0x10000u

#define FRAME_FLAGS     0x00u
#define FRAME_W         0x04u
#define FRAME_H         0x08u
#define FRAME_ROWS      0x14u
#define FLAG_OPAQUE     0x40000000u
#define FLAG_2BYTE      0x400u

static unsigned g_clearCount, g_clearLogged;

static int icon_pixel_clear(uintptr_t icon, int x, int y)
{
    uintptr_t hdr = rd32(icon + ICON_FRAME);
    uint32_t flags, w, h;
    const uint8_t *px;

    if (!hdr || !readable4(hdr + FRAME_H)) return 0;
    flags = rd32(hdr + FRAME_FLAGS);
    if (flags & FLAG_OPAQUE) return 0;
    w = rd32(hdr + FRAME_W);
    h = rd32(hdr + FRAME_H);
    if (!w || !h || w > 4096 || h > 4096) return 0;
    if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) return 1;
    px = (const uint8_t *)(hdr + FRAME_ROWS + 4u * h);
    if (flags & FLAG_2BYTE)
        return !(((const uint16_t *)px)[(uint32_t)x + w * (uint32_t)y] & 0x8000u);
    return px[(uint32_t)x + w * (uint32_t)y] == 0xFE;
}

typedef int (U9CALL *CollideFn)(void *self, const uint32_t *ev);
static CollideFn o_collide;

static int U9CALL h_icon_collide(void *self, const uint32_t *ev)
{
    uintptr_t icon = (uintptr_t)self;
    uint32_t type;
    int x, y;

    if (!o_collide(self, ev)) return 0;
    type = ev[0];
    if (type != EV_LDOWN && type != EV_LUP && type != EV_RDOWN &&
        type != EV_RUP && type != EV_MOVE)
        return 1;
    if (*(const volatile uint8_t *)(icon + W_DRAGGING) ||
        *(const volatile uint8_t *)(icon + W_ARMED) ||
        *(const volatile uint8_t *)(icon + ICON_PRESSED))
        return 1;
    x = (int)ev[1] - (int)rd32(icon + W_LEFT);
    y = (int)ev[2] - (int)rd32(icon + W_TOP);
    if (!icon_pixel_clear(icon, x, y)) return 1;
    g_clearCount++;
    if (type != EV_MOVE && g_clearLogged < 8) {
        g_clearLogged++;
        u9log_line("FREEPLACE icon hit: event %X at (%d,%d) is on a clear pixel -"
                   " passed to what is underneath", type, x, y);
    }
    return 0;
}

static const uint8_t COLLIDE_EXPECT[4] = { 0x70, 0x56, 0x5C, 0x00 };

static const U9PatchSpec COLLIDE_SPEC = {
    "FREEPLACE_ICON_COLLIDE", U9PATCH_VTABLE, U9SYM_ITEMICON_COLLIDE_SLOT,
    4, COLLIDE_EXPECT, 4, U9PF_EXCLUSIVE
};

#define CAPACITY_MAX    4096

static const uint8_t CAP_BACKPACK_EXPECT[4] = { 20, 0, 0, 0 };
static const uint8_t CAP_BAG_EXPECT[4]      = {  9, 0, 0, 0 };
static const uint8_t GRID_BACKPACK_EXPECT[8] = { 4, 0, 0, 0, 5, 0, 0, 0 };
static const uint8_t GRID_BAG_EXPECT[8]      = { 3, 0, 0, 0, 3, 0, 0, 0 };

static const U9PatchSpec CAP_BACKPACK_SPEC = {
    "FREEPLACE_CAPACITY_BACKPACK", U9PATCH_STUB, U9SYM_CAPACITY_BACKPACK,
    4, CAP_BACKPACK_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec CAP_BAG_SPEC = {
    "FREEPLACE_CAPACITY_BAG", U9PATCH_STUB, U9SYM_CAPACITY_BAG,
    4, CAP_BAG_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec GRID_BACKPACK_SPEC = {
    "FREEPLACE_GRID_BACKPACK", U9PATCH_STUB, U9SYM_GRID_BACKPACK,
    8, GRID_BACKPACK_EXPECT, 8, U9PF_EXCLUSIVE
};
static const U9PatchSpec GRID_BAG_SPEC = {
    "FREEPLACE_GRID_BAG", U9PATCH_STUB, U9SYM_GRID_BAG,
    8, GRID_BAG_EXPECT, 8, U9PF_EXCLUSIVE
};

static int raise_capacity(const U9PatchSpec *grid, const U9PatchSpec *cap,
                          int stockRows, int cols, int want)
{
    int32_t rows = (want + cols - 1) / cols;
    int32_t n = rows * cols;
    uint8_t g[8], c[4];

    if (rows <= stockRows) return stockRows * cols;
    memcpy(g, &rows, 4);
    memcpy(g + 4, &cols, 4);
    memcpy(c, &n, 4);
    if (!u9patch_install_bytes(grid, g, 8)) return stockRows * cols;
    if (!u9patch_install_bytes(cap, c, 4)) return stockRows * cols;
    return n;
}

#define BIAS            512
#define PIX_MAX         511
#define OLD_BIAS        0x1000
#define ICON            32

#define WIN_KIND        0x80u
#define WIN_ITEM        0x84u
#define WIN_CELLS       0xB8u
#define WIN_CHILDREN    0x1Cu
#define WIN_LAST_CHILD  0x20u
#define NODE_NEXT       0x04u
#define NODE_PREV       0x08u
#define CELL_SIZE       28u
#define CELL_USED       0x18u
#define ICON_ITEM       0x80u
#define ICON_GRABX      0x78u
#define ICON_GRABY      0x7Cu
#define VTBL_ITEMICON   0x007536A0u

#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define OBJF_CONTAINED  0x200u
#define OBJ_CELLX       0x04u
#define OBJ_CELLY       0x06u

#define LAYOUT_BACKPACK 0x007A1A20u
#define LAYOUT_BAG      0x007A1AB0u
#define LAY_ROWS        0x10u
#define LAY_COLS        0x14u
#define LAY_GAPX        0x18u
#define LAY_GAPY        0x1Cu
#define LAY_FIRSTX      0x20u
#define LAY_FIRSTY      0x24u
#define OBJ_AVATAR     0
#define OBJ_BAG        2342

#define STASH_MAX       (CAPACITY_MAX + 64)
#define PLACE_LOG_MAX   40

typedef struct { int l, t, r, b; uintptr_t layout; } FreeRect;
typedef struct { uintptr_t obj; int x, y; } Stash;

typedef void *(U9CALL *PlaceFn)(void *win, uint32_t hItem, int *cellOut,
                                int x, int y, int byIndex);
typedef void  (U9CALL *FillFn)(void *win);
typedef void  (U9CALL *MoveToFn)(void *widget, int x, int y);
typedef void *(U9CALL *ContentFn)(void *obj);

static PlaceFn   o_placeBag, o_placePack;
static FillFn    o_fillBag, o_fillPack;
static MoveToFn  f_moveTo;
static ContentFn f_first, f_next;
static Stash     g_stash[STASH_MAX];
static uintptr_t g_chain[STASH_MAX];
static struct { uintptr_t icon; int idx; } g_order[STASH_MAX];
static int       g_occ[STASH_MAX][2];
static unsigned  g_placeLogged, g_fillLogged, g_places, g_fills;

static uintptr_t fp_obj(uint32_t h)
{
    uintptr_t tab = rd32(ENTITY_TABLE);
    if (!h || !tab || !readable4(tab + h * 12u + 8u)) return 0;
    return rd32(tab + h * 12u + 8u);
}

static int free_rect(uintptr_t win, FreeRect *fr)
{
    uint32_t kind = rd32(win + WIN_KIND);
    uintptr_t item = fp_obj(rd32(win + WIN_ITEM));
    unsigned type;
    int rows, cols;

    if (!item) return 0;
    type = *(const volatile uint16_t *)(item + OBJ_TYPE);
    if (kind == 1 && type == OBJ_AVATAR)   { fr->layout = LAYOUT_BACKPACK; rows = 4; cols = 5; }
    else if (kind == 0 && type == OBJ_BAG) { fr->layout = LAYOUT_BAG;      rows = 3; cols = 3; }
    else return 0;
    fr->l = (int)rd32(fr->layout + LAY_FIRSTX);
    fr->t = (int)rd32(fr->layout + LAY_FIRSTY);
    fr->r = fr->l + cols * ICON + (cols - 1) * (int)rd32(fr->layout + LAY_GAPX);
    fr->b = fr->t + rows * ICON + (rows - 1) * (int)rd32(fr->layout + LAY_GAPY);
    if (fr->l < 0) fr->l = 0;
    if (fr->t < 0) fr->t = 0;
    if (fr->r > PIX_MAX + ICON) fr->r = PIX_MAX + ICON;
    if (fr->b > PIX_MAX + ICON) fr->b = PIX_MAX + ICON;
    if (fr->r - fr->l < ICON || fr->b - fr->t < ICON) return 0;
    return 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void clear_cells(uintptr_t win, const FreeRect *fr)
{
    uintptr_t cells = rd32(win + WIN_CELLS);
    uint32_t n = rd32(fr->layout + LAY_ROWS) * rd32(fr->layout + LAY_COLS), i;

    if (!cells || n > 65536u || !readable4(cells + (n - 1) * CELL_SIZE + CELL_USED))
        return;
    for (i = 0; i < n; i++)
        *(volatile uint8_t *)(cells + i * CELL_SIZE + CELL_USED) = 0;
}

static int contained(uintptr_t item)
{
    return item && readable4(item + OBJ_FLAGS) &&
           (rd32(item + OBJ_FLAGS) & OBJF_CONTAINED) != 0;
}

static void set_pos(uintptr_t icon, uintptr_t item, int x, int y)
{
    f_moveTo((void *)icon, x, y);
    *(volatile int16_t *)(item + OBJ_CELLX) = (int16_t)(x + BIAS);
    *(volatile int16_t *)(item + OBJ_CELLY) = (int16_t)(y + BIAS);
}

static int biased(int v)    { return v >= BIAS && v < 2 * BIAS; }
static int old_biased(int v) { return v >= OLD_BIAS && v < 2 * OLD_BIAS; }

static void *place_common(PlaceFn orig, void *self, uint32_t h, int *cellOut,
                            int x, int y, int byIndex)
{
    uintptr_t win = (uintptr_t)self, icon, item;
    FreeRect fr;
    int gx, gy, px, py;
    void *r = orig(self, h, cellOut, x, y, byIndex);

    icon = (uintptr_t)r;
    if (!icon || byIndex || !free_rect(win, &fr)) return r;
    item = fp_obj(rd32(icon + ICON_ITEM));
    if (!contained(item)) return r;
    gx = clampi((int)rd32(icon + ICON_GRABX), 0, ICON - 1);
    gy = clampi((int)rd32(icon + ICON_GRABY), 0, ICON - 1);
    px = clampi(x - gx, fr.l, fr.r - ICON);
    py = clampi(y - gy, fr.t, fr.b - ICON);
    set_pos(icon, item, px, py);
    clear_cells(win, &fr);
    g_places++;
    if (g_placeLogged < PLACE_LOG_MAX) {
        g_placeLogged++;
        u9log_line("FREEPLACE place: drop (%d,%d) byIndex %d, grab (%d,%d) -> icon at"
                   " (%d,%d) in [%d,%d]-[%d,%d]", x, y, byIndex, gx, gy, px, py,
                   fr.l, fr.t, fr.r, fr.b);
    }
    return r;
}

static void *U9CALL h_placeBag(void *self, uint32_t h, int *cellOut, int x, int y, int byIndex)
{ return place_common(o_placeBag, self, h, cellOut, x, y, byIndex); }

static void *U9CALL h_placePack(void *self, uint32_t h, int *cellOut, int x, int y, int byIndex)
{ return place_common(o_placePack, self, h, cellOut, x, y, byIndex); }

static int overlaps(int n, int x, int y)
{
    int i;
    for (i = 0; i < n; i++)
        if (x < g_occ[i][0] + ICON && g_occ[i][0] < x + ICON &&
            y < g_occ[i][1] + ICON && g_occ[i][1] < y + ICON)
            return 1;
    return 0;
}

static void first_free(const FreeRect *fr, int n, int *ox, int *oy)
{
    int stepx = ICON + (int)rd32(fr->layout + LAY_GAPX);
    int stepy = ICON + (int)rd32(fr->layout + LAY_GAPY);
    int pass, x, y;

    if (stepx < 8) stepx = 8;
    if (stepy < 8) stepy = 8;
    for (pass = 0; pass < 6; pass++) {
        int o = pass * 6;
        for (y = fr->t + o; y + ICON <= fr->b; y += stepy)
            for (x = fr->l + o; x + ICON <= fr->r; x += stepx)
                if (!overlaps(n, x, y)) { *ox = x; *oy = y; return; }
    }
    *ox = clampi(fr->l + (n % 8) * 4, fr->l, fr->r - ICON);
    *oy = clampi(fr->t + (n % 8) * 4, fr->t, fr->b - ICON);
}

static void to_tail(uintptr_t win, uintptr_t node)
{
    uintptr_t next = rd32(node + NODE_NEXT), prev = rd32(node + NODE_PREV);
    uintptr_t tail;

    if (rd32(win + WIN_LAST_CHILD) == node) return;
    if (prev) *(volatile uint32_t *)(prev + NODE_NEXT) = (uint32_t)next;
    else      *(volatile uint32_t *)(win + WIN_CHILDREN) = (uint32_t)next;
    if (next) *(volatile uint32_t *)(next + NODE_PREV) = (uint32_t)prev;
    else      *(volatile uint32_t *)(win + WIN_LAST_CHILD) = (uint32_t)prev;
    tail = rd32(win + WIN_LAST_CHILD);
    *(volatile uint32_t *)(node + NODE_PREV) = (uint32_t)tail;
    *(volatile uint32_t *)(node + NODE_NEXT) = 0;
    if (tail) *(volatile uint32_t *)(tail + NODE_NEXT) = (uint32_t)node;
    else      *(volatile uint32_t *)(win + WIN_CHILDREN) = (uint32_t)node;
    *(volatile uint32_t *)(win + WIN_LAST_CHILD) = (uint32_t)node;
}

static void order_icons(uintptr_t win, int nc)
{
    uintptr_t c;
    int n = 0, i, j, guard;

    for (c = rd32(win + WIN_CHILDREN), guard = 0; c && n < STASH_MAX && guard < 100000;
         c = rd32(c + NODE_NEXT), guard++) {
        uintptr_t item;
        int k;
        if (rd32(c) != VTBL_ITEMICON || *(const volatile uint8_t *)(c + W_DRAGGING))
            continue;
        item = fp_obj(rd32(c + ICON_ITEM));
        for (k = 0; k < nc; k++) if (g_chain[k] == item) break;
        g_order[n].icon = c;
        g_order[n].idx = k;
        n++;
    }
    for (i = 1; i < n; i++) {
        uintptr_t ic = g_order[i].icon;
        int ix = g_order[i].idx;
        for (j = i - 1; j >= 0 && g_order[j].idx < ix; j--) g_order[j + 1] = g_order[j];
        g_order[j + 1].icon = ic;
        g_order[j + 1].idx = ix;
    }
    for (i = 0; i < n; i++) to_tail(win, g_order[i].icon);
}

static void fill_common(FillFn orig, void *self)
{
    uintptr_t win = (uintptr_t)self, cont, o, c;
    FreeRect fr;
    int ns = 0, nc = 0, nocc = 0, restored = 0, kept = 0, moved = 0, skipped = 0, guard;

    if (!free_rect(win, &fr)) { orig(self); return; }

    cont = fp_obj(rd32(win + WIN_ITEM));
    for (o = (uintptr_t)f_first((void *)cont), guard = 0;
         o && nc < STASH_MAX && guard < 100000;
         o = (uintptr_t)f_next((void *)o), guard++) {
        int x = *(const volatile int16_t *)(o + OBJ_CELLX);
        g_chain[nc++] = o;
        int y = *(const volatile int16_t *)(o + OBJ_CELLY);
        if (biased(x) && biased(y)) {
            g_stash[ns].obj = o;
            g_stash[ns].x = x - BIAS;
            g_stash[ns].y = y - BIAS;
            ns++;
        } else if (old_biased(x) && old_biased(y)) {
            g_stash[ns].obj = o;
            g_stash[ns].x = x - OLD_BIAS;
            g_stash[ns].y = y - OLD_BIAS;
            ns++;
        }
    }

    orig(self);

    for (c = rd32(win + WIN_CHILDREN), guard = 0; c && guard < 100000;
         c = rd32(c + 4), guard++) {
        uintptr_t item;
        int i;
        if (rd32(c) != VTBL_ITEMICON || *(const volatile uint8_t *)(c + W_DRAGGING))
            continue;
        item = fp_obj(rd32(c + ICON_ITEM));
        if (!contained(item)) { skipped++; continue; }
        for (i = 0; i < ns; i++) if (g_stash[i].obj == item) break;
        if (i == ns) continue;
        {
            int x = clampi(g_stash[i].x, fr.l, fr.r - ICON);
            int y = clampi(g_stash[i].y, fr.t, fr.b - ICON);
            set_pos(c, item, x, y);
            if (nocc < STASH_MAX) { g_occ[nocc][0] = x; g_occ[nocc][1] = y; nocc++; }
            restored++;
        }
    }
    for (c = rd32(win + WIN_CHILDREN), guard = 0; c && guard < 100000;
         c = rd32(c + 4), guard++) {
        uintptr_t item;
        int x, y, i;
        if (rd32(c) != VTBL_ITEMICON || *(const volatile uint8_t *)(c + W_DRAGGING))
            continue;
        item = fp_obj(rd32(c + ICON_ITEM));
        if (!contained(item)) continue;
        for (i = 0; i < ns; i++) if (g_stash[i].obj == item) break;
        if (i < ns) continue;
        x = (int)rd32(c + W_LEFT);
        y = (int)rd32(c + W_TOP);
        if (x >= fr.l && y >= fr.t && x + ICON <= fr.r && y + ICON <= fr.b &&
            !overlaps(nocc, x, y)) {
            kept++;
        } else {
            first_free(&fr, nocc, &x, &y);
            moved++;
        }
        set_pos(c, item, x, y);
        if (nocc < STASH_MAX) { g_occ[nocc][0] = x; g_occ[nocc][1] = y; nocc++; }
    }
    order_icons(win, nc);
    clear_cells(win, &fr);
    g_fills++;
    if (g_fillLogged < PLACE_LOG_MAX) {
        g_fillLogged++;
        u9log_line("FREEPLACE open: %s, %d with pixels restored, %d kept their"
                   " stock cell, %d placed in the first free spot, %d icons of"
                   " items no longer contained left alone",
                   fr.layout == LAYOUT_BAG ? "bag" : "backpack", restored, kept, moved,
                   skipped);
    }
}

static void U9CALL h_fillBag(void *self)  { fill_common(o_fillBag, self); }
static void U9CALL h_fillPack(void *self) { fill_common(o_fillPack, self); }

static const uint8_t PLACE_EXPECT[4]     = { 0x80, 0xA4, 0x5C, 0x00 };
static const uint8_t FILL_BAG_EXPECT[4]  = { 0x10, 0x9A, 0x5C, 0x00 };
static const uint8_t FILL_PACK_EXPECT[4] = { 0x60, 0xAB, 0x5C, 0x00 };

static const U9PatchSpec PLACE_BAG_SPEC = {
    "FREEPLACE_PLACE_BAG", U9PATCH_VTABLE, U9SYM_BAG_PLACE_SLOT,
    4, PLACE_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec PLACE_PACK_SPEC = {
    "FREEPLACE_PLACE_BACKPACK", U9PATCH_VTABLE, U9SYM_BACKPACK_PLACE_SLOT,
    4, PLACE_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec FILL_BAG_SPEC = {
    "FREEPLACE_FILL_BAG", U9PATCH_VTABLE, U9SYM_BAG_FILL_SLOT,
    4, FILL_BAG_EXPECT, 4, U9PF_EXCLUSIVE
};
static const U9PatchSpec FILL_PACK_SPEC = {
    "FREEPLACE_FILL_BACKPACK", U9PATCH_VTABLE, U9SYM_BACKPACK_FILL_SLOT,
    4, FILL_PACK_EXPECT, 4, U9PF_EXCLUSIVE
};

static int helpers_ok(void)
{
    static const uint8_t expMove[5]    = { 0x8B, 0x44, 0x24, 0x08, 0x56 };
    static const uint8_t expContent[6] = { 0x8B, 0x41, 0x14, 0xF6, 0xC4, 0x02 };
    uintptr_t m = u9sym(U9SYM_WIDGET_MOVETO);
    uintptr_t a = u9sym(U9SYM_ITEM_FIRST_CONTENT);
    uintptr_t b = u9sym(U9SYM_ITEM_NEXT_CONTENT);

    if (!m || !a || !b || memcmp((const void *)m, expMove, 5) ||
        memcmp((const void *)a, expContent, 6) || memcmp((const void *)b, expContent, 6))
        return 0;
    f_moveTo = (MoveToFn)m;
    f_first  = (ContentFn)a;
    f_next   = (ContentFn)b;
    return 1;
}

static void free_placement_install(void)
{
    if (!helpers_ok()) {
        u9log_line("FREEPLACE free placement: a called helper is not where expected"
                   " - not installed");
        return;
    }
    o_fillBag  = (FillFn)u9patch_install(&FILL_BAG_SPEC, (void *)h_fillBag);
    o_fillPack = (FillFn)u9patch_install(&FILL_PACK_SPEC, (void *)h_fillPack);
    if (o_fillBag)
        o_placeBag = (PlaceFn)u9patch_install(&PLACE_BAG_SPEC, (void *)h_placeBag);
    if (o_fillPack)
        o_placePack = (PlaceFn)u9patch_install(&PLACE_PACK_SPEC, (void *)h_placePack);
    u9log_line("FREEPLACE free placement: bag %s, backpack %s. The free area"
               " = the stock grid's outline",
               o_placeBag ? "on" : "REFUSED", o_placePack ? "on" : "REFUSED");
}

void u9freeplace_install(void)
{
    int want = u9cfg_int("pack_capacity", 0);

    if (want > 0) {
        int pack, bag;
        if (want > CAPACITY_MAX) want = CAPACITY_MAX;
        pack = raise_capacity(&GRID_BACKPACK_SPEC, &CAP_BACKPACK_SPEC, 4, 5, want);
        bag  = raise_capacity(&GRID_BAG_SPEC, &CAP_BAG_SPEC, 3, 3, want);
        u9log_line("FREEPLACE capacity: backpack %d (%d rows of 5), bag %d (%d rows"
                   " of 3); asked for %d. A stock number means that one was refused"
                   " or not raised (see the manifest)",
                   pack, pack / 5, bag, bag / 3, want);
    }

    if (u9cfg_int("icon_pixel_hit", 0)) {
        o_collide = (CollideFn)u9patch_install(&COLLIDE_SPEC, (void *)h_icon_collide);
        if (o_collide)
            u9log_line("FREEPLACE icon hit: an item icon is under the cursor only"
                       " where it is drawn (collide, slot 16); slot 17 untouched");
        else
            u9log_line("FREEPLACE icon hit NOT installed - refused (see the manifest)");
    }

    if (u9cfg_int("free_placement", 0)) free_placement_install();
}
