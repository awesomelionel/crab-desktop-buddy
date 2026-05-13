#include "BusArrivalsFetcher.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <stdio.h>
#include <string.h>

namespace net {

namespace {
constexpr uint32_t kHttpTimeoutMs = 8000;

void setErr(bus_arrivals::BusStopArrivals& out, const char* msg) {
    size_t cap = sizeof(out.last_error) - 1;
    size_t n = strlen(msg);
    if (n > cap) n = cap;
    memcpy(out.last_error, msg, n);
    out.last_error[n] = '\0';
}
}  // namespace

bool BusArrivalsFetcher::fetch(const char* code, uint32_t now_ms,
                               bus_arrivals::BusStopArrivals& out) {
    if (!code || code[0] == '\0') {
        setErr(out, "no stop code");
        return false;
    }

    NetworkClientSecure client;
    client.setInsecure();   // public, unauthenticated data

    char url[96];
    snprintf(url, sizeof(url),
             "https://api.busaunty.com/api/v1/BusArrival?BusStopCode=%s",
             code);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        setErr(out, "http begin failed");
        return false;
    }
    http.addHeader("User-Agent", "claude-buddy/1");
    http.addHeader("Accept", "application/json");

    int status = http.GET();
    if (status != 200) {
        char msg[32];
        snprintf(msg, sizeof(msg), "http %d", status);
        setErr(out, msg);
        http.end();
        return false;
    }

    int body_len = http.getSize();   // -1 if chunked
    if (body_len > (int)bus_arrivals::kMaxResponseBytes) {
        setErr(out, "response too large");
        http.end();
        return false;
    }

    // Read into a heap buffer with a hard cap. We can't stream-parse and
    // also enforce the size cap with the chunked path cleanly, so we
    // accumulate into a bounded buffer. Prefer PSRAM (ESP32-S3 has 8 MB)
    // since internal SRAM is fragmented after Wi-Fi+TLS init and a 32 KB
    // contiguous request often fails there.
    size_t cap = (body_len > 0)
        ? (size_t)body_len + 1
        : bus_arrivals::kMaxResponseBytes + 1;
    if (cap > bus_arrivals::kMaxResponseBytes + 1) {
        cap = bus_arrivals::kMaxResponseBytes + 1;
    }
    char* buf = (char*)ps_malloc(cap);
    if (!buf) buf = (char*)malloc(cap);
    if (!buf) {
        setErr(out, "oom");
        http.end();
        return false;
    }

    NetworkClient& stream = http.getStream();
    size_t pos = 0;
    uint32_t deadline = millis() + kHttpTimeoutMs;
    while (http.connected() && pos + 1 < cap && (int32_t)(deadline - millis()) > 0) {
        int avail = stream.available();
        if (avail > 0) {
            size_t want = (size_t)avail;
            if (want > cap - 1 - pos) want = cap - 1 - pos;
            int got = stream.readBytes(buf + pos, want);
            if (got <= 0) break;
            pos += (size_t)got;
            if (pos >= bus_arrivals::kMaxResponseBytes) {
                setErr(out, "response too large");
                free(buf);
                http.end();
                return false;
            }
        } else {
            delay(2);
        }
        if (body_len > 0 && (int)pos >= body_len) break;
    }
    buf[pos] = '\0';
    http.end();

    if (pos == 0) {
        setErr(out, "empty body");
        free(buf);
        return false;
    }

    bool ok = bus_arrivals::parseBusArrivalsJson(buf, pos, out);
    free(buf);
    if (!ok) return false;

    out.fetched_at_ms          = now_ms;
    out.last_fetch_success_ms  = now_ms;
    return true;
}

}  // namespace net
