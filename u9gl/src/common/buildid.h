#ifndef U9_BUILDID_H
#define U9_BUILDID_H

typedef enum {
    U9BUILD_UNKNOWN = 0,
    U9BUILD_1_19F
} U9BuildId;

U9BuildId u9build_detect(void);

const char *u9build_name(void);

int u9build_is_exact(void);

const char *u9build_mismatch(void);

#endif
