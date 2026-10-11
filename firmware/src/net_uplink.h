// net_uplink.h — Wi-Fi, time sync, and the signed incident POST (FR-A4, FR-A6,
// FR-A7, SEC-2, CN-1, CN-6).
//
// Incidents are queued first and sent second, so a press is never lost to a
// network stall. The queue is drained from the main loop with exponential
// backoff; entries expire after QUEUE_TTL_MS.

#pragma once

#include <stdint.h>

#include "incident.h"

namespace uplink {

void begin();

// Non-blocking. Call every loop iteration.
void service();

// Enqueue for delivery. Returns false only if the queue is full, in which case
// the oldest entry has already been evicted to make room.
bool enqueue(const Incident& inc);

// FR-A7. Sent on a timer from service(); exposed for the startup heartbeat.
void sendHeartbeat();

bool     online();
uint8_t  queueDepth();
uint64_t nowUnix();

}  // namespace uplink
