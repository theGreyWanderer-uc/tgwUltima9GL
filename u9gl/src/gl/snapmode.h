#ifndef U9GL_SNAPMODE_H
#define U9GL_SNAPMODE_H

#include <windows.h>

#define U9SNAPMODE_MSG (WM_APP + 0x39)

void u9snapmode_install(void);

int  u9snapmode_active(void);

void u9snapmode_window(HWND w);

int  u9snapmode_message(UINT msg);

void u9snapmode_frame(int surfW, int surfH);

#endif
