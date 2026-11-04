#include <windows.h>
#include "lifecycle.h"

void u9log_line(const char *fmt, ...);

#define U9LIFE_MAX 32

static struct { const char *name; U9RemoveFn remove; int atExit; } g_stack[U9LIFE_MAX];
static unsigned g_depth;

static void push(const char *name, U9RemoveFn remove, int atExit)
{
    if (!remove) return;
    if (g_depth >= U9LIFE_MAX) {
        u9log_line("LIFECYCLE full (%u) - '%s' will NOT be removed at exit",
                   U9LIFE_MAX, name ? name : "?");
        return;
    }
    g_stack[g_depth].name = name ? name : "?";
    g_stack[g_depth].remove = remove;
    g_stack[g_depth].atExit = atExit;
    g_depth++;
}

void u9life_push(const char *name, U9RemoveFn remove)         { push(name, remove, 0); }
void u9life_push_at_exit(const char *name, U9RemoveFn remove) { push(name, remove, 1); }

void u9life_unwind_at_exit(void)
{
    while (g_depth) {
        U9RemoveFn fn = g_stack[--g_depth].remove;
        const char *nm = g_stack[g_depth].name;
        int atExit = g_stack[g_depth].atExit;
        g_stack[g_depth].remove = 0;
        g_stack[g_depth].name = 0;
        if (!atExit) { u9log_line("  exit %u: %s - skipped, the process is ending", g_depth, nm); continue; }
        u9log_line("  exit %u: %s", g_depth, nm);
        fn();
    }
}

void u9life_unwind(void)
{
    while (g_depth) {
        U9RemoveFn fn = g_stack[--g_depth].remove;
        const char *nm = g_stack[g_depth].name;
        g_stack[g_depth].remove = 0;
        g_stack[g_depth].name = 0;
        u9log_line("  unwind %u: %s", g_depth, nm);
        fn();
    }
}

unsigned u9life_depth(void) { return g_depth; }
