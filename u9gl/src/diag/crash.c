#include <windows.h>
#include "u9gl_version.h"
#include <stdint.h>
#include <stdarg.h>
#include "log.h"
#include "diag.h"

static PVOID  g_veh;
static int    g_reported;
static HANDLE g_file = INVALID_HANDLE_VALUE;

void u9diag_line(const char *fmt, ...);

static void diag_open(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    SYSTEMTIME t;

    if (g_file != INVALID_HANDLE_VALUE) return;
    if (!GetTempPathA(sizeof(dir), dir)) return;
    lstrcatA(dir, "u9gl_diag");
    CreateDirectoryA(dir, NULL);

    GetLocalTime(&t);
    wsprintfA(path, "%s%cu9diag_%04u%02u%02u_%02u%02u%02u_pid%lu.txt",
              dir, 92, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
              GetCurrentProcessId());

    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE) {
        u9log_line("CRASH could not open a diagnostic file in %s", dir);
        return;
    }
    u9diag_line("u9 diagnostic log");
    u9diag_line("started   %04u-%02u-%02u %02u:%02u:%02u",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    u9diag_line("process   %lu", GetCurrentProcessId());
    u9diag_line("version   %s", U9GL_VERSION_FULL);
    u9diag_line("image     0x%08X", (unsigned)(uintptr_t)GetModuleHandleA(NULL));
    u9diag_line("");
    u9diag_line("First-chance exceptions are recorded whether or not the game");
    u9diag_line("survives them. A report followed by later frames was HANDLED.");
    u9diag_line("");

    u9log_line("CRASH diagnostics -> %s", path);
}

void u9diag_line(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    int n;
    DWORD wrote;

    if (g_file == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    n = wvsprintfA(line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    line[n++] = 13;
    line[n++] = 10;
    WriteFile(g_file, line, (DWORD)n, &wrote, NULL);
    FlushFileBuffers(g_file);
}

#define U9CRASH_CODES 24
static struct { DWORD code; unsigned n; } g_codes[U9CRASH_CODES];
static unsigned g_codeCount;
static unsigned g_totalExceptions;

static void tally(DWORD code)
{
    unsigned i;
    g_totalExceptions++;
    for (i = 0; i < g_codeCount; i++)
        if (g_codes[i].code == code) { g_codes[i].n++; return; }
    if (g_codeCount < U9CRASH_CODES) {
        g_codes[g_codeCount].code = code;
        g_codes[g_codeCount].n = 1;
        g_codeCount++;
    }
}

int u9diag_read(const void *addr, void *out, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!addr) return 0;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if ((const char *)addr + n > (const char *)mbi.BaseAddress + mbi.RegionSize)
        return 0;
    memcpy(out, addr, n);
    return 1;
}



static uint32_t type_id_of(uintptr_t obj)
{
    uint16_t id;
    if (!u9diag_vtable(obj)) return 0xFFFFFFFFu;
    if (!u9diag_read((const void *)(obj + 0x0A), &id, 2)) return 0xFFFFFFFFu;
    return id;
}

uint32_t u9diag_u32(uintptr_t addr)
{
    uint32_t v = 0;
    return u9diag_read((const void *)addr, &v, 4) ? v : 0;
}

static void describe(uintptr_t addr, char *out, unsigned cap)
{
    MEMORY_BASIC_INFORMATION mbi;
    char path[MAX_PATH];
    const char *base = "?";
    const char *q;

    out[0] = 0;
    if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
        wsprintfA(out, "0x%08X (unmapped)", (unsigned)addr);
        return;
    }
    if (GetModuleFileNameA((HMODULE)mbi.AllocationBase, path, sizeof(path))) {
        base = path;
        for (q = path; *q; q++) if (*q == 92 || *q == '/') base = q + 1;
        wsprintfA(out, "0x%08X = %s+0x%X", (unsigned)addr, base,
                  (unsigned)(addr - (uintptr_t)mbi.AllocationBase));
    } else {
        wsprintfA(out, "0x%08X (%s)", (unsigned)addr,
                  mbi.State == MEM_COMMIT ? "committed, no module" : "not committed");
    }
    (void)cap;
}


uint32_t u9diag_vtable(uintptr_t addr)
{
    uint32_t vptr, slot0;
    MEMORY_BASIC_INFORMATION mbi;
    if (addr < 0x10000u) return 0;
    if (!u9diag_read((const void *)addr, &vptr, 4)) return 0;
    if (vptr < 0x00400000u || vptr > 0x00800000u) return 0;
    if (!u9diag_read((const void *)(uintptr_t)vptr, &slot0, 4)) return 0;
    if (!VirtualQuery((LPCVOID)(uintptr_t)slot0, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    return vptr;
}

static int printable_run(const unsigned char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        if (b[i] == 0) return (int)i;
        if (b[i] < 32 || b[i] > 126) return -1;
    }
    return (int)n;
}

static int string_at(uintptr_t addr, char *out, unsigned cap)
{
    unsigned i;
    unsigned char c;
    if (addr < 0x10000u) return 0;
    for (i = 0; i + 1 < cap; i++) {
        if (!u9diag_read((const void *)(addr + i), &c, 1)) break;
        if (c == 0) break;
        if (c < 32 || c > 126) return 0;
        out[i] = (char)c;
    }
    out[i] = 0;
    return i >= 3;
}

void u9diag_dump_object(const char *label, uintptr_t addr, unsigned dwords)
{
    unsigned i;
    uint32_t vt = u9diag_vtable(addr);
    uint32_t tid = type_id_of(addr);
    u9diag_line("%s 0x%08X  vtable=0x%08X  its own +0x0A = %u (a type id only if"
              " this class carries one)", label, (unsigned)addr, vt,
              tid == 0xFFFFFFFFu ? 0u : tid);
    for (i = 0; i < dwords; i++) {
        uint32_t v;
        char text[48];
        char asc[8];
        int n;
        if (!u9diag_read((const void *)(addr + i * 4), &v, 4)) {
            u9diag_line("  +0x%03X  <unreadable>", i * 4);
            continue;
        }
        asc[0] = 0;
        n = printable_run((const unsigned char *)&v, 4);
        if (n >= 2) {
            memcpy(asc, &v, (unsigned)n);
            asc[n] = 0;
        }
        if (v && string_at(v, text, sizeof(text))) {
            u9diag_line("  +0x%03X  %08X  -> \"%s\"", i * 4, v, text);
        } else if (asc[0]) {
            u9diag_line("  +0x%03X  %08X  bytes are text: '%s'", i * 4, v, asc);
        } else if (v == 0) {
            u9diag_line("  +0x%03X  %08X", i * 4, v);
        } else if (v < 0x10000u) {
            u9diag_line("  +0x%03X  %08X  small int %u", i * 4, v, v);
        } else if (u9diag_vtable(v)) {
            uint32_t tid = type_id_of(v);
            if (tid != 0xFFFFFFFFu)
                u9diag_line("  +0x%03X  %08X  -> OBJECT vtable=%08X  type id %u",
                          i * 4, v, u9diag_vtable(v), tid);
            else
                u9diag_line("  +0x%03X  %08X  -> OBJECT vtable=%08X", i * 4, v,
                          u9diag_vtable(v));
        } else if (u9diag_u32(v)) {
            u9diag_line("  +0x%03X  %08X  -> readable", i * 4, v);
        } else {
            u9diag_line("  +0x%03X  %08X  not readable", i * 4, v);
        }
    }
}

static const char *code_name(DWORD c)
{
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
    case 0xC0000374u:                     return "HEAP_CORRUPTION";
    case 0xC0000008u:                     return "INVALID_HANDLE";
    default:                              return NULL;
    }
}

#define F9_SITE_FROM 0x004C7880u
#define F9_SITE_TO   0x004C8CFCu
#define F9_HANDLES   0x008515D0u

static int f9_arena(uint32_t obj, uint32_t mgr)
{
    uint32_t b1 = u9diag_u32(mgr + 0x10), r1 = u9diag_u32(mgr + 0x18);
    uint32_t b2 = u9diag_u32(mgr + 0x34), r2 = u9diag_u32(mgr + 0x3C);
    if (b1 && obj >= b1 && obj - b1 < r1) return 1;
    if (b2 && obj >= b2 && obj - b2 < r2) return 2;
    return 0;
}

static void f9_row(const char *mark, uint32_t at, unsigned dwords)
{
    char hex[16 * 9 + 1];
    unsigned i, n = 0;
    for (i = 0; i < dwords && i < 16; i++) {
        uint32_t v;
        if (u9diag_read((const void *)(uintptr_t)(at + i * 4), &v, 4))
            n += wsprintfA(hex + n, "%08X ", (unsigned)v);
        else
            n += wsprintfA(hex + n, "-------- ");
    }
    hex[n] = 0;
    u9diag_line("    %s%08X  %s", mark, (unsigned)at, hex);
}

static void f9_report(const CONTEXT *cx)
{
    static const char *const arena[3] = { "neither", "+0x10", "+0x34" };
    uint32_t mgr  = u9diag_u32((uintptr_t)cx->Ebp - 0x1D8u);
    uint32_t obj  = (uint32_t)cx->Edi;
    uint32_t page = obj & ~0xFFFu;
    uint32_t pos  = u9diag_u32((uintptr_t)cx->Ebp + 8);
    uint32_t flags, holder, handle, table, entry;
    uint16_t type = 0;
    int in, named;
    int r;

    if (!mgr) mgr = (uint32_t)cx->Edx;
    u9diag_line("  F-9 site: WorldObj_Reinsert on manager %08X, moving the record in EDI",
               (unsigned)mgr);
    u9diag_line("    arenas: +0x10 base %08X reserved %08X | +0x34 base %08X reserved %08X"
               " | list head +0x04 = %08X",
               (unsigned)u9diag_u32(mgr + 0x10), (unsigned)u9diag_u32(mgr + 0x18),
               (unsigned)u9diag_u32(mgr + 0x34), (unsigned)u9diag_u32(mgr + 0x3C),
               (unsigned)u9diag_u32(mgr + 0x04));
    if (!u9diag_u32(obj) && !u9diag_u32(obj + 0x14)) {
        u9diag_line("    record %08X is unreadable or empty", (unsigned)obj);
    } else {
        flags = u9diag_u32(obj + 0x14);
        u9diag_read((const void *)(uintptr_t)(obj + 0x0A), &type, 2);
        in    = f9_arena(obj, mgr);
        named = (flags & 1) ? 1 : 2;
        u9diag_line("    record %08X is in arena %s; +0x14 = %08X, bit 0 %s, which names"
                   " arena %s%s", (unsigned)obj, arena[in], (unsigned)flags,
                   (flags & 1) ? "set" : "clear", arena[named],
                   in && in != named ? "  <-- THEY DISAGREE" : "");
        u9diag_line("    type id (+0x0A) %u, fast reference (+0x14 >> 21) %u",
                   (unsigned)type, (unsigned)(flags >> 21));
        if (obj - page >= 0x60u)
            u9diag_line("    page %08X offset 0x%03X: 24-byte slot %u rem %u,"
                       " 32-byte cell %u rem %u", (unsigned)page, (unsigned)(obj - page),
                       (unsigned)((obj - page - 0x60u) / 24u),
                       (unsigned)((obj - page - 0x60u) % 24u),
                       (unsigned)((obj - page - 0x60u) / 32u),
                       (unsigned)((obj - page - 0x60u) % 32u));
        u9diag_line("    page header, 0x60 bytes:");
        for (r = 0; r < 3; r++) f9_row("  ", page + (uint32_t)r * 32u, 8);
        u9diag_line("    records around it, 24 bytes each (> marks the one moved):");
        for (r = -3; r <= 3; r++) {
            uint32_t at = obj + (uint32_t)(r * 24);
            if (at < page + 0x60u || at + 24u > page + 0x1000u) continue;
            f9_row(r ? "  " : "> ", at, 6);
        }
    }
    if (pos)
        u9diag_line("    target position [EBP+8] -> %08X: %d, %d, %d", (unsigned)pos,
                   (int)u9diag_u32(pos), (int)u9diag_u32(pos + 4), (int)u9diag_u32(pos + 8));

    holder = u9diag_u32((uintptr_t)cx->Ebp);
    handle = u9diag_u32(holder);
    table  = u9diag_u32(F9_HANDLES);
    entry  = table + handle * 12u;
    u9diag_line("    mover: WorldObj_MoveTo `this` %08X holds handle %u; table entry"
               " %08X = { %08X, kind %u, object %08X }%s", (unsigned)holder,
               (unsigned)handle, (unsigned)entry, (unsigned)u9diag_u32(entry),
               (unsigned)u9diag_u32(entry + 4), (unsigned)u9diag_u32(entry + 8),
               u9diag_u32(entry + 8) == obj ? " (the record above)" : "");
    u9diag_line("    around the handle (it may sit inside a larger object):");
    for (r = -2; r < 2; r++) f9_row(r ? "  " : "> ", holder + (uint32_t)(r * 32), 8);
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er;
    const CONTEXT *cx;
    const char *name;
    char where[MAX_PATH + 64];
    unsigned i;

    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    er = ep->ExceptionRecord;
    cx = ep->ContextRecord;

    if (er->ExceptionCode == (DWORD)0x80000004u &&
        u9hw_handle(ep->ContextRecord))
        return EXCEPTION_CONTINUE_EXECUTION;

    tally(er->ExceptionCode);
    name = code_name(er->ExceptionCode);
    if (!name) return EXCEPTION_CONTINUE_SEARCH;
    if (g_reported >= 4) return EXCEPTION_CONTINUE_SEARCH;
    g_reported++;

    u9diag_line("");
    u9diag_line("========== FAULT: %s (0x%08X) ==========",
               name, (unsigned)er->ExceptionCode);

    describe((uintptr_t)er->ExceptionAddress, where, sizeof(where));
    u9diag_line("at %s", where);

    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        er->NumberParameters >= 2) {
        const char *how = er->ExceptionInformation[0] == 0 ? "reading"
                        : er->ExceptionInformation[0] == 1 ? "writing"
                        : "executing";
        describe((uintptr_t)er->ExceptionInformation[1], where, sizeof(where));
        u9diag_line("  while %s %s", how, where);
    }

    u9diag_line("  EAX=%08X EBX=%08X ECX=%08X EDX=%08X",
               (unsigned)cx->Eax, (unsigned)cx->Ebx,
               (unsigned)cx->Ecx, (unsigned)cx->Edx);
    u9diag_line("  ESI=%08X EDI=%08X EBP=%08X ESP=%08X EFL=%08X",
               (unsigned)cx->Esi, (unsigned)cx->Edi, (unsigned)cx->Ebp,
               (unsigned)cx->Esp, (unsigned)cx->EFlags);

    {
        unsigned char op[16];
        char hex[64]; int n = 0;
        if (u9diag_read(er->ExceptionAddress, op, sizeof(op))) {
            for (i = 0; i < sizeof(op); i++)
                n += wsprintfA(hex + n, "%02X ", op[i]);
            u9diag_line("  bytes at EIP: %s", hex);
        }
    }

    {
        uintptr_t sp = (uintptr_t)cx->Esp;
        unsigned found = 0;
        u9diag_line("  stack, code addresses only:");
        for (i = 0; i < 256 && found < 32; i++) {
            uint32_t v = u9diag_u32(sp + i * 4);
            MEMORY_BASIC_INFORMATION mbi;
            if (!v) continue;
            if (!VirtualQuery((LPCVOID)(uintptr_t)v, &mbi, sizeof(mbi))) continue;
            if (mbi.State != MEM_COMMIT) continue;
            if (!(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                continue;
            describe(v, where, sizeof(where));
            u9diag_line("    [esp+%03X] %s", i * 4, where);
            found++;
        }
    }

    {
        struct { const char *name; uint32_t val; } regs[7] = {
            { "ECX (this?)", (uint32_t)cx->Ecx }, { "ESI", (uint32_t)cx->Esi },
            { "EDI", (uint32_t)cx->Edi },         { "EAX", (uint32_t)cx->Eax },
            { "EBX", (uint32_t)cx->Ebx },         { "EDX", (uint32_t)cx->Edx },
            { "EBP", (uint32_t)cx->Ebp },
        };
        unsigned shown = 0;
        for (i = 0; i < 7 && shown < 3; i++) {
            if (!u9diag_vtable(regs[i].val)) continue;
            u9diag_dump_object(regs[i].name, regs[i].val, 48);
            shown++;
        }
        if (!shown)
            u9diag_line("  no register points at anything with a vtable");

        if ((uintptr_t)er->ExceptionAddress >= 0x004DD0F0u &&
            (uintptr_t)er->ExceptionAddress <= 0x004DD2EAu) {
            uintptr_t self = (uintptr_t)cx->Esi;
            u9diag_line("  F-3 site: actor +0x284 current / +0x29C previous");
            for (i = 0; i < 6; i++) {
                uint32_t cur  = u9diag_u32(self + 0x284 + i * 4);
                uint32_t prev = u9diag_u32(self + 0x29C + i * 4);
                u9diag_line("    [%u] current=%08X (%s)  previous=%08X",
                           i, cur,
                           cur == 0 ? "empty"
                           : cur < 0x10000u ? "SMALL INT - not a pointer"
                           : u9diag_vtable(cur) ? "object"
                           : u9diag_u32(cur) ? "readable, no vtable" : "unreadable",
                           prev);
            }
        }

        if ((uintptr_t)er->ExceptionAddress >= F9_SITE_FROM &&
            (uintptr_t)er->ExceptionAddress < F9_SITE_TO)
            f9_report(cx);
    }

    u9diag_line("========== end of fault report ==========");
    return EXCEPTION_CONTINUE_SEARCH;
}

void u9crash_report_tally(void)
{
    unsigned i;

    if (!g_totalExceptions) {
        u9diag_line("frame %u: alive, no exceptions seen",
                  (unsigned)u9log_frame());
        return;
    }
    u9diag_line("");
    u9diag_line("frame %u: %u exceptions seen first-chance across %u codes",
              (unsigned)u9log_frame(), g_totalExceptions, g_codeCount);
    for (i = 0; i < g_codeCount; i++) {
        const char *n = code_name(g_codes[i].code);
        u9diag_line("  0x%08X  x%-6u %s", (unsigned)g_codes[i].code,
                   g_codes[i].n,
                   n ? n : (g_codes[i].code == 0xE06D7363u ? "C++ throw"
                          : g_codes[i].code == 0x40010006u ? "OutputDebugString"
                          : "(not fatal, or unknown)"));
    }
}

void u9crash_install(void)
{
    if (g_veh) return;
    diag_open();
    g_veh = AddVectoredExceptionHandler(1, on_exception);
    u9diag_line("handler installed (%s)", g_veh ? "ok" : "FAILED");
    u9log_line("CRASH handler installed (%s)", g_veh ? "ok" : "FAILED");
}

void u9crash_remove(void)
{
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = NULL; }
    if (g_file != INVALID_HANDLE_VALUE) {
        u9crash_report_tally();
        u9diag_line("");
        u9diag_line("clean detach");
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}
