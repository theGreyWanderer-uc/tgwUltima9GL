#include <windows.h>
#include <stddef.h>
#include "texdecode.h"

static void *tex_malloc(size_t n)
{
    return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
}
static void *tex_realloc(void *p, size_t n)
{
    if (!p) return tex_malloc(n);
    return HeapReAlloc(GetProcessHeap(), 0, p, n ? n : 1);
}
static void tex_free(void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

#define STBI_MALLOC(n)        tex_malloc(n)
#define STBI_REALLOC(p, n)    tex_realloc(p, n)
#define STBI_FREE(p)          tex_free(p)
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "../../third_party/stb_image.h"
#pragma GCC diagnostic pop

unsigned char *u9tex_decode_file(const char *path, unsigned *w, unsigned *h,
                                 const char **why)
{
    int x = 0, y = 0, comp = 0;
    unsigned char *px = stbi_load(path, &x, &y, &comp, 4);
    if (!px) {
        if (why) *why = stbi_failure_reason();
        return NULL;
    }
    *w = (unsigned)x;
    *h = (unsigned)y;
    return px;
}
