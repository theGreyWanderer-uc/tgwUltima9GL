#ifndef U9GL_CONSOLE_H
#define U9GL_CONSOLE_H

void u9console_install(void);

void u9console_draw(int surfW, int surfH);

void u9console_notice(const char *fmt, ...);

void u9selfsample_after(unsigned seconds, void (*out)(const char *fmt, ...));
void u9selfsample_poll(void);

#endif
