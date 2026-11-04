#ifndef U9GL_GLBLOOM_H
#define U9GL_GLBLOOM_H

#define U9BLOOM_LEVELS 5

unsigned u9bloom_run(unsigned src, int w, int h, int scale);

void u9bloom_release(void);

#endif
