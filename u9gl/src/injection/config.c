#include <windows.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "../diag/log.h"

#define U9CFG_MAX 1024
#define U9CFG_KEY 32
#define U9CFG_VAL 512

static struct { char key[U9CFG_KEY]; char val[U9CFG_VAL]; unsigned hash; } g_cfg[U9CFG_MAX];
static unsigned g_count;
static unsigned g_dropped;
static char     g_firstDropped[U9CFG_KEY];
static unsigned g_cut;
static char     g_firstCut[U9CFG_KEY];
static int      g_loaded;

static void trim(char *s)
{
    char *a = s, *b;
    while (*a == ' ' || *a == '\t') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    b = s + strlen(s);
    while (b > s && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n'))
        *--b = 0;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s += 32;
}

static int ascii_equal_nocase(const char *a, const char *b)
{
    for (;; a++, b++) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

static unsigned key_hash(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        h = (h ^ c) * 16777619u;
    }
    return h;
}

static const struct { const char *old, *now; } k_renamed[] = {
    { "fix_tele_watch",          "diag_tele_watch" },
    { "fix_path_watch",          "diag_path_watch" },
    { "fix_path_walker",         "diag_path_walker" },
    { "fix_bag_watch",           "diag_bag_watch" },
    { "fix_npc_watch",           "diag_npc_watch" },
    { "fix_npc_watch_ms",        "diag_npc_watch_ms" },
    { "fix_travel_watch",        "diag_travel_watch" },
    { "fix_mesh_watch",          "diag_mesh_watch" },
    { "fix_mesh_watch_ms",       "diag_mesh_watch_ms" },
    { "fix_mesh_watch_hw",       "diag_mesh_watch_hw" },
    { "fix_mesh_watch_hw_hits",  "diag_mesh_watch_hw_hits" },
    { "fix_mesh_stat",           "diag_mesh_stat" },
    { "fix_mesh_stat_ms",        "diag_mesh_stat_ms" },
    { "fix_mesh_stat_streak",    "diag_mesh_stat_streak" },
    { "fix_mesh_stat_held_s",    "diag_mesh_stat_held_s" },
    { "fix_furniture_watch",     "diag_furniture_watch" },
    { "fix_furniture_watch_ms",  "diag_furniture_watch_ms" },
    { "fix_census",              "diag_census" },
    { "fix_dump_entity",         "diag_dump_entity" },
    { "fix_fastarea",            "diag_fastarea" },
    { "fix_probe_index",         "diag_probe_index" },
    { "fix_probe_deep",          "diag_probe_deep" },
};

static const char *current_name(const char *key)
{
    unsigned i;
    for (i = 0; i < sizeof k_renamed / sizeof k_renamed[0]; i++)
        if (ascii_equal_nocase(key, k_renamed[i].old)) {
            u9log_line("CFG  '%s' is now '%s' - read as that", k_renamed[i].old, k_renamed[i].now);
            return k_renamed[i].now;
        }
    return key;
}

#define U9CFG_MEMO 512
static struct { const char *ptr; int def, val; unsigned gen; char key[U9CFG_KEY]; } g_memo[U9CFG_MEMO];
static unsigned g_memoGen = 1;
static DWORD    g_memoTid;
unsigned        g_cfgLookups, g_cfgRemembered;

static int put(const char *key, const char *val)
{
    unsigned i, h;

    g_memoGen++;

    key = current_name(key);
    if (strlen(val) >= U9CFG_VAL && !g_cut++) lstrcpynA(g_firstCut, key, U9CFG_KEY);
    h = key_hash(key);
    for (i = 0; i < g_count; i++)
        if (g_cfg[i].hash == h && ascii_equal_nocase(g_cfg[i].key, key)) {
            lstrcpynA(g_cfg[i].val, val, U9CFG_VAL);
            return 1;
        }
    if (g_count >= U9CFG_MAX) {
        if (!g_dropped++) lstrcpynA(g_firstDropped, key, U9CFG_KEY);
        return 0;
    }
    lstrcpynA(g_cfg[g_count].key, key, U9CFG_KEY);
    lstrcpynA(g_cfg[g_count].val, val, U9CFG_VAL);
    g_cfg[g_count].hash = key_hash(g_cfg[g_count].key);
    g_count++;
    return 0;
}

#define U9CFG_FILE_MAX (1u << 20)

static void read_ini(const char *path, int optional)
{
    char   line[1024];
    HANDLE f;
    char  *buf;
    DWORD  size, got;
    DWORD  i, n;
    unsigned before = g_count, longLines = 0, replaced = 0;
    int    overflow = 0;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        if (!optional) u9log_line("CFG  no %s - defaults, environment still applies", path);
        return;
    }
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > U9CFG_FILE_MAX) {
        u9log_line("CFG  WARNING %s is over %u KB and was NOT read - defaults, environment still applies",
                   path, U9CFG_FILE_MAX >> 10);
        CloseHandle(f);
        return;
    }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(f); return; }
    if (!ReadFile(f, buf, size, &got, NULL)) got = 0;
    buf[got] = 0;
    CloseHandle(f);

    n = 0;
    for (i = 0; i <= got; i++) {
        if (buf[i] != '\n' && buf[i] != 0) {
            if (n < sizeof(line) - 1) line[n++] = buf[i];
            else if (!overflow) { overflow = 1; longLines++; }
            continue;
        }
        line[n] = 0;
        n = 0;
        overflow = 0;
        trim(line);
        if (line[0] && line[0] != '#' && line[0] != ';') {
            char *eq = strchr(line, '=');
            if (eq) {
                char key[U9CFG_KEY];
                *eq = 0;
                trim(line);
                lower(line);
                lstrcpynA(key, line, U9CFG_KEY);
                trim(eq + 1);
                replaced += put(key, eq + 1);
            }
        }
        if (buf[i] == 0) break;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    u9log_line("CFG  loaded %s (%u settings, %u new, %u replaced an earlier value)",
               path, g_count, g_count - before, replaced);
    if (longLines)
        u9log_line("CFG  WARNING %u line(s) in %s are over %u characters; each was cut there",
                   longLines, path, (unsigned)sizeof(line) - 1);
    if (g_cut)
        u9log_line("CFG  WARNING %u value(s) so far are over %u characters and were cut there - first %s",
                   g_cut, (unsigned)U9CFG_VAL - 1, g_firstCut);
}

static unsigned merge_environment(void)
{
    char *block = GetEnvironmentStringsA(), *p;
    unsigned merged = 0;

    if (!block) return 0;
    for (p = block; *p; p += strlen(p) + 1) {
        char key[U9CFG_KEY];
        const char *name = p + 5, *eq;
        unsigned i;

        if (!(p[0] == 'U' || p[0] == 'u') || !(p[1] == '9') ||
            !(p[2] == 'G' || p[2] == 'g') || !(p[3] == 'L' || p[3] == 'l') || p[4] != '_')
            continue;
        eq = strchr(name, '=');
        if (!eq || eq == name || !eq[1]) continue;
        for (i = 0; i < U9CFG_KEY - 1 && name + i < eq; i++) key[i] = name[i];
        key[i] = 0;
        lower(key);
        put(key, eq + 1);
        merged++;
    }
    FreeEnvironmentStringsA(block);
    return merged;
}

void u9cfg_load(HINSTANCE self)
{
    char     path[MAX_PATH];
    unsigned fromEnv;

    if (g_loaded) return;

    if (GetModuleFileNameA(self, path, sizeof(path))) {
        char *slash = strrchr(path, '\\');
        if (slash) {
            int room = (int)(sizeof(path) - (slash + 1 - path));
            lstrcpynA(slash + 1, "u9gl.ini", room);
            read_ini(path, 0);
            lstrcpynA(slash + 1, "u9gl.diag.ini", room);
            read_ini(path, 1);
        }
    }
    fromEnv = merge_environment();
    if (fromEnv)
        u9log_line("CFG  %u setting(s) from U9GL_ environment variables, which win over the file", fromEnv);
    if (g_dropped)
        u9log_line("CFG  WARNING table full at %u: %u more settings were NOT read, the first '%s' - they run at their defaults",
                   (unsigned)U9CFG_MAX, g_dropped, g_firstDropped);
    g_loaded = 1;
    g_memoTid = GetCurrentThreadId();
}

static int parse(const char *v, int def)
{
    if (!v || !*v) return def;
    if ((v[0] >= '0' && v[0] <= '9') || v[0] == '-' || v[0] == '+') {
        if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
            return (int)strtol(v + 2, NULL, 16);
        return (int)strtol(v, NULL, 10);
    }
    if (ascii_equal_nocase(v, "on")  || ascii_equal_nocase(v, "true") ||
        ascii_equal_nocase(v, "yes")) return 1;
    if (ascii_equal_nocase(v, "off") || ascii_equal_nocase(v, "false") ||
        ascii_equal_nocase(v, "no"))  return 0;
    return (int)strtol(v, NULL, 10);
}

int u9cfg_int(const char *key, int def)
{
    unsigned i, h;

    if (!g_loaded) {
        char env[64], val[U9CFG_VAL];
        lstrcpyA(env, "U9GL_");
        lstrcpynA(env + 5, key, (int)(sizeof(env) - 5));
        for (i = 5; env[i]; i++)
            if (env[i] >= 'a' && env[i] <= 'z') env[i] -= 32;
        if (GetEnvironmentVariableA(env, val, sizeof(val)) > 0)
            return parse(val, def);
        return def;
    }

    if (g_memoTid && GetCurrentThreadId() == g_memoTid) {
        unsigned slot = (unsigned)(((uintptr_t)key >> 2) ^ ((uintptr_t)key >> 11)) & (U9CFG_MEMO - 1);
        int v = def, found = 0;
        g_cfgLookups++;
        if (g_memo[slot].gen == g_memoGen && g_memo[slot].ptr == key && g_memo[slot].def == def &&
            !strcmp(g_memo[slot].key, key)) {
            g_cfgRemembered++;
            return g_memo[slot].val;
        }
        h = key_hash(key);
        for (i = 0; i < g_count && !found; i++)
            if (g_cfg[i].hash == h && ascii_equal_nocase(g_cfg[i].key, key)) { v = parse(g_cfg[i].val, def); found = 1; }
        if (strlen(key) < U9CFG_KEY) {
            g_memo[slot].ptr = key;
            g_memo[slot].def = def;
            g_memo[slot].val = v;
            lstrcpynA(g_memo[slot].key, key, U9CFG_KEY);
            g_memo[slot].gen = g_memoGen;
        }
        return v;
    }
    h = key_hash(key);
    for (i = 0; i < g_count; i++)
        if (g_cfg[i].hash == h && ascii_equal_nocase(g_cfg[i].key, key))
            return parse(g_cfg[i].val, def);
    return def;
}

const char *u9cfg_str(const char *key, const char *def)
{
    unsigned i, h;
    if (!g_loaded) return def;
    h = key_hash(key);
    for (i = 0; i < g_count; i++)
        if (g_cfg[i].hash == h && ascii_equal_nocase(g_cfg[i].key, key))
            return g_cfg[i].val[0] ? g_cfg[i].val : def;
    return def;
}
