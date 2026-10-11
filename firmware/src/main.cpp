// main.cpp — CAS Relay Node, budget build.
//
//   SYNLETT/Gaxin F007 button
//        │ 433.92 MHz OOK
//        ▼
//   CC1101 A ─┐                      ┌─ OokDecoder A ─┐
//             ├─ GDO0 edge ISR ──────┤                ├─ Debouncer ─► Incident
//   CC1101 B ─┘                      └─ OokDecoder B ─┘      │
//                                                            ▼
//                                              queue ─► HMAC-signed HTTPS POST
//
// Both radios decode independently and merge at the Debouncer, keyed on the
// frame signature, so one press raises one incident however many receivers
// heard it.
//
// REL-1 is structural, not defended by code here: this node only listens. The
// SYNLETT pager keeps working if every line below fails.

#include <Arduino.h>

#include "button_table.h"
#include "cc1101_rx.h"
#include "config.h"
#include "debounce.h"
#include "incident.h"
#include "net_uplink.h"
#include "ook_decoder.h"
#include "pins.h"

static Cc1101Rx gRadioA(0, PIN_A_CSN, PIN_A_GDO0, PIN_A_GDO2, "A");
static Cc1101Rx gRadioB(1, PIN_B_CSN, PIN_B_GDO0, PIN_B_GDO2, "B");
static Cc1101Rx* const gRadios[] = {&gRadioA, &gRadioB};
static const uint8_t kRadioCount = sizeof(gRadios) / sizeof(gRadios[0]);

static Debouncer gDebouncer;

// ---------------------------------------------------------------------------
// Status LED (FR-A8)
// ---------------------------------------------------------------------------
enum class Health { Ok, NoNetwork, RadioFault };

static Health gHealth = Health::Ok;

static void serviceLed() {
    // Slow pulse = healthy. Fast blink = no uplink. Double blink = a radio is
    // down. Chosen so "something is wrong" is legible from across the room
    // without reading anything.
    const uint32_t now = millis();
    bool on = false;
    switch (gHealth) {
        case Health::Ok:         on = (now % 3000) < 80;  break;
        case Health::NoNetwork:  on = (now % 500)  < 250; break;
        case Health::RadioFault: on = ((now % 1200) < 120) || ((now % 1200) > 240 && (now % 1200) < 360); break;
    }
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

static void updateHealth() {
    bool radiosOk = true;
    for (uint8_t i = 0; i < kRadioCount; i++) {
        if (!gRadios[i]->ok()) radiosOk = false;
    }

    if (!radiosOk)              gHealth = Health::RadioFault;
    else if (!uplink::online()) gHealth = Health::NoNetwork;
    else                        gHealth = Health::Ok;
}

// ---------------------------------------------------------------------------
// Frame handling
// ---------------------------------------------------------------------------
static void handleFrame(Cc1101Rx& radio, const OokFrame& frame) {
    char sigHex[17];
    ookSignatureHex(frame.signature, sigHex);

    const ButtonEntry* entry = buttonLookup(frame.signature);

    if (LOG_RAW_FRAMES) {
        log_i("radio %s: frame %u bits sig=%s %s",
              radio.tag(), frame.bitCount, sigHex,
              entry ? entry->buttonId : "(unknown)");
    }

    if (entry == nullptr) {
        // Not an error. This is how a replacement for the retired unit 2 gets
        // learned: press it, read the signature off this line, add it to
        // button_table.cpp.
        return;
    }

    // Rule 3 plus FR-A5. Keyed on signature, so radio B's copy of the same
    // press lands in the same slot and does not double-fire.
    if (!gDebouncer.offer(frame.signature, frame.endedAtMs)) return;

    Incident inc = {};
    incidentGenerateId(inc.incidentId);
    strncpy(inc.buttonId, entry->buttonId, sizeof(inc.buttonId) - 1);
    strncpy(inc.label, entry->label, sizeof(inc.label) - 1);
    strncpy(inc.signatureHex, sigHex, sizeof(inc.signatureHex) - 1);
    strncpy(inc.radioTag, radio.tag(), sizeof(inc.radioTag) - 1);
    inc.rssiDbm       = radio.rssiDbm();
    inc.createdAtMs   = millis();
    inc.createdAtUnix = uplink::nowUnix();
    inc.attempts      = 0;
    inc.nextAttemptMs = millis();

    log_w("INCIDENT %s button=%s radio=%s rssi=%d",
          inc.incidentId, inc.buttonId, inc.radioTag, inc.rssiDbm);

    uplink::enqueue(inc);
}

// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_TEST_BTN, INPUT_PULLUP);

    log_i("CAS relay node %s starting (device %s)", CAS_FW_VERSION, DEVICE_ID);
    log_i("%u buttons in table; listening on %.2f MHz OOK",
          buttonTableSize(), RF_FREQ_MHZ);

    for (uint8_t i = 0; i < kRadioCount; i++) {
        if (gRadios[i]->begin()) {
            log_i("radio %s: online", gRadios[i]->tag());
        } else {
            // Degrade, do not stop. One working receiver still covers most of
            // the house, and a node that refuses to boot over a bad solder
            // joint on the second module is worse than one that says so.
            log_e("radio %s: no SPI response — check wiring, 3V3, and CSN",
                  gRadios[i]->tag());
        }
    }

    uplink::begin();
    uplink::sendHeartbeat();
}

void loop() {
    OokFrame frame;

    for (uint8_t i = 0; i < kRadioCount; i++) {
        if (!gRadios[i]->ok()) continue;
        // Drain fully: a burst delivers dozens of frames and the repeat
        // threshold needs to see them.
        while (gRadios[i]->poll(frame)) {
            handleFrame(*gRadios[i], frame);
        }
    }

    gDebouncer.prune(millis());
    uplink::service();
    updateHealth();
    serviceLed();

    // No delay(). The edge ISR is what catches presses, but poll() has to keep
    // the 512-entry ring from overflowing during a burst.
}
