// incident.h — one alert event (REQUIREMENTS.md §3, FR-A4, FR-C9).

#pragma once

#include <stdint.h>

struct Incident {
    // Client-generated, stable across retries. The backend keys idempotency on
    // this (FR-C9), so it must NOT be regenerated when a queued incident is
    // retried, or one press becomes several alerts.
    char incidentId[37];

    char     buttonId[24];
    char     label[40];
    char     signatureHex[17];

    char     radioTag[4];   // which receiver heard it first, for diagnostics
    int      rssiDbm;

    uint32_t createdAtMs;   // millis() at decode, for queue TTL
    uint64_t createdAtUnix; // 0 if NTP has not synced yet

    uint8_t  attempts;
    uint32_t nextAttemptMs;
};

// Fills `out` with a fresh RFC-4122-shaped v4 identifier. Buffer must hold 37.
void incidentGenerateId(char* out);
