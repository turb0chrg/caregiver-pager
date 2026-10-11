// debounce.h — burst collapse and repeat threshold (decode rule 3, FR-A5).
//
// This is also the layer where the two radios merge. Both decoders feed the
// same Debouncer, keyed on the frame signature rather than on the radio, so a
// press heard by radio A and radio B raises exactly one incident.

#pragma once

#include <stdint.h>

#include "config.h"

class Debouncer {
public:
    // Returns true when this frame should raise a new incident. Returns false
    // for frames that are below the repeat threshold, or that belong to a
    // burst already reported.
    bool offer(uint64_t signature, uint32_t nowMs);

    // Drop slots that have been idle long enough that they cannot belong to a
    // live burst. Call from the main loop.
    void prune(uint32_t nowMs);

private:
    static const uint8_t kSlots = 8;

    struct Slot {
        uint64_t signature    = 0;
        uint32_t lastFrameMs  = 0;
        uint32_t lastFiredMs  = 0;
        uint8_t  repeats      = 0;
        bool     inUse        = false;
        bool     everFired    = false;
    };

    Slot* findOrClaim(uint64_t signature, uint32_t nowMs);

    Slot slots_[kSlots];
};
