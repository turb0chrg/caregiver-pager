# Relay Node — Pin Map (budget build, v0.1)

**Project:** Caregiver Alert System (CAS)
**Board:** ESP32-WROOM-32D DevKitC, 38-pin, USB-C
**Date:** 2026-09-24
**Companion docs:** `REQUIREMENTS.md`, `RELAY-NODE-DESIGN.md`, `FIRMWARE.md`, `RF-VERIFICATION.md`

---

## 0. Build scope

Per `CLAUDE.md`, the Relay Node is a **Wi-Fi-only, mains-powered, fixed-location**
node on a plain ESP32-WROOM-32D DevKitC with **two** CC1101 receivers.

What that means for `REQUIREMENTS.md`:

| Requirement | Status |
|---|---|
| CN-1, CN-6 (Wi-Fi uplink over TLS) | **Met.** Wi-Fi is the uplink. |
| FR-A6 (queue + retry) | **Met**, and load-bearing: it is the only thing standing between a router reboot and a lost alert. |
| FR-L1 (onboard GNSS) | **Not fitted.** The node is fixed-location, so every incident carries the configured install location (FR-L2), flagged `mode: "fixed"` per FR-L3. |
| PW-1 (≥24 h battery backup) | **Not fitted.** Mains only. |
| HW-5, PW-3 (radio wake / low power) | **Not needed.** Mains-powered, so the MCU stays awake and GDO0 carries raw data rather than acting as a wake line. GDO2 is wired anyway to keep Wake-on-Radio available later. |
| FR-A1…A5, FR-A7, FR-A8, REL-1 | **Met.** |

---

## 1. Why two CC1101s

The second receiver is **RX redundancy**, not extra throughput. Both radios sit on
the same SPI bus, tune the same 433.92 MHz channel, and decode independently with
their own GDO0 line and their own pulse decoder. Their outputs merge in the
de-bounce layer, keyed on the button signature, so a press heard by **either**
radio raises exactly one incident (FR-A5).

That buys two things:

- **Antenna diversity.** Two whips a few tens of cm apart, ideally at right
  angles, kill the multipath null that would otherwise make one corner of the
  house unreliable.
- **Single-radio failure tolerance.** A dead or desensed module degrades range
  rather than silencing the node. Per-radio decode counters are reported in the
  heartbeat (FR-A7) so a silent radio is visible before it matters.

---

## 2. Bus topology

One shared VSPI bus, two chip selects, two interrupt lines.

```
                 ESP32-WROOM-32D (DevKitC)
                 ┌───────────────────────────┐
                 │                           │
   ┌─────────────┤ GPIO18  SCK  ─────────────┼──────┬──────────┐
   │  ┌──────────┤ GPIO23  MOSI ─────────────┼───┬──┼───────┐  │
   │  │  ┌───────┤ GPIO19  MISO ─────────────┼┬──┼──┼────┐  │  │
   │  │  │       │                           ││  │  │    │  │  │
   │  │  │  ┌────┤ GPIO21  CSN_A             ││  │  │    │  │  │
   │  │  │  │ ┌──┤ GPIO22  CSN_B             ││  │  │    │  │  │
   │  │  │  │ │  │                           ││  │  │    │  │  │
   │  │  │  │ │  │ GPIO34  GDO0_A ◄──────────┼┼──┼──┼─┐  │  │  │
   │  │  │  │ │  │ GPIO35  GDO0_B ◄──────────┼┼──┼──┼─┼──┼──┼──┼─┐
   │  │  │  │ │  │ GPIO25  GDO2_A ◄──────────┼┼──┼──┼─┼┐ │  │  │ │
   │  │  │  │ │  │ GPIO26  GDO2_B ◄──────────┼┼──┼──┼─┼┼─┼──┼──┼─┼┐
   │  │  │  │ │  │ GPIO2   STATUS LED        ││  │  │ ││ │  │  │ ││
   │  │  │  │ │  └───────────────────────────┘│  │  │ ││ │  │  │ ││
   │  │  │  │ │      ┌────────────────────────┴──┴──┴─┴┴┐ │  │  │ ││
   │  │  │  │ └──────┤ CSN   MISO  MOSI  SCK  GDO0 GDO2 │ │  │  │ ││
   │  │  │  │        │           CC1101  "A"            │ │  │  │ ││
   │  │  │  │        │        433 MHz whip ~17.3 cm     │ │  │  │ ││
   │  │  │  │        └──────────────────────────────────┘ │  │  │ ││
   │  │  │  │        ┌─────────────────────────────────────┴──┴──┴─┴┴┐
   │  │  │  └────────┤ CSN   MISO  MOSI  SCK            GDO0    GDO2 │
   │  │  │           │                CC1101  "B"                    │
   │  │  │           │             433 MHz whip ~17.3 cm             │
   └──┴──┴───────────┴───────────────────────────────────────────────┘
```

MISO is shared safely: the CC1101 tri-states SO whenever CSN is high.

---

## 3. Wiring table

CC1101 module = the common 8-pin green breakout (E07-M1101D and clones).

| CC1101 pin | Radio A | Radio B | Notes |
|---|---|---|---|
| VCC | 3V3 | 3V3 | **3.3 V only.** The CC1101 is *not* 5 V tolerant. Never feed it the DevKitC `5V` pin. |
| GND | GND | GND | Use two different GND pins on the board; keep the return short. |
| SCK | **GPIO18** | **GPIO18** | Shared VSPI clock. |
| MOSI | **GPIO23** | **GPIO23** | Shared VSPI MOSI. |
| MISO / GDO1 | **GPIO19** | **GPIO19** | Shared VSPI MISO; tri-stated when CSN is high. |
| CSN | **GPIO21** | **GPIO22** | Per-radio chip select. Idle high. |
| GDO0 | **GPIO34** | **GPIO35** | Raw demodulated OOK data in async serial mode. Input-only pins — correct, this line is never driven by the ESP32. |
| GDO2 | **GPIO25** | **GPIO26** | Not used by v0.1 firmware. Wired now so carrier-sense / Wake-on-Radio is available without a rework. |

Other pins in use:

| Function | GPIO | Notes |
|---|---|---|
| Status LED | **GPIO2** | Onboard blue LED on most DevKitC boards. Strapping pin, but an LED-to-GND load is safe at boot. |
| Test / provisioning button | **GPIO0** | The onboard BOOT button, reused at runtime. Long-press = self-test. |
| USB-C serial console | GPIO1 / GPIO3 | UART0. Do not reuse. |

### Required passives

| Part | Where | Why |
|---|---|---|
| 100 kΩ to GND | on **GPIO34** and **GPIO35** | GPIO34/35 have **no internal pull resistors**. Between power-up and CC1101 init, GDO0 floats and the edge ISR would otherwise count noise. |
| 10 µF + 100 nF | across VCC/GND **at each CC1101** | The module has minimal local bulk. Decouple at the module, not at the DevKitC. |
| ~17.3 cm wire whip or SMA | each CC1101 ANT pad | Quarter-wave at 433.92 MHz. Keep the two antennas ≥ 20 cm apart and roughly orthogonal (see §1). |

---

## 4. Reserved and forbidden pins

| GPIO | Status | Reason |
|---|---|---|
| 6, 7, 8, 9, 10, 11 | **Never use** | Wired to the SPI flash inside the WROOM module. Using them bricks boot. |
| 1, 3 | Reserved | UART0 console / flashing. |
| 12 | **Avoid** | MTDI strapping pin. Held high at boot it sets the flash to 1.8 V and the board will not start. |
| 0, 2, 5, 15 | Use with care | Strapping pins. GPIO0 (BOOT button) and GPIO2 (LED) are used here in ways that are boot-safe; GPIO5 and GPIO15 are left free. |
| 34–39 | Input only | No output drive, no internal pull-ups/downs. GPIO34/35 are used for GDO0 precisely because those lines are inputs. |
| ADC2 channels (0, 2, 4, 12, 13, 14, 15, 25, 26, 27) | No ADC while Wi-Fi is up | ADC2 is unavailable when the Wi-Fi radio is active. Irrelevant here — GPIO25/26 are used as plain digital inputs — but it rules those pins out for any future battery-sense divider. Use an ADC1 pin (32/33/36/39) for that. |

### Free for expansion

GPIO4, GPIO13, GPIO14, GPIO16, GPIO17, GPIO27, GPIO32, GPIO33, GPIO5, GPIO15.

GPIO32 and GPIO33 are the ones to reach for first: they are RTC-capable *and* on
ADC1, so they suit either a wake source or a battery-voltage divider if battery
backup is ever added (see `RELAY-NODE-DESIGN.md` §5.1).

---

## 5. CC1101 configuration target

Driven by the OQ-1 captures in `RF-VERIFICATION.md` §7.

| Setting | Value | Source |
|---|---|---|
| Carrier | 433.92 MHz | OQ-1 confirmed; cross-checked against FCC ID 2A4LY-F007 |
| Modulation | ASK/OOK | OQ-1 |
| Packet format | **Asynchronous serial** | Puts the raw demodulated envelope on GDO0 so the MCU can time pulses itself. The CC1101 packet engine cannot parse EV1527 PWM. |
| RX bandwidth | ~203 kHz | Wide enough for an untrimmed RC-timed transmitter and the dongle-confirmed drift |
| Data rate register | ~4 kBaud | Only sets the demodulator's internal filtering in async mode; it does not gate the bitstream |
| Sync word / CRC / address | **Disabled** | No packet engine in async mode |

AGC and carrier-sense thresholds are the one part of this that **must be tuned on
the bench**, not taken from a datasheet default. Start with max LNA gain and no
carrier-sense squelch, then raise the threshold only as far as needed to stop
noise-triggered frame aborts. Watch the per-radio abort counter while doing it.

---

## 6. Bring-up order

1. Power the board with **no CC1101s attached**. Confirm it boots and the console
   prints the banner. This proves nothing is loading the strapping pins.
2. Attach **Radio A only**. Confirm the firmware reads back the CC1101 part number
   and version registers over SPI. A read of `0x00`/`0xFF` means wiring or power,
   not configuration.
3. Press **Unit 1**. Expect the raw-frame log to show repeated 68-bit frames whose
   trailing 64 bits are `0076803b401da00e`.
4. Press **Unit 3**. Expect `3bc29de14ef0a778`.
5. Press **Unit 2**. Expect **nothing** — no frame, no incident. Unit 2 is the
   known-faulty fragmenting transmitter, and silence here is the pass condition
   for decode rule 2, not a failure.
6. Attach **Radio B**. Confirm both radios enumerate and that a single press
   produces two decode log lines but exactly **one** incident.
7. Walk the house pressing Unit 1, logging per-radio decode counts, to place the
   two antennas.
