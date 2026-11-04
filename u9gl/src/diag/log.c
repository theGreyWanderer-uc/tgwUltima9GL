#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "log.h"

#define DETAIL_FRAMES   10u
#define DETAIL_CALLS     8u
#define MAX_BYTES       (16u * 1024u * 1024u)
#define BUF_BYTES       (64u * 1024u)

static HANDLE   g_file  = INVALID_HANDLE_VALUE;
static char     g_buf[BUF_BYTES];
static unsigned g_used;
static unsigned g_written;
static uint32_t g_frame;
static int      g_capped;

static void raw_write(const char *p, unsigned n)
{
    DWORD done;
    if (g_file == INVALID_HANDLE_VALUE) return;
    WriteFile(g_file, p, n, &done, NULL);
}

void u9log_flush(void)
{
    if (g_used) { raw_write(g_buf, g_used); g_used = 0; }
}

void u9log_open(const char *path)
{
    char tmp[MAX_PATH];
    if (!path) {
        DWORD n = GetTempPathA(sizeof tmp - 16, tmp);
        if (n == 0 || n > sizeof tmp - 16) return;
        strcat(tmp, "u9log.txt");
        path = tmp;
    }
    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    g_used = g_written = 0;
    g_frame = 0;
    g_capped = 0;
}

void u9log_close(void)
{
    u9log_flush();
    if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
}

void u9log_line(const char *fmt, ...)
{
    char line[512];
    int  n;
    va_list ap;

    if (g_capped || g_file == INVALID_HANDLE_VALUE) return;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';

    if (g_written + n > MAX_BYTES) {
        g_capped = 1;
        u9log_flush();
        raw_write("-- log cap reached, stopping --\n", 32);
        return;
    }
    if (g_used + (unsigned)n > BUF_BYTES) u9log_flush();
    memcpy(g_buf + g_used, line, (unsigned)n);
    g_used    += (unsigned)n;
    g_written += (unsigned)n;
}

uint32_t u9log_frame(void)      { return g_frame; }
void     u9log_frame_advance(void) { g_frame++; }

int u9log_want_detail(uint32_t seq)
{
    return !g_capped && g_frame < DETAIL_FRAMES && seq < DETAIL_CALLS;
}
