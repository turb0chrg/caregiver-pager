// pins.h — GPIO assignments for the CAS Relay Node (budget build).
//
// Authoritative pin map with full rationale: docs/PIN-MAP.md.
// Board: ESP32-WROOM-32D DevKitC (38-pin).
//
// Do not change a value here without updating docs/PIN-MAP.md. The forbidden
// pins listed in §4 of that doc are forbidden for real reasons (SPI flash,
// strapping, input-only); this header is not the place to relitigate them.

#pragma once

// ---- Shared VSPI bus (both CC1101 modules) --------------------------------
// MISO is safe to share: the CC1101 tri-states SO whenever CSN is high.
static const int PIN_SPI_SCK  = 18;
static const int PIN_SPI_MISO = 19;
static const int PIN_SPI_MOSI = 23;

// ---- Radio A --------------------------------------------------------------
static const int PIN_A_CSN  = 21;
static const int PIN_A_GDO0 = 34;  // raw OOK data in. Input-only pin, by design.
static const int PIN_A_GDO2 = 25;  // wired, unused in v0.1 (see PIN-MAP.md §3)

// ---- Radio B --------------------------------------------------------------
static const int PIN_B_CSN  = 22;
static const int PIN_B_GDO0 = 35;  // raw OOK data in. Input-only pin, by design.
static const int PIN_B_GDO2 = 26;  // wired, unused in v0.1

// ---- Local indication / input (FR-A8) -------------------------------------
static const int PIN_STATUS_LED = 2;  // onboard blue LED on most DevKitC boards
static const int PIN_TEST_BTN   = 0;  // onboard BOOT button, reused at runtime

// GPIO34 and GPIO35 have no internal pull resistors. Each needs an external
// 100k pulldown to GND or the edge ISR counts noise before the CC1101 is
// initialised and starts driving the line. See docs/PIN-MAP.md §3.
