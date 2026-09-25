# Caregiver Alert System (CAS)

Custom caregiver alert built on the SYNLETT/Gaxin F007 wireless pager.
See REQUIREMENTS.md, RELAY-NODE-DESIGN.md, PIN-MAP.md, FIRMWARE.md, RF-VERIFICATION.md, RELAY-NODE-BOM.xlsx.

## Hardware (budget build)
- ESP32-WROOM-32D, USB-C, DevKitC (KIT B pin breakout) — WiFi-only, mains-powered, fixed location
- CC1101 433 MHz receiver ×2 (SPI + GDO0 wake)
- Buttons: SYNLETT F007, 433.92 MHz OOK_PWM, fixed code

## OQ-1 CONFIRMED
- 433.92 MHz, OOK_PWM, short≈272–292µs / long≈836–864µs / reset≈1850µs
- Unit 1 code: d0076803b401da00e   (sig: 0076803b401da00e)
- Unit 3 code: d3bc29de14ef0a778   (sig: 3bc29de14ef0a778)
- Unit 2: faulty, retire/replace

## CC1101 decode rules (load-bearing)
1. Tolerance band ±~30% on pulse widths, never exact
2. Require full ~68-bit frame before accepting (fail-safe on fragments)
3. Repeat-count threshold, then de-bounce burst into one incident
4. Match on stable signature substring, not whole frame

## Conventions
- Firmware target: ESP32, Arduino framework under PlatformIO (see FIRMWARE.md §1)
- Docs are Markdown; keep them updated as the build progresses