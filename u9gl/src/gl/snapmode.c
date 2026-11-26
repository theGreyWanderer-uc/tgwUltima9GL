#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../fix/newgame.h"
#include "../common/u9time.h"
#include "metrics.h"
#include "snapmode.h"

void u9log_line(const char *fmt, ...);

#define DATUM_STORE   0x01333E64u
#define ANKH_DATUM    213
#define WAIT_MS       4500u
#define RETRY_MS      1000u
#define RETRIES       30
#define GIVE_UP_MS    (10u * 60u * 1000u)
#define CLOSE_OK_MS   4000u
#define CLOSE_FAIL_MS 15000u

enum { OFF, WAITING, DUE, POSTED, DONE, FAILED };

static int   g_state = OFF;
static int   g_seenZero;
static int   g_retries;
static int   g_framed, g_closing;
static DWORD g_firstFrame, g_due, g_closeAt;
static HWND  g_wnd;
static char  g_line2[300];
static char  g_dir[MAX_PATH];

static int dll_dir(void)
{
    HMODULE self = NULL;
    char *slash;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&dll_dir, &self) || !GetModuleFileNameA(self, g_dir, sizeof g_dir))
        return 0;
    slash = strrchr(g_dir, '\\');
    if (!slash || (size_t)(slash - g_dir) + 40 >= sizeof g_dir) return 0;
    slash[1] = 0;
    return 1;
}

static void in_dir(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s%s", g_dir, name);
}

static int readable(uintptr_t at, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((const void *)at, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return at + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

static int ankh_datum(void)
{
    uintptr_t store;
    if (!readable(DATUM_STORE, 4)) return -1;
    store = *(const volatile uintptr_t *)DATUM_STORE;
    if (!store || !readable(store + ANKH_DATUM, 1)) return -1;
    return *(const volatile uint8_t *)(store + ANKH_DATUM);
}

static void write_result(int ok, const char *text)
{
    char path[MAX_PATH + 64], body[600];
    HANDLE h;
    DWORD put = 0;
    int n;
    in_dir(path, sizeof path, "newgame-snapshot.result");
    n = snprintf(body, sizeof body, "%s\r\n%s\r\n", ok ? "ok" : "failed", text);
    if (n < 0) return;
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { u9log_line("NEWGAME could not write %s (error %lu)", path, GetLastError()); return; }
    WriteFile(h, body, (DWORD)(n < (int)sizeof body ? n : (int)sizeof body - 1), &put, NULL);
    CloseHandle(h);
}

static void finish(int ok, const char *text)
{
    g_state = ok ? DONE : FAILED;
    snprintf(g_line2, sizeof g_line2, "%s", text);
    g_closeAt = u9time_now() + (ok ? CLOSE_OK_MS : CLOSE_FAIL_MS);
    g_closing = 1;
    write_result(ok, text);
    u9log_line("NEWGAME %s: %s", ok ? "done" : "FAILED", text);
}

void u9snapmode_install(void)
{
    char request[MAX_PATH + 64], start[MAX_PATH], *cut;
    if (!dll_dir()) return;
    in_dir(request, sizeof request, "newgame-snapshot.request");
    if (GetFileAttributesA(request) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA(request);

    g_state = WAITING;
    snprintf(g_line2, sizeof g_line2, "Escape skips the intro video. Then wait, and don't touch anything: the game closes itself.");
    u9log_line("NEWGAME snapshot mode: waiting for datum %d (the first hint ankh), then %u ms", ANKH_DATUM, WAIT_MS);

    if (!u9newgame_path(start, sizeof start) || !(cut = strstr(start, "static\\u9game0.sav"))) {
        finish(0, "The game folder could not be found.");
        return;
    }
    strcpy(cut, "savegame\\start.dat");
    if (GetFileAttributesA(start) != INVALID_FILE_ATTRIBUTES)
        finish(0, "savegame\\start.dat is there, so a saved game loads instead of a new one. Nothing was written.");
}

int u9snapmode_active(void) { return g_state != OFF; }

void u9snapmode_window(HWND w) { if (!g_wnd) g_wnd = w; }

int u9snapmode_message(UINT msg)
{
    char text[480], check[400];
    int r;
    if (msg != U9SNAPMODE_MSG) return 0;
    if (g_state != POSTED) return 1;
    r = u9newgame_write(0, text, sizeof text);
    if (r == U9NEWGAME_NOT_NOW && ++g_retries <= RETRIES) {
        u9log_line("NEWGAME not yet: %s - retry %d", text, g_retries);
        g_due = u9time_now() + RETRY_MS;
        g_state = DUE;
        return 1;
    }
    if (r != U9NEWGAME_OK) { finish(0, text); return 1; }
    u9log_line("NEWGAME %s", text);
    if (!u9newgame_verify(check, sizeof check)) {
        char path[MAX_PATH];
        if (u9newgame_path(path, sizeof path)) {
            SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(path);
        }
        snprintf(text, sizeof text, "%s. It was removed again.", check);
        finish(0, text);
        return 1;
    }
    finish(1, check);
    return 1;
}

static void draw_banner(int surfW, int surfH)
{
    const char *line1;
    float r = 1.0f, g = 0.85f, b = 0.35f;
    int cw, ch, n1, n2, w, x, y, h;

    switch (g_state) {
    case DONE:   line1 = "U9GL: new-game snapshot taken and checked. Closing the game..."; r = 0.55f; g = 0.95f; b = 0.55f; break;
    case FAILED: line1 = "U9GL could not make the new-game snapshot. Closing the game..."; r = 1.0f; g = 0.5f; b = 0.45f; break;
    case POSTED: line1 = "U9GL is taking the new-game snapshot..."; break;
    default:     line1 = "U9GL is preparing your new game."; break;
    }
    if (!u9metrics_text_init()) return;
    u9metrics_char_size(&cw, &ch);
    n1 = (int)strlen(line1);
    n2 = (int)strlen(g_line2);
    w = (n1 > n2 ? n1 : n2) * cw + 32;
    if (w > surfW - 16) w = surfW - 16;
    h = 2 * ch + 28;
    x = (surfW - w) / 2;
    y = surfH / 8;

    glViewport(0, 0, surfW, surfH);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0.0, (double)surfW, (double)surfH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST); glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.03f, 0.04f, 0.08f, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f((float)x, (float)y); glVertex2f((float)(x + w), (float)y);
    glVertex2f((float)(x + w), (float)(y + h)); glVertex2f((float)x, (float)(y + h));
    glEnd();
    glDisable(GL_BLEND);
    glColor3f(0.85f, 0.7f, 0.25f);
    glBegin(GL_LINE_LOOP);
    glVertex2f((float)x + 0.5f, (float)y + 0.5f); glVertex2f((float)(x + w) - 0.5f, (float)y + 0.5f);
    glVertex2f((float)(x + w) - 0.5f, (float)(y + h) - 0.5f); glVertex2f((float)x + 0.5f, (float)(y + h) - 0.5f);
    glEnd();
    glColor3f(r, g, b);
    u9metrics_text((float)(x + 16), (float)(y + 10 + ch), line1);
    glColor3f(0.9f, 0.9f, 0.9f);
    u9metrics_text((float)(x + 16), (float)(y + 18 + 2 * ch), g_line2);
    glColor3f(1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void u9snapmode_frame(int surfW, int surfH)
{
    DWORD now;
    int datum;
    GLboolean sciss;

    if (g_state == OFF) return;
    now = u9time_now();
    if (!g_framed) { g_framed = 1; g_firstFrame = now; }

    switch (g_state) {
    case WAITING:
        datum = ankh_datum();
        if (datum == 0) g_seenZero = 1;
        else if (datum > 0 && g_seenZero) {
            g_due = now + WAIT_MS;
            g_state = DUE;
            u9log_line("NEWGAME datum %d is %d: the snapshot in %u ms", ANKH_DATUM, datum, WAIT_MS);
        } else if (now - g_firstFrame > GIVE_UP_MS) {
            finish(0, "The opening did not reach the first hint ankh within 10 minutes. Nothing was written.");
        }
        break;
    case DUE:
        if ((LONG)(now - g_due) >= 0 && g_wnd) {
            g_state = POSTED;
            PostMessageA(g_wnd, U9SNAPMODE_MSG, 0, 0);
        }
        break;
    case DONE:
    case FAILED:
        if (g_closing && (LONG)(now - g_closeAt) >= 0 && g_wnd) {
            g_closing = 0;
            u9log_line("NEWGAME closing the game");
            PostMessageA(g_wnd, WM_CLOSE, 0, 0);
        }
        break;
    default:
        break;
    }

    if (surfW <= 0 || surfH <= 0) return;
    sciss = glIsEnabled(GL_SCISSOR_TEST);
    if (sciss) glDisable(GL_SCISSOR_TEST);
    draw_banner(surfW, surfH);
    if (sciss) glEnable(GL_SCISSOR_TEST);
}
