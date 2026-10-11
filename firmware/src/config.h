// config.h — build-time tunables for the CAS Relay Node.
//
// The RF constants in section 1 are not guesses. They come from the RTL-SDR
// captures recorded in docs/RF-VERIFICATION.md §7 and are cross-checked against
// the FCC grant for the Gaxin F007. Changing them invalidates OQ-1.

#pragma once

#include <stdint.h>

#ifndef CAS_FW_VERSION
#define CAS_FW_VERSION "0.1.0-dev"
#endif

// ===========================================================================
// 1. RF decode — OQ-1 confirmed parameters
// ===========================================================================

static const float RF_FREQ_MHZ = 433.92f;

// Captured pulse widths, in microseconds:
//   unit 1: short 292, long 864
//   unit 3: short 272, long 836
// The nominals below sit between the two captured units.
static const uint32_t OOK_SHORT_NOM_US = 282;
static const uint32_t OOK_LONG_NOM_US  = 850;
static const uint32_t OOK_RESET_US     = 1850;

// DECODE RULE 1 — tolerance band, never exact widths.
// Units 1 and 3 already differ by ~7%, and RC-timed EV1527-class encoders vary
// 20-30% unit-to-unit and with temperature and battery state. A tight match
// silently drops valid units: that is exactly how fob #2's first capture
// produced garbage when decoded with unit 1's timing.
static const uint32_t OOK_TOLERANCE_PCT = 30;

static const uint32_t OOK_SHORT_MIN_US = OOK_SHORT_NOM_US * (100 - OOK_TOLERANCE_PCT) / 100;  //  197
static const uint32_t OOK_SHORT_MAX_US = OOK_SHORT_NOM_US * (100 + OOK_TOLERANCE_PCT) / 100;  //  366
static const uint32_t OOK_LONG_MIN_US  = OOK_LONG_NOM_US  * (100 - OOK_TOLERANCE_PCT) / 100;  //  595
static const uint32_t OOK_LONG_MAX_US  = OOK_LONG_NOM_US  * (100 + OOK_TOLERANCE_PCT) / 100;  // 1105

// A low period at or above this is an inter-frame delimiter, not a bit gap.
// Sits clear of OOK_LONG_MAX_US (1105) with margin.
static const uint32_t OOK_GAP_RESET_MIN_US = OOK_RESET_US * (100 - OOK_TOLERANCE_PCT) / 100;  // 1295

// DECODE RULE 2 — minimum valid frame length, fail-safe on fragments.
// The captured word is ~68 bits. Requiring the whole thing means a fragmenting
// transmitter (fob #2, which radiates strongly but never completes a frame)
// yields *nothing* rather than a partial or false trigger. For a panic button
// that is the correct failure direction: a missed alert is visible to the user,
// a phantom alert erodes trust in every future alert.
static const uint16_t OOK_FRAME_MIN_BITS = 68;
static const uint16_t OOK_FRAME_MAX_BITS = 72;

// DECODE RULE 4 — match on the stable signature substring, not the whole frame.
// rtl_433 renders the frame as 17 hex chars (68 bits) whose *leading* nibble
// shifts with sync-gap alignment between repeats, e.g. "d0076803b401da00e".
// The trailing 64 bits are stable, so the decoder accumulates only those.
static const uint16_t OOK_SIGNATURE_BITS = 64;

// ===========================================================================
// 2. Incident de-bounce (FR-A5)
// ===========================================================================

// DECODE RULE 3 — repeat-count threshold, then collapse the burst.
// One physical press repeats the frame dozens of times. Requiring N good copies
// before firing rejects a single lucky decode out of noise.
static const uint8_t REPEAT_THRESHOLD = 3;

// Frames farther apart than this belong to different presses, not one burst.
static const uint32_t BURST_IDLE_MS = 1000;

// After an incident fires for a button, suppress further incidents for that
// button for this long. REQUIREMENTS.md FR-A5 calls for 3-5 s.
static const uint32_t DEBOUNCE_WINDOW_MS = 4000;

// ===========================================================================
// 3. Identity and uplink
// ===========================================================================

static const char DEVICE_ID[] = "cas-relay-01";

// TODO(provisioning): move WIFI_* and DEVICE_HMAC_SECRET out of the source tree
// before this is flashed to anything real. They belong in NVS, written by a
// provisioning sketch, so the secret is per-device and individually revocable
// (SEC-2, SEC-4). Hardcoding them here is a bring-up convenience only.
static const char WIFI_SSID[]     = "CHANGEME";
static const char WIFI_PASSWORD[] = "CHANGEME";

static const char BACKEND_URL[] = "https://backend.example.invalid/v1/incident";

// Per-device HMAC-SHA256 secret (SEC-2). Placeholder.
static const char DEVICE_HMAC_SECRET[] = "CHANGEME-32-bytes-minimum-please";

// TODO(SEC/CN-6): pin the backend's CA certificate here. Until this is a real
// root cert, net_uplink.cpp falls back to setInsecure(), which is fine on the
// bench and unacceptable in the field.
static const char* const BACKEND_CA_PEM = nullptr;

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static const uint32_t HTTP_TIMEOUT_MS         = 10000;

// ===========================================================================
// 4. Location (FR-L2, FR-L3)
// ===========================================================================

// The budget build has no GNSS, so every incident carries the fixed install
// location and is flagged as such. See docs/PIN-MAP.md §0.
static const double FIXED_LAT = 49.2827;   // TODO: real install coordinates
static const double FIXED_LON = -123.1207;

// ===========================================================================
// 5. Housekeeping
// ===========================================================================

static const uint32_t HEARTBEAT_INTERVAL_MS = 15UL * 60UL * 1000UL;  // FR-A7

// Retry queue (FR-A6). In-RAM for v0.1; see the TODO in net_uplink.cpp about
// backing this with NVS so a reboot mid-outage does not drop a queued incident.
static const uint8_t  QUEUE_DEPTH     = 16;
static const uint32_t QUEUE_TTL_MS    = 30UL * 60UL * 1000UL;
static const uint32_t RETRY_BASE_MS   = 2000;
static const uint32_t RETRY_MAX_MS    = 120000;

// Log every accepted raw frame, including ones with no table match. Essential
// while learning a replacement for the retired unit 2; noisy in production.
static const bool LOG_RAW_FRAMES = true;
