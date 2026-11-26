#ifndef U9_NEWGAME_H
#define U9_NEWGAME_H

#include <stddef.h>

void u9newgame_command(const char *arg, void (*out)(const char *fmt, ...));

#define U9NEWGAME_OK      0
#define U9NEWGAME_NOT_NOW 1
#define U9NEWGAME_FAILED  2

int u9newgame_path(char *out, size_t cap);

int u9newgame_write(int replace, char *msg, size_t cap);

int u9newgame_verify(char *msg, size_t cap);

#endif
