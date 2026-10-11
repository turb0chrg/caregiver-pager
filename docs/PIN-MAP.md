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

### Tenstar CC1101 module: numbered-pin map

The Tenstar module has only pin numbers on the silkscreen, no names. Pin 1 = GND was
confirmed by continuity to the antenna ground; the remaining order follows the
Elechouse-compatible layout this module copies. Re-check against your board before first power-up.

| Board pin | Function | Radio A → ESP32 | Radio B → ESP32 | Suggested wire colour |
|---|---|---|---|---|
| 1 | GND | GND | GND | Black |
| 2 | VCC (3.3 V) | 3V3 | 3V3 | Red |
| 3 | GDO0 | GPIO34 | GPIO35 | Orange |
| 4 | CSN | GPIO21 | GPIO22 | White (A) / Grey (B) |
| 5 | SCK | GPIO18 | GPIO18 | Yellow |
| 6 | MOSI (SI) | GPIO23 | GPIO23 | Green |
| 7 | MISO (SO / GDO1) | GPIO19 | GPIO19 | Blue |
| 8 | GDO2 | GPIO25 | GPIO26 | Brown (optional, unused by v0.1 firmware) |

Wire colours are a suggestion, not a standard. Only red = positive and black = ground are
widely followed. Keep the same colour for the same signal on both radios, and never use
red for anything other than 3V3.

Before first power-up: confirm pin 1 beeps to the antenna ground, and confirm pin 2 does
**not** beep to ground. A swapped VCC and GND can destroy the module.

On the KIT B breakout, each GPIO has an S/V/G header row: **S** (yellow) is the GPIO signal,
**V** (red) is the shared power rail, **G** (black) is ground. Take GND from a G pin, VCC from
a V pin, and every CC1101 signal from the **S** pin of the matching GPIO row. The V rail is
usually selectable between 3.3 V and 5 V by a jumper or switch. Set it to 3.3 V and measure
about 3.3 V between a V and a G pin before connecting any CC1101.

The ESP32-WROOM-32U is pin-identical to the 32D. It differs only in having a U.FL socket
for an external antenna, so every GPIO assignment in this document applies unchanged.

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

#### Wiring the passives on a breadboard

A Dupont wire already occupies the breakout's S pin, so the resistor has no free hole there.
The Tenstar's 2×4 header cannot be plugged into the breadboard: its pins would land in the
same breadboard row and short together. Instead, use **female-to-male Dupont jumpers** (2.54 mm
pitch) from each module pin to its own breadboard row, and use that row as a junction.

Header orientation, antenna end up, numbers readable (odd pins in one column, even in the other):

```
   1 GND    2 VCC
   3 GDO0   4 CSN
   5 SCK    6 MOSI
   7 MISO   8 GDO2
```

Half-size breadboard: holes a–e of a row are connected, f–j are connected, and the centre gap
separates them. Rows 5–12 are used for Radio A and rows 18–25 for Radio B.

| Breadboard row (Radio A / B) | Module pin | Jumper from module pin to | Then from column e to | Wire colour |
|---|---|---|---|---|
| 5 / 18 | 1 GND | a5 / a18 | blue **−** rail | Black |
| 6 / 19 | 2 VCC | a6 / a19 | red **+** rail | Red |
| 7 / 20 | 3 GDO0 | a7 / a20 | GPIO34 **S** / GPIO35 **S** | Orange |
| 8 / 21 | 4 CSN | a8 / a21 | GPIO21 **S** / GPIO22 **S** | White (A) / Grey (B) |
| 9 / 22 | 5 SCK | a9 / a22 | GPIO18 **S** (A); link e9 to e22 for B | Yellow |
| 10 / 23 | 6 MOSI | a10 / a23 | GPIO23 **S** (A); link e10 to e23 for B | Green |
| 11 / 24 | 7 MISO | a11 / a24 | GPIO19 **S** (A); link e11 to e24 for B | Blue |
| 12 / 25 | 8 GDO2 | a12 / a25 | GPIO25 **S** / GPIO26 **S** (optional) | Brown |

Use the same colour for both jumpers on a signal (module pin to breadboard, breadboard to
ESP32), and for the A-to-B link wires. Colours are a suggestion, not a standard. Red for
power and black for ground is the only widely followed convention, so keep red for 3V3 only.

Passives, Radio A (add 13 to each row number for Radio B):

- **Pull-down:** 100 kΩ from **c7** to **c5**, between GDO0 and GND. The resistor sits at the
  module, which is electrically the same as at the ESP32.
- **100 nF ceramic:** **b5** to **b6**, between GND and VCC. No polarity.
- **10 µF electrolytic:** **d5** to **d6**. The long leg (+) goes in d6 (VCC) and the stripe
  side (−) goes in d5 (GND). Reversing it can make it fail.
- Keep the caps in the holes next to the module's VCC and GND rows.
- Jumper the red **+** rail to a V pin and the blue **−** rail to a G pin on the ESP32 breakout.
  Confirm the V rail measures 3.3 V first. Check the rails are continuous top to bottom, since
  some breadboards split them in the middle.
- If the module header is 1.27 mm pitch, standard Dupont connectors won't fit. Use a
  1.27 mm-to-2.54 mm adapter or solder wires to the pins.

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
