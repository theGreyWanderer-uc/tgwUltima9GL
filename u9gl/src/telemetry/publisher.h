#ifndef U9GL_TELEMETRY_PUBLISHER_H
#define U9GL_TELEMETRY_PUBLISHER_H
#include <stdint.h>
void u9telemetry_install(void);
void u9telemetry_frame_end(void);
uint32_t u9telemetry_requested_groups(void);
#endif
