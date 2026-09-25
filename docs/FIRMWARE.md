# Relay Node — Firmware (v0.1 skeleton)

**Project:** Caregiver Alert System (CAS)
**Component:** Relay Node (Path A ingest)
**Target:** ESP32-WROOM-32D DevKitC, Arduino framework under PlatformIO
**Source:** `firmware/`
**Companion docs:** `PIN-MAP.md`, `RELAY-NODE-DESIGN.md`, `RF-VERIFICATION.md`, `REQUIREMENTS.md`

---

## 1. Framework decision

`CLAUDE.md` left the target as "Arduino or ESP-IDF — TBD". This build uses
**Arduino-on-ESP32, built under PlatformIO**.

Arduino wins here because the budget build is mains-powered and fixed-location,
so there is no power budget to defend — which is the main thing ESP-IDF would
buy. The CC1101, TLS, HMAC and NVS work is all covered by mature Arduino
libraries. PlatformIO rather than the Arduino IDE keeps library versions pinned
and the source layout reviewable.

The one ESP-IDF dependency is `mbedtls`, used directly for HMAC-SHA256. It ships
inside the Arduino-ESP32 core, so no extra dependency is introduced.

---

## 2. Signal path

```
  SYNLETT/Gaxin F007 button
        │  433.92 MHz OOK, ~68-bit fixed-code frame, repeated dozens of times
        ▼
  ┌──────────┐            ┌──────────┐
  │ CC1101 A │            │ CC1101 B │      async serial mode:
  └────┬─────┘            └────┬─────┘      GDO0 = raw demodulated envelope
       │ GDO0 edge IRQ         │ GDO0 edge IRQ
       ▼                       ▼
  ┌──────────┐            ┌──────────┐
  │ edge ring│            │ edge ring│      512 × (level, duration µs)
  └────┬─────┘            └────┬─────┘
       ▼                       ▼
  ┌──────────┐            ┌──────────┐
  │OokDecoder│            │OokDecoder│      rules 1, 2, 4  →  64-bit signature
  └────┬─────┘            └────┬─────┘
       └───────────┬───────────┘
                   ▼
            ┌─────────────┐
            │  Debouncer  │                 rule 3 + FR-A5; keyed on signature,
            └──────┬──────┘                 so both radios collapse to one event
                   ▼
            ┌─────────────┐
            │ButtonTable  │                 signature → button_id (FR-A2)
            └──────┬──────┘
                   ▼
            ┌─────────────┐
            │   Incident  │                 UUIDv4 id, fixed location, timestamp
            └──────┬──────┘
                   ▼
            ┌─────────────┐
            │ queue+retry │                 FR-A6, exponential backoff, TTL
            └──────┬──────┘
                   ▼
        HMAC-signed HTTPS POST → Backend    SEC-2, CN-6
```

The merge point is deliberate. Both radios decode independently, but the
`Debouncer` is keyed on the **frame signature**, not on the radio, so a press
heard by both receivers raises exactly one incident.

---

## 3. File map

| File | Responsibility |
|---|---|
| `main.cpp` | Wiring, main loop, status LED, incident construction |
| `pins.h` | GPIO assignments only. Mirror of `PIN-MAP.md` |
| `config.h` | RF timing constants, de-bounce tunables, identity, secrets |
| `cc1101_rx.h/.cpp` | One CC1101 in OOK async serial RX; GDO0 edge ISR and ring buffer |
| `ook_decoder.h/.cpp` | Pulse train → 64-bit frame signature. Decode rules 1, 2, 4 |
| `button_table.h/.cpp` | Signature → `button_id` + human label (FR-A2, FR-A3) |
| `debounce.h/.cpp` | Repeat threshold and burst collapse. Decode rule 3, FR-A5 |
| `incident.h` | Incident record and UUIDv4 generation |
| `net_uplink.h/.cpp` | Wi-Fi, NTP, retry queue, HMAC signing, HTTPS POST, heartbeat |

No dynamic allocation anywhere in the RF path. The ring buffers and the queue
are fixed-size arrays.

---

## 4. Where the decode rules live

The four rules in `RELAY-NODE-DESIGN.md` §4 are load-bearing, and each one maps
to a specific place in the code. This table is the thing to check if a decode
regression ever appears.

| Rule | Implementation | Constant |
|---|---|---|
| **1.** Tolerance band, never exact widths | `OokDecoder::classify()` | `OOK_TOLERANCE_PCT = 30` |
| **2.** Require a full frame before accepting | `OokDecoder::closeFrame()` | `OOK_FRAME_MIN_BITS = 68` |
| **3.** Repeat-count threshold, then de-bounce | `Debouncer::offer()` | `REPEAT_THRESHOLD = 3`, `DEBOUNCE_WINDOW_MS = 4000` |
| **4.** Match on the stable signature substring | `OokDecoder::appendBit()` accumulates a rolling 64-bit tail | `OOK_SIGNATURE_BITS = 64` |

Two implementation choices deserve calling out, because both are easy to
"simplify" into a bug:

**Bits are classified on pulse width alone**, not on the pulse/gap pair. In PWM
the pulse width already determines the bit, and the final bit of a frame has its
gap replaced by the inter-frame reset gap. A pair-based decoder loses that last
bit, lands on 67 bits, and then rule 2 rejects every single frame.

**Only the trailing 64 bits are accumulated.** `rtl_433` renders the frame as 17
hex characters whose leading nibble shifts with sync-gap alignment between
repeats. Keeping a rolling 64-bit tail means the unstable nibble is discarded by
construction rather than by a string operation after the fact.

### Timing constants

Derived from the captures in `RF-VERIFICATION.md` §7, at ±30%:

| Symbol | Nominal | Accepted range |
|---|---|---|
| Short pulse (bit 0) | 282 µs | 197 – 366 µs |
| Long pulse (bit 1) | 850 µs | 595 – 1105 µs |
| Inter-frame reset gap | 1850 µs | ≥ 1295 µs |

The short and long bands do not overlap, and the reset threshold sits clear of
the long-pulse maximum with margin.

---

## 5. Before flashing anything real

`config.h` ships with placeholders. Every item below is a **must** before this
node is trusted:

| Item | Constant | Why it matters |
|---|---|---|
| Wi-Fi credentials | `WIFI_SSID`, `WIFI_PASSWORD` | Placeholder values |
| Backend URL | `BACKEND_URL` | Points at `example.invalid` |
| Per-device HMAC secret | `DEVICE_HMAC_SECRET` | SEC-2. Must be per-device and revocable (SEC-4) |
| Backend CA certificate | `BACKEND_CA_PEM` | **Currently `nullptr`, which falls back to `setInsecure()`.** That is still TLS, so it stops an eavesdropper, but it does not stop a man in the middle. Bench only. |
| Install location | `FIXED_LAT`, `FIXED_LON` | Placeholder coordinates. Every incident carries these (FR-L2) |

Secrets in a header are a bring-up convenience, not the design. The intended end
state is NVS, written by a separate provisioning sketch, so the secret is
per-device and individually revocable without a rebuild. That is tracked as a
`TODO(provisioning)` in `config.h`.

---

## 6. Build and flash

```bash
cd firmware
pio run                  # build
pio run -t upload        # flash over USB-C
pio device monitor       # 115200 baud, exception decoder enabled
```

---

## 7. Learning a new button

Needed at minimum for the replacement for retired unit 2 (`RF-VERIFICATION.md` §7).

1. Leave `LOG_RAW_FRAMES = true` in `config.h`.
2. Press the new unit near the node.
3. The console prints one line per accepted frame:
   `radio A: frame 68 bits sig=3bc29de14ef0a778 (unknown)`
4. Add that signature to `kButtons[]` in `button_table.cpp` with a `button_id`
   and a human label. The label is what reaches the caregiver (FR-N3), so write
   it for someone reading a phone alert under stress: "Bedroom call button", not
   "unit-4".
5. Rebuild, flash, confirm the press now raises an incident.

If no line appears at all, the unit is not completing frames. That is the fob #2
signature and means a hardware fault in the button, not a firmware problem —
rule 2 is working as designed.

---

## 8. Status LED (FR-A8)

| Pattern | Meaning |
|---|---|
| Slow pulse, ~once every 3 s | Healthy: radios up, Wi-Fi up |
| Fast even blink, ~2 Hz | No uplink. Incidents are queueing |
| Double blink | At least one CC1101 did not respond over SPI |

Chosen so "something is wrong" is legible across a room without reading
anything.

---

## 9. Diagnostics

`OokStats` per radio, reported in the heartbeat and available on the console:

| Counter | Reading it |
|---|---|
| `accepted` | Frames that passed every rule |
| `tooShort` | Fragments rejected by rule 2. Non-zero near a failing transmitter is **correct behaviour** |
| `tooLong` | Two frames ran together; a delimiter was missed |
| `aborted` | A pulse or gap fell outside every band |
| `edges` | Raw ISR edge count. Rises with RF noise even when nothing decodes |

**Tuning the CC1101 AGC** is the main open bench task. The radio currently runs
at driver defaults. A climbing `aborted` with a flat `accepted` means the squelch
is too open and the decoder is chewing on noise. Raise the carrier-sense
threshold only as far as needed: too tight and weak-but-valid presses from the
far end of the house stop decoding, which is the failure mode nobody notices
until it matters.

Also watch `overflows()` on each radio. Non-zero means the main loop fell behind
and the edge ring wrapped. Dropped edges corrupt the frame in progress, but rule
2 turns that into a rejected frame rather than a wrong one, and the transmitter
repeats dozens of times per press.

---

## 10. Known gaps

- **Not yet compiled.** No toolchain was available in the session that wrote
  this, so the skeleton has not been built against a real ESP32 core. Expect
  ordinary first-compile fixes.
- **CC1101 register values are unverified.** The frequency, modulation and
  async-serial packet mode are right in principle; the AGC, RX bandwidth and
  data-rate values need bench confirmation against a real module.
- **The retry queue is RAM-only.** A reboot mid-outage drops queued incidents.
  FR-A6 is only fully met once the queue is backed by NVS.
- **Heartbeats POST to the same endpoint as incidents.** The backend will likely
  want a separate route; the payload carries `"type": "heartbeat"` so it can be
  split either way.
- **No acknowledgement path.** FR-N5 is a backend and Pushover concern, but if
  the device is ever meant to learn that an alert was acked, nothing here
  receives it.
