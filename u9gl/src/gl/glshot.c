#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../diag/log.h"
#include "../injection/config.h"
#include "glshot.h"

#ifndef GL_BGR_EXT
#define GL_BGR_EXT 0x80E0
#endif
#define GL_COLOR_ATTACHMENT0_ 0x8CE0

int u9glpost_frame(int *w, int *h);

static volatile int g_pending;
static char g_enginePath[MAX_PATH];
static unsigned g_shots;

int u9shot_request(const char *enginePath)
{
    int mode = u9cfg_int("gl_screenshot", 0);
    if (mode < 1 || mode > 4) return 0;
    if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
        u9log_line("GLSHOT ignored a screenshot request without Ctrl held (Print Screen is left to Windows)");
        return 1;
    }
    lstrcpynA(g_enginePath, enginePath ? enginePath : "u9-0.tga", sizeof g_enginePath);
    g_pending = mode;
    return 1;
}

int u9shot_pending(void)
{
    return g_pending > 0;
}

static int write_file(const char *path, const void *a, DWORD na, const void *b, DWORD nb)
{
    DWORD wrote;
    int ok;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, a, na, &wrote, NULL) && wrote == na;
    if (ok && nb) ok = WriteFile(f, b, nb, &wrote, NULL) && wrote == nb;
    CloseHandle(f);
    if (!ok) DeleteFileA(path);
    return ok;
}

static int write_tga(const char *path, const unsigned char *bgr, int w, int h)
{
    unsigned char hdr[18];
    memset(hdr, 0, sizeof hdr);
    hdr[2] = 2;
    hdr[12] = (unsigned char)w; hdr[13] = (unsigned char)(w >> 8);
    hdr[14] = (unsigned char)h; hdr[15] = (unsigned char)(h >> 8);
    hdr[16] = 24;
    return write_file(path, hdr, sizeof hdr, bgr, (DWORD)w * (DWORD)h * 3u);
}

typedef struct {
    unsigned char *p;
    size_t n, cap;
    unsigned bits;
    int nbits, ok;
} Out;

static void out_byte(Out *o, unsigned char b)
{
    if (o->n == o->cap) {
        size_t cap = o->cap ? o->cap * 2 : 65536;
        unsigned char *q = (unsigned char *)realloc(o->p, cap);
        if (!q) { o->ok = 0; return; }
        o->p = q;
        o->cap = cap;
    }
    o->p[o->n++] = b;
}

static void out_bytes(Out *o, const void *src, size_t n)
{
    const unsigned char *s = (const unsigned char *)src;
    while (n-- && o->ok) out_byte(o, *s++);
}

static void out_be32(Out *o, unsigned v)
{
    out_byte(o, (unsigned char)(v >> 24)); out_byte(o, (unsigned char)(v >> 16));
    out_byte(o, (unsigned char)(v >> 8));  out_byte(o, (unsigned char)v);
}

static void put_bits(Out *o, unsigned v, int n)
{
    o->bits |= v << o->nbits;
    o->nbits += n;
    while (o->nbits >= 8) {
        out_byte(o, (unsigned char)o->bits);
        o->bits >>= 8;
        o->nbits -= 8;
    }
}

static void put_code(Out *o, unsigned code, int len)
{
    unsigned r = 0;
    int i;
    for (i = 0; i < len; i++) { r = (r << 1) | (code & 1u); code >>= 1; }
    put_bits(o, r, len);
}

static void put_lit(Out *o, int sym)
{
    if (sym < 144)      put_code(o, 0x30u + (unsigned)sym, 8);
    else if (sym < 256) put_code(o, 0x190u + (unsigned)(sym - 144), 9);
    else if (sym < 280) put_code(o, (unsigned)(sym - 256), 7);
    else                put_code(o, 0xC0u + (unsigned)(sym - 280), 8);
}

static const unsigned short k_lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char  k_lext[29]  = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short k_dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                            8193, 12289, 16385, 24577 };
static const unsigned char  k_dext[30]  = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void put_match(Out *o, int len, int dist)
{
    int i = 28, j = 29;
    while (k_lbase[i] > len) i--;
    put_lit(o, 257 + i);
    put_bits(o, (unsigned)(len - k_lbase[i]), k_lext[i]);
    while (k_dbase[j] > dist) j--;
    put_code(o, (unsigned)j, 5);
    put_bits(o, (unsigned)(dist - k_dbase[j]), k_dext[j]);
}

#define WSIZE    32768
#define HBITS    16
#define MAXCHAIN 16

static unsigned hash3(const unsigned char *p)
{
    return ((unsigned)p[0] * 506832829u ^ (unsigned)p[1] * 2654435761u ^ (unsigned)p[2] * 40503u) >> (32 - HBITS);
}

static void deflate_zlib(Out *o, const unsigned char *d, size_t n)
{
    int *head = (int *)malloc(sizeof(int) << HBITS), *prev = (int *)malloc(sizeof(int) * WSIZE);
    size_t i = 0, k;
    unsigned a = 1, b = 0;
    if (!head || !prev) { free(head); free(prev); o->ok = 0; return; }
    memset(head, 0xFF, sizeof(int) << HBITS);
    out_byte(o, 0x78);
    out_byte(o, 0x01);
    put_bits(o, 1, 1);
    put_bits(o, 1, 2);
    while (i < n && o->ok) {
        int best = 0, bestDist = 0;
        if (i + 2 < n) {
            unsigned h = hash3(d + i);
            int cand = head[h], chain = MAXCHAIN;
            size_t lim = n - i < 258 ? n - i : 258;
            while (cand >= 0 && chain-- > 0 && (int)i - cand <= WSIZE - 1) {
                const unsigned char *p = d + cand, *q = d + i;
                size_t l = 0;
                while (l < lim && p[l] == q[l]) l++;
                if ((int)l > best) { best = (int)l; bestDist = (int)i - cand; if (l == lim) break; }
                {
                    int next = prev[cand & (WSIZE - 1)];
                    if (next >= cand) break;
                    cand = next;
                }
            }
            prev[i & (WSIZE - 1)] = head[h];
            head[h] = (int)i;
        }
        if (best >= 3) {
            put_match(o, best, bestDist);
            for (k = 1; k < (size_t)best; k++) {
                size_t at = i + k;
                if (at + 2 < n) {
                    unsigned h = hash3(d + at);
                    prev[at & (WSIZE - 1)] = head[h];
                    head[h] = (int)at;
                }
            }
            i += (size_t)best;
        } else {
            put_lit(o, d[i]);
            i++;
        }
    }
    put_lit(o, 256);
    if (o->nbits) put_bits(o, 0, 8 - o->nbits);
    for (k = 0; k < n; k++) {
        a = (a + d[k]) % 65521u;
        b = (b + a) % 65521u;
    }
    out_be32(o, (b << 16) | a);
    free(head);
    free(prev);
}

static unsigned g_crcTable[256];

static unsigned crc32_of(const unsigned char *p, size_t n)
{
    unsigned c = 0xFFFFFFFFu;
    size_t i;
    if (!g_crcTable[1]) {
        unsigned k, v;
        int j;
        for (k = 0; k < 256; k++) {
            v = k;
            for (j = 0; j < 8; j++) v = (v & 1u) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            g_crcTable[k] = v;
        }
    }
    for (i = 0; i < n; i++) c = g_crcTable[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void out_chunk(Out *o, const char *type, const unsigned char *data, size_t n)
{
    size_t start;
    out_be32(o, (unsigned)n);
    start = o->n;
    out_bytes(o, type, 4);
    if (n) out_bytes(o, data, n);
    if (o->ok) out_be32(o, crc32_of(o->p + start, n + 4));
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static int write_png(const char *path, const unsigned char *rgb, int w, int h)
{
    size_t stride = (size_t)w * 3u, rows = (size_t)h, y, x;
    unsigned char *raw = (unsigned char *)malloc((stride + 1u) * rows), *cand = (unsigned char *)malloc(stride * 5u);
    unsigned char ihdr[13];
    Out z = { 0 }, f = { 0 };
    int ok = 0;
    z.ok = f.ok = 1;
    if (!raw || !cand) goto done;
    for (y = 0; y < rows; y++) {
        const unsigned char *cur = rgb + (rows - 1u - y) * stride;
        const unsigned char *up = y ? rgb + (rows - y) * stride : NULL;
        unsigned best = 0xFFFFFFFFu;
        int t, pick = 0;
        for (t = 0; t < 5; t++) {
            unsigned char *o = cand + (size_t)t * stride;
            unsigned sum = 0;
            for (x = 0; x < stride; x++) {
                int a = x >= 3 ? cur[x - 3] : 0, b = up ? up[x] : 0, c = (x >= 3 && up) ? up[x - 3] : 0;
                int pred = t == 0 ? 0 : t == 1 ? a : t == 2 ? b : t == 3 ? (a + b) / 2 : paeth(a, b, c);
                o[x] = (unsigned char)(cur[x] - pred);
                sum += (unsigned)abs((int)(signed char)o[x]);
            }
            if (sum < best) { best = sum; pick = t; }
        }
        raw[y * (stride + 1u)] = (unsigned char)pick;
        memcpy(raw + y * (stride + 1u) + 1u, cand + (size_t)pick * stride, stride);
    }
    deflate_zlib(&z, raw, (stride + 1u) * rows);
    if (!z.ok) goto done;
    ihdr[0] = (unsigned char)(w >> 24); ihdr[1] = (unsigned char)(w >> 16);
    ihdr[2] = (unsigned char)(w >> 8);  ihdr[3] = (unsigned char)w;
    ihdr[4] = (unsigned char)(h >> 24); ihdr[5] = (unsigned char)(h >> 16);
    ihdr[6] = (unsigned char)(h >> 8);  ihdr[7] = (unsigned char)h;
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    out_bytes(&f, "\x89PNG\r\n\x1a\n", 8);
    out_chunk(&f, "IHDR", ihdr, sizeof ihdr);
    out_chunk(&f, "IDAT", z.p, z.n);
    out_chunk(&f, "IEND", NULL, 0);
    if (f.ok) ok = write_file(path, f.p, (DWORD)f.n, NULL, 0);
done:
    free(raw);
    free(cand);
    free(z.p);
    free(f.p);
    return ok;
}

static int g_number = -1;

static void png_name(char *out, size_t cap, int n, int before)
{
    _snprintf(out, cap, "u9-%d-%s.png", n, before ? "before" : "after");
    out[cap - 1] = 0;
}

static int free_number(void)
{
    char a[MAX_PATH], b[MAX_PATH];
    int n;
    for (n = 0; n < 100000; n++) {
        png_name(a, sizeof a, n, 1);
        png_name(b, sizeof b, n, 0);
        if (GetFileAttributesA(a) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(b) == INVALID_FILE_ATTRIBUTES)
            return n;
    }
    return 99999;
}

enum { SHOT_BEFORE, SHOT_AFTER, SHOT_TGA };

static void take(int what, int fromFrame, int w, int h, const char *note)
{
    unsigned char *px;
    char path[MAX_PATH], full[MAX_PATH];
    GLint readBuf = 0, align = 4;
    int ok;
    DWORD t0 = GetTickCount();
    if (w <= 0 || h <= 0) return;
    px = (unsigned char *)malloc((size_t)w * (size_t)h * 3u);
    if (!px) { u9log_line("GLSHOT no memory for %dx%d", w, h); return; }
    glGetIntegerv(GL_READ_BUFFER, &readBuf);
    glGetIntegerv(GL_PACK_ALIGNMENT, &align);
    glReadBuffer(fromFrame ? GL_COLOR_ATTACHMENT0_ : GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, what == SHOT_TGA ? GL_BGR_EXT : GL_RGB, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_PACK_ALIGNMENT, align);
    glReadBuffer((GLenum)readBuf);
    if (what == SHOT_TGA) {
        lstrcpynA(path, g_enginePath, sizeof path);
        ok = write_tga(path, px, w, h);
    } else {
        if (g_number < 0) g_number = free_number();
        png_name(path, sizeof path, g_number, what == SHOT_BEFORE);
        ok = write_png(path, px, w, h);
    }
    free(px);
    if (!GetFullPathNameA(path, sizeof full, full, NULL)) lstrcpynA(full, path, sizeof full);
    g_shots++;
    u9log_line("GLSHOT %s %s -> %s (%dx%d, %s, %lu ms)%s", ok ? "wrote" : "FAILED to write",
               what == SHOT_BEFORE ? "the game's picture before the post pass" : "the finished picture", full, w, h,
               what == SHOT_TGA ? "TGA" : "PNG", (unsigned long)(GetTickCount() - t0), note);
}

void u9shot_before_post(void)
{
    int w, h, mode = g_pending;
    if ((mode != 1 && mode != 4) || !u9glpost_frame(&w, &h)) return;
    g_number = -1;
    take(SHOT_BEFORE, 1, w, h, "");
    if (mode == 1) g_pending = 0;
    else g_pending = -4;
}

void u9shot_after_post(int surfW, int surfH)
{
    static const char sameNote[] = " - no post pass running: before and after are the same picture";
    int mode = g_pending;
    if (!mode) return;
    g_pending = 0;
    if (mode != -4) g_number = -1;
    switch (mode) {
    case 1:  take(SHOT_BEFORE, 0, surfW, surfH, sameNote); break;
    case 2:  take(SHOT_AFTER, 0, surfW, surfH, ""); break;
    case 3:  take(SHOT_TGA, 0, surfW, surfH, ""); break;
    case 4:  take(SHOT_BEFORE, 0, surfW, surfH, sameNote); take(SHOT_AFTER, 0, surfW, surfH, sameNote); break;
    case -4: take(SHOT_AFTER, 0, surfW, surfH, ""); break;
    default: break;
    }
}
