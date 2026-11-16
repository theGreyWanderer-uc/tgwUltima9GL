#ifndef U9GL_BACKFACE_H
#define U9GL_BACKFACE_H

void u9gl_back_face_install(void);
void u9gl_back_face_report(void);

extern int g_bfOn;
extern unsigned g_bfSent, g_bfSkipped;
int u9gl_back_face_installed(void);
void u9gl_back_face_held_report(void (*out)(const char *fmt, ...));

#endif
