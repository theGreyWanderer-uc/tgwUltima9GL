#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "bagprobe.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define OBJ_TYPE        0x0Au
#define OBJ_FLAGS       0x14u
#define OBJF_WINDOW     0x800u
#define OBJ_BAG        0x0926u

#define WIN_ITEM        0x84u
#define WIN_KIND        0x80u

#define TOG_ITEM        0xD8u

#define UI_CODE_LO      0x005C9000u
#define UI_CODE_HI      0x005D0000u

#define LINE_CAP        600

static DWORD    g_t0;
static unsigned g_lines;
static unsigned g_uses, g_opens, g_closes, g_closeEmpty, g_winCloses, g_closeFors;

static void blog(const char *fmt, ...)
{
    char line[400];
    va_list ap;

    if (g_lines >= LINE_CAP) {
        if (g_lines == LINE_CAP) {
            g_lines++;
            u9log_line("BAG line cap reached - counting continues");
        }
        return;
    }
    g_lines++;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    u9log_line("BAG %7lu ms %s", (unsigned long)(u9time_now() - g_t0), line);
}

static uintptr_t obj_of(uint32_t h)
{
    uintptr_t tab = rd32(ENTITY_TABLE);
    if (!h || !tab || !readable4(tab + h * 12u + 8u)) return 0;
    return rd32(tab + h * 12u + 8u);
}

static unsigned obj_type(uintptr_t o)
{
    return (o && readable4(o + OBJ_TYPE)) ? *(const volatile uint16_t *)(o + OBJ_TYPE) : 0;
}

static unsigned obj_flag(uintptr_t o)
{
    return (o && readable4(o + OBJ_FLAGS)) ? (rd32(o + OBJ_FLAGS) & OBJF_WINDOW) != 0 : 0;
}

typedef void (U9CALL *UseFn)(void *obj, void *user);
static UseFn o_use;

static void U9CALL h_use(void *obj, void *user)
{
    uintptr_t o = (uintptr_t)obj;
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    unsigned type = obj_type(o);
    int fromUi = ret >= UI_CODE_LO && ret < UI_CODE_HI;

    if (type == OBJ_BAG || fromUi || obj_flag(o)) {
        g_uses++;
        blog("use     obj %08X type %u open-bit %u, from %08X%s", (unsigned)o,
             type, obj_flag(o), (unsigned)ret, fromUi ? " (window code)" : "");
    }
    o_use(obj, user);
}

typedef void (U9CALL *ToggleFn)(void *self, int open);
static ToggleFn o_toggle;

static void U9CALL h_toggle(void *self, int open)
{
    uintptr_t s = (uintptr_t)self;
    uint32_t h = readable4(s + TOG_ITEM) ? rd32(s + TOG_ITEM) : 0;
    uintptr_t o = obj_of(h);
    unsigned before = obj_flag(o);

    if ((open & 0xFF) != 0) g_opens++;
    else { g_closes++; if (!before) g_closeEmpty++; }
    blog("toggle  %s handle %u obj %08X type %u open-bit %u, by %08X (vtbl %08X)",
         (open & 0xFF) ? "OPEN " : "CLOSE", h, (unsigned)o, obj_type(o), before,
         (unsigned)(uintptr_t)__builtin_return_address(0),
         readable4(s) ? rd32(s) : 0);
    o_toggle(self, open);
    blog("toggle  done, open-bit now %u", obj_flag(o));
}

typedef void (U9CALL *CloseForFn)(void *ecx, uint32_t h);
static CloseForFn o_closeFor;

static void U9CALL h_closeFor(void *ecx, uint32_t h)
{
    uintptr_t o = obj_of(h);
    g_closeFors++;
    blog("closefor handle %u obj %08X type %u open-bit %u, from %08X", h,
         (unsigned)o, obj_type(o), obj_flag(o),
         (unsigned)(uintptr_t)__builtin_return_address(0));
    o_closeFor(ecx, h);
}

typedef void (U9CALL *CloseFn)(void *self);
static CloseFn o_closeBag, o_closePack;

static void close_log(void *self, const char *who, uintptr_t ret)
{
    uintptr_t w = (uintptr_t)self;
    uint32_t h = readable4(w + WIN_ITEM) ? rd32(w + WIN_ITEM) : 0;
    uintptr_t o = obj_of(h);
    g_winCloses++;
    blog("close   %s window %08X handle %u obj %08X type %u open-bit %u, from %08X",
         who, (unsigned)w, h, (unsigned)o, obj_type(o), obj_flag(o),
         (unsigned)ret);
}

static void U9CALL h_closeBag(void *self)
{
    close_log(self, "bag     ", (uintptr_t)__builtin_return_address(0));
    o_closeBag(self);
}

static void U9CALL h_closePack(void *self)
{
    close_log(self, "backpack", (uintptr_t)__builtin_return_address(0));
    o_closePack(self);
}

void u9bag_install(void)
{
    static const uint8_t expUse[6]    = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t expToggle[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t expFor[7]    = { 0x6A, 0xFF, 0x68, 0x4C, 0x29, 0x73, 0x00 };
    static const uint8_t expClose[4]  = { 0xC0, 0x93, 0x5C, 0x00 };
    static const U9PatchSpec specUse = {
        "BAG_ITEM_USE", U9PATCH_INLINE, U9SYM_ITEM_USE, 6, expUse, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specToggle = {
        "BAG_TOGGLE", U9PATCH_INLINE, U9SYM_BAG_TOGGLE, 6, expToggle, 6, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specFor = {
        "BAG_CLOSEFOR", U9PATCH_INLINE, U9SYM_CONTAINER_CLOSEFOR, 7, expFor, 7, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specCloseBag = {
        "BAG_CLOSE_BAG", U9PATCH_VTABLE, U9SYM_CONTAINER_CLOSE_SLOT, 4, expClose, 4, U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specClosePack = {
        "BAG_CLOSE_BACKPACK", U9PATCH_VTABLE, U9SYM_BACKPACK_CLOSE_SLOT, 4, expClose, 4, U9PF_EXCLUSIVE
    };

    g_t0 = u9time_now();
    o_use       = (UseFn)u9patch_install(&specUse, (void *)h_use);
    o_toggle    = (ToggleFn)u9patch_install(&specToggle, (void *)h_toggle);
    o_closeFor  = (CloseForFn)u9patch_install(&specFor, (void *)h_closeFor);
    o_closeBag  = (CloseFn)u9patch_install(&specCloseBag, (void *)h_closeBag);
    o_closePack = (CloseFn)u9patch_install(&specClosePack, (void *)h_closePack);
    u9log_line("BAG container watch: use %s, toggle %s, closefor %s, bag close %s,"
               " backpack close %s. Quit right after a failed click; the last"
               " BAG lines are that click",
               o_use ? "hooked" : "REFUSED", o_toggle ? "hooked" : "REFUSED",
               o_closeFor ? "hooked" : "REFUSED", o_closeBag ? "hooked" : "REFUSED",
               o_closePack ? "hooked" : "REFUSED");
}

void u9bag_report(void)
{
    if (!o_use && !o_toggle) return;
    u9log_line("BAG totals: %u uses logged, %u toggle opens, %u toggle closes"
               " (%u with the bit already clear), %u window closes, %u closefors",
               g_uses, g_opens, g_closes, g_closeEmpty, g_winCloses, g_closeFors);
}
