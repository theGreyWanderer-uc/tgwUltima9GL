#ifndef U9_CONFIG_H
#define U9_CONFIG_H

#include <windows.h>

void u9cfg_load(HINSTANCE self);

int  u9cfg_int(const char *key, int def);
const char *u9cfg_str(const char *key, const char *def);

#endif
