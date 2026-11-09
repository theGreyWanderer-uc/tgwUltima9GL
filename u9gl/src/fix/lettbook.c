#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "fixmem.h"
#include "gold.h"
#include "lettbook.h"

void u9log_line(const char *fmt, ...);

#define LETT_TOPIC   188u
#define OFF_DEX      0x2Cu
#define OFF_2H       0x12Cu
#define PENDING_MS   (30u * 60u * 1000u)
#define LINE_TIME    6

static const char LINE_LEARNED[] = "You have learned the teachings of William Lett.";
static const char LINE_NOT[]     = "You do not comprehend the teachings of William Lett.";

typedef void (U9CALL *Raise2HFn)(void *unused);
typedef void (U9CALL *BookCloseFn)(void *book);
typedef int (__attribute__((fastcall)) *ObjectSayFn)(void *obj, int zero, const char *text, int a, int b);

static BookCloseFn  g_origClose;
static const char  *g_pending;
static DWORD        g_pendingAt;
static unsigned     g_reads, g_bagReads, g_raised, g_shown;

static void __cdecl lett_read(uintptr_t obj)
{
    uintptr_t rec = readable4(ACTOR_RECORDS) ? rd32(ACTOR_RECORDS) : 0;
    uintptr_t raise = u9sym(U9SYM_SKILL_RAISE_2H);
    int inWorld, dex, two, teach;

    g_reads++;
    if (!rec || !readable4(rec + OFF_2H)) return;
    inWorld = obj && readable4(obj + 0x14) && (*(const volatile uint8_t *)(obj + 0x14) & 0x10u);
    dex = (int)rd32(rec + OFF_DEX);
    if (dex > 3) dex = 3;
    two = (int)rd32(rec + OFF_2H);
    teach = (dex == 3 && two == 2);

    g_pending = two >= 3 ? NULL : teach ? LINE_LEARNED : LINE_NOT;
    g_pendingAt = GetTickCount();

    if (!inWorld) {
        g_bagReads++;
        if (teach && raise) {
            ((Raise2HFn)raise)(NULL);
            g_raised++;
        }
    }
    u9log_line("LETT BOOK: read %s - dexterity %d, two-handed %d -> %s%s", inWorld ? "on the ground" : "from the backpack",
               dex, two, two >= 3 ? "already Master, nothing said" :
               teach ? (inWorld ? "the game teaches it" : "taught here") : "nothing taught",
               g_origClose && g_pending ? "; the line follows when the book closes" : "");
}

static void U9CALL h_book_close(void *book)
{
    const char *line = g_pending;
    uintptr_t say = u9sym(U9SYM_OBJECT_SAY), av;
    g_origClose(book);
    if (!line) return;
    g_pending = NULL;
    if (GetTickCount() - g_pendingAt > PENDING_MS) return;
    av = u9avatar_object();
    if (!av || !say) return;
    ((ObjectSayFn)say)((void *)av, 0, line, LINE_TIME, 1);
    g_shown++;
    u9log_line("LETT BOOK: book closed - \"%s\"", line);
}

static const uint8_t GATE_EXPECT[10] = { 0xF6, 0x41, 0x14, 0x10, 0x0F, 0x84, 0x49, 0x08, 0x00, 0x00 };
static const U9PatchSpec GATE_SPEC = {
    "LETT_BOOK_CONTAINER", U9PATCH_STUB, U9SYM_SPEECH215_WORLD_GATE,
    10, GATE_EXPECT, 10, U9PF_EXCLUSIVE
};
static const uint8_t CLOSE_EXPECT[4] = { 0xF0, 0x31, 0x42, 0x00 };
static const U9PatchSpec CLOSE_SPEC = {
    "LETT_BOOK_CLOSE_LINE", U9PATCH_VTABLE, U9SYM_BOOK_CLOSE_SLOT,
    4, CLOSE_EXPECT, 4, U9PF_EXCLUSIVE
};

static unsigned put_rel32(uint8_t *st, unsigned n, uint8_t op0, int two, uintptr_t target)
{
    unsigned len = two ? 6u : 5u;
    if (two) { st[n] = 0x0F; st[n + 1] = op0; } else st[n] = op0;
    *(int32_t *)(st + n + len - 4) = (int32_t)(target - ((uintptr_t)st + n + len));
    return n + len;
}

void u9lettbook_install(void)
{
    uintptr_t site = u9sym(U9SYM_SPEECH215_WORLD_GATE);
    uintptr_t pass = u9sym(U9SYM_SPEECH215_PASS);
    uintptr_t skip = u9sym(U9SYM_SPEECH215_SKIP);
    uint8_t *st, jmp[10];
    unsigned n = 0, jneAt;

    if (!site || !pass || !skip) return;

    g_origClose = (BookCloseFn)u9patch_install(&CLOSE_SPEC, (void *)h_book_close);
    if (!g_origClose)
        u9log_line("LETT BOOK: the closing line's hook was REFUSED (see the manifest) - the lesson stays silent");

    st = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!st) { u9log_line("LETT BOOK: no memory for the stub - not installed"); return; }

    st[n++] = 0x66; st[n++] = 0x81; st[n++] = 0x7E; st[n++] = 0x04;
    st[n++] = (uint8_t)(LETT_TOPIC & 0xFF); st[n++] = (uint8_t)(LETT_TOPIC >> 8);
    st[n++] = 0x75; jneAt = n++;
    st[n++] = 0x60;
    st[n++] = 0x9C;
    st[n++] = 0x51;
    n = put_rel32(st, n, 0xE8, 0, (uintptr_t)&lett_read);
    st[n++] = 0x83; st[n++] = 0xC4; st[n++] = 0x04;
    st[n++] = 0x9D;
    st[n++] = 0x61;
    st[jneAt] = (uint8_t)(n - (jneAt + 1));
    memcpy(st + n, GATE_EXPECT, 4); n += 4;
    n = put_rel32(st, n, 0x85, 1, pass);
    n = put_rel32(st, n, 0xE9, 0, skip);
    FlushInstructionCache(GetCurrentProcess(), st, 64);

    jmp[0] = 0xE9;
    *(int32_t *)(jmp + 1) = (int32_t)((uintptr_t)st - (site + 5));
    memset(jmp + 5, 0x90, 5);
    if (!u9patch_install_bytes(&GATE_SPEC, jmp, 10)) {
        VirtualFree(st, 0, MEM_RELEASE);
        u9log_line("LETT BOOK: REFUSED (see the manifest) - the book teaches only when read on the ground, as stock");
        return;
    }
    u9log_line("LETT BOOK: installed - Sir William Lett's book teaches from the backpack as on the ground%s"
               " (stub %08X, %u bytes)", g_origClose ? ", and says so when closed" : "", (unsigned)(uintptr_t)st, n);
}

void u9lettbook_report(void)
{
    if (g_reads)
        u9log_line("LETT BOOK: read %u times (%u from the backpack, %u raised there), %u closing lines shown",
                   g_reads, g_bagReads, g_raised, g_shown);
}
