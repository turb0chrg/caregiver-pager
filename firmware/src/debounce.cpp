#include "debounce.h"

// Slot lifetime is generous: long enough to span a whole burst plus the
// suppression window, short enough that a slot frees up well before the next
// plausible press.
static const uint32_t kSlotIdleMs = BURST_IDLE_MS + DEBOUNCE_WINDOW_MS;

// Returns an existing slot for this signature, or claims a fresh one. A freshly
// claimed slot is fully zeroed apart from its signature, so `repeats` is 0 and
// `everFired` is false; offer() treats that as the start of a new burst.
Debouncer::Slot* Debouncer::findOrClaim(uint64_t signature, uint32_t nowMs) {
    Slot* freeSlot = nullptr;
    Slot* oldest   = nullptr;

    for (uint8_t i = 0; i < kSlots; i++) {
        Slot& s = slots_[i];
        if (s.inUse && s.signature == signature) return &s;
        if (!s.inUse) {
            if (freeSlot == nullptr) freeSlot = &s;
            continue;
        }
        if (oldest == nullptr || (int32_t)(s.lastFrameMs - oldest->lastFrameMs) < 0) {
            oldest = &s;
        }
    }

    // With 8 slots and a handful of real buttons, eviction should never happen
    // in the field. If it does, the least recently active slot is the safest
    // thing to lose: an evicted live burst restarts its repeat count and
    // reaches the threshold again within the same burst, so the worst outcome
    // is a slightly delayed incident, never a suppressed one.
    Slot* s = (freeSlot != nullptr) ? freeSlot : oldest;
    if (s == nullptr) return nullptr;

    *s = Slot();
    s->signature   = signature;
    s->inUse       = true;
    s->lastFrameMs = nowMs;
    return s;
}

bool Debouncer::offer(uint64_t signature, uint32_t nowMs) {
    Slot* s = findOrClaim(signature, nowMs);
    if (s == nullptr) return false;

    // A freshly claimed slot has repeats == 0 and lastFrameMs == nowMs, so it
    // falls into the same-burst branch and lands on repeats == 1. That is the
    // intended reading: this frame is the first copy of a new press.
    const bool sameBurst = (uint32_t)(nowMs - s->lastFrameMs) <= BURST_IDLE_MS;
    s->lastFrameMs = nowMs;

    if (sameBurst) {
        if (s->repeats < 0xFF) s->repeats++;
    } else {
        // A gap this long means the previous burst is over. Start counting the
        // new press from one.
        s->repeats = 1;
    }

    // Rule 3: require N good copies of the frame before firing at all.
    if (s->repeats < REPEAT_THRESHOLD) return false;

    // FR-A5: collapse the rest of the burst, and any immediate re-press, into
    // the incident already sent.
    if (s->everFired && (uint32_t)(nowMs - s->lastFiredMs) < DEBOUNCE_WINDOW_MS) {
        return false;
    }

    s->everFired   = true;
    s->lastFiredMs = nowMs;
    return true;
}

void Debouncer::prune(uint32_t nowMs) {
    for (uint8_t i = 0; i < kSlots; i++) {
        Slot& s = slots_[i];
        if (!s.inUse) continue;
        if ((uint32_t)(nowMs - s.lastFrameMs) > kSlotIdleMs) {
            s = Slot();
        }
    }
}
