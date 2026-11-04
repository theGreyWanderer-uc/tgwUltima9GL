#ifndef U9_LOG_H
#define U9_LOG_H

#include <stdint.h>

void u9log_open(const char *path);
void u9log_close(void);

void u9log_line(const char *fmt, ...);

void u9log_flush(void);

uint32_t u9log_frame(void);
void     u9log_frame_advance(void);

int u9log_want_detail(uint32_t seq);

#endif
