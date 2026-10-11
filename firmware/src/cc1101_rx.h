// cc1101_rx.h — one CC1101 in 433.92 MHz OOK asynchronous-serial receive mode.
//
// The CC1101's packet engine cannot parse EV1527-class PWM, so the radio is put
// into asynchronous serial mode: GDO0 carries the raw demodulated envelope and
// the ESP32 times the edges itself. An edge ISR pushes (level, duration) pairs
// into a lock-free ring; the main loop drains them into an OokDecoder.
//
// Two instances share one SPI bus. Every register access must be bracketed by
// select(), because the underlying SmartRC driver keeps per-module state behind
// a global "current module" selector.

#pragma once

#include <stdint.h>

#include "ook_decoder.h"

class Cc1101Rx {
public:
    Cc1101Rx(uint8_t modulIndex, int csnPin, int gdo0Pin, int gdo2Pin, const char* tag);

    // Configures the radio and attaches the GDO0 interrupt.
    // Returns false if the part/version registers do not read back sanely,
    // which in practice always means wiring or power rather than config.
    bool begin();

    // Drains the edge ring into the decoder. Returns true and fills `out` for
    // the first complete frame found this call; call again until it returns
    // false to drain a whole burst.
    bool poll(OokFrame& out);

    int      rssiDbm();
    bool     ok()      const { return ok_; }
    uint32_t overflows() const { return overflows_; }

    const OokDecoder& decoder() const { return decoder_; }
    const char*       tag()     const { return tag_; }

private:
    static void IRAM_ATTR isrTrampoline(void* arg);
    void        IRAM_ATTR onEdge();

    void select();

    static const uint16_t kRingSize = 512;  // ~2 KB; a frame is ~136 edges

    const uint8_t modulIndex_;
    const int     csnPin_;
    const int     gdo0Pin_;
    const int     gdo2Pin_;
    const char*   tag_;

    OokDecoder decoder_;

    // Ring of packed edges: bit31 = level held, bits 0..30 = duration in µs.
    volatile uint32_t ring_[kRingSize];
    volatile uint16_t head_ = 0;
    volatile uint16_t tail_ = 0;
    volatile uint32_t lastEdgeUs_ = 0;
    volatile uint32_t overflows_  = 0;

    bool ok_ = false;
};
