#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
#  error "MSVC rejects __thiscall on free C functions (C2061). Build this with MinGW i686; see README.md."
#endif

#define TC __attribute__((thiscall))

typedef struct R R;

typedef struct {
    void          (TC *a0)(R *);
    void          (TC *a1)(R *, unsigned);
    void          (TC *a2)(R *, unsigned, unsigned);
    void          (TC *a3)(R *, unsigned, unsigned, unsigned);
    void          (TC *a4)(R *, const void *, const void *, const void *, float);
    unsigned char (TC *pred)(R *, const void *, const void *, const void *, unsigned);
} Vtbl;

struct R {
    const Vtbl *vtbl;
    unsigned    f[8];
    float       alpha;
};

static void TC impl_a0(R *t) { t->f[0] = 0xA0u; }
static void TC impl_a1(R *t, unsigned a) { t->f[1] = a; }
static void TC impl_a2(R *t, unsigned a, unsigned b) { t->f[2] = a; t->f[3] = b; }
static void TC impl_a3(R *t, unsigned a, unsigned b, unsigned c)
{ t->f[4] = a; t->f[5] = b; t->f[6] = c; }

static void TC impl_a4(R *t, const void *v0, const void *v1, const void *v2, float alpha)
{ t->f[7] = (v0 && v1 && v2) ? 0xD3u : 0u; t->alpha = alpha; }

static unsigned char TC impl_pred(R *t, const void *v0, const void *v1,
                                  const void *v2, unsigned flags)
{ (void)t; return (v0 && v1 && v2 && flags == 0x2Eu) ? 0x21u : 0u; }

static const Vtbl g_vtbl = { impl_a0, impl_a1, impl_a2, impl_a3, impl_a4, impl_pred };

static int fails;
static void check(int cond, const char *what)
{
    printf("  %-46s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    R obj;
    unsigned esp_before = 0, esp_after = 0;
    unsigned char pred;

    memset(&obj, 0, sizeof obj);
    obj.vtbl = &g_vtbl;

    printf("tgwUltimaGL __thiscall ABI test\n");

    __asm__ volatile ("mov %%esp,%0" : "=r"(esp_before));

    obj.vtbl->a0(&obj);
    obj.vtbl->a1(&obj, 0xA1u);
    obj.vtbl->a2(&obj, 0xA2u, 0xA3u);
    obj.vtbl->a3(&obj, 1u, 2u, 3u);
    obj.vtbl->a4(&obj, &obj, &obj, &obj, -1.0f);
    pred = obj.vtbl->pred(&obj, &obj, &obj, &obj, 0x2Eu);

    __asm__ volatile ("mov %%esp,%0" : "=r"(esp_after));

    check(sizeof(void *) == 4,            "pointer size is 4 (32-bit build)");
    check(sizeof(Vtbl)   == 6 * 4,        "vtable entries are 4 bytes each");
    check(obj.f[0] == 0xA0u,              "arity 0 dispatched");
    check(obj.f[1] == 0xA1u,              "arity 1 argument intact");
    check(obj.f[2] == 0xA2u && obj.f[3] == 0xA3u,
                                          "arity 2 arguments intact and ordered");
    check(obj.f[4] == 1u && obj.f[5] == 2u && obj.f[6] == 3u,
                                          "arity 3 arguments intact and ordered");
    check(obj.f[7] == 0xD3u,              "arity 4 pointer arguments intact");
    check(obj.alpha == -1.0f,             "float sentinel -1.0f survives");
    check(pred == 0x21u,                  "byte return value intact");
    check(esp_before == esp_after,        "stack balanced after mixed-arity calls");

    if (fails) {
        printf("\n%d check(s) FAILED - do not build the backend with this toolchain.\n", fails);
        printf("See tests/abi/README.md.\n");
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
