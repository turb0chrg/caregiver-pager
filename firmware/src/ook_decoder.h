// ook_decoder.h — pulse train to frame signature, for EV1527/PT2262-class OOK.
//
// Consumes (level, duration) pairs measured from a CC1101 GDO0 line running in
// asynchronous serial mode, and emits a 64-bit frame signature when a complete,
// well-formed frame closes.
//
// Implements decode rules 1, 2 and 4 from RELAY-NODE-DESIGN.md §4. Rule 3
// (repeat threshold) belongs to the burst layer, not here — see debounce.h.
//
// One instance per radio. No dynamic allocation, no blocking.

#pragma once

#include <stdint.h>

struct OokFrame {
    // Trailing OOK_SIGNATURE_BITS (64) bits of the frame, MSB-first.
    // Rule 4: only the tail is stable. The leading nibble shifts with sync-gap
    // alignment between repeats, so it is deliberately never accumulated.
    uint64_t signature;

    // Total bits decoded in the frame, including the ones shifted off the top
    // of `signature`. Used to enforce rule 2.
    uint16_t bitCount;

    uint32_t endedAtMs;
};

// Counters for health reporting and for bench-tuning the CC1101 AGC. A rising
// `aborted` count with a flat `accepted` count means the squelch is too open.
struct OokStats {
    uint32_t accepted   = 0;  // frames that passed rules 1, 2 and 4
    uint32_t tooShort   = 0;  // closed below OOK_FRAME_MIN_BITS (rule 2 working)
    uint32_t tooLong    = 0;  // closed above OOK_FRAME_MAX_BITS
    uint32_t aborted    = 0;  // a pulse or gap fell outside every band (rule 1)
    uint32_t edges      = 0;
};

class OokDecoder {
public:
    explicit OokDecoder(const char* tag) : tag_(tag) {}

    // Feed one completed level period. `level` is the logic level that was just
    // held for `durUs` microseconds. Returns true and fills `out` when a frame
    // closes and passes every rule.
    bool feed(bool level, uint32_t durUs, OokFrame& out);

    // Close a frame that ended in silence. A burst's final frame has no
    // trailing edge to terminate it, so the main loop calls this when the line
    // has been idle longer than the reset gap. Same return contract as feed().
    bool flushIfIdle(uint32_t idleUs, OokFrame& out);

    void reset();

    const OokStats& stats() const { return stats_; }
    const char*     tag()   const { return tag_; }

private:
    enum class Sym { Short, Long, Invalid };

    static Sym classify(uint32_t durUs);

    void appendBit(bool bit);
    bool closeFrame(OokFrame& out);
    void abortFrame();

    const char* tag_;

    uint64_t acc_        = 0;  // rolling tail of the frame
    uint16_t bitCount_   = 0;
    uint32_t pendingHighUs_ = 0;  // 0 = no pulse awaiting its gap

    OokStats stats_;
};

// Render a signature as 16 lowercase hex chars. `out` must hold 17 bytes.
void ookSignatureHex(uint64_t signature, char* out);
