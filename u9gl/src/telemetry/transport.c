#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "transport.h"
#include "../common/perfclock.h"

static void open_requests(U9TelemetryTransport *t, uint64_t birth)
{
    U9TelemetryRequest *block;
    DWORD error;
    snprintf(t->request_name, sizeof t->request_name, "Local\\U9GL.TelemetryRequest.v2.%lu.%016" PRIx64,
             (unsigned long)t->identity.producer_pid, birth);
    t->request_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                            0, U9TM_REQUEST_BYTES, t->request_name);
    error = GetLastError();
    if (!t->request_mapping) return;
    if (error == ERROR_ALREADY_EXISTS) { CloseHandle(t->request_mapping); t->request_mapping = NULL; return; }
    block = (U9TelemetryRequest *)MapViewOfFile(t->request_mapping, FILE_MAP_WRITE | FILE_MAP_READ,
                                                0, 0, U9TM_REQUEST_BYTES);
    if (!block) { CloseHandle(t->request_mapping); t->request_mapping = NULL; return; }
    memset(block, 0, U9TM_REQUEST_BYTES);
    block->major = U9TM_VERSION_MAJOR;
    block->minor = U9TM_VERSION_MINOR;
    block->bytes = U9TM_REQUEST_BYTES;
    __atomic_store_n(&block->magic, U9TM_REQUEST_MAGIC, __ATOMIC_RELEASE);
    t->request = block;
}

int u9telemetry_transport_open(U9TelemetryTransport *t, unsigned kind, const char *version)
{
    FILETIME created, exited, kernel, user;
    uint64_t birth, start, frequency;
    DWORD error;
    size_t length = version ? strlen(version) : 0, i;
    if (!t || t->mapping || !length || length > 31 ||
        (kind != U9TM_PRODUCER_GAME && kind != U9TM_PRODUCER_SYNTHETIC)) {
        SetLastError(ERROR_INVALID_PARAMETER); return 0;
    }
    for (i = 0; i < length; i++)
        if ((unsigned char)version[i] < 32 || (unsigned char)version[i] > 126) {
            SetLastError(ERROR_INVALID_PARAMETER); return 0;
        }
    frequency = u9perf_frequency(); start = u9perf_now();
    if (!frequency || !start || !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    birth = ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
    memset(t, 0, sizeof *t);
    t->identity.magic = U9TM_MAGIC;
    t->identity.major = U9TM_VERSION_MAJOR;
    t->identity.minor = U9TM_VERSION_MINOR;
    t->identity.header_bytes = U9TM_HEADER_BYTES;
    t->identity.entry_bytes = U9TM_ENTRY_BYTES;
    t->identity.mapping_bytes = U9TM_MAPPING_BYTES;
    t->identity.producer_pid = GetCurrentProcessId();
    t->identity.process_created_filetime = birth;
    t->identity.qpc_frequency = frequency;
    t->identity.session_start_qpc = start;
    t->identity.producer_kind = kind;
    memcpy(t->identity.producer_version, version, length);
    memcpy(t->identity.session_id, &birth, 8);
    memcpy(t->identity.session_id + 8, &start, 8);
    snprintf(t->name, sizeof t->name, "Local\\U9GL.Telemetry.v2.%lu.%016" PRIx64,
             (unsigned long)t->identity.producer_pid, birth);
    t->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                    0, U9TM_MAPPING_BYTES, t->name);
    error = GetLastError();
    if (!t->mapping) return 0;
    if (error == ERROR_ALREADY_EXISTS) {
        CloseHandle(t->mapping); t->mapping = NULL; SetLastError(error); return 0;
    }
    t->view = (unsigned char *)MapViewOfFile(t->mapping, FILE_MAP_WRITE | FILE_MAP_READ,
                                             0, 0, U9TM_MAPPING_BYTES);
    if (!t->view) {
        error = GetLastError(); CloseHandle(t->mapping); t->mapping = NULL;
        SetLastError(error); return 0;
    }
    memset(t->view, 0, U9TM_MAPPING_BYTES);
    open_requests(t, birth);
    return 1;
}

int u9telemetry_transport_publish(U9TelemetryTransport *t, U9TelemetryHeader *record)
{
    U9TelemetryHeader own;
    FILETIME utc;
    uint64_t began, elapsed;
    uint32_t size, *sequence;
    if (!t || !t->view || !record || t->published == UINT64_MAX) return 0;
    size = record->snapshot_bytes;
    if (size < U9TM_HEADER_BYTES || size > U9TM_MAPPING_BYTES || record->group_count > U9TM_MAX_GROUPS ||
        U9TM_HEADER_BYTES + (uint32_t)record->group_count * U9TM_ENTRY_BYTES > size) return 0;
    began = u9perf_now();
    own = *record;
    *record = t->identity;
    record->snapshot_bytes = size;
    record->group_count = own.group_count;
    record->requested_groups = own.requested_groups;
    record->sample_index = t->published + 1;
    record->sample_qpc = began;
    GetSystemTimeAsFileTime(&utc);
    record->sample_utc_filetime = ((uint64_t)utc.dwHighDateTime << 32) | utc.dwLowDateTime;
    sequence = (uint32_t *)(t->view + U9TM_SEQUENCE_OFF);
    __atomic_add_fetch(sequence, 1u, __ATOMIC_SEQ_CST);
    memcpy(t->view, record, U9TM_SEQUENCE_OFF);
#ifdef U9TM_TESTING
    { extern void u9telemetry_test_write_pause(void); u9telemetry_test_write_pause(); }
#endif
    memcpy(t->view + U9TM_SEQUENCE_OFF + 4, (const unsigned char *)record + U9TM_SEQUENCE_OFF + 4,
           size - U9TM_SEQUENCE_OFF - 4);
    __atomic_add_fetch(sequence, 1u, __ATOMIC_SEQ_CST);
    t->published++;
    elapsed = u9perf_now() - began;
    t->publish_ticks += elapsed;
    if (elapsed > t->publish_max_ticks) t->publish_max_ticks = elapsed;
    return 1;
}

uint32_t u9telemetry_transport_requests(U9TelemetryTransport *t, uint64_t now_qpc)
{
    uint32_t beat, mask;
    uint64_t expiry;
    if (!t || !t->request) return 0;
    beat = __atomic_load_n(&t->request->heartbeat, __ATOMIC_ACQUIRE);
    mask = __atomic_load_n(&t->request->mask, __ATOMIC_ACQUIRE);
    if (beat != t->request_beat) { t->request_beat = beat; t->request_beat_qpc = now_qpc; }
    if (!t->request_beat_qpc) return 0;
    expiry = t->identity.qpc_frequency * U9TM_REQUEST_EXPIRY_MS / 1000u;
    return now_qpc - t->request_beat_qpc < expiry ? mask : 0;
}

void u9telemetry_transport_close(U9TelemetryTransport *t)
{
    if (t->request) UnmapViewOfFile((LPCVOID)t->request);
    if (t->request_mapping) CloseHandle(t->request_mapping);
    if (t->view) UnmapViewOfFile(t->view);
    if (t->mapping) CloseHandle(t->mapping);
    t->request = NULL; t->request_mapping = NULL;
    t->view = NULL; t->mapping = NULL;
}
