# OQ-1 — RF Verification Procedure (SYNLETT buttons)

**Goal:** confirm the SYNLETT call buttons' **frequency**, **modulation/encoding**, and each button's **code**, and decide whether the codes are **fixed** (buildable) or **rolling** (not). This closes OQ-1 and produces the `code → button_id` table the CC1101 firmware needs.

**Tool:** RTL-SDR **V2** class dongle — actual tuner turned out to be a **Fitipower FC0012** (covers ~22–948 MHz, so 433 & 315 MHz are fine; less sensitive than R820T2, so keep the fob within ~1 m). Standard drivers; no TCXO (frequency wanders — irrelevant for OOK). If it has a bias-tee option in software, keep it **OFF**.

**Companion docs:** `RELAY-NODE-DESIGN.md` (§4, §10), `REQUIREMENTS.md` (OQ-1).

---

## 0. Antenna note

For 433 MHz a **~17 cm** vertical whip is a quarter-wave (for 315 MHz, ~24 cm). The bundled telescopic antenna is fine — just extend it to roughly that length. You'll be receiving a strong transmitter from ≤ 1–2 m away, so antenna precision barely matters here.

---

## 1. Install drivers + software (one-time)

### Windows
1. Plug in the dongle.
2. Run **Zadig** (zadig.akeo.ie) → *Options → List All Devices* → select **"Bulk-In, Interface (Interface 0)"** → set target driver to **WinUSB** → *Replace/Install Driver*.
3. Download prebuilt **rtl_433** for Windows (github.com/merbanan/rtl_433 releases) and unzip. Optionally install **SDR#** or **SDR++** for a waterfall view.
4. Open a terminal in the rtl_433 folder; commands below are the same.

### Linux (Debian/Ubuntu)
```bash
sudo apt update
sudo apt install rtl-433 rtl-sdr
# stop the TV-tuner kernel driver grabbing the dongle:
echo 'blacklist dvb_usb_rtl28xxu' | sudo tee /etc/modprobe.d/blacklist-rtl.conf
# unplug/replug the dongle, then verify it enumerates:
rtl_test        # Ctrl-C after it lists the tuner (R820T2)
```

### macOS
```bash
brew install rtl_433 librtlsdr
rtl_test        # confirm the tuner appears, Ctrl-C
```

> Sanity check for all OSes: `rtl_test` should report the tuner as **R820T2** (or R860 — same chip). If it says "No supported devices found," the driver step didn't take.

---

## 2. Step 1 — Confirm the band (433.92 vs 315 MHz)

Let rtl_433 hop between the two candidate bands while you press a button repeatedly (~1 press/sec for a minute):

```bash
rtl_433 -f 433.92M -f 315M -H 30
```

- `-H 30` hops between the listed frequencies every 30 s.
- Runs the **default** decoders. (The old `-G` / `-G 4` "enable everything" flag is **deprecated in rtl_433 25.x** and prints a warning — don't use it; it just adds false positives.) The default set already covers most EV1527/PT2262-class remotes, so a standard button often decodes right here. If it doesn't, that's normal for an unknown device — use the analyzer in Step 3.

**Watch for lines that appear only when you press.** The frequency printed in the header when those lines show up is your band. Lock onto it for the next step.

*Alternative visual method:* open **SDR++/SDR#/GQRX**, tune 433.92 MHz with a wide (~2 MHz) view, press the button, and watch for a bright vertical burst in the waterfall. If nothing at 433.92, try 315 MHz.

---

## 3. Step 2 — Decode it (the money step)

Sit on the confirmed frequency and press each button several times. Try the **default** decoders first:

```bash
rtl_433 -f 433.92M        # use 315M instead if Step 1 said so
```

If the button uses a standard fixed-code chip (**EV1527 / PT2262-class** — the overwhelmingly likely case), rtl_433 prints a decode line per press with a stable **id/code** field, e.g. a `Generic remote / EV1527` model with an `id:` value. **That id is your `button_id`.** (If nothing decodes on defaults, go to Step 3 — that's the analyzer path for unknown devices.)

Press each of the 3 buttons ~10× and note whether:
- each button yields a **distinct** code, and
- the code is **identical on every press** of the same button.

If both hold → fixed code, buildable. Record them in the table in §7 and you're essentially done.

---

## 4. Step 3 — If nothing decodes (raw pulse analysis)

Drop to the analyzer and save captures:

```bash
rtl_433 -f 433.92M -A -S all
```

- `-A` = pulse **analyzer**: prints pulse/gap timing and *guesses* the modulation (e.g. `OOK_PWM`), often printing candidate parameters and even the bitstream.
- `-S all` = saves each captured signal to a `.cu8` file for §5.

From `-A` output, note the modulation family (PWM/PPM/Manchester) and the short/long pulse widths. EV1527 is **OOK_PWM**, ~24 bits, with a long sync gap between repeats.

---

## 5. Step 4 — Read the bits by hand (only if Step 3 still didn't yield a code)

Install **Universal Radio Hacker**:
```bash
pip install urh        # then run:  urh
```

Open the `.cu8` capture (File → Open) or record live (URH can drive the RTL-SDR directly). In the **Interpretation** tab you'll see the demodulated pulse train.

**EV1527 frame anatomy to look for:**
- A **preamble/sync**: one short high pulse followed by a long low gap (~10–30× the bit period).
- **24 data bits** = **20-bit address** (unique per button) + **4-bit data** (which button/channel).
- Each bit is **PWM**: a `0` = short-high / long-low; a `1` = long-high / short-low.
- The whole frame **repeats** several times per press.

Set URH's bit length to the short-pulse width so the train resolves into clean 0/1s, then read the 24-bit word. The 20-bit address portion is your `button_id`.

---

## 6. Fixed vs rolling — the go/no-go check

For each button, press it **~10 times** and compare the decoded word:

- **Identical every press** → **fixed code**. ✅ The passive-decode design works as planned; proceed to build the CC1101 side.
- **Changes every press** (and never repeats) → **rolling code** (e.g. KeeLoq-style). ⚠️ Passive re-broadcast/decode won't give a stable ID. Stop and rethink: either use different buttons, or tap the SYNLETT *receiver's* decode output instead of sniffing RF. (Very unlikely for a budget caregiver pager, but this is the one result that changes the architecture — so verify it explicitly.)

---

## 7. Results (OQ-1 — CLOSED, 2026-09-15)

**Confirmed signal parameters:**

| Field | Value |
|---|---|
| Confirmed frequency | **433.92 MHz** |
| Modulation | **OOK_PWM** (with sync/delimiter) |
| Encoder family | EV1527 / PT2262-class fixed-code |
| Pulse timing | short ≈ **272–292 µs**, long ≈ **836–864 µs** (~3:1), reset ≈ **1850 µs** |
| Frame | ~68-bit word, repeated ~dozens of times per press |
| Fixed or rolling? | **Fixed** (identical payload every press) ✅ |

**Per-unit codes** (each physical pager = one button; the code identifies the *unit*):

| Unit | Decoded frame (repeating) | Stable match signature | Status |
|---|---|---|---|
| 1 | `d0076803b401da00e` | `0076803b401da00e` | ✅ confirmed, distinct |
| 2 | — (fragments to `{1}8`; never a full frame) | — | ❌ **faulty — retire/replace** |
| 3 | `d3bc29de14ef0a778` | `3bc29de14ef0a778` | ✅ confirmed, distinct |

**Fob #2 finding:** transmits strong RF (RSSI ≈ −7 dB) but never emits a complete frame — the burst arrives as tiny fragments that collapse to a single bit. Consistent with a failing button membrane or an intermittent battery/PCB contact (or a coin cell that sags under TX current). An intermittent panic button is a safety hazard, so fob #2 is retired; capture a replacement the same way units 1 & 3 were done.

**Firmware implications carried into the CC1101 decode spec** (`RELAY-NODE-DESIGN.md` §4):
- **Tolerance band, not exact widths** — units 1 (292/864) and 3 (272/836) differ by ~7%; cheap RC-timed encoders vary 20–30% unit-to-unit. Match short/long within ±~30%.
- **Minimum valid frame length** — require the full ~68-bit word before accepting, so a fragmenting/failing transmitter (fob #2) produces *nothing*, never a partial/false trigger.
- **Repeat-count threshold** — require the frame to repeat N times within the press before firing, then de-bounce the rest of the burst into one incident.
- **Match on the stable signature substring**, not the whole frame (leading nibble shifts with sync alignment).

**OQ-1 is closed:** frequency + modulation confirmed; two distinct, repeatable, fixed codes captured; one unit identified as hardware-faulty.

**Independent regulatory cross-check (§8):** button is labelled **FCC ID 2A4LY-F007 / ISED 29122-F007** — the Zhongshan Gaxin Technology **F007** wireless call button (SYNLETT is a rebrand), companion pager/base ID **2A4WH-QF018**. The FCC grant lists operating frequency **433 MHz**, Part 15 unlicensed ISM — matching the captured 433.92 MHz. OQ-1 is thus verified two independent ways: live RF capture **and** the regulatory record.

---

## 8. Free cross-check (do this first, costs nothing)

Look for an **ISED (Canada)** or **FCC ID** printed on the button or its battery compartment. Search that ID in the ISED REL database (ised-isde.canada.ca) or fcc.gov/oet/ea/fccid — the grant lists the **certified operating frequency**, confirming Step 1 before you even open the software.

---

## 9. What comes after OQ-1

Once the table in §7 is filled with fixed codes:
1. Wire the **CC1101** to the ESP32 (SPI + GDO0) and configure it for the confirmed frequency + OOK.
2. Have firmware match incoming codes against the §7 table → emit `button_id`.
3. Add the 3–5 s de-bounce window (a press repeats the frame several times).

That's the hand-off point to the firmware skeleton.
