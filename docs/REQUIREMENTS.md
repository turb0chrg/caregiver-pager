# Caregiver Alert System (CAS) — Requirements

**Status:** Draft v0.1
**Date:** 2026-08-21
**Author:** Joel
**Scope of this document:** functional and non-functional requirements only. No implementation / code.

---

## 1. Purpose

Add a remote-notification layer on top of an existing off-the-shelf caregiver pager (SYNLETT wireless call-button system, Amazon ASIN B0CKQY99NH) so that a button press also delivers a rich alert — button ID, location, timestamp — to one or more caregivers' phones, wherever they are. A software-only "virtual button" on an iPhone provides an equivalent alert path for the person being cared for (or a caregiver) without any physical hardware.

The system is **additive**: the SYNLETT buttons and pagers continue to work exactly as shipped (local RF beep). CAS listens in parallel and adds the remote path. Failure of any CAS component must not disable the existing local pager function.

---

## 2. Scope

### In scope (this spec)
- Passive RF ingest of SYNLETT button presses via an ESP32 relay node.
- A software virtual button on iOS that produces an equivalent alert.
- A backend webhook that authenticates, enriches, logs, and fans out alerts.
- Push notification delivery to caregiver phones (v1).
- SMS and text-to-voice call delivery (v2).
- A subscription/tiering model for number of recipients and delivery channels.

### Out of scope
- Any claim of being a certified life-safety, medical, or 911 device.
- Replacing the SYNLETT local pager.
- Two-way voice between button and caregiver.
- Fall detection or automatic (unpressed) triggering.

---

## 3. Definitions

| Term | Meaning |
|------|---------|
| **SYNLETT** | The off-the-shelf 433 MHz caregiver call-button + pager system being integrated. |
| **Relay Node** | The custom ESP32 device that receives SYNLETT RF and calls the backend. |
| **Virtual Button** | An iOS-native trigger (Shortcut) that calls the backend directly, bypassing the Relay Node. |
| **Backend** | The cloud webhook + logic that authenticates, enriches, logs, and fans out alerts. |
| **Recipient / Caregiver** | A person who receives alerts on their phone. |
| **Incident** | A single alert event (one button press or one virtual-button activation). |

---

## 4. System architecture (summary)

Two ingest paths converge on one backend, which fans out to recipients.

```
Path A (physical):  SYNLETT button --433MHz RF--> Relay Node (ESP32) --HTTPS--> Backend
Path B (virtual):   iPhone Shortcut ------------------------HTTPS--> Backend
                                                                        |
                                                          fan-out to recipients
                                                          (Pushover v1; SMS/voice v2)
```

See `system-diagram.svg` for the full diagram.

---

## 5. Functional requirements

### 5.1 Physical ingest — Relay Node (Path A)

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-A1 | The Relay Node SHALL passively receive SYNLETT button transmissions without pairing to, or interfering with, the SYNLETT pager. | Must |
| FR-A2 | The Relay Node SHALL decode each unique button's RF code and map it to a stable, human-readable `button_id`. | Must |
| FR-A3 | The Relay Node SHALL support at least the 3 buttons shipped with the kit, and be expandable to additional SYNLETT buttons. | Must |
| FR-A4 | On a decoded press, the Relay Node SHALL acquire a location fix (see 5.4) and a UTC timestamp, then POST an incident to the Backend. | Must |
| FR-A5 | The Relay Node SHALL de-bounce repeated RF frames from a single physical press into one incident (configurable window, e.g. 3–5 s). | Must |
| FR-A6 | If the POST fails, the Relay Node SHALL queue the incident locally and retry with backoff until delivered or expired (configurable TTL). | Must |
| FR-A7 | The Relay Node SHALL send a periodic heartbeat to the Backend (configurable, e.g. every 15 min) including battery %, power source, and firmware version. | Should |
| FR-A8 | The Relay Node SHALL locally indicate its own health (e.g. LED: OK / no-connectivity / low-battery). | Should |

### 5.2 Virtual ingest — iOS Virtual Button (Path B)

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-B1 | An iPhone SHALL be able to raise an incident that flows into the same Backend and fan-out as a physical press, without any Relay Node involved. | Must |
| FR-B2 | The virtual button SHALL be implemented with native iOS mechanisms only — no custom app store install required for v1 (Apple Shortcuts). | Must |
| FR-B3 | The virtual button SHALL capture the iPhone's current GPS location and a UTC timestamp and include them in the incident. | Must |
| FR-B4 | The incident SHALL carry a `source` of `virtual` and a distinct `device_id` (e.g. `ios-joel`) so the Backend and recipients can tell physical vs virtual apart. | Must |
| FR-B5 | The virtual button SHALL be launchable from as many native surfaces as practical (see 5.8) to minimise time-to-trigger. | Should |
| FR-B6 | The virtual button SHALL complete and confirm send with minimal taps and minimal on-screen confirmation prompts. | Should |
| FR-B7 | The virtual button SHALL give the user clear success/failure feedback (haptic + banner). | Should |

### 5.3 Backend

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-C1 | The Backend SHALL expose a single HTTPS (TLS) webhook endpoint that accepts incidents from both Path A and Path B. | Must |
| FR-C2 | The Backend SHALL authenticate every inbound incident (see 8, Security). | Must |
| FR-C3 | The Backend SHALL persist every incident (raw payload, source, auth result, delivery outcomes) to an append-only log for audit. | Must |
| FR-C4 | The Backend SHALL resolve which recipients are subscribed to the given button/caregiver and fan out to their configured channels. | Must |
| FR-C5 | The Backend SHALL enrich incidents with a human-readable location (reverse-geocode lat/long to an address/place) before delivery. | Should |
| FR-C6 | The Backend SHALL de-duplicate incidents (same source + button within a short window) so retries/duplicate frames don't double-alert. | Must |
| FR-C7 | The Backend SHALL record per-recipient, per-channel delivery status and expose it for review. | Should |
| FR-C8 | The Backend SHALL raise an operational alert if a Relay Node misses N consecutive heartbeats (device-down detection). | Should |
| FR-C9 | The Backend SHALL be idempotent on a client-supplied incident ID so a retried POST does not create a second incident. | Must |

### 5.4 Location

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-L1 | Path A location SHALL come from the Relay Node's onboard GNSS/GPS at time of press. | Must |
| FR-L2 | If no GPS fix is available within a bounded time (e.g. indoor install), the Relay Node SHALL fall back to a pre-configured fixed location (the install address). | Must |
| FR-L3 | Each incident SHALL indicate whether its location is a live GPS fix or the fixed fallback. | Must |
| FR-L4 | Path B location SHALL come from the iPhone's location services at time of activation. | Must |

### 5.5 Notifications — v1

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-N1 | v1 SHALL deliver alerts to iOS and Android via Pushover. | Must |
| FR-N2 | Alerts SHALL use an emergency-class priority that re-alerts until acknowledged (Pushover priority 2), with configurable retry interval and expiry. | Must |
| FR-N3 | Alert content SHALL include: caregiver/person name, button label, live-vs-fixed location with a map link, and local timestamp. | Must |
| FR-N4 | Alerts SHALL use a distinct, attention-grabbing sound. | Should |
| FR-N5 | A recipient acknowledging an alert SHALL be recorded by the Backend and SHALL stop re-alerts to that recipient. | Should |

### 5.6 Notifications — v2

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-N6 | v2 SHALL add SMS delivery via a programmable provider (voip.ms or Twilio). | Should |
| FR-N7 | v2 SHALL add outbound text-to-voice phone calls that read the incident details (button, location, time), via the same provider. | Should |
| FR-N8 | Voice calls SHALL support a simple acknowledgement (e.g. press a key) recorded by the Backend. | Could |
| FR-N9 | Channel selection (push / SMS / voice) SHALL be per-recipient and driven by the subscription tier (see 9). | Should |

### 5.7 Subscription / tiering
See section 9.

### 5.8 iOS Virtual Button — native surfaces
The virtual button SHALL be reachable from as many of these as practical, in rough priority order:

| ID | Surface | Notes | Priority |
|----|---------|-------|----------|
| FR-I1 | Home Screen icon | Shortcut "Add to Home Screen" with custom icon/label. | Must |
| FR-I2 | Back Tap | Accessibility → double/triple tap back of phone runs the Shortcut. Fastest eyes-free trigger. | Should |
| FR-I3 | Action Button | iPhone 15 Pro and later; single dedicated physical button. | Should |
| FR-I4 | Control Center / Lock Screen control | iOS 18+ custom control. | Should |
| FR-I5 | Siri voice ("Hey Siri, <name>") | Hands-free. | Could |
| FR-I6 | Apple Watch | Shortcut on watch face / complication — wearable panic path. | Could |

---

## 6. Hardware requirements (Relay Node)

| ID | Requirement | Priority |
|----|-------------|----------|
| HW-1 | The Relay Node SHALL be based on an ESP32 (Wi-Fi capable) MCU. | Must |
| HW-2 | The Relay Node SHALL include a sub-GHz radio suitable for reliable 433 MHz OOK decode (CC1101-class, not superregen). | Must |
| HW-4 | The Relay Node SHALL include onboard GNSS/GPS. | Must |
| HW-5 | The sub-GHz radio SHALL support an interrupt/wake line to the ESP32 so the MCU can sleep while still catching button presses (power budget, see 7). | Must |
| HW-6 | The Relay Node SHALL run from mains power in normal operation, with an internal rechargeable battery for backup (see 7). | Must |
| HW-7 | The Relay Node SHALL include a battery fuel gauge or equivalent so battery % can be reported (FR-A7). | Should |
| HW-8 | Antenna placement/design SHALL support the 433 MHz RX and GNSS paths without mutual desensitisation. | Should |

> **Note / open question:** exact SYNLETT frequency and encoding must be confirmed before selecting the radio (see 12). Assumed 433.92 MHz OOK, EV1527/PT2262-class, pending verification.

---

## 7. Power requirements

Interpretation: the Relay Node is normally mains-powered; the 24-hour figure is **backup runtime during a mains outage**.

| ID | Requirement | Priority |
|----|-------------|----------|
| PW-1 | On loss of mains, the Relay Node SHALL continue full operation on battery for **≥ 24 hours**. | Must |
| PW-3 | The ESP32 SHALL use a low-power state between events, waking on the sub-GHz radio interrupt (HW-5) rather than polling continuously. | Must |
| PW-4 | Wi-Fi SHALL use modem power-save / DTIM where compatible with prompt event delivery. | Should |
| PW-5 | The Relay Node SHALL report battery %, charging state, and "on-battery" status in heartbeats and SHALL push a low-battery alert to the Backend below a threshold. | Must |
| PW-6 | The design SHALL target 24 h backup from a single 18650-class cell (~3000 mAh / ~11 Wh) as the baseline, sizing up if the measured average draw requires it. | Should |
| PW-7 | Battery SHALL recharge automatically when mains returns; the node SHALL remain operational while charging. | Must |

---

## 8. Connectivity requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| CN-1 | Wi-Fi SHALL be the uplink for the Relay Node. | Must |
| CN-3 | A Wi-Fi dropout SHALL NOT lose a queued incident; the node SHALL deliver it on reconnect (see FR-A6). | Must |
| CN-5 | The active connectivity mode SHALL be included in each incident and heartbeat. | Should |
| CN-6 | All uplinks (both paths) SHALL use TLS. | Must |

---

## 9. Subscription / tiering model

Recipients register with the Backend; the tier controls how many recipients and which channels are used per caregiver/button.

| ID | Requirement | Priority |
|----|-------------|----------|
| SUB-1 | The Backend SHALL model recipients, their channels (push / SMS / voice), and their subscription to specific buttons/caregivers. | Must |
| SUB-2 | A **free/base tier** SHALL provide push notifications (Pushover) to a limited number of recipients. | Must |
| SUB-3 | A **paid tier** SHALL unlock additional recipients and the SMS / text-to-voice channels (v2). | Should |
| SUB-4 | Tier and recipient configuration SHALL be changeable without reflashing the Relay Node or editing the iOS Shortcut (server-side config). | Must |
| SUB-5 | The system SHALL enforce tier limits at fan-out time. | Should |

---

## 10. Security & privacy requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| SEC-1 | Every inbound incident SHALL be authenticated before processing. | Must |
| SEC-2 | Path A (Relay Node) SHALL authenticate with a per-device secret using a signed request (e.g. HMAC over the payload) plus a nonce/timestamp to resist replay. | Must |
| SEC-3 | Path B (iOS) SHALL authenticate with a per-device bearer token over TLS. (iOS Shortcuts lack native HMAC crypto, so a bearer token is the pragmatic mechanism; scope tokens narrowly and allow revocation.) | Must |
| SEC-4 | Device secrets/tokens SHALL be individually revocable without affecting other devices. | Must |
| SEC-5 | Location data SHALL only be transmitted as part of an incident (not continuously) and SHALL be access-controlled at the Backend. | Must |
| SEC-6 | The Backend SHALL rate-limit per device to contain a compromised or malfunctioning sender. | Should |
| SEC-7 | Recipient PII (phone numbers, push keys) SHALL be stored securely and access-logged. | Should |
| SEC-8 | Incident/audit logs SHALL have a defined retention period. | Should |

---

## 11. Reliability & failure modes

| ID | Requirement | Priority |
|----|-------------|----------|
| REL-1 | CAS SHALL be strictly additive: any CAS failure SHALL leave the SYNLETT local pager fully functional. | Must |
| REL-2 | The system SHALL degrade gracefully: no GPS → fixed location; no network → local queue + retry. | Must |
| REL-3 | The Backend SHALL detect and surface a dead/offline Relay Node via missed heartbeats (FR-C8). | Should |
| REL-4 | The system SHALL detect low battery early and alert before runtime is exhausted (PW-5). | Must |
| REL-5 | Every incident SHALL be delivered at-least-once; duplicates SHALL be suppressed downstream (FR-C6, FR-C9). | Must |

### Known single points of failure (to be mitigated / documented)
- Backend availability (mitigate: managed/redundant hosting).
- Push provider (Pushover) availability (mitigate: v2 SMS/voice as alternate channel).
- Site Wi-Fi or internet down (residual risk; incidents queue per FR-A6, and the local SYNLETT pager still functions per REL-1).
- RF decode reliability depends on confirmed SYNLETT protocol (section 12).

---

## 12. Open questions / to verify

| ID | Item |
|----|------|
| OQ-1 | ✅ **RESOLVED (2026-09-15):** 433.92 MHz, OOK_PWM, EV1527-class fixed code; short≈272–292µs / long≈836–864µs / reset≈1850µs. Unit codes: 1 `d0076803b401da00e`, 3 `d3bc29de14ef0a778`; unit 2 faulty (retire/replace). See `RF-VERIFICATION.md`. |
| OQ-2 | Confirm each button emits a stable, distinguishable code usable as `button_id`. |
| OQ-5 | Measure real average current draw to validate the ≥24 h / 18650 baseline (PW-6). |
| OQ-6 | Confirm Pushover priority-2 acknowledgement flow meets the "re-alert until acked" need (FR-N2, FR-N5). |
| OQ-7 | Decide reverse-geocoding provider for FR-C5. |
| OQ-8 | Confirm minimum iOS version for Control Center custom controls (FR-I4) across target caregiver phones. |
| OQ-9 | Legal/liability: explicit non-life-safety disclaimer wording and where it must appear. |

---

## 13. Roadmap

**v1 (MVP)**
- Path A: SYNLETT → Relay Node → Backend → Pushover (priority 2).
- Path B: iOS Virtual Button (Home Screen + Back Tap) → Backend → Pushover.
- GPS with fixed fallback; Wi-Fi uplink; ≥24 h battery; heartbeats; incident log; base subscription tier.

**v2**
- SMS + text-to-voice calls via voip.ms/Twilio (SMS/voice channels).
- Voice-call acknowledgement.
- Paid tier: more recipients + SMS/voice channels.
- Device-down alerting; richer admin/config surface.
