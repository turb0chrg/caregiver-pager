#include "cc1101_rx.h"

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

#include "config.h"
#include "pins.h"

Cc1101Rx::Cc1101Rx(uint8_t modulIndex, int csnPin, int gdo0Pin, int gdo2Pin, const char* tag)
    : modulIndex_(modulIndex),
      csnPin_(csnPin),
      gdo0Pin_(gdo0Pin),
      gdo2Pin_(gdo2Pin),
      tag_(tag),
      decoder_(tag) {}

void Cc1101Rx::select() {
    // The SmartRC driver is a singleton with a module selector, not two objects.
    // Forgetting this is the classic two-radio bug: radio B's config silently
    // lands on radio A.
    ELECHOUSE_cc1101.setModul(modulIndex_);
}

bool Cc1101Rx::begin() {
    pinMode(gdo0Pin_, INPUT);  // external 100k pulldown, see docs/PIN-MAP.md §3

    select();
    ELECHOUSE_cc1101.setSpiPin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, csnPin_);
    ELECHOUSE_cc1101.setGDO(gdo0Pin_, gdo2Pin_);

    // getCC1101() reads the part-number / version registers over SPI. A failure
    // here is wiring, power, or a dead module. Configuration cannot fix it, so
    // bail before spending time on registers.
    if (!ELECHOUSE_cc1101.getCC1101()) {
        ok_ = false;
        return false;
    }

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(RF_FREQ_MHZ);   // 433.92, OQ-1 confirmed
    ELECHOUSE_cc1101.setModulation(2);      // 2 = ASK/OOK
    ELECHOUSE_cc1101.setRxBW(203.125);      // wide: untrimmed RC encoders drift
    ELECHOUSE_cc1101.setDRate(4.0);         // async: only sets demod filtering
    ELECHOUSE_cc1101.setCrc(false);
    ELECHOUSE_cc1101.setPktFormat(3);       // 3 = asynchronous serial on GDO0
    ELECHOUSE_cc1101.SetRx();

    // TODO(bench tuning, docs/PIN-MAP.md §5): AGC and carrier-sense thresholds
    // are still at driver defaults. Tune them against the abort counter in
    // OokStats — a climbing `aborted` with a flat `accepted` means the squelch
    // is too open and the decoder is chewing on noise. Raise the threshold only
    // as far as needed; too tight and weak-but-valid presses from the far end
    // of the house stop decoding, which is the failure nobody notices until it
    // matters.

    lastEdgeUs_ = micros();
    attachInterruptArg(digitalPinToInterrupt(gdo0Pin_), isrTrampoline, this, CHANGE);

    ok_ = true;
    return true;
}

void IRAM_ATTR Cc1101Rx::isrTrampoline(void* arg) {
    static_cast<Cc1101Rx*>(arg)->onEdge();
}

void IRAM_ATTR Cc1101Rx::onEdge() {
    const uint32_t now = micros();
    const uint32_t dur = now - lastEdgeUs_;
    lastEdgeUs_ = now;

    // digitalRead() returns the level *after* the edge. The period just
    // measured was therefore spent at the opposite level.
    const bool levelHeld = (digitalRead(gdo0Pin_) == LOW);

    const uint16_t next = (uint16_t)((head_ + 1) % kRingSize);
    if (next == tail_) {
        // Main loop fell behind. Dropping edges corrupts the frame in progress,
        // but decode rule 2 turns that into a rejected frame rather than a bad
        // one, and the transmitter repeats dozens of times per press.
        overflows_++;
        return;
    }

    ring_[head_] = (levelHeld ? 0x80000000UL : 0UL) | (dur & 0x7FFFFFFFUL);
    head_ = next;
}

bool Cc1101Rx::poll(OokFrame& out) {
    while (tail_ != head_) {
        const uint32_t packed = ring_[tail_];
        tail_ = (uint16_t)((tail_ + 1) % kRingSize);

        const bool     level = (packed & 0x80000000UL) != 0;
        const uint32_t durUs = packed & 0x7FFFFFFFUL;

        if (decoder_.feed(level, durUs, out)) return true;
    }

    // No edges pending. The last frame of a burst has no trailing edge to close
    // it, so close it on idle time instead.
    const uint32_t idleUs = micros() - lastEdgeUs_;
    return decoder_.flushIfIdle(idleUs, out);
}

int Cc1101Rx::rssiDbm() {
    if (!ok_) return 0;
    select();
    return ELECHOUSE_cc1101.getRssi();
}
