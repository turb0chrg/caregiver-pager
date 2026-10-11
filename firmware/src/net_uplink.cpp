#include "net_uplink.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/md.h>
#include <time.h>

#include "button_table.h"
#include "config.h"

namespace uplink {
namespace {

Incident  gQueue[QUEUE_DEPTH];
uint8_t   gQueueCount = 0;
uint32_t  gLastHeartbeatMs = 0;
bool      gTimeSynced = false;

void hexEncode(const uint8_t* in, size_t len, char* out) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = kHex[in[i] >> 4];
        out[i * 2 + 1] = kHex[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

// SEC-2: HMAC-SHA256 over "<timestamp>.<nonce>.<body>". Binding the timestamp
// and nonce into the signed string — rather than sending them as unsigned
// headers — is what makes the replay resistance real. The backend rejects a
// stale timestamp or a reused nonce.
void signPayload(const char* timestamp, const char* nonce, const String& body, char* outHex) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1 /* HMAC */);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)DEVICE_HMAC_SECRET, strlen(DEVICE_HMAC_SECRET));
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)timestamp, strlen(timestamp));
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)".", 1);
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)nonce, strlen(nonce));
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)".", 1);
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)body.c_str(), body.length());

    uint8_t mac[32];
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    hexEncode(mac, sizeof(mac), outHex);
}

void makeNonce(char* out17) {
    uint8_t raw[8];
    for (uint8_t i = 0; i < 8; i++) raw[i] = (uint8_t)(esp_random() & 0xFF);
    hexEncode(raw, sizeof(raw), out17);
}

// Shared by incident and heartbeat POSTs.
bool postSigned(const String& body) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    if (BACKEND_CA_PEM != nullptr) {
        client.setCACert(BACKEND_CA_PEM);
    } else {
        // CN-6 requires TLS, and this is still TLS — but unauthenticated, so it
        // stops an eavesdropper and not a man in the middle. Acceptable on the
        // bench only. Set BACKEND_CA_PEM before this leaves the desk.
        client.setInsecure();
    }
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    char timestamp[24];
    snprintf(timestamp, sizeof(timestamp), "%llu", (unsigned long long)nowUnix());

    char nonce[17];
    makeNonce(nonce);

    char sig[65];
    signPayload(timestamp, nonce, body, sig);

    HTTPClient http;
    if (!http.begin(client, BACKEND_URL)) return false;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-CAS-Device", DEVICE_ID);
    http.addHeader("X-CAS-Timestamp", timestamp);
    http.addHeader("X-CAS-Nonce", nonce);
    http.addHeader("X-CAS-Signature", String("sha256=") + sig);

    const int code = http.POST(body);
    http.end();

    // 2xx is delivered. 4xx other than 429 means the backend rejected the
    // payload permanently, so retrying cannot help and would burn the queue;
    // treat it as done and let the log carry the failure.
    if (code >= 200 && code < 300) return true;
    if (code >= 400 && code < 500 && code != 429) {
        log_e("backend rejected payload permanently: HTTP %d", code);
        return true;
    }
    log_w("POST failed: HTTP %d", code);
    return false;
}

String buildIncidentBody(const Incident& inc) {
    JsonDocument doc;
    doc["incident_id"] = inc.incidentId;
    doc["source"]      = "relay";
    doc["device_id"]   = DEVICE_ID;
    doc["button_id"]   = inc.buttonId;
    doc["label"]       = inc.label;
    doc["signature"]   = inc.signatureHex;
    doc["radio"]       = inc.radioTag;
    doc["rssi_dbm"]    = inc.rssiDbm;
    doc["ts"]          = (unsigned long long)inc.createdAtUnix;
    doc["conn"]        = "wifi";
    doc["fw"]          = CAS_FW_VERSION;

    // FR-L3: the recipient has to be able to tell a live fix from the fallback.
    // This build has no GNSS, so it is always the fallback and says so.
    JsonObject loc = doc["location"].to<JsonObject>();
    loc["mode"] = "fixed";
    loc["lat"]  = FIXED_LAT;
    loc["lon"]  = FIXED_LON;

    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        log_i("wifi up: %s", WiFi.localIP().toString().c_str());
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    } else {
        // Not fatal. The RF path is already listening, incidents queue, and
        // WiFi.setAutoReconnect keeps trying in the background. REL-1 holds
        // regardless: the SYNLETT pager is unaffected by any of this.
        log_w("wifi did not come up within %lu ms; queueing until it does",
              (unsigned long)WIFI_CONNECT_TIMEOUT_MS);
    }
}

uint64_t nowUnix() {
    time_t t = time(nullptr);
    if (t < 1700000000) {  // NTP has not landed yet
        gTimeSynced = false;
        return 0;
    }
    gTimeSynced = true;
    return (uint64_t)t;
}

bool online() { return WiFi.status() == WL_CONNECTED; }

uint8_t queueDepth() { return gQueueCount; }

bool enqueue(const Incident& inc) {
    bool evicted = false;
    if (gQueueCount >= QUEUE_DEPTH) {
        // Drop the oldest. A fresh alert matters more than a stale one, and the
        // stale one has already outlived its usefulness to a caregiver.
        log_w("queue full; evicting oldest incident %s", gQueue[0].incidentId);
        memmove(&gQueue[0], &gQueue[1], sizeof(Incident) * (QUEUE_DEPTH - 1));
        gQueueCount = QUEUE_DEPTH - 1;
        evicted = true;
    }
    gQueue[gQueueCount++] = inc;
    return !evicted;
}

void service() {
    const uint32_t now = millis();

    // Drain one queued incident per call, oldest first.
    if (gQueueCount > 0 && online()) {
        Incident& inc = gQueue[0];
        if ((int32_t)(now - inc.nextAttemptMs) >= 0) {
            if (now - inc.createdAtMs > QUEUE_TTL_MS) {
                log_e("incident %s expired undelivered after %lu ms",
                      inc.incidentId, (unsigned long)QUEUE_TTL_MS);
                memmove(&gQueue[0], &gQueue[1], sizeof(Incident) * (gQueueCount - 1));
                gQueueCount--;
            } else {
                // Stamp the time now if NTP was not ready at decode. The
                // incident_id is NOT regenerated — FR-C9 idempotency depends on
                // it surviving every retry.
                if (inc.createdAtUnix == 0) inc.createdAtUnix = nowUnix();

                if (postSigned(buildIncidentBody(inc))) {
                    log_i("delivered incident %s (%s)", inc.incidentId, inc.buttonId);
                    memmove(&gQueue[0], &gQueue[1], sizeof(Incident) * (gQueueCount - 1));
                    gQueueCount--;
                } else {
                    inc.attempts++;
                    uint32_t backoff = RETRY_BASE_MS << (inc.attempts > 6 ? 6 : inc.attempts);
                    if (backoff > RETRY_MAX_MS) backoff = RETRY_MAX_MS;
                    inc.nextAttemptMs = now + backoff;
                    log_w("retry %u for %s in %lu ms",
                          inc.attempts, inc.incidentId, (unsigned long)backoff);
                }
            }
        }
    }

    if (now - gLastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        gLastHeartbeatMs = now;
        sendHeartbeat();
    }
}

void sendHeartbeat() {
    if (!online()) return;

    JsonDocument doc;
    doc["type"]        = "heartbeat";
    doc["device_id"]   = DEVICE_ID;
    doc["fw"]          = CAS_FW_VERSION;
    doc["conn"]        = "wifi";
    doc["rssi_wifi"]   = WiFi.RSSI();
    doc["uptime_s"]    = millis() / 1000;
    doc["queue_depth"] = gQueueCount;
    doc["buttons"]     = buttonTableSize();
    doc["time_synced"] = gTimeSynced;
    doc["ts"]          = (unsigned long long)nowUnix();

    // PW-5 / HW-7 fields (battery_pct, power_source) are absent by design:
    // the budget build is mains-only with no fuel gauge. See docs/PIN-MAP.md §0.
    doc["power_source"] = "mains";

    String body;
    serializeJson(doc, body);
    postSigned(body);
}

}  // namespace uplink

void incidentGenerateId(char* out) {
    uint8_t b[16];
    for (uint8_t i = 0; i < 16; i++) b[i] = (uint8_t)(esp_random() & 0xFF);
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);  // version 4
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);  // variant 10x

    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}
