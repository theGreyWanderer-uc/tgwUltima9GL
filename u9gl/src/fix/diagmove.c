#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "diagmove.h"

void u9log_line(const char *fmt, ...);

#define AVATAR_CONTROL   0x0131F668u
#define AC_SIDE          0x14
#define AC_FWD           0x18
#define AC_LOCK          0x1C
#define AC_LATCH         0x1D
#define AC_HELD          0x1E
#define AC_SHIP          0x20
#define AC_LOCK2         0x50
#define VTBL_AVATAR_MOVER 0x00741F70u
#define EV_KEYDOWN       0x20u

#define DIAG_UNIT        0.70710678f

enum { K_FORWARD, K_BACKWARD, K_LEFT, K_RIGHT, K_COUNT };

typedef void (__attribute__((stdcall)) *CmdFn)(uint32_t, uint32_t, uint32_t, uint32_t,
                                               uint32_t, uint32_t, uint32_t, uint32_t);

typedef struct {
    int      down;
    int      side, fwd;
    unsigned seq;
} HeldKey;

static CmdFn    o_cmd[K_COUNT];
static HeldKey  g_key[K_COUNT];
static unsigned g_seq;
static int      g_diagSide, g_diagFwd;
static float    g_vec[3];
static unsigned g_diagSteps, g_diagStarts, g_faceTurns;

static uintptr_t control(void)
{
    uintptr_t c = readable4(AVATAR_CONTROL) ? rd32(AVATAR_CONTROL) : 0;
    return (c && readable4(c + AC_LOCK2)) ? c : 0;
}

#define GAME_GLOBALS     0x00850228u
#define COMBAT_STATE     0x0098F324u
#define AVATAR_MOVER_OF  0xE8
#define SLOT_TURN_TO     0x90
#define QUARTER_PI       0.78539816f

typedef void (U9CALL *TurnToFn)(void *mover, float yaw, int a, int b);

static int   g_face;
static float g_faceApplied;

static uintptr_t rd_ptr(uintptr_t p) { return readable4(p) ? rd32(p) : 0; }

static int first_person(void)
{
    uintptr_t g = rd_ptr(GAME_GLOBALS), mode = g ? rd_ptr(g + 0xC8) : 0;
    return mode && readable4(mode + 8) && rd32(mode + 8) == 1;
}

static int target_locked(void)
{
    uintptr_t cs = rd_ptr(COMBAT_STATE);
    return cs && readable4(cs + 0x100C) && rd32(cs + 0x100C) != 0;
}

static float face_offset(void)
{
    uintptr_t c;
    if (!g_face || !g_diagSide) return 0.0f;
    c = control();
    if (!c || rd32(c + AC_SHIP) || first_person() || target_locked()) return 0.0f;
    return -(float)(g_diagSide * g_diagFwd) * QUARTER_PI;
}

static void turn_to(uintptr_t mover, float yaw)
{
    uintptr_t vt = rd_ptr(mover);
    TurnToFn fn = vt && readable4(vt + SLOT_TURN_TO) ? (TurnToFn)rd32(vt + SLOT_TURN_TO) : NULL;
    if (fn) fn((void *)mover, yaw, -1, 0);
}

static void U9CALL h_turn_to(void *mover, float yaw, int a, int b)
{
    float off = face_offset();
    TurnToFn fn = (TurnToFn)rd32(rd32((uintptr_t)mover) + SLOT_TURN_TO);
    g_faceApplied = off;
    fn(mover, yaw + off, a, b);
}

static void face_refresh(void)
{
    uintptr_t c, actor, mover, g, cam;
    float off = face_offset();
    if (!g_face || off == g_faceApplied) return;
    c = control();
    if (!c || *(volatile uint8_t *)(c + AC_LOCK) || first_person()) { g_faceApplied = off; return; }
    actor = rd_ptr(c);
    mover = actor ? rd_ptr(actor + AVATAR_MOVER_OF) : 0;
    g = rd_ptr(GAME_GLOBALS);
    cam = g ? rd_ptr(g + 0xC0) : 0;
    if (!mover || !cam || !readable4(cam + 0x44)) return;
    turn_to(mover, *(volatile float *)(cam + 0x44) + off);
    g_faceApplied = off;
    if (off != 0.0f) g_faceTurns++;
}

static int any_key_down(void)
{
    int vk;
    for (vk = 0x08; vk < 0xFF; vk++)
        if (GetAsyncKeyState(vk) & 0x8000) return 1;
    return 0;
}

static void forget_keys(void)
{
    memset(g_key, 0, sizeof g_key);
    g_diagSide = g_diagFwd = 0;
}

static void apply_keys(uintptr_t c)
{
    unsigned sSeq = 0, fSeq = 0;
    int side = 0, fwd = 0, i;
    for (i = 0; i < K_COUNT; i++) {
        const HeldKey *k = &g_key[i];
        if (!k->down) continue;
        if (k->side && k->seq >= sSeq) { side = k->side; sSeq = k->seq; }
        if (k->fwd  && k->seq >= fSeq) { fwd  = k->fwd;  fSeq = k->seq; }
    }
    if (side && fwd) {
        if (!g_diagSide) g_diagStarts++;
        g_diagSide = side; g_diagFwd = fwd;
        *(volatile int32_t *)(c + AC_SIDE) = 0;
        *(volatile int32_t *)(c + AC_FWD)  = fwd;
    } else {
        g_diagSide = g_diagFwd = 0;
        *(volatile int32_t *)(c + AC_SIDE) = side;
        *(volatile int32_t *)(c + AC_FWD)  = fwd;
    }
    *(volatile uint8_t *)(c + AC_LATCH) = 1;
    *(volatile uint8_t *)(c + AC_HELD)  = 1;
}

static void on_cmd(int key, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                   uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uintptr_t c = control();
    int others = 0, i;

    if (!c || rd32(c + AC_SHIP) || *(volatile uint8_t *)(c + AC_LOCK) || rd32(c + AC_LOCK2)) {
        forget_keys();
        o_cmd[key](a0, a1, a2, a3, a4, a5, a6, a7);
        return;
    }

    if (a5 & EV_KEYDOWN) {
        int32_t side0 = (int32_t)rd32(c + AC_SIDE), fwd0 = (int32_t)rd32(c + AC_FWD);
        uint8_t latch0 = *(volatile uint8_t *)(c + AC_LATCH);
        int32_t side, fwd;
        if (!latch0) forget_keys();
        *(volatile int32_t *)(c + AC_SIDE) = 0;
        *(volatile int32_t *)(c + AC_FWD)  = 0;
        *(volatile uint8_t *)(c + AC_LATCH) = 0;
        o_cmd[key](a0, a1, a2, a3, a4, a5, a6, a7);
        side = (int32_t)rd32(c + AC_SIDE);
        fwd  = (int32_t)rd32(c + AC_FWD);
        if (!side && !fwd) {
            *(volatile int32_t *)(c + AC_SIDE) = side0;
            *(volatile int32_t *)(c + AC_FWD)  = fwd0;
            *(volatile uint8_t *)(c + AC_LATCH) = latch0;
            return;
        }
        if (!g_key[key].down) g_key[key].seq = ++g_seq;
        g_key[key].down = 1;
        g_key[key].side = side;
        g_key[key].fwd  = fwd;
        apply_keys(c);
        return;
    }

    g_key[key].down = 0;
    for (i = 0; i < K_COUNT; i++) if (g_key[i].down) others = 1;
    if (others && *(volatile uint8_t *)(c + AC_LATCH) && any_key_down()) {
        apply_keys(c);
        return;
    }
    forget_keys();
    o_cmd[key](a0, a1, a2, a3, a4, a5, a6, a7);
}

#define CMD_HOOK(fn, key) \
    static void __attribute__((stdcall)) fn(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, \
                                            uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) \
    { on_cmd(key, a0, a1, a2, a3, a4, a5, a6, a7); face_refresh(); }
CMD_HOOK(h_forward,  K_FORWARD)
CMD_HOOK(h_backward, K_BACKWARD)
CMD_HOOK(h_left,     K_LEFT)
CMD_HOOK(h_right,    K_RIGHT)

static const float * __attribute__((cdecl, used)) diag_vector(int back)
{
    uintptr_t c;
    if (!g_diagSide || g_diagFwd != (back ? -1 : 1)) return NULL;
    if (face_offset() != 0.0f) return NULL;
    c = control();
    if (!c || !*(volatile uint8_t *)(c + AC_LATCH) || rd32(c + AC_SHIP)
        || (int32_t)rd32(c + AC_FWD) != g_diagFwd || rd32(c + AC_SIDE))
        return NULL;
    g_vec[0] = (float)g_diagSide * DIAG_UNIT;
    g_vec[1] = (float)g_diagFwd * DIAG_UNIT;
    g_vec[2] = 0.0f;
    g_diagSteps++;
    return g_vec;
}

static const uint8_t EXP_FWD[5]  = { 0x8B, 0x4C, 0x24, 0x18, 0x53 };
static const uint8_t EXP_OTHER[5] = { 0x8B, 0x44, 0x24, 0x18, 0x53 };
static const uint8_t EXP_AHEAD[7] = { 0x6A, 0x00, 0x68, 0x00, 0x00, 0x80, 0x3F };
static const uint8_t EXP_BACK[7]  = { 0x6A, 0x00, 0x68, 0x00, 0x00, 0x80, 0xBF };

static const U9PatchSpec SPEC_CMD[K_COUNT] = {
    { "DIAG_CMD_FORWARD",  U9PATCH_INLINE, U9SYM_CMD_FORWARD,        5, EXP_FWD,   5, U9PF_EXCLUSIVE },
    { "DIAG_CMD_BACKWARD", U9PATCH_INLINE, U9SYM_CMD_BACKWARD,       5, EXP_OTHER, 5, U9PF_EXCLUSIVE },
    { "DIAG_CMD_LEFT",     U9PATCH_INLINE, U9SYM_CMD_SIDESTEP_LEFT,  5, EXP_OTHER, 5, U9PF_EXCLUSIVE },
    { "DIAG_CMD_RIGHT",    U9PATCH_INLINE, U9SYM_CMD_SIDESTEP_RIGHT, 5, EXP_OTHER, 5, U9PF_EXCLUSIVE },
};
static const U9PatchSpec SPEC_AHEAD = {
    "DIAG_STEP_AHEAD", U9PATCH_STUB, U9SYM_STEPDIR_CASE_AHEAD, 7, EXP_AHEAD, 7, U9PF_EXCLUSIVE
};
static const uint8_t EXP_TURN[6] = { 0xFF, 0x90, 0x90, 0x00, 0x00, 0x00 };
static const U9PatchSpec SPEC_TURN = {
    "DIAG_FACE_TURN", U9PATCH_STUB, U9SYM_INPUT_AVATAR_TURN, 6, EXP_TURN, 6, U9PF_EXCLUSIVE
};
static const U9PatchSpec SPEC_BACK = {
    "DIAG_STEP_BACK", U9PATCH_STUB, U9SYM_STEPDIR_CASE_BACK, 7, EXP_BACK, 7, U9PF_EXCLUSIVE
};

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void rel32(uint8_t *at, uintptr_t target) { put32(at, (uint32_t)(target - ((uintptr_t)at + 4))); }

static unsigned build_stub(uint8_t *st, int back, uintptr_t join)
{
    unsigned n = 0, jne, jz, jmp, stock, tail;
    st[n++] = 0x81; st[n++] = 0x3E; put32(st + n, VTBL_AVATAR_MOVER); n += 4;
    st[n++] = 0x0F; st[n++] = 0x85; jne = n; n += 4;
    st[n++] = 0x6A; st[n++] = (uint8_t)back;
    st[n++] = 0xE8; rel32(st + n, (uintptr_t)&diag_vector); n += 4;
    st[n++] = 0x83; st[n++] = 0xC4; st[n++] = 0x04;
    st[n++] = 0x85; st[n++] = 0xC0;
    st[n++] = 0x0F; st[n++] = 0x84; jz = n; n += 4;
    st[n++] = 0xFF; st[n++] = 0x70; st[n++] = 0x08;
    st[n++] = 0xFF; st[n++] = 0x70; st[n++] = 0x04;
    st[n++] = 0xFF; st[n++] = 0x30;
    st[n++] = 0xEB; jmp = n; n += 1;
    stock = n;
    memcpy(st + n, back ? EXP_BACK : EXP_AHEAD, 7); n += 7;
    st[n++] = 0x6A; st[n++] = 0x00;
    tail = n;
    st[n++] = 0x8D; st[n++] = 0x4D; st[n++] = back ? 0x98 : 0x8C;
    st[n++] = 0xE9; rel32(st + n, join); n += 4;
    rel32(st + jne, (uintptr_t)(st + stock));
    rel32(st + jz,  (uintptr_t)(st + stock));
    st[jmp] = (uint8_t)(tail - (jmp + 1));
    return n;
}

static int jump_to(const U9PatchSpec *spec, const uint8_t *stub)
{
    uintptr_t site = u9sym(spec->target);
    uint8_t j[7];
    if (!site) return 0;
    j[0] = 0xE9;
    put32(j + 1, (uint32_t)((uintptr_t)stub - (site + 5)));
    j[5] = 0x90; j[6] = 0x90;
    return u9patch_install_bytes(spec, j, 7);
}

void u9diagmove_install(void)
{
    static void *const hooks[K_COUNT] = { (void *)h_forward, (void *)h_backward, (void *)h_left, (void *)h_right };
    uintptr_t join = u9sym(U9SYM_STEPDIR_UNIT_BUILD);
    uint8_t *st;
    int i;

    if (!join) return;
    st = (uint8_t *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("DIAGONAL MOVE: no memory for the stubs - not installed"); return; }
    build_stub(st, 0, join);
    build_stub(st + 64, 1, join);
    FlushInstructionCache(GetCurrentProcess(), st, 128);

    if (!jump_to(&SPEC_AHEAD, st) || !jump_to(&SPEC_BACK, st + 64)) {
        u9log_line("DIAGONAL MOVE: REFUSED at the step (see the manifest) - one direction at a time, as stock");
        return;
    }
    for (i = 0; i < K_COUNT; i++) {
        o_cmd[i] = (CmdFn)u9patch_install(&SPEC_CMD[i], hooks[i]);
        if (!o_cmd[i]) {
            u9log_line("DIAGONAL MOVE: REFUSED at %s (see the manifest) - keys behave as stock", SPEC_CMD[i].name);
            return;
        }
    }
    if (u9cfg_int("diagonal_face", 1)) {
        uintptr_t site = u9sym(U9SYM_INPUT_AVATAR_TURN);
        uint8_t call[6];
        if (site) {
            call[0] = 0xE8;
            put32(call + 1, (uint32_t)((uintptr_t)&h_turn_to - (site + 5)));
            call[5] = 0x90;
            g_face = u9patch_install_bytes(&SPEC_TURN, call, 6);
        }
        if (!g_face)
            u9log_line("DIAGONAL MOVE: facing REFUSED (see the manifest) - diagonals walk straight-faced");
    }
    u9log_line("DIAGONAL MOVE: installed - forward or back plus a side key walks diagonally%s;"
               " releasing one key leaves the other in force (stubs %08X)",
               g_face ? ", the Avatar turned to face it (backpedalling for back diagonals), the camera not"
                      : " without turning", (unsigned)(uintptr_t)st);
}

void u9diagmove_report(void)
{
    if (g_diagStarts)
        u9log_line("DIAGONAL MOVE: %u diagonals started, %u straight-faced diagonal steps, %u turns to face one",
                   g_diagStarts, g_diagSteps, g_faceTurns);
}
