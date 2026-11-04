#ifndef U9_TIME_H
#define U9_TIME_H

#include <windows.h>

DWORD u9time_now(void);

DWORD u9time_engine(void);

void u9time_set_engine_base(DWORD base);

void u9time_init(void);

#endif
