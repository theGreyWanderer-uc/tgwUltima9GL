#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "fixmem.h"
#include "doorfix.h"
#include "portalfix.h"

void u9log_line(const char *fmt, ...);

#define DOOR_OPEN_BIT      0x20000u
#define DOOR_REVERSE_BIT   0x4000u
#define DOOR_ACTIVE_BIT    0x10u
#define DOOR_FIXED_BIT     0x01u
#define PROC_STATE         0x168u
#define PROC_DOOR_HANDLE   0xD8u
#define PROP_CLOSED_ANGLE  0x3D
#define HANDLE_GET_PROPERTY 0x004C5640u
#define OBJECT_WORLD_POSITION 0x004BE730u
#define MAX_LINES          2000u

typedef void (U9CALL *StepFn)(void *self);
typedef int  (U9CALL *GetPropFn)(uint32_t *handleAddr, int property);
typedef void *(U9CALL *WorldPosFn)(uintptr_t obj, void *out);

#define FIND_ITEM_PROCESS  0x00576FD0u
#define OBJECT_ADD_FLAGS   0x004BEAE0u
#define PROC_TYPE_OPEN     0x35
#define PROC_TYPE_CLOSE    0x36
#define AJAR_DEG           5.7
#define FIX_LINES          200u

typedef void *(__attribute__((fastcall)) *FindProcFn)(uintptr_t obj, int type);
typedef void  (U9CALL *AddFlagsFn)(uintptr_t obj, unsigned bits);

static StepFn   o_open, o_close;
static int      g_on;
static int      g_fix;
static int      g_portal;
static unsigned g_lines, g_opens, g_closes, g_ajar;
static unsigned g_fixLines, g_busy, g_keptOpen, g_reconciled;

static int door_busy(uintptr_t obj)
{
    FindProcFn find = (FindProcFn)FIND_ITEM_PROCESS;
    return find(obj, PROC_TYPE_OPEN) != 0 || find(obj, PROC_TYPE_CLOSE) != 0;
}

#define OBJECT_MASK_FLAGS  0x004BEB00u
typedef void  (U9CALL *MaskFlagsFn)(uintptr_t obj, unsigned mask);

static void door_mark_open(uintptr_t obj)
{
    ((AddFlagsFn)OBJECT_ADD_FLAGS)(obj, 0x20000u);
}

static void door_mark_shut(uintptr_t obj)
{
    ((MaskFlagsFn)OBJECT_MASK_FLAGS)(obj, ~0x20000u);
}

static uintptr_t door_object(uint32_t handle)
{
    uintptr_t tbl, slot;
    if (!readable4(ENTITY_TABLE)) return 0;
    tbl = rd32(ENTITY_TABLE);
    if (!tbl) return 0;
    slot = tbl + (uintptr_t)handle * 0x0Cu + 8;
    if (!readable4(slot)) return 0;
    slot = rd32(slot);
    return (slot && readable4(slot + 0x14)) ? slot : 0;
}

static double door_z_angle(uintptr_t obj, int q[4])
{
    const int16_t *s = (const int16_t *)(obj + 0x0C);
    double z, w, a;
    q[0] = s[0]; q[1] = s[1]; q[2] = s[2]; q[3] = s[3];
    z = q[2] / 32768.0;
    w = (q[3] == -32768 && !q[0] && !q[1] && !q[2]) ? 1.0 : q[3] / 32768.0;
    a = -2.0 * atan2(z, w) * (180.0 / 3.14159265358979);
    while (a < 0.0)    a += 360.0;
    while (a >= 360.0) a -= 360.0;
    return a;
}

static double door_closed_angle_at(uint32_t *handleAddr)
{
    GetPropFn get = (GetPropFn)HANDLE_GET_PROPERTY;
    int bits = get(handleAddr, PROP_CLOSED_ANGLE);
    float f;
    double a;
    if (!bits) return -1.0;
    memcpy(&f, &bits, sizeof f);
    a = ((double)f - 10.0) * (180.0 / 3.14159265358979);
    while (a < 0.0)    a += 360.0;
    while (a >= 360.0) a -= 360.0;
    return a;
}

static double door_closed_angle(void *proc)
{
    return door_closed_angle_at((uint32_t *)((uintptr_t)proc + PROC_DOOR_HANDLE));
}

#define DOOR_TABLE        0x007A7FA8u
#define OBJTYPES_ADDR        0x00AE7C7Cu
#define DOOR_TABLE_ROWS   7
#define MAX_CLOSE_SWING   240.0

static int door_axis_code(uintptr_t obj)
{
    unsigned type = *(const uint16_t *)(obj + 0x0A);
    uintptr_t tbl;
    int i, row = -1;
    int16_t base;
    for (i = 0; i < DOOR_TABLE_ROWS; i++)
        if (rd32(DOOR_TABLE + (uintptr_t)i * 12u) == type) return (int)rd32(DOOR_TABLE + (uintptr_t)i * 12u + 4);
    if (!readable4(OBJTYPES_ADDR) || !(tbl = rd32(OBJTYPES_ADDR))) return -1;
    base = *(const int16_t *)(tbl + (uintptr_t)type * 0x10u + 4);
    if (base == 0x448) row = 0;
    else if (base == 0x449) row = 1;
    else if (base == 0x44A) row = 2;
    return row < 0 ? -1 : (int)rd32(DOOR_TABLE + (uintptr_t)row * 12u + 4);
}

static double close_swing(uintptr_t obj, double cur, double closed)
{
    int code = door_axis_code(obj), sign;
    double d;
    if (code != 4 && code != 5) return -1.0;
    sign = (code == 4) ? 1 : -1;
    if (!(rd32(obj + 0x14) & DOOR_REVERSE_BIT)) sign = -sign;
    d = (sign > 0) ? cur - closed : closed - cur;
    while (d < 0.0)    d += 360.0;
    while (d >= 360.0) d -= 360.0;
    return d;
}

static double angle_gap(double a, double b)
{
    double d = fabs(a - b);
    return d > 180.0 ? 360.0 - d : d;
}

static void door_line(const char *kind, const char *when, void *proc, unsigned flagsBefore)
{
    uint32_t  handle = rd32((uintptr_t)proc + PROC_DOOR_HANDLE);
    unsigned  state = rd32((uintptr_t)proc + PROC_STATE);
    uintptr_t obj = door_object(handle);
    unsigned  flags, type;
    int       q[4];
    double    ang, closed;

    if (!g_on || !obj || g_lines >= MAX_LINES) return;
    flags  = rd32(obj + 0x14);
    type   = *(const uint16_t *)(obj + 0x0A);
    ang    = door_z_angle(obj, q);
    closed = door_closed_angle(proc);
    g_lines++;
    u9log_line("DOOR %s %s handle %u type %u state %u flags %08X%s%s%s%s angle %.1f closed %s%.1f"
               " gap %.1f quat %d,%d,%d,%d%s",
               kind, when, (unsigned)handle, type, state, flags,
               (flags & DOOR_OPEN_BIT) ? " OPEN" : " shut",
               (flags & DOOR_REVERSE_BIT) ? " rev" : "",
               (flags & DOOR_ACTIVE_BIT) ? "" : " NOT-ACTIVE",
               (flags & DOOR_FIXED_BIT) ? " FIXED" : "",
               ang, closed < 0 ? "unset " : "", closed < 0 ? 0.0 : closed,
               closed < 0 ? -1.0 : angle_gap(ang, closed), q[0], q[1], q[2], q[3],
               (flagsBefore != ~0u && flagsBefore != flags) ? " (flags changed)" : "");
}

static void U9CALL h_open(void *self)
{
    unsigned state = rd32((uintptr_t)self + PROC_STATE), before = 0;
    uint32_t  handle = rd32((uintptr_t)self + PROC_DOOR_HANDLE);
    uintptr_t obj = door_object(handle);
    if (state == 2) g_opens++;
    door_line("open ", "pre ", self, ~0u);
    if (obj) before = rd32(obj + 0x14);
    o_open(self);
    if (state < 3) door_line("open ", "post", self, before);
    if (g_portal && obj && state >= 2)
        u9portal_door_note(obj, (unsigned)handle, state == 2 ? "open-start" : "open-end");
}

static void U9CALL h_close(void *self)
{
    unsigned state = rd32((uintptr_t)self + PROC_STATE), before = 0;
    uint32_t  handle = rd32((uintptr_t)self + PROC_DOOR_HANDLE);
    uintptr_t obj = door_object(handle);
    if (state == 0) g_closes++;
    door_line("close", "pre ", self, ~0u);
    if (obj) before = rd32(obj + 0x14);
    o_close(self);
    if (g_portal && obj)
        u9portal_door_note(obj, (unsigned)handle, state == 0 ? "close-start" :
                           state == 1 ? "close-end" : "close-pop");
    if (state >= 2) return;
    door_line("close", "post", self, before);
    if (state == 1 && obj) {
        int q[4];
        double ang = door_z_angle(obj, q), closed = door_closed_angle(self);
        if (closed >= 0.0 && angle_gap(ang, closed) > AJAR_DEG) {
            double swing = close_swing(obj, ang, closed);
            int keep = g_fix && swing >= 0.0 && swing <= MAX_CLOSE_SWING;
            g_ajar++;
            if (keep) {
                door_mark_open(obj);
                g_keptOpen++;
            }
            if ((g_on && g_lines < MAX_LINES) || (g_fix && g_fixLines < FIX_LINES)) {
                if (g_on) g_lines++; else g_fixLines++;
                u9log_line("DOOR AJAR handle %u: close finished %.1f deg from its closed angle"
                           " (%.1f vs %.1f, a close from here turns %.1f) - %s",
                           (unsigned)rd32((uintptr_t)self + PROC_DOOR_HANDLE),
                           angle_gap(ang, closed), ang, closed, swing,
                           keep ? "fix_doors keeps it marked OPEN, the next use closes it" :
                           g_fix ? "past closed: left marked shut, the next use opens it through closed"
                                 : "the door is now marked shut");
            }
        }
    }
}

#define RET_USE_OPEN   0x0063BBFFu
#define RET_USE_CLOSE  0x0063BCFBu
#define CODE_LO        0x00401000u
#define CODE_HI        0x00730000u

typedef void (U9CALL *DriverFn)(void *usecode);
static DriverFn o_driver;
static unsigned g_drives, g_drivesOther;

static int after_call(uint32_t ret)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)ret;
    if (ret < CODE_LO + 8 || ret >= CODE_HI) return 0;
    return p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
}

static void U9CALL h_driver(void *uc)
{
    uint32_t  ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
    uint32_t  handle = rd32((uintptr_t)uc);
    unsigned  mode = rd32((uintptr_t)uc + 0x0C);
    uintptr_t obj = door_object(handle);
    const char *via = ret == RET_USE_OPEN ? "use-open" : ret == RET_USE_CLOSE ? "use-close" : "OTHER";
    const char *what = (mode & 0xFF) == 0 ? "open" : (mode & 0xFF) == 1 ? "close" :
                       (mode & 0xFF) == 2 ? "toggle" : "?";

    g_drives++;
    if (ret != RET_USE_OPEN && ret != RET_USE_CLOSE) g_drivesOther++;
    if (g_fix && obj && door_busy(obj)) {
        g_busy++;
        if ((g_on && g_lines < MAX_LINES) || g_fixLines < FIX_LINES) {
            if (g_on) g_lines++; else g_fixLines++;
            u9log_line("DOOR busy handle %u: %s request %s while it is still swinging -"
                       " ignored (fix_doors)", (unsigned)handle, what,
                       ret == RET_USE_OPEN || ret == RET_USE_CLOSE ? "from a use" : "from a trigger or timer");
        }
        return;
    }
    if (g_on && obj && g_lines < MAX_LINES) {
        uint32_t stack[4] = { 0, 0, 0, 0 };
        unsigned found = 0, i;
        int q[4];
        unsigned flags = rd32(obj + 0x14);
        double ang = door_z_angle(obj, q), closed = door_closed_angle_at((uint32_t *)uc);
        struct { int32_t x, y; int16_t z, pad; } pos = { 0, 0, 0, 0 };
        ((WorldPosFn)OBJECT_WORLD_POSITION)(obj, &pos);
        if (ret != RET_USE_OPEN && ret != RET_USE_CLOSE) {
            const uint32_t *sp = (const uint32_t *)__builtin_frame_address(0);
            for (i = 2; i < 160 && found < 4; i++) {
                uint32_t v;
                if (!readable4((uintptr_t)(sp + i))) break;
                v = sp[i];
                if (v != ret && after_call(v)) stack[found++] = v;
            }
        }
        g_lines++;
        u9log_line("DOOR drive handle %u type %u at %d,%d,%d mode %04X (%s%s) via %s ret %08X"
                   " | %s angle %.1f closed %s%.1f | callers %08X %08X %08X %08X",
                   (unsigned)handle, (unsigned)*(const uint16_t *)(obj + 0x0A),
                   (int)pos.x, (int)pos.y, (int)pos.z,
                   mode, what, (mode & 0x8000) ? " + pair" : "", via,
                   (unsigned)ret, (flags & DOOR_OPEN_BIT) ? "OPEN" : "shut", ang,
                   closed < 0 ? "unset " : "", closed < 0 ? 0.0 : closed,
                   (unsigned)stack[0], (unsigned)stack[1], (unsigned)stack[2], (unsigned)stack[3]);
    }
    o_driver(uc);
}

typedef void (U9CALL *UseFn)(void *usecode);
static UseFn o_use;

static void U9CALL h_use(void *uc)
{
    uint32_t  handle = rd32((uintptr_t)uc);
    uintptr_t obj = door_object(handle);
    if (g_fix && obj && (rd32(obj + 0x14) & DOOR_OPEN_BIT) && !door_busy(obj)) {
        int q[4];
        double ang = door_z_angle(obj, q), closed = door_closed_angle_at((uint32_t *)uc);
        double swing = (closed >= 0.0) ? close_swing(obj, ang, closed) : -1.0;
        if (closed >= 0.0 && angle_gap(ang, closed) > AJAR_DEG && swing > MAX_CLOSE_SWING) {
            door_mark_shut(obj);
            g_reconciled++;
            if (g_fixLines < FIX_LINES) {
                g_fixLines++;
                u9log_line("DOOR reconcile handle %u: marked OPEN but past closed (%.1f vs %.1f,"
                           " a close would turn %.1f) - marked shut, so this use opens it through"
                           " closed (fix_doors)", (unsigned)handle, ang, closed, swing);
            }
        }
    } else if (g_fix && obj && !(rd32(obj + 0x14) & DOOR_OPEN_BIT) && !door_busy(obj)) {
        int q[4];
        double ang = door_z_angle(obj, q), closed = door_closed_angle_at((uint32_t *)uc);
        if (closed >= 0.0 && angle_gap(ang, closed) > AJAR_DEG) {
            double swing = close_swing(obj, ang, closed);
            int fixIt = swing >= 0.0 && swing <= MAX_CLOSE_SWING;
            if (fixIt) {
                door_mark_open(obj);
                g_reconciled++;
            }
            if (g_fixLines < FIX_LINES) {
                g_fixLines++;
                u9log_line("DOOR reconcile handle %u: marked shut but %.1f deg from closed"
                           " (%.1f vs %.1f, a close turns %.1f) - %s", (unsigned)handle,
                           angle_gap(ang, closed), ang, closed, swing,
                           fixIt ? "marked OPEN, so this use closes it (fix_doors)"
                                 : "past closed: left to the game, this use opens it through closed");
            }
        }
    }
    o_use(uc);
}

void u9door_install(void)
{
    static const uint8_t expClose[4] = { 0x10, 0xC3, 0x63, 0x00 };
    static const uint8_t expOpen[4]  = { 0xB0, 0xC8, 0x63, 0x00 };
    static const U9PatchSpec specClose = {
        "DOOR_CLOSE_STEP", U9PATCH_VTABLE, U9SYM_DOOR_CLOSE_STEP_SLOT, 4, expClose, 4,
        U9PF_EXCLUSIVE
    };
    static const U9PatchSpec specOpen = {
        "DOOR_OPEN_STEP", U9PATCH_VTABLE, U9SYM_DOOR_OPEN_STEP_SLOT, 4, expOpen, 4,
        U9PF_EXCLUSIVE
    };

    static const uint8_t expDriver[7] = { 0x6A, 0xFF, 0x68, 0x39, 0x65, 0x73, 0x00 };
    static const U9PatchSpec specDriver = {
        "DOOR_DRIVER", U9PATCH_INLINE, U9SYM_DOOR_DRIVER, 7, expDriver, 7, U9PF_EXCLUSIVE
    };

    static const uint8_t expUse[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    static const U9PatchSpec specUse = {
        "DOOR_USE", U9PATCH_INLINE, U9SYM_DOOR_USE, 6, expUse, 6, U9PF_EXCLUSIVE
    };

    g_on  = u9cfg_int("diag_door_watch", 0);
    g_fix = u9cfg_int("fix_doors", 1);
    g_portal = u9cfg_int("diag_portal_watch", 0);
    if (!g_fix && !g_on && !g_portal) {
        u9log_line("I-16 door fix disabled by config - doors behave as stock");
        return;
    }
    o_close  = (StepFn)u9patch_install(&specClose, (void *)h_close);
    o_driver = (DriverFn)u9patch_install(&specDriver, (void *)h_driver);
    if (g_on || g_portal) o_open = (StepFn)u9patch_install(&specOpen, (void *)h_open);
    if (g_fix) o_use = (UseFn)u9patch_install(&specUse, (void *)h_use);
    if (g_fix && (!o_close || !o_driver || !o_use))
        u9log_line("I-16 door fix INCOMPLETE: close step %s, driver %s, use %s",
                   o_close ? "hooked" : "REFUSED", o_driver ? "hooked" : "REFUSED",
                   o_use ? "hooked" : "REFUSED");
    else if (g_fix)
        u9log_line("I-16 door fix on: one swing at a time, a close that stops short stays"
                   " OPEN, a shut-marked door that is not shut is closed by its next use");
    if (g_on)
        u9log_line("DOOR watch: close step %s, open step %s, driver %s; a line per step with"
                   " state, OPEN bit, angle and the stored closed angle, AJAR when a close ends"
                   " away from closed, and a drive line naming who asked (use or OTHER)",
                   o_close ? "hooked" : "REFUSED", o_open ? "hooked" : "REFUSED",
                   o_driver ? "hooked" : "REFUSED");
}

void u9door_report(void)
{
    if (g_fix)
        u9log_line("I-16 door fix: %u requests ignored while a door was swinging, %u short"
                   " closes kept OPEN, %u shut-marked doors closed by their use",
                   g_busy, g_keptOpen, g_reconciled);
    if (g_on)
        u9log_line("DOOR totals: %u drives (%u not from a use), %u opens, %u closes,"
                   " %u closes ended ajar, %u lines",
                   g_drives, g_drivesOther, g_opens, g_closes, g_ajar, g_lines);
}
