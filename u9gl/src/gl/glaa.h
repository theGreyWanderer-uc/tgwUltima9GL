#ifndef U9GL_GLAA_H
#define U9GL_GLAA_H

void u9aa_init(void);

int u9aa_scale(void);

void u9aa_scale_refused(const char *why);

int u9aa_begin(int w, int h, int scale);

void u9aa_end(int w, int h);

unsigned u9aa_fbo(void);

void u9aa_taa_jitter(int vw, int vh, float out[2]);

void u9aa_command(const char *arg, void (*out)(const char *fmt, ...));
void u9aa_taa_command(const char *arg, void (*out)(const char *fmt, ...));
void u9aa_ss_command(const char *arg, void (*out)(const char *fmt, ...));
void u9aa_report(void);

void u9aa_release(void);

#endif
