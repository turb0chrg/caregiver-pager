#include "ook_decoder.h"

#include <Arduino.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Rule 1: tolerance bands, never exact widths.
// ---------------------------------------------------------------------------
OokDecoder::Sym OokDecoder::classify(uint32_t durUs) {
    if (durUs >= OOK_SHORT_MIN_US && durUs <= OOK_SHORT_MAX_US) return Sym::Short;
    if (durUs >= OOK_LONG_MIN_US  && durUs <= OOK_LONG_MAX_US)  return Sym::Long;
    return Sym::Invalid;
}

void OokDecoder::reset() {
    acc_            = 0;
    bitCount_       = 0;
    pendingHighUs_  = 0;
}

void OokDecoder::abortFrame() {
    // Only count an abort if we had actually started accumulating. Idle-line
    // noise between bursts would otherwise swamp the counter and make the
    // AGC-tuning signal useless.
    if (bitCount_ > 0) stats_.aborted++;
    reset();
}

void OokDecoder::appendBit(bool bit) {
    // Rule 4: keep only the trailing 64 bits. Bits shifted off the top are the
    // unstable leading nibble and whatever preceded it.
    acc_ = (acc_ << 1) | (bit ? 1ULL : 0ULL);
    if (bitCount_ < 0xFFFF) bitCount_++;
}

// ---------------------------------------------------------------------------
// Rule 2: a frame is accepted only at full length.
// ---------------------------------------------------------------------------
bool OokDecoder::closeFrame(OokFrame& out) {
    // Snapshot before reset() clears the accumulator.
    const uint16_t bits = bitCount_;
    const uint64_t sig  = acc_;
    reset();

    if (bits == 0) return false;  // nothing accumulated; not worth counting

    if (bits < OOK_FRAME_MIN_BITS) {
        // This is the fob #2 path: strong RF, fragmentary frames, no output.
        stats_.tooShort++;
        return false;
    }
    if (bits > OOK_FRAME_MAX_BITS) {
        // Two frames ran together because a delimiter was missed. The tail is
        // not trustworthy as a signature, so drop it rather than guess.
        stats_.tooLong++;
        return false;
    }

    out.signature = sig;
    out.bitCount  = bits;
    out.endedAtMs = millis();
    stats_.accepted++;
    return true;
}

bool OokDecoder::feed(bool level, uint32_t durUs, OokFrame& out) {
    stats_.edges++;

    if (level) {
        // A pulse. Stash it; in PWM the pulse width alone carries the bit, and
        // the following gap is only checked for plausibility.
        if (pendingHighUs_ != 0) {
            // Two highs with no intervening low: the ring buffer dropped an
            // edge, or the line glitched. Either way the frame is unreliable.
            abortFrame();
        }
        pendingHighUs_ = durUs;
        return false;
    }

    // A gap.
    if (pendingHighUs_ == 0) {
        // Low period with no preceding pulse — the quiet before a burst, or
        // the tail of an aborted frame. Nothing to decode.
        return false;
    }

    const Sym pulse = classify(pendingHighUs_);
    pendingHighUs_ = 0;

    if (pulse == Sym::Invalid) {
        abortFrame();
        return false;
    }

    // OOK_PWM: long pulse = 1, short pulse = 0.
    appendBit(pulse == Sym::Long);

    if (durUs >= OOK_GAP_RESET_MIN_US) {
        // Inter-frame delimiter. The bit just appended is the frame's last one
        // — which is why bits are classified on pulse width rather than on the
        // pulse/gap pair. A pair-based decoder loses the final bit here and
        // would never reach OOK_FRAME_MIN_BITS.
        return closeFrame(out);
    }

    if (classify(durUs) == Sym::Invalid) {
        // Gap is neither a valid bit gap nor a delimiter.
        abortFrame();
        return false;
    }

    return false;
}

bool OokDecoder::flushIfIdle(uint32_t idleUs, OokFrame& out) {
    if (idleUs < OOK_GAP_RESET_MIN_US) return false;

    if (pendingHighUs_ != 0) {
        // The line went high and stayed high past a full reset gap. That is a
        // stuck carrier or a jammer, not a frame.
        abortFrame();
        return false;
    }
    if (bitCount_ == 0) return false;

    return closeFrame(out);
}

void ookSignatureHex(uint64_t signature, char* out) {
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[15 - i] = kHex[(signature >> (i * 4)) & 0xF];
    }
    out[16] = '\0';
}
