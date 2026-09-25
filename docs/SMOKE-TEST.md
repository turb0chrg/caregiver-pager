# Relay Node — Hardware Smoke Test

**Run this before the real firmware.** It proves the board, the USB driver, the
toolchain, the Wi-Fi radio and the CC1101 wiring all work, one layer at a time,
so that when something fails later you already know it isn't any of those.

Written for a first ESP32 build. If you have done this before, the short version
is: `pio run -e smoke-test -t upload && pio device monitor`, then read §5 and §6.

**Companion docs:** `FIRMWARE.md`, `PIN-MAP.md`, `RF-VERIFICATION.md`
**Source:** `firmware/src/smoke/smoke_test.cpp`

---

## 0. What you need

- The ESP32-WROOM-32D DevKitC and a USB-C cable. **Check the cable carries data.**
  A charge-only cable is the single most common first-time failure and looks
  identical to a good one.
- Python 3 and PlatformIO Core: `pip install platformio`
  (or the PlatformIO IDE extension for VS Code, which bundles it).
- The CC1101 modules and jumper wires — but **do not connect them yet**. Stage 4
  is where they go on.

---

## 1. Before you plug anything in

Do the board on its own first, with **no CC1101s attached**. If a miswired
module pulls on a strapping pin, the board won't boot, and you'd be debugging
two problems at once.

---

## 2. Windows USB driver

The DevKitC talks to your PC through a USB-to-serial chip. Windows needs its
driver. Which one depends on the chip next to the USB-C connector:

| Chip marking | Driver |
|---|---|
| **CP2102** / **CP2102N** (Silicon Labs) | "CP210x USB to UART Bridge VCP Drivers" from silabs.com. Most genuine DevKitC boards. |
| **CH340** / **CH9102** (WCH) | "CH341SER" from wch.cn. Common on clones. |

Plug the board in and check it appeared:

```bash
pio device list
```

You want a line naming a COM port, e.g. `COM5`. If you see nothing, or Device
Manager shows a yellow "Unknown device", the driver is missing — install it and
replug. Note the port number; you'll need it if auto-detect fails.

---

## 3. Build and flash the smoke test

```bash
cd firmware
pio run -e smoke-test -t upload
```

The first build downloads the ESP32 toolchain and takes a few minutes. Later
builds are seconds.

If upload fails with a timeout or "Failed to connect to ESP32":

- **Hold the BOOT button** while the upload starts, release once it says
  "Writing at...". Some boards can't auto-enter bootloader mode.
- Close anything else holding the port, including an open serial monitor.
- Drop the speed: add `upload_speed = 115200` under `[env]` in `platformio.ini`,
  and name the port explicitly with `--upload-port COM5` if auto-detect picks
  the wrong one.

Then open the console:

```bash
pio device monitor
```

Press the board's **EN/RST** button to re-run the test from the top at any time.

---

## 4. Stages 1 to 3 — board, GPIO, Wi-Fi

You should see something like:

```
CAS Relay Node - HARDWARE SMOKE TEST

==================================================
  STAGE 1 - board and toolchain
==================================================
  chip model    : ESP32-D0WD-V3 rev 3
  cores         : 2
  cpu freq      : 240 MHz
  flash size    : 4194304 bytes
  MAC           : 24:6F:28:XX:XX:XX
  PASS if you can read this line at 115200 baud.
```

| Stage | Proves | What failure means |
|---|---|---|
| **1** Board info | Toolchain, flashing and serial all work | Garbled text = wrong baud. Set the monitor to 115200. |
| **2** GPIO | LED output and BOOT input work | LED never blinks: your board may not have an onboard LED on GPIO2. Not fatal — note it and move on. |
| **3** Wi-Fi | The radio and antenna work | No networks found = suspect the module. Join fails = check credentials, and that the network is **2.4 GHz**. ESP32 cannot see 5 GHz networks at all. |

Stage 3 only tries to connect if you've set real credentials in `config.h`.
Otherwise it just scans, which is enough to prove the radio.

**Stop here if any of these fail.** None of the RF work can be debugged on a
board that isn't sound.

---

## 5. Stage 4 — CC1101 wiring

Now **unplug USB**, wire the radios per `PIN-MAP.md` §3, and plug back in.

Wire **radio A only** for the first run. Two modules wired at once means a fault
could be either of them, or the shared bus.

This stage talks to the chip with raw SPI rather than a driver, on purpose: a
failure here is unambiguously wiring, power or the bus, never configuration.

```
  radio A (CSN=GPIO21, GDO0=GPIO34):
    PARTNUM=0x00  VERSION=0x14
    PASS - CC1101 detected.
  radio B (CSN=GPIO22, GDO0=GPIO35):
    FAIL - MISO never went low. The chip is not responding at all.
```

The readback tells you *which* wire is wrong:

| Reading | Meaning | Check first |
|---|---|---|
| `PARTNUM=0x00 VERSION=0x14` (or `0x04`/`0x17`) | Working | — |
| `MISO never went low` | No response at all | 3V3 and GND at the module, then the MISO wire |
| `all 1s` (`0xFF`/`0xFF`) | MISO floating | The MISO wire, and that the module has power |
| `all 0s` (`0x00`/`0x00`) | MISO stuck low | SCK and MOSI may be swapped |
| Responded, unknown signature | Possibly a clone | Not necessarily broken — judge on stage 5 |

Two things that bite here:

- **The CC1101 is not 5 V tolerant.** It goes on `3V3`, never the `5V` pin.
- **GPIO34 and GPIO35 need external 100k pulldowns** to GND. They have no
  internal pull resistors, so without them GDO0 floats and stage 5 reports
  phantom edges.

Once radio A passes, power down, add radio B, and re-run. Both should pass.

---

## 6. Stage 5 — live RF

This stage runs until you reset the board. It counts edges on each GDO0 line and
sorts the HIGH pulses into the bands from `config.h`.

**Read the note it prints first:** the smoke test deliberately does not
configure the radio, so on many modules GDO0 sits silent here. **Zero edges at
this stage is not a failure.** GDO0 only carries the demodulated envelope once
the real firmware puts the chip into OOK asynchronous serial mode.

If your module does drive GDO0 after reset, press a SYNLETT button near the node
and you'll see bursts:

```
  radio A:  1432 edges | HIGH pulses  short=34   long=31   other=5    | range 271-869 us
```

That line is the payoff. It says the pulse widths coming off your actual
transmitters land inside the short and long bands the decoder expects, which
validates the timing constants before any decode logic is involved.

| What you see | Reading it |
|---|---|
| Steady counts with nothing transmitting | Noise, or a missing pulldown on GPIO34/35 |
| Burst on press, mostly `short` and `long` | Exactly right — the bands match your units |
| Burst on press, mostly `other` | Widths are outside the bands. Note the printed range and compare against `RF-VERIFICATION.md` §7 |
| Nothing at all on press | Expected at this stage. Confirm with the real firmware, which configures the radio |

If `other` dominates and the printed range sits well away from roughly 280 µs
and 850 µs, do not widen the tolerance to make it pass. Re-check the capture in
`RF-VERIFICATION.md` first: the bands are already ±30%, and a unit outside that
is more likely a different transmitter or a failing one than a tolerance
problem.

---

## 7. When it all passes

```bash
pio run -e relay-node -t upload && pio device monitor
```

Continue with `PIN-MAP.md` §6, which picks up at pressing unit 1 and confirming
the decoded signature. Note that **unit 2 producing nothing is a pass**, not a
failure — it is the known-faulty transmitter, and silence is the fail-safe
working.

Before that firmware is trusted for anything real, work through `FIRMWARE.md` §5.
It ships with placeholder Wi-Fi credentials, a placeholder backend URL and secret,
and no pinned CA certificate.
