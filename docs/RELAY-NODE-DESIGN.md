# Relay Node — Hardware Design (v0.1)

**Project:** Caregiver Alert System (CAS)
**Component:** Relay Node (Path A ingest)
**Date:** 2026-08-21
**Companion docs:** `REQUIREMENTS.md`, `PIN-MAP.md`, `FIRMWARE.md`, `system-diagram.svg`, `RELAY-NODE-BOM.xlsx`

---

## 1. Design goal

A small always-on box that passively hears SYNLETT 433 MHz button presses, stamps each one with a location and time, and delivers a signed alert to the backend over Wi-Fi. It never sits in the path of the existing SYNLETT pager (REL-1).

This is the **budget build** defined in `CLAUDE.md`: Wi-Fi-only, mains-powered, fixed-location. Two requirements from `REQUIREMENTS.md` are knowingly not met by it — **PW-1** (≥ 24 h battery backup, see section 5.1) and **FR-L1** (live GNSS fix, see section 6). Both gaps are deliberate and are recorded in `PIN-MAP.md` §0.

---

## 2. Hardware selection

Per `CLAUDE.md`, the build is a **budget, Wi-Fi-only, mains-powered, fixed-location** node:

| Block | Part | Notes |
|---|---|---|
| MCU + Wi-Fi | **ESP32-WROOM-32D DevKitC**, USB-C, 38-pin | Wi-Fi is the only uplink, and the MCU's own radio provides it. |
| Sub-GHz RX | **CC1101 433 MHz ×2** | The one external radio sub-system (section 4). Two of them for RX redundancy and antenna diversity — see `PIN-MAP.md` §1. |
| GNSS | *(not fitted)* | The node is fixed-location; see section 6. |
| Power | USB-C mains adapter | See section 5. |

Because the node is fixed and mains-powered, an integrated multi-radio board
would buy nothing here — the ESP32's own Wi-Fi is the whole uplink, and the
fixed install address stands in for GNSS. A plain DevKitC plus two CC1101
breakouts is fewer parts, not more, and every pin is broken out for the one bus
that matters.

The full GPIO assignment, including the pins that are unusable on a WROOM-32D
and why, is in **`PIN-MAP.md`**.

---

## 3. Functional block diagram (electrical)

```
                 ┌──────────────────────────────────────────────┐
   433 MHz RF    │  RELAY NODE                                  │
  SYNLETT  ~~~►  │  ┌──────────┐  shared SPI + GDO0 (IRQ)       │
  buttons        │  │ CC1101 A │──────────────┐                 │
                 │  │  433 RX  │              │                 │
                 │  └──────────┘              ▼                 │
                 │        ▲            ┌─────────────┐          │
                 │     433 ant         │    ESP32    │          │
                 │                     │ WROOM-32D   │          │
                 │  ┌──────────┐       │             │          │
                 │  │ CC1101 B │──────►│             │          │
                 │  │  433 RX  │       └─────────────┘          │
                 │  └──────────┘          │       │             │
                 │        ▲             Wi-Fi   status LED      │
                 │     433 ant            │                     │
   USB-C 5V ────►│  ── 5V ──► onboard 3V3 regulator ──► rails   │
   (mains)       │                                              │
                 └──────────────────────────────────────────────┘
                                          │
                                       HTTPS to backend
```

Both CC1101s connect to the ESP32 over one **shared SPI bus** (SCK/MOSI/MISO)
with a **separate CSn and GDO0 per radio**. MISO is safe to share because the
CC1101 tri-states SO whenever CSn is high. GDO0 carries the raw demodulated OOK
envelope rather than acting as a wake line (the node is mains-powered and stays
awake). Both CC1101 and ESP32 are 3.3 V logic — no level shifting required, and
the CC1101 is **not** 5 V tolerant.

---

## 4. Sub-GHz front end (CC1101)

- **Why CC1101, not a cheap OOK receiver:** superregen/superhet ASK modules (RXB6 etc.) decode unreliably and have no squelch/RSSI. The CC1101 is a real configurable transceiver with a 64-byte FIFO, RSSI/LQI, packet handling, and — critically for power — **Wake-on-Radio (WOR)**.
- **Role:** RX-only in this design. It listens on the SYNLETT frequency, decodes each button's fixed code, and raises GDO0 so the ESP32 can wake and read the payload.
- **Button → ID mapping:** each SYNLETT unit (one button per pager) transmits a stable fixed code that identifies the unit. The firmware keeps a lookup table `code → unit/label` (FR-A2). New units are added by "learning" their code.
- **De-bounce:** a single press repeats the frame ~dozens of times; the ESP32 collapses repeats within a 3–5 s window into one incident (FR-A5).

> **OQ-1 — CONFIRMED (2026-09-15)** via RTL-SDR + `rtl_433` (see `RF-VERIFICATION.md`):
> - **433.92 MHz**, **OOK_PWM**, EV1527/PT2262-class **fixed** code (not rolling ✅).
> - Pulse timing: short ≈ **272–292 µs**, long ≈ **836–864 µs** (~3:1), reset ≈ **1850 µs**; ~68-bit frame.
> - Captured unit codes: Unit 1 `d0076803b401da00e`, Unit 3 `d3bc29de14ef0a778` (distinct). Unit 2 was found **hardware-faulty** (radiates strong RF but only fragments — retire/replace).

**CC1101 decode rules (derived from the OQ-1 captures — all four are load-bearing):**
1. **Tolerance band, not exact widths.** Units 1 (292/864) and 3 (272/836) already differ ~7%, and these RC-timed encoders vary 20–30% unit-to-unit. Match short/long within **±~30%** — a zero-tolerance match silently drops valid units (this is exactly why fob #2's first capture, decoded with unit 1's tight timing, produced only garbage).
2. **Minimum valid frame length.** Require the full ~68-bit word before accepting. A fragmenting/failing transmitter (fob #2) then yields *nothing* rather than a partial or false trigger — the correct fail-safe for a panic button.
3. **Repeat-count threshold.** Require the frame to repeat N times within a press before firing, then de-bounce the remainder into one incident.
4. **Match on the stable signature substring** (e.g. `0076803b401da00e`), not the whole frame — the leading nibble shifts with sync-gap alignment between repeats.

---

## 5. Power architecture

The node runs from a **USB-C 5 V wall adapter**. No battery, no charger, no
sleep states: the ESP32 stays awake and both CC1101s sit in continuous RX.

### 5.1 Why there is no battery in this build

The original design carried an 18650 for ≥ 24 h outage backup (PW-1). That
requirement is **not met here**, and the reason is worth stating plainly rather
than leaving as an omission:

**During a mains outage the home Wi-Fi router is almost certainly also dead.**
Wi-Fi is this build's only uplink, so a battery would keep the node *awake* but
not *connected*. It would decode presses it could not deliver until mains came
back — at which point the queue (FR-A6) flushes and the alert arrives late.
A late panic-button alert is close to worthless.

So the battery is not a cheap partial win here; it buys decoding without
delivery. The honest options are:

1. **Accept the gap** (current build). A mains outage means no remote alerting.
   The SYNLETT pager still beeps locally on its own batteries (REL-1), which is
   the failure floor the whole system is designed around.
2. **Put the router and the node on one small UPS.** This is the cheap real fix:
   it restores the uplink, not just the MCU, and needs no board changes.
3. Re-add an independent uplink, which is out of scope for this build.

Option 2 is the recommendation if outage coverage is wanted. Note it is an
install decision, not a firmware or PCB decision.

### 5.2 Power budget (mains, steady state)

| Rail / block | State | Avg current |
|---|---|---|
| ESP32-WROOM-32D | awake, Wi-Fi station associated | ~80–120 mA |
| CC1101 ×2 | continuous RX | ~32 mA |
| Regulator losses + status LED | | ~10 mA |
| **Estimated average** | | **~130–170 mA @ 3.3 V** |

A 5 V / 1 A USB-C supply is ample. The onboard AMS1117 on a DevKitC handles this
comfortably; Wi-Fi TX peaks are the only transient worth decoupling for.

### 5.3 Power design notes
- **Decouple at each CC1101**, not at the DevKitC: 10 µF + 100 nF across the
  module's own VCC/GND pins. The breakouts ship with minimal local bulk.
- **Wi-Fi TX bursts** pull a few hundred mA for a few ms. If the 3V3 rail sags
  enough to brown out a CC1101, add bulk capacitance on 3V3 before suspecting
  the radio configuration.
- Continuous RX on both radios is a deliberate trade: Wake-on-Radio would cut
  CC1101 draw substantially, but duty-cycled RX can miss the opening frames of
  a burst, and on mains there is nothing to spend the saving on. GDO2 is wired
  anyway so WOR stays available without a rework (`PIN-MAP.md` §3).

---

## 6. Location strategy (FR-L1…L4)

This build has **no GNSS fitted**. The node is fixed-location, so:

- Every incident carries the **configured install location** (FR-L2) and is
  flagged `mode: "fixed"` so recipients can tell it is not a live fix (FR-L3).
- **Indoor reality backs this up:** GPS is weak indoors, so even a fitted
  receiver would fall back to the configured address most of the time for a
  home install.
- FR-L1 (live GNSS fix) is therefore **not met**. It only matters if the node
  is ever mobile, which contradicts the fixed-location build. If that changes,
  the fallback path stays as-is and a GNSS fix is layered in front of it.

---

## 7. Connectivity (CN-1, CN-3, CN-5, CN-6)

- **Uplink:** Wi-Fi station mode, and nothing else. This is the single uplink,
  which makes the local queue the only thing protecting an alert from a router
  reboot — so FR-A6 is load-bearing, not a nicety.
- **Queue first, send second (CN-3, FR-A6):** an incident is written to the
  queue the moment it is decoded, then delivered. A dropout mid-send leaves the
  item queued and it goes out on reconnect, with the **same `incident_id`** so
  the backend's idempotency check collapses the retry (FR-C9).
- **Auto-reconnect** is left to the ESP32 Wi-Fi stack; the firmware simply does
  not drain the queue while the link is down.
- **Data volume** is negligible — a few hundred bytes per incident plus small
  periodic heartbeats.
- **TLS (CN-6)** on every POST, with the backend CA pinned in firmware.

---

## 8. Security on the device (SEC-2)

- The Relay Node signs each incident with a **per-device HMAC** over the payload, plus a **nonce/timestamp** to resist replay. The shared secret lives in ESP32 NVS/efuse, provisioned at build time and individually revocable server-side (SEC-4).
- All uplinks use TLS (CN-6). (The iOS virtual-button path uses a bearer token instead, per SEC-3 — that's a Shortcuts limitation, not this device.)

---

## 9. Antennas & enclosure

- **Antennas:** two 433 MHz whips, one per CC1101 — either the SMA whip the
  module ships with or a ~17.3 cm quarter-wave wire soldered to the ANT pad.
- **Separation (HW-8):** keep the two 433 MHz antennas **≥ 20 cm apart and
  roughly orthogonal**. That is the whole point of the second receiver: two
  co-located, parallel antennas share the same multipath null and buy nothing.
- **Enclosure:** **non-metallic** (ABS/PC) so it doesn't detune 433 MHz. Either
  let the whips exit the box or use SMA panel-mount pigtails for a tidy external
  mount. Provide venting.
- Keep both whips away from the ESP32 module's own Wi-Fi antenna and from the
  USB-C cable, which is the usual unintended radiator on a DevKitC bench setup.

---

## 10. Bring-up / verification plan

1. **Confirm SYNLETT RF (OQ-1):** RTL-SDR capture → confirm frequency, modulation, and per-button codes. *Gate for everything else.*
2. **CC1101 decode:** receive and log each button's code; build the `code → button_id` table; verify de-bounce.
3. **Backend POST over Wi-Fi:** signed incident round-trip; confirm dedup/idempotency on retries.
4. **Fixed location:** confirm every incident carries the install address and is flagged `mode: "fixed"` (FR-L3).
5. **Queue survival:** pull the router mid-press, confirm the incident queues and is delivered on reconnect with the same `incident_id` and no duplicate alert (CN-3, FR-A6, FR-C9).
6. **Antenna placement:** walk the house pressing unit 1, log per-radio decode counts, and position the two whips to cover each other's dead spots.
7. **Soak:** multi-day run logging heartbeats, per-radio decode/abort counters, and any missed presses.

---

## 11. Assumptions & open risks

- **A-1:** SYNLETT is 433.92 MHz OOK_PWM, EV1527/PT2262-class **fixed** code — **CONFIRMED** (OQ-1, 2026-09-15). Passive-decode approach validated.
- **A-2:** Node is mains-powered, with no battery backup. A mains outage means no remote alerting until power returns (section 5.1). *Confirm this is accepted, or put the router and node on a UPS.*
- **A-3:** Home install with a usable fixed location; no GNSS fitted.
- **A-4:** Site Wi-Fi is reachable from wherever the node is mounted — which is also where it must hear the buttons. If those two constraints fight, the CC1101 antennas move, not the node.
- **A-5:** Backend, Pushover, and TLS details are per `REQUIREMENTS.md`; this doc covers only the device.

---

## 12. What this design does *not* lock in yet

- ~~Final GPIO pin map for CC1101 SPI + GDO0~~ — **resolved**, see `PIN-MAP.md`.
- CC1101 AGC and carrier-sense thresholds (`PIN-MAP.md` §5) — must be tuned on the bench against the decoder's abort counter, not taken from defaults.
- Exact bulk-capacitance value on 3V3 (measure Wi-Fi TX sag on the real board).
- Whether outage coverage is wanted, and if so whether the router+node go on a shared UPS (section 5.1).
- Final antenna placement for the two receivers (bring-up step 6).
