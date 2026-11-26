#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "newgame.h"

#define SAVELOAD_WRITE_ARCHIVE   0x0051D250u
#define G_SAVELOAD               0x00AD126Cu
#define SAVELOAD_DISABLED        0x14
#define G_WRITE_NEWGAME_SNAPSHOT 0x00AD1270u

static const uint8_t WRITE_ARCHIVE_ENTRY[8] = { 0x83, 0xEC, 0x38, 0x53, 0x8B, 0xD9, 0x55, 0x56 };

typedef void (__attribute__((thiscall)) *WriteArchiveFn)(void *saveLoad, int slot, const char *label,
                                                         const char *description);

static int snapshot_path(char *out, size_t cap)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
    char *slash;
    if (n == 0 || n >= sizeof exe) return 0;
    slash = strrchr(exe, '\\');
    if (!slash) return 0;
    slash[1] = 0;
    if (strlen(exe) + sizeof "static\\u9game0.sav" > cap) return 0;
    strcpy(out, exe);
    strcat(out, "static\\u9game0.sav");
    return 1;
}

static int start_dat_path(char *out, size_t cap)
{
    char *slash;
    if (!snapshot_path(out, cap)) return 0;
    slash = strstr(out, "static\\u9game0.sav");
    if (!slash || (size_t)(slash - out) + sizeof "savegame\\start.dat" > cap) return 0;
    strcpy(slash, "savegame\\start.dat");
    return 1;
}

static int read_small(const char *path, uint8_t *buf, DWORD cap, DWORD *len)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, got = 0;
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(h, NULL);
    ok = size != INVALID_FILE_SIZE && size <= cap && ReadFile(h, buf, size, &got, NULL) && got == size;
    CloseHandle(h);
    if (ok) *len = got;
    return ok;
}

static void write_small(const char *path, const uint8_t *buf, DWORD len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, len, &put, NULL);
    CloseHandle(h);
}

static int readable(uintptr_t at, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((const void *)at, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return at + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

int u9newgame_path(char *out, size_t cap) { return snapshot_path(out, cap); }

int u9newgame_write(int replace, char *msg, size_t cap)
{
    char path[MAX_PATH], start[MAX_PATH];
    uint8_t startBytes[64];
    DWORD startLen = 0;
    DWORD attrs;
    int hadStart;
    void *saveLoad;

    if (!snapshot_path(path, sizeof path)) { snprintf(msg, cap, "the game folder could not be found"); return U9NEWGAME_FAILED; }
    attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!replace) { snprintf(msg, cap, "a snapshot already exists (%s)", path); return U9NEWGAME_FAILED; }
        if (!SetFileAttributesA(path, attrs & ~(DWORD)FILE_ATTRIBUTE_READONLY)) {
            snprintf(msg, cap, "%s could not be made writable (error %lu)", path, GetLastError());
            return U9NEWGAME_FAILED;
        }
    }

    if (!readable(SAVELOAD_WRITE_ARCHIVE, sizeof WRITE_ARCHIVE_ENTRY)
        || memcmp((const void *)SAVELOAD_WRITE_ARCHIVE, WRITE_ARCHIVE_ENTRY, sizeof WRITE_ARCHIVE_ENTRY) != 0) {
        snprintf(msg, cap, "SaveLoad_WriteArchive is not the expected code - not build 1.19F? Nothing written");
        return U9NEWGAME_FAILED;
    }
    saveLoad = *(void **)G_SAVELOAD;
    if (!saveLoad) { snprintf(msg, cap, "the save system is not running yet"); return U9NEWGAME_NOT_NOW; }
    if (*(int *)((uint8_t *)saveLoad + SAVELOAD_DISABLED) > 0) {
        snprintf(msg, cap, "the game is not allowing saves right now (a conversation or a scene)");
        return U9NEWGAME_NOT_NOW;
    }

    if (!start_dat_path(start, sizeof start)) start[0] = 0;
    hadStart = start[0] && read_small(start, startBytes, sizeof startBytes, &startLen);

    *(volatile int *)G_WRITE_NEWGAME_SNAPSHOT = 1;
    ((WriteArchiveFn)SAVELOAD_WRITE_ARCHIVE)(saveLoad, 0, NULL, "New game");
    *(volatile int *)G_WRITE_NEWGAME_SNAPSHOT = 0;

    if (start[0]) {
        if (hadStart) write_small(start, startBytes, startLen);
        else DeleteFileA(start);
    }

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        snprintf(msg, cap, "the game did not write %s - the folder may be read-only", path);
        return U9NEWGAME_FAILED;
    }
    SetFileAttributesA(path, attrs | FILE_ATTRIBUTE_READONLY);
    snprintf(msg, cap, "new-game snapshot written: %s (read-only)", path);
    return U9NEWGAME_OK;
}

static int take_i32(const uint8_t *buf, DWORD len, DWORD *at, int32_t *v)
{
    if (len - *at < 4 || *at > len) return 0;
    memcpy(v, buf + *at, 4);
    *at += 4;
    return 1;
}

static int skip_block(const uint8_t *buf, DWORD len, DWORD *at)
{
    int32_t n;
    if (!take_i32(buf, len, at, &n) || n < 0 || (DWORD)n > len - *at) return 0;
    *at += (DWORD)n;
    return 1;
}

int u9newgame_verify(char *msg, size_t cap)
{
    char path[MAX_PATH];
    HANDLE h;
    DWORD size, got = 0, at = 6;
    uint8_t *buf;
    int32_t map;
    int have0 = 0, have14 = 0, maps = 0, ok = 0;
    const char *why = "it is not a whole save archive";

    if (!snapshot_path(path, sizeof path)) { snprintf(msg, cap, "the game folder could not be found"); return 0; }
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { snprintf(msg, cap, "%s cannot be opened (error %lu)", path, GetLastError()); return 0; }
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size < 64 || size > 64u << 20) {
        CloseHandle(h);
        snprintf(msg, cap, "%s has an unlikely size (%lu bytes)", path, (unsigned long)size);
        return 0;
    }
    buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) { CloseHandle(h); snprintf(msg, cap, "no memory to read %s back", path); return 0; }
    if (!ReadFile(h, buf, size, &got, NULL) || got != size) {
        why = "it could not be read back";
    } else if (memcmp(buf, "U9:008", 6) != 0) {
        why = "it does not start as a save archive does";
    } else if (skip_block(buf, size, &at) && skip_block(buf, size, &at) && size - at >= 52
               && (at += 52, skip_block(buf, size, &at)) && skip_block(buf, size, &at) && skip_block(buf, size, &at)) {
        for (;;) {
            if (!take_i32(buf, size, &at, &map)) break;
            if (map == -1) { ok = at == size; if (!ok) why = "there are bytes after its last map"; break; }
            if (map < 0 || map > 255 || !skip_block(buf, size, &at)) break;
            maps++;
            if (map == 0) have0 = 1;
            if (map == 14) have14 = 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(h);

    if (ok && (!have0 || !have14)) { ok = 0; why = have0 ? "it holds no map 14, where the game starts" : "it holds no map 0"; }
    if (ok) snprintf(msg, cap, "%s is a whole save archive of %lu bytes with %d maps, 0 and 14 among them",
                     path, (unsigned long)size, maps);
    else snprintf(msg, cap, "%s was written, but %s", path, why);
    return ok;
}

void u9newgame_command(const char *arg, void (*out)(const char *fmt, ...))
{
    char path[MAX_PATH], msg[400];
    int result;

    while (arg && *arg == ' ') arg++;
    if (!snapshot_path(path, sizeof path)) { out("newgame: the game folder could not be found"); return; }

    if (!arg || !*arg) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            out("no new-game snapshot: the journal's New Game takes the game's fallback (I-32). "
                "'newgame snapshot' right after the opening writes one");
        else
            out("new-game snapshot present: %s - the journal's New Game loads it", path);
        return;
    }
    if (strncmp(arg, "snapshot", 8) != 0) { out("usage: newgame | newgame snapshot [replace]"); return; }

    result = u9newgame_write(strstr(arg + 8, "replace") != NULL, msg, sizeof msg);
    if (result != U9NEWGAME_OK) {
        if (strncmp(msg, "a snapshot already exists", 25) == 0) out("%s; 'newgame snapshot replace' writes over it", msg);
        else out("newgame: %s%s", msg, result == U9NEWGAME_NOT_NOW ? "; try again in a moment" : "");
        return;
    }
    out("%s. The journal's New Game now loads it", msg);
    if (!u9newgame_verify(msg, sizeof msg)) out("newgame: %s", msg);
}
