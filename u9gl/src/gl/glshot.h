#ifndef U9GL_GLSHOT_H
#define U9GL_GLSHOT_H

int  u9shot_request(const char *enginePath);
int  u9shot_pending(void);
void u9shot_before_post(void);
void u9shot_after_post(int surfW, int surfH);

#endif
