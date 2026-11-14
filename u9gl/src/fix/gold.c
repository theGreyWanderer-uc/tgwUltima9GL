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

uintptr_t u9avatar_object(void)
{
    return actor_object(avatar_actor());
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
        out("give sigils     the eight Sigils of virtue, for the pillars in the Black Gate hall");
        out("give fragments  three Black Gate fragments (5483-5485)");
        return;
    }
    v = arg;
    while (*v && *v != ' ') v++;
    while (*v == ' ') v++;
    for (n = 0; (*v >= '0' && *v <= '9') || *v == ',' || *v == '_'; v++)
        if (*v >= '0' && *v <= '9' && n < 100000000) n = n * 10 + (*v - '0');
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
    if (!strncmp(arg, "sigils", 6) && (arg[6] == ' ' || !arg[6])) {
        static const int sigil[8] = { 4956, 4938, 5472, 5496, 5508, 5468, 5291, 5288 };
        for (k = 0; k < 8; k++) made += drop_stack(sigil[k], 0, 100.0f, 30.0f * ((float)k - 3.5f));
        if (made == 8) out("the eight Sigils dropped in front of you: 4956 4938 5472 5496 5508 5468 5291 5288");
        else out("%d of the 8 Sigils dropped - see GIVE in the log", made);
        return;
    }
    if (!strncmp(arg, "fragments", 9) && (arg[9] == ' ' || !arg[9])) {
        for (k = 0; k < 3; k++) made += drop_stack(5483 + k, 0, 100.0f, 30.0f * ((float)k - 1.0f));
        if (made == 3) out("three Black Gate fragments (5483, 5484, 5485) dropped in front of you");
        else out("%d of the 3 fragments dropped - see GIVE in the log", made);
        return;
    }
    out("give gold <n> | give regs <n> | give item <type> | give lenses | give sigils | give fragments");
}

typedef void (U9CALL *BlitFn)(void *target, const void *frame, int x, int y);

typedef struct {
    int       w, h;
    uint16_t *px;
    uint8_t  *frame;
} Letter;

enum { SFX_NONE = 0, SFX_K = 1, SFX_M = 2 };
static Letter   g_letter[3];
static int      g_suffix;
static int      g_lettersOn, g_letterRefused;
static BlitFn   o_blit;
static unsigned g_letterDraws;

static void dll_path(char *out_, size_t cap, const char *name)
{
    HMODULE self = 0;
    char *slash;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)dll_path, &self);
    GetModuleFileNameA(self, out_, (DWORD)cap);
    slash = strrchr(out_, 92);
    if (slash) lstrcpynA(slash + 1, name, (int)(cap - (size_t)(slash + 1 - out_)));
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int load_letter(const char *name, Letter *out)
{
    char path[MAX_PATH];
    HANDLE f;
    DWORD size, got;
    uint8_t *buf;
    int ok = 0;

    dll_path(path, sizeof path, name);
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    buf = (size > 54 && size < 65536) ? (uint8_t *)malloc(size) : NULL;
    if (buf && ReadFile(f, buf, size, &got, NULL) && got == size && buf[0] == 'B' && buf[1] == 'M') {
        uint32_t off = le32(buf + 10), bpp = buf[28] | buf[29] << 8, comp = le32(buf + 30);
        int32_t w = (int32_t)le32(buf + 18), h = (int32_t)le32(buf + 22);
        int top_down = h < 0, x, y, any_alpha = 0;
        uint32_t stride;
        if (top_down) h = -h;
        stride = (uint32_t)((w * (int)(bpp / 8) + 3) & ~3);
        if ((bpp == 24 || bpp == 32) && (comp == 0 || comp == 3) && w > 0 && h > 0 &&
            w <= 64 && h <= 64 && off + stride * (uint32_t)h <= size) {
            if (bpp == 32)
                for (y = 0; y < h && !any_alpha; y++)
                    for (x = 0; x < w; x++)
                        if (buf[off + (uint32_t)y * stride + (uint32_t)x * 4 + 3]) { any_alpha = 1; break; }
            out->px = (uint16_t *)calloc((size_t)(w * h), 2);
            if (out->px) {
                for (y = 0; y < h; y++) {
                    const uint8_t *row = buf + off + (uint32_t)(top_down ? y : h - 1 - y) * stride;
                    for (x = 0; x < w; x++) {
                        const uint8_t *p = row + x * (int)(bpp / 8);
                        int b = p[0], g = p[1], r = p[2];
                        int clear = (bpp == 32 && any_alpha) ? p[3] < 128 : (r == 255 && g == 0 && b == 255);
                        out->px[y * w + x] = clear ? 0 :
                            (uint16_t)(0x8000 | (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
                    }
                }
                out->w = w; out->h = h; ok = 1;
            }
        }
    }
    free(buf);
    CloseHandle(f);
    if (ok) u9log_line("GOLD letter %s: %dx%d", name, out->w, out->h);
    else    u9log_line("GOLD letter %s: present but not a 24- or 32-bit BMP up to 64x64 - not used", name);
    return ok;
}

static const uint8_t *letter_frame(Letter *L, const uint8_t *tpl)
{
    uint32_t flags, wd, hd, r0, r1, rel, stride, y;
    uint8_t *fr;

    if (L->frame || !L->px) return L->frame;
    flags = rd32((uintptr_t)tpl);
    wd = rd32((uintptr_t)tpl + 4); hd = rd32((uintptr_t)tpl + 8);
    if (!(flags & 0x400u) || ((flags >> 8) & 3u) != 1u) {
        if (!g_letterRefused++)
            u9log_line("GOLD letters refused: the digit frame is not 16-bit ARGB1555 (flags %08X)",
                       (unsigned)flags);
        return NULL;
    }
    r0 = rd32((uintptr_t)tpl + 0x14);
    r1 = hd > 1 ? rd32((uintptr_t)tpl + 0x18) : r0 + wd * 2;
    rel = r0 - (0x14u + 4u * hd);
    if (rel > 0x100000u) rel = 0;
    stride = (uint32_t)L->w * 2u;
    fr = (uint8_t *)calloc(1, 0x14u + 4u * (uint32_t)L->h + stride * (uint32_t)L->h);
    if (!fr) return NULL;
    memcpy(fr, &flags, 4);
    memcpy(fr + 4, &L->w, 4); memcpy(fr + 8, &L->h, 4);
    memcpy(fr + 0x0C, tpl + 0x0C, 8);
    for (y = 0; y < (uint32_t)L->h; y++) {
        uint32_t v = rel + 0x14u + 4u * (uint32_t)L->h + y * stride;
        memcpy(fr + 0x14 + 4 * y, &v, 4);
    }
    {
        uint16_t key = (uint16_t)((flags >> 12) & 0xFFFFu), *dst = (uint16_t *)(fr + 0x14 + 4 * L->h);
        uint32_t i, n = (uint32_t)(L->w * L->h);
        for (i = 0; i < n; i++) dst[i] = L->px[i] ? L->px[i] : key;
    }
    u9log_line("GOLD letter frame %dx%d built from the digit frame (flags %08X, key %04X, %ux%u, anchors"
               " %u,%u, rows from %u step %u)", L->w, L->h, (unsigned)flags,
               (unsigned)((flags >> 12) & 0xFFFFu), (unsigned)wd, (unsigned)hd,
               (unsigned)rd32((uintptr_t)tpl + 0x0C), (unsigned)rd32((uintptr_t)tpl + 0x10),
               (unsigned)r0, (unsigned)(r1 - r0));
    L->frame = fr;
    return fr;
}

#define LETTER_GAP (-1)

static int letter_shift(void)
{
    return (g_suffix && g_letter[g_suffix].px) ? (g_letter[g_suffix].w + LETTER_GAP) / 2 : 0;
}

static void U9CALL h_digit(void *t, const void *f, int x, int y)
{
    o_blit(t, f, x - letter_shift(), y);
}

static void U9CALL h_units(void *t, const void *f, int x, int y)
{
    int sh = letter_shift();
    o_blit(t, f, x - sh, y);
    if (sh && f) {
        Letter *L = &g_letter[g_suffix];
        const uint8_t *fr = letter_frame(L, (const uint8_t *)f);
        if (fr) {
            int dh = (int)rd32((uintptr_t)f + 8);
            o_blit(t, fr, x - sh + (int)rd32((uintptr_t)f + 4) + LETTER_GAP, y + dh - L->h);
            g_letterDraws++;
        }
    }
}

static int U9CALL h_redraw(void *self)
{
    uintptr_t w = (uintptr_t)self;
    uint32_t total, shown;
    int changed = 0, sfx;

    total = (uint32_t)((GoldTotalFn)u9sym(U9SYM_GOLD_TOTAL))();
    if (total > 999999u && g_lettersOn && g_letter[SFX_M].px) { shown = total / 1000000u; sfx = SFX_M; }
    else if (total > 9999u)  { shown = total / 1000u; sfx = (g_lettersOn && g_letter[SFX_K].px) ? SFX_K : SFX_NONE; }
    else                     { shown = total; sfx = SFX_NONE; }
    if (*(volatile uint16_t *)(w + WIDGET_CACHE) != (uint16_t)shown || sfx != g_suffix) {
        *(volatile uint16_t *)(w + WIDGET_CACHE) = (uint16_t)shown;
        g_suffix = sfx;
        changed = 1;
    }
    if (total != g_lastTotal) {
        if (g_totalLogged < 200) {
            g_totalLogged++;
            u9log_line("GOLD total %u, the backpack shows %u%s", (unsigned)total, (unsigned)shown,
                       sfx == SFX_M ? "M" : sfx == SFX_K ? "K" : total > 9999u ? " (thousands)" : "");
        }
        g_lastTotal = total;
    }
    return (*(volatile uint32_t *)(w + WIDGET_FORCE) != 0) | changed;
}

static const uint8_t BLIT_TH_EXPECT[5] = { 0xE8, 0xF4, 0x57, 0x01, 0x00 };
static const uint8_t BLIT_HU_EXPECT[5] = { 0xE8, 0xC3, 0x57, 0x01, 0x00 };
static const uint8_t BLIT_TE_EXPECT[5] = { 0xE8, 0x92, 0x57, 0x01, 0x00 };
static const uint8_t BLIT_UN_EXPECT[5] = { 0xE8, 0x6D, 0x57, 0x01, 0x00 };
static const U9PatchSpec BLIT_TH_SPEC = { "GOLD_BLIT_THOUSANDS", U9PATCH_CALLSITE, U9SYM_GOLD_BLIT_THOUSANDS, 5, BLIT_TH_EXPECT, 5, U9PF_EXCLUSIVE };
static const U9PatchSpec BLIT_HU_SPEC = { "GOLD_BLIT_HUNDREDS",  U9PATCH_CALLSITE, U9SYM_GOLD_BLIT_HUNDREDS,  5, BLIT_HU_EXPECT, 5, U9PF_EXCLUSIVE };
static const U9PatchSpec BLIT_TE_SPEC = { "GOLD_BLIT_TENS",      U9PATCH_CALLSITE, U9SYM_GOLD_BLIT_TENS,      5, BLIT_TE_EXPECT, 5, U9PF_EXCLUSIVE };
static const U9PatchSpec BLIT_UN_SPEC = { "GOLD_BLIT_UNITS",     U9PATCH_CALLSITE, U9SYM_GOLD_BLIT_UNITS,     5, BLIT_UN_EXPECT, 5, U9PF_EXCLUSIVE };

static void letters_install(void)
{
    int k = load_letter("gold_K.bmp", &g_letter[SFX_K]);
    int m = load_letter("gold_M.bmp", &g_letter[SFX_M]);
    if (!k && !m) {
        u9log_line("GOLD letters: no gold_K.bmp or gold_M.bmp beside the DLL - thousands show with no letter");
        return;
    }
    o_blit = (BlitFn)u9patch_install(&BLIT_UN_SPEC, (void *)h_units);
    if (!o_blit) { u9log_line("GOLD letters: the units blit was refused - no letters"); return; }
    if (!u9patch_install(&BLIT_TH_SPEC, (void *)h_digit) ||
        !u9patch_install(&BLIT_HU_SPEC, (void *)h_digit) ||
        !u9patch_install(&BLIT_TE_SPEC, (void *)h_digit))
        u9log_line("GOLD letters: a digit blit was refused - a reading may sit off centre");
    g_lettersOn = 1;
    u9log_line("GOLD letters on: %s%s%s", k ? "K above 9,999" : "", k && m ? ", " : "",
               m ? "M above 999,999" : "");
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
        if (g_origRedraw) {
            u9log_line("GOLD backpack count: above 9,999 it shows thousands (10,000"
                       " reads 10)");
            if (u9cfg_int("gold_letters", 1)) letters_install();
        } else
            u9log_line("GOLD backpack count NOT replaced - refused (see the manifest)");
    }
}

void u9gold_report(void)
{
    if (g_lettersOn)
        u9log_line("  GOLD letters drawn %u times", g_letterDraws);
}
