#ifndef U9_PLAYERSET_H
#define U9_PLAYERSET_H

void u9playerset_get_stat(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_set_stat(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_get_skill(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_set_skill(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_get_effect(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_set_effect(const char *arg, void (*out)(const char *fmt, ...));
void u9playerset_unset_effect(const char *arg, void (*out)(const char *fmt, ...));
const char *u9playerset_effect_name(int i);

#endif
