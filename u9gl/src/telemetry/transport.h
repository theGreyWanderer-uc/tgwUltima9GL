#ifndef U9GL_TELEMETRY_TRANSPORT_H
#define U9GL_TELEMETRY_TRANSPORT_H
#include <windows.h>
#include "../../include/u9telemetry.h"

typedef struct U9TelemetryTransport {
    HANDLE mapping, request_mapping;
    unsigned char *view;
    volatile U9TelemetryRequest *request;
    U9TelemetryHeader identity;
    uint64_t published, publish_ticks, publish_max_ticks;
    uint32_t request_beat;
    uint64_t request_beat_qpc;
    char name[128], request_name[128];
} U9TelemetryTransport;

int u9telemetry_transport_open(U9TelemetryTransport *t, unsigned kind, const char *version);
int u9telemetry_transport_publish(U9TelemetryTransport *t, U9TelemetryHeader *record);
uint32_t u9telemetry_transport_requests(U9TelemetryTransport *t, uint64_t now_qpc);
void u9telemetry_transport_close(U9TelemetryTransport *t);
#endif
