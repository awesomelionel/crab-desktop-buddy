#include "ConfigStore.h"

#include <Preferences.h>
#include <string.h>

namespace {
constexpr const char* NS       = "claude-buddy";
constexpr const char* KEY_SSID = "ssid";
constexpr const char* KEY_PASS = "pass";

size_t copyOutString(const char* src, char* buf, size_t buf_len) {
    if (!buf || buf_len == 0) return 0;
    size_t n = strlen(src);
    if (n >= buf_len) n = buf_len - 1;
    memcpy(buf, src, n);
    buf[n] = 0;
    return n;
}
}  // namespace

void ConfigStore::begin() {
    // Touch the namespace once so it exists before first read.
    Preferences p;
    if (p.begin(NS, /*readOnly=*/false)) {
        p.end();
    }
}

bool ConfigStore::hasCreds() const {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return false;
    bool present = p.isKey(KEY_SSID) && p.isKey(KEY_PASS);
    p.end();
    return present;
}

size_t ConfigStore::getSsid(char* buf, size_t buf_len) const {
    if (!buf || buf_len == 0) return 0;
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) {
        buf[0] = 0;
        return 0;
    }
    String s = p.getString(KEY_SSID, "");
    p.end();
    return copyOutString(s.c_str(), buf, buf_len);
}

size_t ConfigStore::getPassword(char* buf, size_t buf_len) const {
    if (!buf || buf_len == 0) return 0;
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) {
        buf[0] = 0;
        return 0;
    }
    String s = p.getString(KEY_PASS, "");
    p.end();
    return copyOutString(s.c_str(), buf, buf_len);
}

bool ConfigStore::setCreds(const char* ssid, const char* password) {
    if (!ssid || !password) return false;
    if (strlen(ssid) == 0 || strlen(ssid) > SSID_MAX_LEN) return false;
    if (strlen(password) > PASS_MAX_LEN) return false;
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return false;
    bool ok = p.putString(KEY_SSID, ssid) > 0 &&
              p.putString(KEY_PASS, password) >= 0;  // empty PSK valid for open AP
    p.end();
    return ok;
}

void ConfigStore::clear() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return;
    p.remove(KEY_SSID);
    p.remove(KEY_PASS);
    p.end();
}
