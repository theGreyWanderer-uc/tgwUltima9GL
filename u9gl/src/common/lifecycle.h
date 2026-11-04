#ifndef U9_LIFECYCLE_H
#define U9_LIFECYCLE_H

typedef void (*U9RemoveFn)(void);

void u9life_push(const char *name, U9RemoveFn remove);

void u9life_push_at_exit(const char *name, U9RemoveFn remove);

void u9life_unwind(void);

void u9life_unwind_at_exit(void);

unsigned u9life_depth(void);

#endif
