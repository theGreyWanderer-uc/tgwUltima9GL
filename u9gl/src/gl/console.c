#include <windows.h>
#include <GL/gl.h>
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/lifecycle.h"
#include "../common/symbols.h"
#include "../injection/config.h"
#include "../fix/fixmem.h"
#include "../fix/gold.h"
#include "../fix/playerset.h"
#include "../fix/fixes.h"
#include "metrics.h"
#include "npcscale.h"
#include "npccmd.h"
#include "questcmd.h"
#include "highway.h"
#include "regions.h"
#include "console.h"
#include "../fix/lightwatch.h"
#include "glsun.h"
#include "glfog.h"
#include "glunder.h"
#include "glbeam.h"
#include "glrays.h"
void u9gl_dump_after(unsigned seconds);
#include "glpost.h"
#include "../common/u9time.h"

void u9log_line(const char *fmt, ...);

#define GAME_WNDCLASS   "OFCBlackWindowClass"
#define WORLD           0x00850228u
#define OPT_TABLE       0x0078B538u
#define OPT_ENTRIES     102
#define OPT_STRIDE      0x48u
#define OPT_KEY         0x20u
#define OPT_RECORD      0x40u
#define OPT_VALUE       0x48u
#define WORLD_MAP_MGR   0xCCu
#define MGR_MAP         0x20u
#define SAVELOAD_OBJ    0x00AD126Cu
#define BUSY_OBJ        0x0131F668u
#define MC_SIZE         0xC0u
#define MC_STATE        0xB4u
#define VT_TEARDOWN     0x2Cu

#define CON_ROWS        20
#define OUT_LINES       64
#define OUT_W           200
#define IN_W            120
#define HL_ON           '\001'
#define HL_OFF          '\002'
#define HIST            16

typedef void *(__attribute__((cdecl)) *NewFn)(unsigned size);
typedef void *(U9CALL *MCCtorFn)(void *self, void *mgr, void *dest, uint32_t word, void *trigger);
typedef void *(__attribute__((fastcall)) *GetMgrFn)(int map);
typedef void (U9CALL *ChangeFn)(void *world, void *mgr, void *dest, int relative,
                                const float *loc, const float *rot, int sameMap);
typedef int  (U9CALL *HeightFn)(void *mgr, unsigned tx, unsigned ty);
typedef void (U9CALL *VoidFn)(void *self);

static int     g_enabled, g_open;
static HWND    g_wnd;
static WNDPROC g_origProc;
static char    g_out[OUT_LINES][OUT_W];
static unsigned g_outNext, g_outCount;
static unsigned g_outBack;
static unsigned g_outCols = 100;
static char    g_in[IN_W];
static unsigned g_inLen, g_cur;
static unsigned g_view;
static char    g_hist[HIST][IN_W];
static unsigned g_histCount, g_histPos;
static char    g_draft[IN_W];

static struct { void *proc; int map, hasZ; float x, y, z; } g_tp;
static uint32_t g_vtable[32];

static void out(const char *fmt, ...)
{
    char plain[OUT_W], *d = plain;
    const char *s;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_out[g_outNext], OUT_W, fmt, ap);
    va_end(ap);
    for (s = g_out[g_outNext]; *s; s++)
        if (*s != HL_ON && *s != HL_OFF) *d++ = *s;
    *d = 0;
    u9log_line("CONSOLE %s", plain);
    g_outNext = (g_outNext + 1) % OUT_LINES;
    if (g_outCount < OUT_LINES) g_outCount++;
    g_outBack = 0;
}

#define NOTICE_MS 6000u
static char  g_notice[OUT_W];
static DWORD g_noticeAt;
static int   g_noticeOn;

void u9console_notice(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_notice, sizeof g_notice, fmt, ap);
    va_end(ap);
    out("%s", g_notice);
    g_noticeAt = u9time_now();
    g_noticeOn = 1;
}

static void draw_notice(int surfW, int surfH)
{
    int cw, ch, w, n;
    if (!g_noticeOn) return;
    if (u9time_now() - g_noticeAt > NOTICE_MS) { g_noticeOn = 0; return; }
    if (!u9metrics_text_init()) return;
    u9metrics_char_size(&cw, &ch);
    n = (int)strlen(g_notice);
    w = n * cw + 16;
    glViewport(0, 0, surfW, surfH);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)surfW, (double)surfH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.03f, 0.04f, 0.08f, 0.85f);
    glBegin(GL_QUADS);
    glVertex2f(8.0f, 8.0f); glVertex2f((float)(8 + w), 8.0f);
    glVertex2f((float)(8 + w), (float)(8 + ch + 12)); glVertex2f(8.0f, (float)(8 + ch + 12));
    glEnd();
    glDisable(GL_BLEND);
    glColor3f(1.0f, 0.85f, 0.35f);
    u9metrics_text(16.0f, (float)(8 + ch + 4), g_notice);
    glColor3f(1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

static void out_show(unsigned fromEnd)
{
    unsigned want = fromEnd > CON_ROWS - 3 ? fromEnd - (CON_ROWS - 3) : 0;
    unsigned maxBack = g_outCount > CON_ROWS ? g_outCount - CON_ROWS : 0;
    g_outBack = want < maxBack ? want : maxBack;
}

static uintptr_t current_manager(void)
{
    uintptr_t w = readable4(WORLD) ? rd32(WORLD) : 0;
    return (w && readable4(w + WORLD_MAP_MGR)) ? rd32(w + WORLD_MAP_MGR) : 0;
}

static int current_map(void)
{
    uintptr_t m = current_manager();
    return (m && readable4(m + MGR_MAP)) ? (int)rd32(m + MGR_MAP) : -1;
}

int u9console_current_map(void) { return current_map(); }

static int map_exists(int map)
{
    char path[MAX_PATH], *slash;
    DWORD a;
    if (map < 0 || map > 999) return 0;
    GetModuleFileNameA(NULL, path, sizeof path);
    slash = strrchr(path, '\\');
    if (!slash) return 0;
    snprintf(slash + 1, (size_t)(path + sizeof path - slash - 1), "static\\terrain.%d", map);
    a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void points_path(char *out_, size_t cap)
{
    HMODULE self = 0;
    char *slash;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(void *)points_path, &self);
    GetModuleFileNameA(self, out_, (DWORD)cap);
    slash = strrchr(out_, '\\');
    if (slash) snprintf(slash + 1, cap - (size_t)(slash + 1 - out_), "u9gl_teleports.ini");
}

static int valid_name(const char *n)
{
    if (!*n) return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') ||
              (*n >= '0' && *n <= '9') || *n == '_' || *n == '-')) return 0;
    return 1;
}

static void U9CALL tp_tick(void *self)
{
    uintptr_t s = (uintptr_t)self;
    uintptr_t world = readable4(WORLD) ? rd32(WORLD) : 0;

    if (rd32(s + MC_STATE) == 0 && world) {
        void *mgr = ((GetMgrFn)u9sym(U9SYM_MAP_GET_MANAGER))(g_tp.map);
        if (!mgr) {
            out("map %d did not load - staying here", g_tp.map);
        } else {
            float loc[3], rot[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            if (!g_tp.hasZ) {
                int h = ((HeightFn)u9sym(U9SYM_TERRAIN_HEIGHT))(mgr, (unsigned)(int)(g_tp.x / 128.0f),
                                                                (unsigned)(int)(g_tp.y / 128.0f));
                g_tp.z = (float)h + 1.0f;
            }
            loc[0] = g_tp.x; loc[1] = g_tp.y; loc[2] = g_tp.z;
            u9log_line("CONSOLE travel: World_ChangeMap to map %d at %.0f,%.0f,%.0f (manager %08X)",
                       g_tp.map, loc[0], loc[1], loc[2], (unsigned)(uintptr_t)mgr);
            ((ChangeFn)u9sym(U9SYM_WORLD_CHANGEMAP))((void *)world, mgr, 0, 0, loc, rot, 0);
        }
    }
    if (readable4(SAVELOAD_OBJ) && rd32(SAVELOAD_OBJ))
        ((VoidFn)u9sym(U9SYM_SAVELOAD_ENABLE))((void *)(uintptr_t)rd32(SAVELOAD_OBJ));
    if (readable4(BUSY_OBJ) && rd32(BUSY_OBJ) && readable4(rd32(BUSY_OBJ) + 0x50))
        *(volatile uint32_t *)(uintptr_t)(rd32(BUSY_OBJ) + 0x50) = 0;
    g_tp.proc = 0;
    ((VoidFn)(uintptr_t)rd32(rd32(s) + VT_TEARDOWN))(self);
}

static void travel(int map, float x, float y, float z, int hasZ)
{
    uintptr_t mgr = current_manager();
    void *mc;

    if (g_tp.proc) { out("a teleport is already under way"); return; }
    if (!mgr) { out("no world is loaded"); return; }
    if (!map_exists(map)) { out("there is no map %d (no static\\terrain.%d)", map, map); return; }

    if (!g_vtable[0]) {
        memcpy(g_vtable, (const void *)u9sym(U9SYM_MAPCHANGER_VTABLE), sizeof g_vtable);
        g_vtable[1] = (uint32_t)(uintptr_t)tp_tick;
    }
    mc = ((NewFn)u9sym(U9SYM_OPERATOR_NEW))(MC_SIZE);
    if (!mc) { out("out of memory"); return; }
    g_tp.map = map; g_tp.x = x; g_tp.y = y; g_tp.z = z; g_tp.hasZ = hasZ;
    ((MCCtorFn)u9sym(U9SYM_MAPCHANGER_CONSTRUCT))(mc, (void *)mgr, 0, 0, 0);
    *(uint32_t *)mc = (uint32_t)(uintptr_t)g_vtable;
    g_tp.proc = mc;
    if (hasZ) out("going to map %d at %.0f,%.0f,%.0f", map, x, y, z);
    else      out("going to map %d at %.0f,%.0f, on the ground", map, x, y);
}

static int parse_coords(const char *s, float v[3])
{
    int n = 0;
    char *end;
    while (*s && n < 3) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        v[n] = (float)strtod(s, &end);
        if (end == s) return -1;
        s = end;
        n++;
    }
    while (*s == ' ' || *s == ',') s++;
    return *s ? -1 : n;
}

static void cmd_opt(const char *filter)
{
    int i, shown = 0;
    char lf[64];
    size_t k;
    U9FrameView v;
    for (k = 0; filter[k] && k < sizeof lf - 1; k++) lf[k] = (char)tolower((unsigned char)filter[k]);
    lf[k] = 0;
    if (!readable4(OPT_TABLE)) { out("the option table cannot be read"); return; }
    for (i = 0; i < OPT_ENTRIES; i++) {
        uintptr_t e = OPT_TABLE + (uintptr_t)i * OPT_STRIDE, rec;
        char sec[33], key[33], both[80], val[64];
        uint32_t raw;
        if (!readable4(e + OPT_RECORD)) break;
        memcpy(sec, (const void *)e, 32); sec[32] = 0;
        memcpy(key, (const void *)(e + OPT_KEY), 32); key[32] = 0;
        if (!key[0]) continue;
        snprintf(both, sizeof both, "%s %s", sec, key);
        for (k = 0; both[k]; k++) both[k] = (char)tolower((unsigned char)both[k]);
        if (lf[0] && !strstr(both, lf)) continue;
        rec = rd32(e + OPT_RECORD);
        if (!rec || !readable4(rec + OPT_VALUE)) { snprintf(val, sizeof val, "(no record)"); }
        else {
            const unsigned char *t = (const unsigned char *)(rec + OPT_VALUE);
            size_t n = 0;
            while (n < 47 && readable4((uintptr_t)(t + n)) && isprint(t[n])) n++;
            raw = rd32(rec + OPT_VALUE);
            if (n >= 2 && readable4((uintptr_t)(t + n)) && t[n] == 0) {
                snprintf(val, sizeof val, "\"%.*s\"", (int)n, (const char *)t);
            } else if (raw > 0x10000u && readable4(raw) && isprint(*(const unsigned char *)raw)) {
                snprintf(val, sizeof val, "\"%.48s\"", (const char *)raw);
            } else {
                snprintf(val, sizeof val, "%d", (int)raw);
            }
        }
        out("[%s] %s = %s", sec, key, val);
        u9log_line("CONSOLE opt [%s] %s = %s (record %08X)", sec, key, val, (unsigned)rec);
        shown++;
    }
    if (!shown) out("no option matches '%s' (opt with nothing lists all %d)", filter, OPT_ENTRIES);
    if (u9gl_frame_view(&v))
        out("this frame: drawn out to %.0f from the camera (depth mode %d)", (double)v.zFar, v.depthMode);
}

static void cmd_pos(void)
{
    int x, y, z, map = current_map();
    if (map < 0 || !u9avatar_position(&x, &y, &z)) { out("no world is loaded"); return; }
    if (u9regions_exists(map)) out("map %d %s at %d,%d,%d", map, u9regions_name(map), x, y, z);
    else out("map %d at %d,%d,%d", map, x, y, z);
}

static void cmd_set_tp(const char *name)
{
    char path[MAX_PATH], val[64];
    int x, y, z, map = current_map();
    if (!valid_name(name)) { out("usage: set tp <name>  (letters, digits, _ and -)"); return; }
    if (map < 0 || !u9avatar_position(&x, &y, &z)) { out("no world is loaded"); return; }
    points_path(path, sizeof path);
    snprintf(val, sizeof val, "%d,%d,%d,%d", map, x, y, z);
    if (!WritePrivateProfileStringA("tp", name, val, path)) { out("could not write %s", path); return; }
    out("stored '%s': map %d at %d,%d,%d", name, map, x, y, z);
}

static void cmd_unset_tp(const char *name)
{
    char path[MAX_PATH], val[64];
    if (!valid_name(name)) { out("usage: unset tp <name>"); return; }
    points_path(path, sizeof path);
    if (!GetPrivateProfileStringA("tp", name, "", val, sizeof val, path) || !*val) {
        out("no point called '%s' - list tp lists them", name);
        return;
    }
    if (!WritePrivateProfileStringA("tp", name, NULL, path)) { out("could not write %s", path); return; }
    out("forgot '%s' (was %s)", name, val);
}

static void cmd_tp(const char *name)
{
    char path[MAX_PATH], val[64];
    int map, x, y, z;
    points_path(path, sizeof path);
    if (!*name) {
        char names[2048], *p;
        DWORD n = GetPrivateProfileStringA("tp", NULL, "", names, sizeof names, path);
        if (!n) { out("no stored points - set tp <name> stores where you stand"); return; }
        for (p = names; *p; p += strlen(p) + 1) {
            GetPrivateProfileStringA("tp", p, "", val, sizeof val, path);
            out("  %-16s %s", p, val);
        }
        return;
    }
    if (!GetPrivateProfileStringA("tp", name, "", val, sizeof val, path) || !*val) {
        out("no point called '%s' - list tp lists them", name);
        return;
    }
    if (sscanf(val, "%d,%d,%d,%d", &map, &x, &y, &z) != 4) { out("'%s' is not map,x,y,z: %s", name, val); return; }
    travel(map, (float)x, (float)y, (float)z, 1);
}

#define NPC_ARRIVE_OFFSET 128.0f
#define NPC_ARRIVE_ABOVE  32.0f
#define NPC_NEAR_XY       10000
#define NPC_NEAR_Z        2500

static void cmd_tppos_npc(const char *who)
{
    U9NpcPlace p;
    if (!*who) {
        int x, y, z, map = current_map();
        if (map < 0 || !u9avatar_position(&x, &y, &z)) { out("no world is loaded"); return; }
        u9npc_list_near(map, x, y, z, NPC_NEAR_XY, NPC_NEAR_Z, CON_ROWS * 2, out);
        return;
    }
    if (!u9npc_locate(who, current_map(), &p, out)) return;
    out("%u %s at %d,%d,%d", p.npc, p.name, p.x, p.y, p.z);
    travel(p.map, (float)p.x, (float)p.y - NPC_ARRIVE_OFFSET, (float)p.z + NPC_ARRIVE_ABOVE, 1);
}

static void cmd_tppos_highway(const char *args)
{
    char *end;
    unsigned long link = strtoul(args, &end, 10);
    long want = -1;
    const U9HighwayPoint *p;
    int map;
    if (end == args) { out("usage: tppos highway <link> [<map>] - get npc highway <name> lists an NPC's"); return; }
    while (*end == ' ') end++;
    if (*end) {
        char *e2;
        want = strtol(end, &e2, 10);
        if (e2 == end || *e2) { out("usage: tppos highway <link> [<map>]"); return; }
    }
    if (!(p = u9highway_point((unsigned)link))) { out("there is no highway point %lu", link); return; }
    if ((map = u9highway_pick_map(p, (int)want, current_map())) < 0) {
        if (want >= 0) out("highway point %lu is not in map %ld", link, want);
        else out("highway point %lu has no marker in any region", link);
        return;
    }
    out("highway point %lu at %d,%d,%d, map %d%s", link, p->x, p->y, p->z, map,
        p->nmaps > 1 ? " (also in other regions)" : "");
    travel(map, (float)p->x, (float)p->y, (float)p->z + 32.0f, 1);
}

static void cmd_tppos(const char *args)
{
    char *end;
    long map;
    float v[3];
    int n, hasZ;
    if (!_strnicmp(args, "highway", 7) && (args[7] == ' ' || !args[7])) {
        const char *rest = args + 7;
        while (*rest == ' ') rest++;
        cmd_tppos_highway(rest);
        return;
    }
    if (!_strnicmp(args, "npc", 3) && (args[3] == ' ' || !args[3])) {
        const char *who = args + 3;
        while (*who == ' ') who++;
        cmd_tppos_npc(who);
        return;
    }
    map = strtol(args, &end, 10);
    if (end != args && !*end) {
        if (!u9regions_centre((int)map, &v[0], &v[1], &v[2], &hasZ)) {
            if (u9regions_exists((int)map)) out("region %ld has no terrain to arrive on", map);
            else out("there is no region %ld - list map lists them", map);
            return;
        }
        out("the centre of %ld %s", map, u9regions_name((int)map));
        travel((int)map, v[0], v[1], v[2], hasZ);
        return;
    }
    if (end == args || (*end != ' ' && *end != ',')) { out("usage: tppos <map> [x,y[,z]] | tppos npc <name>"); return; }
    n = parse_coords(end, v);
    if (n < 2) { out("usage: tppos <map> [x,y[,z]]"); return; }
    travel((int)map, v[0], v[1], n == 3 ? v[2] : 0.0f, n == 3);
}

static int name_contains(const char *name, const char *want)
{
    size_t n = strlen(want), i;
    for (i = 0; name[i]; i++)
        if (!_strnicmp(name + i, want, n)) return 1;
    return 0;
}

static void cmd_list_map(const char *filter)
{
    int ids[U9REGION_SLOTS], n = 0, cur = current_map(), m, i, cols, rows, curRow = -1;
    unsigned nameW, avail = g_outCols < OUT_W - 1 ? g_outCols : OUT_W - 1;

    for (m = 0; m < U9REGION_SLOTS; m++)
        if (u9regions_exists(m) && (!*filter || name_contains(u9regions_name(m), filter))) ids[n++] = m;
    if (!n) { out("no region name contains '%s'", filter); return; }

    cols = (int)(avail / (1 + 3 + 1 + 16 + 2));
    if (cols < 1) cols = 1;
    if (*filter && n <= cols) cols = n;
    nameW = avail / (unsigned)cols > 7 ? avail / (unsigned)cols - 7 : 16;
    if (nameW > 24) nameW = 24;
    if (nameW < 8) nameW = 8;
    rows = (n + cols - 1) / cols;

    if (*filter) out("%d region%s with '%s' in the name:", n, n == 1 ? "" : "s", filter);
    else if (u9regions_exists(cur))
        out("%d regions; you are in %c%d %s%c - PgUp/PgDn scroll, list map <text> filters",
            n, HL_ON, cur, u9regions_name(cur), HL_OFF);
    else out("%d regions - PgUp/PgDn scroll, list map <text> filters", n);

    for (i = 0; i < rows; i++) {
        char line[OUT_W];
        size_t len = 0;
        int c;
        for (c = 0; c < cols; c++) {
            int k = c * rows + i, w;
            if (k >= n) break;
            m = ids[k];
            if (m == cur) {
                curRow = i;
                w = snprintf(line + len, sizeof line - len, "%c>%3d %-*.*s%c  ",
                             HL_ON, m, (int)nameW, (int)nameW, u9regions_name(m), HL_OFF);
            } else {
                w = snprintf(line + len, sizeof line - len, " %3d %-*.*s  ",
                             m, (int)nameW, (int)nameW, u9regions_name(m));
            }
            if (w < 0 || (size_t)w >= sizeof line - len) break;
            len += (size_t)w;
        }
        while (len && line[len - 1] == ' ') line[--len] = 0;
        out("%s", line);
    }
    if (curRow >= 0) out_show((unsigned)(rows - 1 - curRow));
}

static void cmd_list(char *arg)
{
    char *what = arg, *rest = arg;
    while (*rest && *rest != ' ') rest++;
    if (*rest) *rest++ = 0;
    while (*rest == ' ') rest++;
    if (!strcmp(what, "tp"))       cmd_tp("");
    else if (!strcmp(what, "map")) cmd_list_map(rest);
    else out("usage: list tp | list map [<text>]");
}

static void execute(char *line)
{
    char *cmd = line, *arg;
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;
    out("> %s", cmd);
    if (*cmd == '/') { out("commands take no / - type %s", cmd + 1); return; }
    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    while (*arg == ' ') arg++;
    { char *e = arg + strlen(arg); while (e > arg && e[-1] == ' ') *--e = 0; }

    if (!strcmp(cmd, "help")) {
        out("pos                   where you are");
        out("opt [<text>]          the engine's options now (Options.ini), e.g. opt clipping; and the far plane");
        out("set tp <name>         store this spot as <name>");
        out("get npc pos <name>    where an NPC on this map stands (names are one word)");
        out("set npc pos <name> <x,y[,z]|here>  move it there, or the nearest legal spot");
        out("get npc target <name> its combat target");
        out("set npc target <name> <who|none>");
        out("get npc flag <name> [<flag>]  its state flags by name (hostile, routine, ...)");
        out("set npc flag <name> <flag> <0|1>");
        out("get npc highway <name> the highway points in an NPC's schedule, any map");
        out("get npc size [all|<n>|<name>]  NPC body sizes: npc %%, uniform, axes, live");
        out("get stat [str|dex|int]   the Avatar's attributes; bare: all three");
        out("set stat str|dex|int <0-3>");
        out("get skill [fists|1h|2h|staff|ranged]   weapon skills (Novice..Master); bare: all five");
        out("set skill fists|1h|2h|staff|ranged <0-3>");
        out("get effect            the Avatar's status effects (state bits)");
        out("set effect <name>     set one (poison [hits], invisible, sleep, lock, ...); bare: the names");
        out("unset effect <name>|all  clear one, or every one");
        out("unset tp <name>       forget a stored spot");
        out("tp <name>             go to a stored spot");
        out("tppos <map>           go to the middle of what is built in region <map>");
        out("tppos <map> x,y[,z]   go to coordinates; without z, the ground");
        out("tppos npc <name>      go to an NPC on this map; or tppos npc <number>");
        out("tppos npc             the NPCs around you, nearest first");
        out("tppos highway <link> [<map>]  go to a highway point (get npc highway lists an NPC's)");
        out("list tp               the stored spots");
        out("list map [<text>]     the regions, yours highlighted; <text> filters by name");
        out("lights [<n>]          the n engine lights nearest the camera (8 by default)");
        out("sun                   the sun's direction, time and height, from the sky");
        out("moon [shadow ...]     the moons' phases and directions, and their shadows");
        out("fog [volume on|off]   the mist sheet as measured, and the volume fog in its place");
        out("water [on|off|margin|pad|forget]   is the camera under the water, and the water's height");
        out("beam [on|off|depth|strength]   light shafts (texture 1760) as volumes; post beam: as sent");
        out("rays [on|off|view|strength ...]   sun rays: the sky near the sun blurred toward it");
        out("halberd grip|back [x y z [rx ry rz]|reset]  the halberd in the hand (grip) or carried (back); redraw to see");
        out("dump [seconds]        a frame dump (as Ctrl+F9) that many seconds from now, 5 by default");
        out("sample [seconds]      a CPU sample (u9sample) that many seconds from now, 5 by default; a notice when done");
        out("post [check|grid|probe|off|on|night ...]  the post pass: status, copy check, world grid, centre position, night grade");
        out("pool [on|off|replace on|off|march on|off|strength N]  the light pools");
        out("datum <n> [<value>]   read or write a quest datum (0-1023, a byte each)");
        out("gate [on|off]         open or close the Earth moongate (map 14); bare: its state");
        out("give gold <n>         gold in front of you; give regs <n>: <n> of each reagent");
        out("give item <type>      one item of a type; give lenses: the Codex's Blue and Red lenses");
        out("show <virtue> [rune|glyph|item]  reveal the nearest hidden rune, glyph and item of a virtue");
        out("show <type> [<reach>] reveal hidden objects of a type near you, as a quest would");
        out("show <type> [<reach>] shrine  the same, a shrine's own copies too - F-18 test only");
        out("clear                 clear this console");
        out("keys                  the editing keys");
        out("quit                  leave the game now - nothing is saved");
        out("Ctrl+` closes the console; so does Esc on an empty line");
    } else if (!strcmp(cmd, "pos")) {
        cmd_pos();
    } else if (!strcmp(cmd, "opt")) {
        cmd_opt(arg);
    } else if (!strcmp(cmd, "get")) {
        if (!strncmp(arg, "npc ", 4)) u9npc_get_command(arg + 4, out);
        else if (!strncmp(arg, "stat", 4) && (arg[4] == ' ' || !arg[4])) u9playerset_get_stat(arg + 4, out);
        else if (!strncmp(arg, "skill", 5) && (arg[5] == ' ' || !arg[5])) u9playerset_get_skill(arg + 5, out);
        else if (!strncmp(arg, "effect", 6) && (arg[6] == ' ' || !arg[6])) u9playerset_get_effect(arg + 6, out);
        else out("usage: get npc pos|target|flag <name> ... | get stat [<name>] | get skill [<name>] | get effect");
    } else if (!strcmp(cmd, "set")) {
        if (!strncmp(arg, "tp ", 3)) cmd_set_tp(arg + 3);
        else if (!strncmp(arg, "npc ", 4)) u9npc_set_command(arg + 4, out);
        else if (!strncmp(arg, "stat", 4) && (arg[4] == ' ' || !arg[4])) u9playerset_set_stat(arg + 4, out);
        else if (!strncmp(arg, "skill", 5) && (arg[5] == ' ' || !arg[5])) u9playerset_set_skill(arg + 5, out);
        else if (!strncmp(arg, "effect", 6) && (arg[6] == ' ' || !arg[6])) u9playerset_set_effect(arg + 6, out);
        else out("usage: set tp <name> | set npc pos|target|flag <name> ... | set stat <name> <0-3> | set skill <name> <0-3> | set effect <name>");
    } else if (!strcmp(cmd, "unset")) {
        if (!strncmp(arg, "tp ", 3)) cmd_unset_tp(arg + 3);
        else if (!strncmp(arg, "effect", 6) && (arg[6] == ' ' || !arg[6])) u9playerset_unset_effect(arg + 6, out);
        else out("usage: unset tp <name> | unset effect <name>|all");
    } else if (!strcmp(cmd, "tp")) {
        cmd_tp(arg);
    } else if (!strcmp(cmd, "tppos")) {
        cmd_tppos(arg);
    } else if (!strcmp(cmd, "list")) {
        cmd_list(arg);
    } else if (!strcmp(cmd, "readcache")) {
        extern int g_u9ReadCache;
        extern unsigned g_u9ReadCalls, g_u9ReadAsked;
        if (arg && *arg) {
            int n = atoi(arg);
            g_u9ReadCache = n < 1 ? 1 : n > 8 ? 8 : n;
            u9log_line("CONSOLE readable checks remember %d region(s) a tick", g_u9ReadCache);
        }
        out("readable checks remember %d region(s) a tick (readcache 1-8; 1 as before); %u checks so far, %u asked Windows",
            g_u9ReadCache, g_u9ReadCalls, g_u9ReadAsked);
    } else if (!strcmp(cmd, "lights")) {
        u9light_command(arg, out);
    } else if (!strcmp(cmd, "sun")) {
        u9sun_command(arg, out);
    } else if (!strcmp(cmd, "moon")) {
        u9moon_command(arg, out);
    } else if (!strcmp(cmd, "fog")) {
        u9fog_command(arg, out);
    } else if (!strcmp(cmd, "water")) {
        u9under_command(arg, out);
    } else if (!strcmp(cmd, "beam")) {
        u9beam_command(arg, out);
    } else if (!strcmp(cmd, "halberd")) {
        u9halberd_command(arg, out);
    } else if (!strcmp(cmd, "rays")) {
        u9rays_command(arg, out);
    } else if (!strcmp(cmd, "dump")) {
        int s = (arg && *arg) ? atoi(arg) : 5;
        if (s < 1) s = 1;
        if (s > 120) s = 120;
        u9gl_dump_after((unsigned)s);
        out("a frame dump in %d s (as Ctrl+F9): close the console and open what you want caught", s);
    } else if (!strcmp(cmd, "sample")) {
        int s = (arg && *arg) ? atoi(arg) : 5;
        if (s < 1) s = 1;
        if (s > 120) s = 120;
        u9selfsample_after((unsigned)s, out);
    } else if (!strcmp(cmd, "post")) {
        u9glpost_command(arg, out);
    } else if (!strcmp(cmd, "pool")) {
        u9glpost_pool_command(arg, out);
    } else if (!strcmp(cmd, "datum")) {
        u9quest_datum_command(arg, out);
    } else if (!strcmp(cmd, "gate")) {
        u9quest_gate_command(arg, out);
    } else if (!strcmp(cmd, "give")) {
        u9give_command(arg, out);
    } else if (!strcmp(cmd, "show")) {
        u9quest_show_command(arg, out);
    } else if (!strcmp(cmd, "keys")) {
        out("Left/Right, Home/End    move in the line; with Ctrl, a word at a time");
        out("Backspace/Del           delete a character; with Ctrl, a word");
        out("Tab                     complete a command or a stored spot's name");
        out("Up/Down                 earlier commands; Down past the last gives back your line");
        out("PgUp/PgDn               scroll this console");
        out("Ctrl+V                  paste");
        out("Esc                     clear the line; on an empty line, close");
    } else if (!strcmp(cmd, "clear") || !strcmp(cmd, "cls")) {
        g_outCount = 0;
        g_outBack = 0;
    } else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
        out("quitting");
        g_open = 0;
        PostMessageA(g_wnd, WM_CLOSE, 0, 0);
    } else if (!strcmp(cmd, "save")) {
        out("save is reserved for saving the game - not yet built");
    } else {
        out("unknown command '%s' - help lists them", cmd);
    }
}

static DWORD g_keyTick;

static int ctrl_down(void) { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
static int is_sep(char c) { return c == ' ' || c == ','; }

static void splice(unsigned at, unsigned del, const char *ins, unsigned n)
{
    if (g_inLen - del + n > IN_W - 1) n = IN_W - 1 - (g_inLen - del);
    memmove(g_in + at + n, g_in + at + del, g_inLen - at - del + 1);
    memcpy(g_in + at, ins, n);
    g_inLen = g_inLen - del + n;
    g_cur = at + n;
}

static void set_line(const char *s)
{
    size_t n = strlen(s);
    if (n > IN_W - 1) n = IN_W - 1;
    memmove(g_in, s, n);
    g_in[n] = 0;
    g_inLen = g_cur = (unsigned)n;
}

static unsigned word_left(unsigned p)
{
    while (p > 0 && is_sep(g_in[p - 1])) p--;
    while (p > 0 && !is_sep(g_in[p - 1])) p--;
    return p;
}

static unsigned word_right(unsigned p)
{
    while (p < g_inLen && is_sep(g_in[p])) p++;
    while (p < g_inLen && !is_sep(g_in[p])) p++;
    return p;
}

static void paste(void)
{
    char buf[IN_W];
    unsigned n = 0;
    HANDLE h;
    const char *s;
    if (!OpenClipboard(g_wnd)) return;
    h = GetClipboardData(CF_TEXT);
    s = h ? (const char *)GlobalLock(h) : NULL;
    if (s) {
        for (; *s && *s != '\r' && *s != '\n' && n + 1 < IN_W; s++)
            if (*s == '\t') buf[n++] = ' ';
            else if ((unsigned char)*s >= 32 && (unsigned char)*s < 127) buf[n++] = *s;
        GlobalUnlock(h);
    }
    CloseClipboard();
    splice(g_cur, 0, buf, n);
}

static void complete(void)
{
    static const char *const cmds[] = { "help", "keys", "pos", "opt", "get", "set", "unset", "tp", "tppos", "list", "lights", "sun", "moon", "fog", "water", "beam", "rays", "halberd", "dump", "post", "pool", "datum", "gate", "give", "show", "clear", "quit" };
    static char names[2048];
    const char *pool[64], *cand[64];
    char words[2][IN_W], path[MAX_PATH], *p;
    unsigned ws = g_cur, plen, np = 0, n = 0, nw = 0, i, common;
    int names_done = 0;
    const char *s = g_in;

    while (ws > 0 && g_in[ws - 1] != ' ') ws--;
    plen = g_cur - ws;
    while (s < g_in + ws) {
        const char *e;
        while (s < g_in + ws && *s == ' ') s++;
        if (s >= g_in + ws) break;
        for (e = s; e < g_in + ws && *e != ' '; e++) {}
        if (nw < 2) { memcpy(words[nw], s, (size_t)(e - s)); words[nw][e - s] = 0; }
        nw++;
        s = e;
    }
    if (nw == 0) {
        for (i = 0; i < sizeof cmds / sizeof cmds[0]; i++) pool[np++] = cmds[i];
    } else if (nw == 1 && (!strcmp(words[0], "set") || !strcmp(words[0], "unset"))) {
        pool[np++] = "tp";
        pool[np++] = "effect";
    } else if (nw == 2 && !strcmp(words[1], "effect") &&
               (!strcmp(words[0], "set") || !strcmp(words[0], "unset"))) {
        const char *nm;
        for (i = 0; (nm = u9playerset_effect_name((int)i)) != NULL && np < 63; i++) pool[np++] = nm;
        if (!strcmp(words[0], "unset")) pool[np++] = "all";
        names_done = 1;
    } else if (nw == 1 && !strcmp(words[0], "list")) {
        pool[np++] = "tp";
        pool[np++] = "map";
    } else if ((nw == 1 && !strcmp(words[0], "tp")) ||
               (nw == 2 && !strcmp(words[1], "tp") &&
                (!strcmp(words[0], "set") || !strcmp(words[0], "unset")))) {
        points_path(path, sizeof path);
        if (GetPrivateProfileStringA("tp", NULL, "", names, sizeof names, path))
            for (p = names; *p && np < 64; p += strlen(p) + 1) pool[np++] = p;
        names_done = 1;
    } else {
        return;
    }
    for (i = 0; i < np; i++)
        if (strlen(pool[i]) >= plen && !_strnicmp(pool[i], g_in + ws, plen)) cand[n++] = pool[i];
    if (!n) return;
    if (n == 1) {
        char ins[IN_W];
        int space = !names_done && (g_cur == g_inLen || g_in[g_cur] != ' ');
        snprintf(ins, sizeof ins, "%s%s", cand[0], space ? " " : "");
        splice(ws, plen, ins, (unsigned)strlen(ins));
        return;
    }
    common = (unsigned)strlen(cand[0]);
    for (i = 1; i < n; i++) {
        unsigned k = 0;
        while (k < common && cand[i][k] && tolower((unsigned char)cand[i][k]) == tolower((unsigned char)cand[0][k])) k++;
        common = k;
    }
    if (common > plen) { splice(ws, plen, cand[0], common); return; }
    {
        char line[OUT_W];
        size_t len = 0;
        line[0] = 0;
        for (i = 0; i < n && len + 1 < sizeof line; i++) {
            int k = snprintf(line + len, sizeof line - len, "%s  ", cand[i]);
            if (k < 0) break;
            len += (size_t)k;
        }
        out("%s", line);
    }
}

static LRESULT CALLBACK console_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_OEM_3 && ctrl_down()) {
        g_open = !g_open;
        if (g_open && !g_outCount) out("U9GL console - help for commands, keys for editing");
        return 0;
    }
    if (!g_open) return CallWindowProcA(g_origProc, w, msg, wp, lp);

    if (msg == WM_KEYDOWN) {
        int ctrl = ctrl_down();
        unsigned maxBack = g_outCount > CON_ROWS ? g_outCount - CON_ROWS : 0;
        g_keyTick = u9time_now();
        switch (wp) {
        case VK_ESCAPE:
            if (g_inLen) { set_line(""); g_histPos = g_histCount; }
            else g_open = 0;
            break;
        case VK_RETURN:
            if (g_inLen && (!g_histCount || strcmp(g_hist[(g_histCount - 1) % HIST], g_in))) {
                memcpy(g_hist[g_histCount % HIST], g_in, IN_W);
                g_histCount++;
            }
            g_histPos = g_histCount;
            g_draft[0] = 0;
            execute(g_in);
            set_line("");
            break;
        case VK_BACK:
            if (g_cur) { unsigned p = ctrl ? word_left(g_cur) : g_cur - 1; splice(p, g_cur - p, "", 0); }
            break;
        case VK_DELETE:
            if (g_cur < g_inLen) {
                unsigned q = ctrl ? word_right(g_cur) : g_cur + 1, at = g_cur;
                splice(at, q - at, "", 0);
            }
            break;
        case VK_LEFT:  if (g_cur) g_cur = ctrl ? word_left(g_cur) : g_cur - 1; break;
        case VK_RIGHT: if (g_cur < g_inLen) g_cur = ctrl ? word_right(g_cur) : g_cur + 1; break;
        case VK_HOME:  g_cur = 0; break;
        case VK_END:   g_cur = g_inLen; break;
        case VK_TAB:   complete(); break;
        case VK_PRIOR: g_outBack = g_outBack + CON_ROWS / 2 < maxBack ? g_outBack + CON_ROWS / 2 : maxBack; break;
        case VK_NEXT:  g_outBack = g_outBack > CON_ROWS / 2 ? g_outBack - CON_ROWS / 2 : 0; break;
        case VK_UP:
            if (g_histPos > 0 && g_histCount - g_histPos < HIST) {
                if (g_histPos == g_histCount) memcpy(g_draft, g_in, IN_W);
                g_histPos--;
                set_line(g_hist[g_histPos % HIST]);
            }
            break;
        case VK_DOWN:
            if (g_histPos < g_histCount) {
                g_histPos++;
                set_line(g_histPos < g_histCount ? g_hist[g_histPos % HIST] : g_draft);
            }
            break;
        case 'V': if (ctrl) paste(); break;
        default: break;
        }
        return 0;
    }
    if (msg == WM_CHAR) {
        if (wp >= 32 && wp < 127 && !ctrl_down()) {
            char c = (char)wp;
            if (g_inLen + 1 < IN_W) splice(g_cur, 0, &c, 1);
        }
        return 0;
    }
    return CallWindowProcA(g_origProc, w, msg, wp, lp);
}

static void unsubclass(void)
{
    if (g_wnd && g_origProc && IsWindow(g_wnd) &&
        (WNDPROC)GetWindowLongA(g_wnd, GWL_WNDPROC) == console_proc)
        SetWindowLongA(g_wnd, GWL_WNDPROC, (LONG)(uintptr_t)g_origProc);
    g_origProc = 0;
}

static BOOL CALLBACK find_game(HWND w, LPARAM lp)
{
    char cls[64];
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!GetClassNameA(w, cls, sizeof cls) || strcmp(cls, GAME_WNDCLASS) != 0) return TRUE;
    *(HWND *)lp = w;
    return FALSE;
}

static void subclass_once(void)
{
    static int tries;
    HWND w = 0;
    if (g_origProc || tries > 600) return;
    tries++;
    EnumWindows(find_game, (LPARAM)&w);
    if (!w) return;
    g_wnd = w;
    g_origProc = (WNDPROC)(uintptr_t)SetWindowLongA(w, GWL_WNDPROC, (LONG)(uintptr_t)console_proc);
    if (!g_origProc) { u9log_line("CONSOLE could not subclass the game window (err %lu)", GetLastError()); return; }
    u9life_push("console window procedure", unsubclass);
    u9log_line("CONSOLE ready on window %08X - Ctrl+` opens it", (unsigned)(uintptr_t)w);
}

static void draw_out_line(float x0, float base, const char *s, int cw, int ch)
{
    char seg[OUT_W];
    int col = 0, hl = 0;
    while (*s) {
        size_t n = 0;
        while (s[n] && s[n] != HL_ON && s[n] != HL_OFF) n++;
        if (n) {
            float sx = x0 + (float)(col * cw);
            memcpy(seg, s, n);
            seg[n] = 0;
            if (hl) {
                glColor3f(0.40f, 0.30f, 0.05f);
                glBegin(GL_QUADS);
                glVertex2f(sx - 2.0f, base - (float)ch + 4.0f); glVertex2f(sx + (float)(n * cw) + 2.0f, base - (float)ch + 4.0f);
                glVertex2f(sx + (float)(n * cw) + 2.0f, base + 4.0f); glVertex2f(sx - 2.0f, base + 4.0f);
                glEnd();
                glColor3f(1.0f, 0.95f, 0.6f);
            } else {
                glColor3f(0.85f, 0.9f, 0.85f);
            }
            u9metrics_text(sx, base, seg);
            col += (int)n;
            s += n;
        }
        if (*s == HL_ON)  { hl = 1; s++; }
        else if (*s == HL_OFF) { hl = 0; s++; }
    }
}

void u9console_draw(int surfW, int surfH)
{
    GLint sc[4];
    GLboolean hadScissor;
    int cw, ch, x, y, w, h, rows, i;

    if (!g_enabled) return;
    subclass_once();
    if (!g_open && surfW > 0 && surfH > 0) {
        GLboolean sciss = glIsEnabled(GL_SCISSOR_TEST);
        if (sciss) glDisable(GL_SCISSOR_TEST);
        draw_notice(surfW, surfH);
        if (sciss) glEnable(GL_SCISSOR_TEST);
    }
    if (!g_open || surfW <= 0 || surfH <= 0 || !u9metrics_text_init()) return;
    u9metrics_char_size(&cw, &ch);

    hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    if (hadScissor) glDisable(GL_SCISSOR_TEST);
    if (sc[2] > 0 && sc[3] > 0 && sc[2] <= surfW && sc[3] <= surfH) {
        x = sc[0]; w = sc[2]; y = surfH - (sc[1] + sc[3]);
    } else {
        x = 0; w = surfW; y = 0;
    }
    rows = CON_ROWS;
    h = ch * (rows + 1) + 16;

    glViewport(0, 0, surfW, surfH);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)surfW, (double)surfH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.03f, 0.04f, 0.08f, 0.85f);
    glBegin(GL_QUADS);
    glVertex2f((float)x, (float)y);       glVertex2f((float)(x + w), (float)y);
    glVertex2f((float)(x + w), (float)(y + h)); glVertex2f((float)x, (float)(y + h));
    glEnd();
    glDisable(GL_BLEND);
    glColor3f(0.85f, 0.7f, 0.25f);
    glBegin(GL_LINES);
    glVertex2f((float)x, (float)(y + h) - 0.5f); glVertex2f((float)(x + w), (float)(y + h) - 0.5f);
    glEnd();

    g_outCols = (unsigned)((w - 16) / cw);
    {
        unsigned back = g_outBack < g_outCount ? g_outBack : 0;
        unsigned avail = g_outCount - back;
        unsigned show = avail < (unsigned)rows ? avail : (unsigned)rows;
        for (i = 0; i < (int)show; i++) {
            unsigned idx = (g_outNext + 2 * OUT_LINES - back - show + (unsigned)i) % OUT_LINES;
            draw_out_line((float)(x + 8), (float)(y + 8 + (i + 1) * ch - 3), g_out[idx], cw, ch);
        }
        if (back) {
            char note[48];
            int n = snprintf(note, sizeof note, "-- %u lines back, PgDn --", back);
            glColor3f(0.85f, 0.7f, 0.25f);
            u9metrics_text((float)(x + w - 8 - n * cw), (float)(y + 8 + (rows + 1) * ch - 3), note);
        }
    }
    {
        char line[IN_W + 4];
        int vis = (w - 16) / cw - 3;
        float base = (float)(y + 8 + (rows + 1) * ch - 3);
        DWORD now = u9time_now();
        if (vis < 8) vis = 8;
        if (g_cur < g_view) g_view = g_cur;
        if (g_cur > g_view + (unsigned)vis) g_view = g_cur - (unsigned)vis;
        if (g_view > g_inLen) g_view = g_inLen;
        snprintf(line, sizeof line, "%c %.*s", g_view ? '<' : ']', vis + 1, g_in + g_view);
        glColor3f(1.0f, 0.85f, 0.35f);
        u9metrics_text((float)(x + 8), base, line);
        if (now - g_keyTick < 500 || (now / 500) & 1) {
            float cx = (float)(x + 8 + (2 + (int)(g_cur - g_view)) * cw);
            glBegin(GL_QUADS);
            glVertex2f(cx, base - (float)ch + 4.0f); glVertex2f(cx + 2.0f, base - (float)ch + 4.0f);
            glVertex2f(cx + 2.0f, base + 3.0f);       glVertex2f(cx, base + 3.0f);
            glEnd();
        }
    }

    glColor3f(1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    if (hadScissor) { glScissor(sc[0], sc[1], sc[2], sc[3]); glEnable(GL_SCISSOR_TEST); }
}

void u9console_install(void)
{
    g_enabled = u9cfg_int("console", 1);
    if (g_enabled) u9regions_load();
    if (g_enabled) u9highway_load();
    u9log_line("CONSOLE %s", g_enabled ? "on - Ctrl+` opens it once the game window exists"
                                       : "off (console = 0)");
}
