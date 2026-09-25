// smoke_test.cpp — bench bring-up check for the CAS Relay Node hardware.
//
// Build and run this BEFORE the real firmware. It answers, in order:
//   1. Does the toolchain build and flash, and does serial work?
//   2. Does the board's GPIO work (LED out, BOOT button in)?
//   3. Does the Wi-Fi radio work?
//   4. Is each CC1101 actually wired up and responding on SPI?
//   5. Does GDO0 carry real RF, and do the pulse widths match config.h?
//
// Stage 5 runs forever so you can walk around pressing buttons. It is also the
// single most useful bench tool in this repo: it validates the decode timing
// constants against your actual transmitters before any decoder is involved.
//
// Procedure and expected output: docs/SMOKE-TEST.md

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>

#include "../config.h"
#include "../pins.h"

// ---------------------------------------------------------------------------
// CC1101 raw SPI access
//
// Deliberately NOT using the CC1101 library here. The point of this stage is to
// test wiring, power and the SPI bus in isolation. Bringing a driver in would
// mean a failure could be either wiring or configuration, which is exactly the
// ambiguity a smoke test exists to remove.
// ---------------------------------------------------------------------------

static const uint8_t CC1101_SRES    = 0x30;  // reset strobe
static const uint8_t CC1101_PARTNUM = 0x30;  // status reg: expect 0x00
static const uint8_t CC1101_VERSION = 0x31;  // status reg: expect 0x04/0x14/0x17

// Wait for the chip to pull MISO low, which is how a CC1101 says "ready".
// Returns false on timeout, which almost always means MISO is not connected.
static bool waitMisoLow(uint32_t timeoutUs = 5000) {
    const uint32_t t0 = micros();
    while (digitalRead(PIN_SPI_MISO) == HIGH) {
        if (micros() - t0 > timeoutUs) return false;
    }
    return true;
}

static void cc1101Reset(int csn) {
    // Manual power-on reset sequence from the CC1101 datasheet §19.1.
    digitalWrite(csn, HIGH);
    delayMicroseconds(5);
    digitalWrite(csn, LOW);
    delayMicroseconds(10);
    digitalWrite(csn, HIGH);
    delayMicroseconds(45);

    digitalWrite(csn, LOW);
    waitMisoLow();
    SPI.transfer(CC1101_SRES);
    waitMisoLow();
    digitalWrite(csn, HIGH);
    delay(1);
}

// Status registers (0x30-0x3D) require the burst bit set alongside read.
static uint8_t cc1101ReadStatus(int csn, uint8_t addr, bool& ok) {
    digitalWrite(csn, LOW);
    if (!waitMisoLow()) {
        digitalWrite(csn, HIGH);
        ok = false;
        return 0xFF;
    }
    SPI.transfer(addr | 0xC0);
    const uint8_t v = SPI.transfer(0x00);
    digitalWrite(csn, HIGH);
    ok = true;
    return v;
}

struct RadioProbe {
    const char* tag;
    int         csn;
    int         gdo0;
    bool        present;
    uint8_t     partnum;
    uint8_t     version;
};

static RadioProbe gProbes[] = {
    {"A", PIN_A_CSN, PIN_A_GDO0, false, 0, 0},
    {"B", PIN_B_CSN, PIN_B_GDO0, false, 0, 0},
};
static const uint8_t kProbeCount = sizeof(gProbes) / sizeof(gProbes[0]);

// ---------------------------------------------------------------------------
// GDO0 edge capture (stage 5)
// ---------------------------------------------------------------------------

struct EdgeCounter {
    volatile uint32_t edges      = 0;
    volatile uint32_t shortHighs = 0;  // inside config.h's short band
    volatile uint32_t longHighs  = 0;  // inside config.h's long band
    volatile uint32_t otherHighs = 0;  // outside both -> noise, or wrong bands
    volatile uint32_t minHighUs  = 0xFFFFFFFF;
    volatile uint32_t maxHighUs  = 0;
    volatile uint32_t lastEdgeUs = 0;
    int               pin        = -1;
};

static EdgeCounter gCounters[kProbeCount];

static void IRAM_ATTR onEdgeIsr(void* arg) {
    EdgeCounter* c = static_cast<EdgeCounter*>(arg);
    const uint32_t now = micros();
    const uint32_t dur = now - c->lastEdgeUs;
    c->lastEdgeUs = now;
    c->edges++;

    // digitalRead returns the level AFTER the edge, so the period just measured
    // was spent at the opposite level. A LOW now means we just timed a HIGH.
    if (digitalRead(c->pin) != LOW) return;

    if (dur < c->minHighUs) c->minHighUs = dur;
    if (dur > c->maxHighUs) c->maxHighUs = dur;

    if (dur >= OOK_SHORT_MIN_US && dur <= OOK_SHORT_MAX_US)      c->shortHighs++;
    else if (dur >= OOK_LONG_MIN_US && dur <= OOK_LONG_MAX_US)   c->longHighs++;
    else                                                          c->otherHighs++;
}

static void resetCounter(EdgeCounter& c) {
    noInterrupts();
    c.edges = c.shortHighs = c.longHighs = c.otherHighs = 0;
    c.minHighUs = 0xFFFFFFFF;
    c.maxHighUs = 0;
    interrupts();
}

// ---------------------------------------------------------------------------
// Stages
// ---------------------------------------------------------------------------

static void banner(const char* title) {
    Serial.println();
    Serial.println("==================================================");
    Serial.printf("  %s\n", title);
    Serial.println("==================================================");
}

static void stage1ChipInfo() {
    banner("STAGE 1 - board and toolchain");
    Serial.printf("  chip model    : %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
    Serial.printf("  cores         : %d\n", ESP.getChipCores());
    Serial.printf("  cpu freq      : %lu MHz\n", (unsigned long)ESP.getCpuFreqMHz());
    Serial.printf("  flash size    : %lu bytes\n", (unsigned long)ESP.getFlashChipSize());
    Serial.printf("  free heap     : %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("  MAC           : %s\n", WiFi.macAddress().c_str());
    Serial.println("  PASS if you can read this line at 115200 baud.");
}

static void stage2Gpio() {
    banner("STAGE 2 - GPIO");
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_TEST_BTN, INPUT_PULLUP);

    Serial.printf("  blinking LED on GPIO%d five times - watch the board\n", PIN_STATUS_LED);
    for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_STATUS_LED, HIGH);
        delay(150);
        digitalWrite(PIN_STATUS_LED, LOW);
        delay(150);
    }

    Serial.printf("  BOOT button on GPIO%d reads: %s (expect HIGH when not pressed)\n",
                  PIN_TEST_BTN, digitalRead(PIN_TEST_BTN) ? "HIGH" : "LOW");
    Serial.println("  hold BOOT now - you have 3 seconds...");
    bool sawPress = false;
    const uint32_t t0 = millis();
    while (millis() - t0 < 3000) {
        if (digitalRead(PIN_TEST_BTN) == LOW) sawPress = true;
        delay(10);
    }
    Serial.printf("  %s BOOT button %s\n",
                  sawPress ? "PASS -" : "SKIP -",
                  sawPress ? "detected" : "never went LOW (not fatal, it is only a test input)");
}

static void stage3WiFi() {
    banner("STAGE 3 - Wi-Fi radio");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("  scanning...");
    const int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("  FAIL - no networks found. The Wi-Fi radio or antenna is suspect.");
        return;
    }

    Serial.printf("  PASS - found %d networks. Strongest few:\n", n);
    for (int i = 0; i < n && i < 5; i++) {
        Serial.printf("    %-32s %4d dBm\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }

    if (strcmp(WIFI_SSID, "CHANGEME") == 0) {
        Serial.println("  SKIP - WIFI_SSID is still CHANGEME, not attempting to connect.");
        WiFi.scanDelete();
        return;
    }

    Serial.printf("  connecting to %s ...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  PASS - connected, IP %s, RSSI %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("  FAIL - could not join. Check SSID/password and that it is 2.4 GHz.");
        Serial.println("         ESP32 does not support 5 GHz networks.");
    }
    WiFi.scanDelete();
}

static void stage4Cc1101() {
    banner("STAGE 4 - CC1101 modules over SPI");

    for (uint8_t i = 0; i < kProbeCount; i++) {
        pinMode(gProbes[i].csn, OUTPUT);
        digitalWrite(gProbes[i].csn, HIGH);
    }
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

    for (uint8_t i = 0; i < kProbeCount; i++) {
        RadioProbe& p = gProbes[i];
        Serial.printf("  radio %s (CSN=GPIO%d, GDO0=GPIO%d):\n", p.tag, p.csn, p.gdo0);

        cc1101Reset(p.csn);

        bool ok1 = false, ok2 = false;
        p.partnum = cc1101ReadStatus(p.csn, CC1101_PARTNUM, ok1);
        p.version = cc1101ReadStatus(p.csn, CC1101_VERSION, ok2);

        if (!ok1 || !ok2) {
            Serial.println("    FAIL - MISO never went low. The chip is not responding at all.");
            Serial.println("           Check 3V3, GND, and the MISO wire first.");
            p.present = false;
            continue;
        }

        Serial.printf("    PARTNUM=0x%02X  VERSION=0x%02X\n", p.partnum, p.version);

        if (p.partnum == 0x00 && (p.version == 0x04 || p.version == 0x14 || p.version == 0x17)) {
            Serial.println("    PASS - CC1101 detected.");
            p.present = true;
        } else if (p.partnum == 0xFF && p.version == 0xFF) {
            Serial.println("    FAIL - reads all 1s. MISO is floating: check the MISO wire and power.");
            p.present = false;
        } else if (p.partnum == 0x00 && p.version == 0x00) {
            Serial.println("    FAIL - reads all 0s. MISO is stuck low, or SCK/MOSI are swapped.");
            p.present = false;
        } else {
            Serial.println("    WARN - responded, but not with a known CC1101 signature.");
            Serial.println("           Could be a clone. Treat stage 5 as the real test.");
            p.present = true;
        }
    }

    SPI.endTransaction();
}

static void stage5Setup() {
    banner("STAGE 5 - live RF on GDO0");
    Serial.println("  NOTE: this stage does NOT configure the radio, so GDO0 shows");
    Serial.println("  whatever the chip drives after reset. Some modules idle silent");
    Serial.println("  here. If you see zero edges, that is expected until the real");
    Serial.println("  firmware puts the CC1101 into OOK async serial mode.");
    Serial.println();
    Serial.printf("  Expected pulse bands from config.h:  short %lu-%lu us   long %lu-%lu us\n",
                  (unsigned long)OOK_SHORT_MIN_US, (unsigned long)OOK_SHORT_MAX_US,
                  (unsigned long)OOK_LONG_MIN_US,  (unsigned long)OOK_LONG_MAX_US);
    Serial.println();
    Serial.println("  Press a SYNLETT button near the node and watch the counts.");
    Serial.println("  A good press shows a burst of edges with most HIGH pulses");
    Serial.println("  landing in the short and long bands, and few in 'other'.");
    Serial.println();

    for (uint8_t i = 0; i < kProbeCount; i++) {
        gCounters[i].pin = gProbes[i].gdo0;
        gCounters[i].lastEdgeUs = micros();
        pinMode(gProbes[i].gdo0, INPUT);
        attachInterruptArg(digitalPinToInterrupt(gProbes[i].gdo0),
                           onEdgeIsr, &gCounters[i], CHANGE);
    }
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(1500);  // give the USB serial port time to attach

    Serial.println();
    Serial.println("CAS Relay Node - HARDWARE SMOKE TEST");
    Serial.printf("firmware %s / built %s %s\n", CAS_FW_VERSION, __DATE__, __TIME__);
    Serial.println("See docs/SMOKE-TEST.md for what each stage proves.");

    stage1ChipInfo();
    stage2Gpio();
    stage3WiFi();
    stage4Cc1101();
    stage5Setup();
}

void loop() {
    static uint32_t lastReport = 0;
    const uint32_t now = millis();
    if (now - lastReport < 1000) {
        // Slow heartbeat on the LED so it is obvious the test is still running.
        digitalWrite(PIN_STATUS_LED, (now % 1000) < 50 ? HIGH : LOW);
        return;
    }
    lastReport = now;

    for (uint8_t i = 0; i < kProbeCount; i++) {
        EdgeCounter& c = gCounters[i];
        const uint32_t edges = c.edges;
        if (edges == 0) continue;  // stay quiet when there is no RF

        const uint32_t s = c.shortHighs, l = c.longHighs, o = c.otherHighs;
        const uint32_t mn = c.minHighUs, mx = c.maxHighUs;
        resetCounter(c);

        Serial.printf("  radio %s: %5lu edges | HIGH pulses  short=%-4lu long=%-4lu other=%-4lu"
                      " | range %lu-%lu us\n",
                      gProbes[i].tag, (unsigned long)edges,
                      (unsigned long)s, (unsigned long)l, (unsigned long)o,
                      (unsigned long)(mn == 0xFFFFFFFF ? 0 : mn), (unsigned long)mx);
    }
}
