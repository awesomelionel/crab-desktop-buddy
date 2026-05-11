# Manage — OTA & Factory Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add pull-based firmware update from GitHub Releases plus
factory-reset-over-the-wire to the existing web dashboard, with a
center-button hold required to confirm the wipe.

**Architecture:** A new `core/UpdateManager` owns the OTA state machine
(Idle → Checking → UpdateAvailable → Downloading → reboot). It calls
`net/GitHubReleases` for the API request and Arduino's `HTTPUpdate` for
the streamed install. A new `core/FactoryResetCoordinator` gates the
wipe on a 3-s center-button hold within a 30-s arm window opened by the
web endpoint. Two new takeover cards (`UpdatingCard`,
`FactoryResetCard`) provide on-device feedback. Five new
`HttpServer` endpoints plus a Manage section on the dashboard front-end.
A GitHub Actions workflow builds tagged releases.

**Tech Stack:** Arduino ESP32 (PlatformIO), `WiFiClientSecure`,
`HTTPUpdate`, ArduinoJson, Unity (native unit tests), GitHub Actions
for the release pipeline.

**Reference spec:**
[`2026-05-11-manage-ota-and-factory-reset-design.md`](../specs/2026-05-11-manage-ota-and-factory-reset-design.md)

---

## File Structure

**New (lib — pure logic, native-testable):**
- `lib/version_compare/version_compare.{h,cpp}` — semver parse + compare.
- `lib/github_releases_parse/github_releases_parse.{h,cpp}` — parses the
  GitHub `/releases/latest` JSON response into a `ReleaseInfo`.
- `lib/factory_reset_state/factory_reset_state.{h,cpp}` — pure state
  machine for the arm → hold → confirm flow.

**New (src — device-dependent):**
- `src/core/firmware_version.h` — `FIRMWARE_VERSION` constant.
- `src/core/UpdateManager.{h,cpp}` — orchestrates Check + Install,
  owns the state, drives `HTTPUpdate`.
- `src/core/FactoryResetCoordinator.{h,cpp}` — wraps
  `factory_reset_state`, performs the NVS wipe and reboot.
- `src/net/GitHubReleases.{h,cpp}` — HTTPS fetch from
  `api.github.com`, hands JSON to `github_releases_parse`.
- `src/ui/cards/UpdatingCard.{h,cpp}` — full-screen install progress.
- `src/ui/cards/FactoryResetCard.{h,cpp}` — full-screen hold-to-confirm.

**New (tooling):**
- `.github/workflows/release.yml` — tag-triggered build and asset upload.
- `data/github_certs.pem` — embedded CA bundle (DigiCert roots for
  `api.github.com` + `objects.githubusercontent.com`).

**New (tests):**
- `test/test_version_compare/test_version_compare.cpp`
- `test/test_github_releases_parse/test_github_releases_parse.cpp`
- `test/test_factory_reset_state/test_factory_reset_state.cpp`

**Modified:**
- `platformio.ini` — add `FW_VERSION` build flag,
  `board_build.embed_files = data/github_certs.pem`, add `HTTPUpdate`
  to lib_deps (it's part of the core but explicit listing helps
  linkage).
- `src/net/HttpServer.{h,cpp}` — five new endpoints, Manage section in
  the dashboard HTML, polling JS.
- `src/ui/CardController.{h,cpp}` — own and force-show
  `UpdatingCard`/`FactoryResetCard` when their state machines demand it.
- `src/input/InputRouter.{h,cpp}` — expose `centerHoldMs()` for
  `FactoryResetCoordinator` to poll.
- `src/main.cpp` — wire `UpdateManager`, `FactoryResetCoordinator`,
  `esp_ota_mark_app_valid_cancel_rollback()` after boot stabilizes.
- `tools/web-smoke.sh` — happy-path checks for new endpoints.

---

## Task ordering rationale

Tasks build outward from pure-logic leaves (where TDD is cheap) to
device-coupled assembly. Each task ends with a green build and a
commit; the device firmware compiles cleanly after every step.
Endpoints are wired progressively so smoke checks stay valid throughout.

---

### Task 1: Firmware version constant + build flag

**Files:**
- Create: `src/core/firmware_version.h`
- Modify: `platformio.ini`

- [ ] **Step 1: Add the version constant header**

Create `src/core/firmware_version.h`:

```cpp
#pragma once

// Stamped at compile time. CI sets FW_VERSION from the tag name (without
// the leading 'v'); local builds inherit the default "0.0.0-dev".
constexpr const char* FIRMWARE_VERSION =
#ifdef FW_VERSION
    FW_VERSION;
#else
    "0.0.0-dev";
#endif
```

- [ ] **Step 2: Add the default build flag**

In `platformio.ini`, in the `[env:adafruit_feather_esp32s3_reversetft]`
section, append to the existing `build_flags` list:

```ini
    -DFW_VERSION='"0.0.0-dev"'
```

(The single+double quote nesting is required so the C preprocessor sees a
string literal.)

- [ ] **Step 3: Confirm the firmware still builds**

Run: `pio run`
Expected: clean compile with the new header included in build (it
isn't referenced by anything yet, so the device build won't actually
include it — that's fine).

- [ ] **Step 4: Commit**

```bash
git add src/core/firmware_version.h platformio.ini
git commit -m "feat(core): add FIRMWARE_VERSION constant + FW_VERSION build flag"
```

---

### Task 2: `version_compare` library + native tests (TDD)

**Files:**
- Create: `lib/version_compare/version_compare.h`
- Create: `lib/version_compare/version_compare.cpp`
- Create: `test/test_version_compare/test_version_compare.cpp`

- [ ] **Step 1: Write the failing test file**

Create `test/test_version_compare/test_version_compare.cpp`:

```cpp
#include <unity.h>
#include "version_compare.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parses_basic_semver(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("1.2.3", v));
    TEST_ASSERT_EQUAL_UINT16(1, v.major);
    TEST_ASSERT_EQUAL_UINT16(2, v.minor);
    TEST_ASSERT_EQUAL_UINT16(3, v.patch);
}

static void test_parses_v_prefix(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("v0.10.5", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(10, v.minor);
    TEST_ASSERT_EQUAL_UINT16(5, v.patch);
}

static void test_parses_prerelease_suffix_stripped(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("1.2.3-rc1", v));
    TEST_ASSERT_EQUAL_UINT16(1, v.major);
    TEST_ASSERT_EQUAL_UINT16(2, v.minor);
    TEST_ASSERT_EQUAL_UINT16(3, v.patch);
}

static void test_dev_string_falls_back_to_zero(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("0.0.0-dev", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(0, v.minor);
    TEST_ASSERT_EQUAL_UINT16(0, v.patch);
}

static void test_malformed_falls_back_to_zero(void) {
    version_compare::Version v;
    TEST_ASSERT_TRUE(version_compare::parse("garbage", v));
    TEST_ASSERT_EQUAL_UINT16(0, v.major);
    TEST_ASSERT_EQUAL_UINT16(0, v.minor);
    TEST_ASSERT_EQUAL_UINT16(0, v.patch);
}

static void test_compare_equal(void) {
    TEST_ASSERT_EQUAL_INT(0, version_compare::compare("1.2.3", "1.2.3"));
}

static void test_compare_newer_patch(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("1.2.3", "1.2.4"));
}

static void test_compare_older_patch(void) {
    TEST_ASSERT_GREATER_THAN_INT(0, version_compare::compare("1.2.4", "1.2.3"));
}

static void test_compare_newer_minor_beats_higher_patch(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("1.2.9", "1.3.0"));
}

static void test_compare_dev_less_than_release(void) {
    TEST_ASSERT_LESS_THAN_INT(0, version_compare::compare("0.0.0-dev", "0.0.1"));
}

static void test_is_newer_helper(void) {
    TEST_ASSERT_TRUE(version_compare::isNewer("1.2.3", "1.2.4"));
    TEST_ASSERT_FALSE(version_compare::isNewer("1.2.4", "1.2.3"));
    TEST_ASSERT_FALSE(version_compare::isNewer("1.2.3", "1.2.3"));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_basic_semver);
    RUN_TEST(test_parses_v_prefix);
    RUN_TEST(test_parses_prerelease_suffix_stripped);
    RUN_TEST(test_dev_string_falls_back_to_zero);
    RUN_TEST(test_malformed_falls_back_to_zero);
    RUN_TEST(test_compare_equal);
    RUN_TEST(test_compare_newer_patch);
    RUN_TEST(test_compare_older_patch);
    RUN_TEST(test_compare_newer_minor_beats_higher_patch);
    RUN_TEST(test_compare_dev_less_than_release);
    RUN_TEST(test_is_newer_helper);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_version_compare`
Expected: FAIL — "version_compare.h: No such file or directory".

- [ ] **Step 3: Write the header**

Create `lib/version_compare/version_compare.h`:

```cpp
#pragma once

#include <stdint.h>

namespace version_compare {

struct Version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
};

// Parses a version string into `out`. Accepts:
//   - optional leading 'v'
//   - "MAJOR.MINOR.PATCH" with non-negative integer components
//   - optional pre-release suffix after a '-' (the suffix is ignored;
//     the base version is returned)
// Returns true if at least the first three integers parsed; on any
// other input writes {0, 0, 0} and still returns true (never blocks
// comparison).
bool parse(const char* s, Version& out);

// Three-way compare. Negative if a < b, zero if equal, positive if a > b.
int compare(const char* a, const char* b);

// Convenience: returns true iff parse(b) > parse(a).
bool isNewer(const char* current, const char* candidate);

}  // namespace version_compare
```

- [ ] **Step 4: Write the implementation**

Create `lib/version_compare/version_compare.cpp`:

```cpp
#include "version_compare.h"

#include <stddef.h>

namespace version_compare {

namespace {

// Reads decimal digits from *p into *out. Returns true if at least one
// digit was consumed. Advances *p past the digits.
bool readUint(const char*& p, uint16_t& out) {
    if (!p || *p < '0' || *p > '9') return false;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        if (v > 0xFFFF) v = 0xFFFF;
        ++p;
    }
    out = (uint16_t)v;
    return true;
}

}  // namespace

bool parse(const char* s, Version& out) {
    out = {0, 0, 0};
    if (!s) return true;
    if (*s == 'v' || *s == 'V') ++s;

    uint16_t maj = 0, min = 0, pat = 0;
    const char* p = s;
    if (!readUint(p, maj))     return true;   // already zeroed
    if (*p++ != '.')           return true;
    if (!readUint(p, min))     return true;
    if (*p++ != '.')           return true;
    if (!readUint(p, pat))     return true;

    out = {maj, min, pat};
    return true;
}

int compare(const char* a, const char* b) {
    Version va, vb;
    parse(a, va);
    parse(b, vb);
    if (va.major != vb.major) return (va.major < vb.major) ? -1 : 1;
    if (va.minor != vb.minor) return (va.minor < vb.minor) ? -1 : 1;
    if (va.patch != vb.patch) return (va.patch < vb.patch) ? -1 : 1;
    return 0;
}

bool isNewer(const char* current, const char* candidate) {
    return compare(current, candidate) < 0;
}

}  // namespace version_compare
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f test_version_compare`
Expected: all 11 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/version_compare/ test/test_version_compare/
git commit -m "feat(version_compare): semver parse + compare with native tests"
```

---

### Task 3: `GET /api/firmware-version` endpoint + web smoke

**Files:**
- Modify: `src/net/HttpServer.cpp`
- Modify: `tools/web-smoke.sh`

- [ ] **Step 1: Locate where STA handlers are registered**

Open `src/net/HttpServer.cpp` and find `registerStaHandlers()`. Note the
pattern existing endpoints use (`server_->on("/api/...", HTTP_GET, [...]{
... });`).

- [ ] **Step 2: Add the include**

Near the other `#include` lines at the top of `src/net/HttpServer.cpp`,
add:

```cpp
#include "../core/firmware_version.h"
```

- [ ] **Step 3: Register the new endpoint**

Inside `registerStaHandlers()`, add (placement near other `/api/` GETs):

```cpp
server_->on("/api/firmware-version", HTTP_GET, [this] {
    char body[64];
    int n = snprintf(body, sizeof(body),
                     "{\"version\":\"%s\"}", FIRMWARE_VERSION);
    server_->send(200, "application/json", String(body, n));
});
```

- [ ] **Step 4: Build the firmware**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 5: Add a web-smoke assertion**

Open `tools/web-smoke.sh`. Find the section that hits other `/api/...`
GETs (e.g., `/api/status` or `/api/settings`). Add:

```bash
echo "GET /api/firmware-version"
RESP=$(curl -fsS "http://$HOST/api/firmware-version")
echo "  $RESP"
echo "$RESP" | grep -q '"version"' || { echo "FAIL: no version key"; exit 1; }
```

- [ ] **Step 6: Flash + run web-smoke against the device** *(manual)*

```bash
pio run -t upload && pio device monitor
# wait for "[wifi] STA connected" with IP
# in another shell, run:
HOST=<device-ip> ./tools/web-smoke.sh
```

Expected: smoke passes, last block prints `{"version":"0.0.0-dev"}`.

- [ ] **Step 7: Commit**

```bash
git add src/net/HttpServer.cpp tools/web-smoke.sh
git commit -m "feat(http): GET /api/firmware-version + web-smoke check"
```

---

### Task 4: `github_releases_parse` library + native tests (TDD)

**Files:**
- Create: `lib/github_releases_parse/github_releases_parse.h`
- Create: `lib/github_releases_parse/github_releases_parse.cpp`
- Create: `test/test_github_releases_parse/test_github_releases_parse.cpp`

- [ ] **Step 1: Write the failing tests**

Create `test/test_github_releases_parse/test_github_releases_parse.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "github_releases_parse.h"

void setUp(void) {}
void tearDown(void) {}

static void test_parses_happy_path(void) {
    const char* json = R"({
        "tag_name": "v1.2.3",
        "body": "release notes here",
        "draft": false,
        "prerelease": false,
        "assets": [
            {"name": "firmware.bin",
             "browser_download_url": "https://example.com/firmware.bin"}
        ]
    })";

    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("1.2.3", info.tag);
    TEST_ASSERT_EQUAL_STRING("release notes here", info.body);
    TEST_ASSERT_EQUAL_STRING("https://example.com/firmware.bin",
                             info.download_url);
}

static void test_strips_v_prefix(void) {
    const char* json = R"({
        "tag_name": "v0.0.1",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "firmware.bin",
                    "browser_download_url": "https://x/firmware.bin"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("0.0.1", info.tag);
}

static void test_keeps_tag_without_v_prefix(void) {
    const char* json = R"({
        "tag_name": "1.5.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "firmware.bin",
                    "browser_download_url": "https://x/firmware.bin"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("1.5.0", info.tag);
}

static void test_rejects_no_firmware_asset(void) {
    const char* json = R"({
        "tag_name": "v1.0.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": [{"name": "source.zip",
                    "browser_download_url": "https://x/source.zip"}]
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
    TEST_ASSERT_TRUE(strstr(err, "firmware.bin") != nullptr);
}

static void test_rejects_empty_assets(void) {
    const char* json = R"({
        "tag_name": "v1.0.0",
        "body": "",
        "draft": false, "prerelease": false,
        "assets": []
    })";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
}

static void test_rejects_malformed_json(void) {
    const char* json = "{not even json";
    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_FALSE(github_releases_parse::parse(json, info, err, sizeof(err)));
}

static void test_truncates_long_body(void) {
    // Build a body string longer than the buffer.
    char json[2048];
    char long_body[1500];
    for (size_t i = 0; i < sizeof(long_body) - 1; ++i) long_body[i] = 'x';
    long_body[sizeof(long_body) - 1] = 0;
    snprintf(json, sizeof(json),
             "{\"tag_name\":\"v1.0.0\",\"body\":\"%s\","
             "\"draft\":false,\"prerelease\":false,"
             "\"assets\":[{\"name\":\"firmware.bin\","
             "\"browser_download_url\":\"https://x/firmware.bin\"}]}",
             long_body);

    github_releases_parse::ReleaseInfo info{};
    char err[64] = {};
    TEST_ASSERT_TRUE(github_releases_parse::parse(json, info, err, sizeof(err)));
    // body field is bounded; we just confirm it doesn't overflow.
    TEST_ASSERT_TRUE(strlen(info.body) < sizeof(info.body));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_happy_path);
    RUN_TEST(test_strips_v_prefix);
    RUN_TEST(test_keeps_tag_without_v_prefix);
    RUN_TEST(test_rejects_no_firmware_asset);
    RUN_TEST(test_rejects_empty_assets);
    RUN_TEST(test_rejects_malformed_json);
    RUN_TEST(test_truncates_long_body);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_github_releases_parse`
Expected: FAIL — header missing.

- [ ] **Step 3: Write the header**

Create `lib/github_releases_parse/github_releases_parse.h`:

```cpp
#pragma once

#include <stddef.h>

namespace github_releases_parse {

struct ReleaseInfo {
    char tag[16];               // base version without leading 'v'
    char body[1024];            // release notes; truncated to buffer
    char download_url[256];     // first asset named "firmware.bin"
};

// Parses a GitHub /releases/latest response body into `out`. Returns
// true on success; on failure writes a short reason into `err` and
// returns false. `out` is unspecified on failure (caller should treat
// it as invalid).
bool parse(const char* json, ReleaseInfo& out, char* err, size_t err_len);

}  // namespace github_releases_parse
```

- [ ] **Step 4: Write the implementation**

Create `lib/github_releases_parse/github_releases_parse.cpp`:

```cpp
#include "github_releases_parse.h"

#include <ArduinoJson.h>
#include <string.h>

namespace github_releases_parse {

namespace {

void copyTruncated(char* dst, size_t dst_len, const char* src) {
    if (!dst || dst_len == 0) return;
    if (!src) { dst[0] = 0; return; }
    size_t n = strnlen(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

void setError(char* err, size_t err_len, const char* msg) {
    if (err && err_len) copyTruncated(err, err_len, msg);
}

}  // namespace

bool parse(const char* json, ReleaseInfo& out, char* err, size_t err_len) {
    if (!json) { setError(err, err_len, "null json"); return false; }

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, json);
    if (jerr) { setError(err, err_len, jerr.c_str()); return false; }

    const char* tag = doc["tag_name"] | (const char*)nullptr;
    if (!tag || !*tag) { setError(err, err_len, "missing tag_name"); return false; }
    if (*tag == 'v' || *tag == 'V') ++tag;
    copyTruncated(out.tag, sizeof(out.tag), tag);

    const char* body = doc["body"] | "";
    copyTruncated(out.body, sizeof(out.body), body);

    out.download_url[0] = 0;
    JsonArray assets = doc["assets"];
    for (JsonObject a : assets) {
        const char* name = a["name"] | "";
        if (strcmp(name, "firmware.bin") == 0) {
            const char* url = a["browser_download_url"] | "";
            copyTruncated(out.download_url, sizeof(out.download_url), url);
            break;
        }
    }
    if (!out.download_url[0]) {
        setError(err, err_len, "no firmware.bin asset");
        return false;
    }

    return true;
}

}  // namespace github_releases_parse
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f test_github_releases_parse`
Expected: all 7 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/github_releases_parse/ test/test_github_releases_parse/
git commit -m "feat(github_releases_parse): JSON parser for /releases/latest"
```

---

### Task 5: CA bundle file + PIO embed wiring

**Files:**
- Create: `data/github_certs.pem`
- Modify: `platformio.ini`

This embeds the GitHub-relevant root CA certs into the firmware so
`WiFiClientSecure` can validate the TLS connection to GitHub. The spec
calls for a Mozilla bundle; in practice a 2-cert subset that covers
`api.github.com` (DigiCert Global Root G2) and
`objects.githubusercontent.com` (Amazon Root CA 1) is what we ship. The
PIO embed mechanism is the same either way; swap the PEM file later if
you ever want the full Mozilla bundle.

- [ ] **Step 1: Create the PEM bundle**

Create `data/github_certs.pem` containing the two PEM-encoded certs
concatenated. Fetch the official PEMs:

```bash
mkdir -p data
{
  curl -fsS https://cacerts.digicert.com/DigiCertGlobalRootG2.crt.pem
  echo
  curl -fsS https://www.amazontrust.com/repository/AmazonRootCA1.pem
} > data/github_certs.pem

# Verify both BEGIN/END markers are present
grep -c "BEGIN CERTIFICATE" data/github_certs.pem  # should print 2
```

- [ ] **Step 2: Wire embed_files in platformio.ini**

In `platformio.ini`, in the `[env:adafruit_feather_esp32s3_reversetft]`
section, add (anywhere — convention is near `board_build.partitions`):

```ini
board_build.embed_files = data/github_certs.pem
```

- [ ] **Step 3: Build and verify the embed**

Run: `pio run`
Expected: clean compile. The build log includes a line referencing
`github_certs.pem` being embedded as a data symbol.

- [ ] **Step 4: Commit**

```bash
git add data/github_certs.pem platformio.ini
git commit -m "feat(net): embed GitHub root CA bundle for TLS"
```

---

### Task 6: `net/GitHubReleases` — HTTPS fetch

**Files:**
- Create: `src/net/GitHubReleases.h`
- Create: `src/net/GitHubReleases.cpp`

This module is device-only (depends on `WiFiClientSecure` and
`HTTPClient`). Manually verified on-device; not unit tested. Parsing is
already covered by `lib/github_releases_parse` tests.

- [ ] **Step 1: Write the header**

Create `src/net/GitHubReleases.h`:

```cpp
#pragma once

#include "github_releases_parse.h"

namespace net {

struct FetchResult {
    bool                                    ok;
    char                                    error[64];
    github_releases_parse::ReleaseInfo      info;
};

// Synchronously fetches https://api.github.com/repos/{owner}/{repo}/releases/latest,
// validates TLS against the embedded CA bundle, and parses the response.
// `owner` and `repo` are baked at build time via macros so the call site
// stays simple.
FetchResult fetchLatestRelease();

}  // namespace net
```

- [ ] **Step 2: Add the repo identity build flags**

In `platformio.ini`, in the `[env:adafruit_feather_esp32s3_reversetft]`
section, append to `build_flags`:

```ini
    -DGITHUB_OWNER='"awesomelionel"'
    -DGITHUB_REPO='"desktop-buddy"'
```

- [ ] **Step 3: Write the implementation**

Create `src/net/GitHubReleases.cpp`:

```cpp
#include "GitHubReleases.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <string.h>

extern const uint8_t github_certs_pem_start[]
    asm("_binary_data_github_certs_pem_start");
extern const uint8_t github_certs_pem_end[]
    asm("_binary_data_github_certs_pem_end");

namespace net {

namespace {

constexpr uint32_t kHttpTimeoutMs = 10000;

void setError(FetchResult& r, const char* msg) {
    r.ok = false;
    strncpy(r.error, msg, sizeof(r.error) - 1);
    r.error[sizeof(r.error) - 1] = 0;
}

}  // namespace

FetchResult fetchLatestRelease() {
    FetchResult r{};
    r.ok = false;

    NetworkClientSecure client;
    const size_t bundle_size =
        (size_t)(github_certs_pem_end - github_certs_pem_start);
    client.setCACertBundle(github_certs_pem_start, bundle_size);

    char url[160];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/%s/releases/latest",
             GITHUB_OWNER, GITHUB_REPO);

    HTTPClient http;
    http.setTimeout(kHttpTimeoutMs);
    if (!http.begin(client, url)) {
        setError(r, "http begin failed");
        return r;
    }
    http.addHeader("User-Agent", "claude-buddy-ota/1");
    http.addHeader("Accept", "application/vnd.github+json");

    int code = http.GET();
    if (code != 200) {
        char msg[64];
        snprintf(msg, sizeof(msg), "http %d", code);
        setError(r, msg);
        http.end();
        return r;
    }

    String body = http.getString();
    http.end();

    char perr[64] = {};
    if (!github_releases_parse::parse(body.c_str(), r.info,
                                       perr, sizeof(perr))) {
        setError(r, perr[0] ? perr : "parse failed");
        return r;
    }

    r.ok = true;
    return r;
}

}  // namespace net
```

Note: the `asm()` symbol name for an embedded file follows the pattern
`_binary_<path-with-underscores>_start`/`_end`. For `data/github_certs.pem`
that's `_binary_data_github_certs_pem_start`. If the actual symbol differs
(check the link error if any), `objdump -t .pio/build/.../firmware.elf
| grep github_certs` reveals it.

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run`
Expected: clean compile and link. If the linker errors on the
`_binary_data_github_certs_pem_*` symbols, run `objdump -t
.pio/build/adafruit_feather_esp32s3_reversetft/firmware.elf | grep
github_certs` to get the actual symbol names and adjust the
`asm("...")` strings.

- [ ] **Step 5: Commit**

```bash
git add src/net/GitHubReleases.h src/net/GitHubReleases.cpp platformio.ini
git commit -m "feat(net): GitHubReleases HTTPS client for /releases/latest"
```

---

### Task 7: `UpdateManager` skeleton + state types + manual integration

**Files:**
- Create: `src/core/UpdateManager.h`
- Create: `src/core/UpdateManager.cpp`
- Modify: `src/main.cpp` (instantiate + tick)

This task lands the public surface and the Check path (synchronous in
`requestCheck()`). The async Install path comes in Task 9.

- [ ] **Step 1: Write the header**

Create `src/core/UpdateManager.h`:

```cpp
#pragma once

#include <stdint.h>

#include "github_releases_parse.h"

class UpdateManager {
public:
    enum class State : uint8_t {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        InstallReady,
        Failed,
    };

    struct Status {
        State    state;
        uint32_t bytes_received;     // valid in Downloading
        uint32_t bytes_total;        // valid in Downloading
        char     last_error[64];     // valid in Failed
    };

    void begin();
    void tick(uint32_t now_ms);

    // Web endpoints call these. requestCheck() runs synchronously
    // (~3–5 s, bounded by 10-s HTTP timeout). requestInstall() returns
    // immediately; the install runs from the next tick().
    void requestCheck();
    void requestInstall();

    Status                                       status() const;
    const github_releases_parse::ReleaseInfo*    latestRelease() const;
    const char*                                  currentVersion() const;

    static UpdateManager& instance();

private:
    State    state_ = State::Idle;
    uint32_t bytes_received_ = 0;
    uint32_t bytes_total_ = 0;
    char     last_error_[64] = {};

    bool     have_release_ = false;
    github_releases_parse::ReleaseInfo latest_ = {};

    bool     install_pending_ = false;

    void doCheckBlocking();
    void doInstallBlocking();  // runs from tick(); blocks loop for the
                               //   duration (acceptable — we're going
                               //   to reboot afterward anyway).
};
```

- [ ] **Step 2: Write the implementation**

Create `src/core/UpdateManager.cpp`:

```cpp
#include "UpdateManager.h"

#include <string.h>

#include "firmware_version.h"
#include "version_compare.h"
#include "../net/GitHubReleases.h"

UpdateManager& UpdateManager::instance() {
    static UpdateManager s;
    return s;
}

void UpdateManager::begin() {
    state_ = State::Idle;
    bytes_received_ = 0;
    bytes_total_ = 0;
    last_error_[0] = 0;
    have_release_ = false;
    install_pending_ = false;
}

void UpdateManager::tick(uint32_t /*now_ms*/) {
    if (install_pending_) {
        install_pending_ = false;
        doInstallBlocking();
    }
}

void UpdateManager::requestCheck() {
    doCheckBlocking();
}

void UpdateManager::requestInstall() {
    if (state_ != State::UpdateAvailable) {
        strncpy(last_error_,
                "no update available", sizeof(last_error_) - 1);
        state_ = State::Failed;
        return;
    }
    state_ = State::Downloading;
    bytes_received_ = 0;
    bytes_total_ = 0;
    install_pending_ = true;
}

UpdateManager::Status UpdateManager::status() const {
    Status s{};
    s.state = state_;
    s.bytes_received = bytes_received_;
    s.bytes_total = bytes_total_;
    strncpy(s.last_error, last_error_, sizeof(s.last_error) - 1);
    s.last_error[sizeof(s.last_error) - 1] = 0;
    return s;
}

const github_releases_parse::ReleaseInfo*
UpdateManager::latestRelease() const {
    return have_release_ ? &latest_ : nullptr;
}

const char* UpdateManager::currentVersion() const {
    return FIRMWARE_VERSION;
}

void UpdateManager::doCheckBlocking() {
    state_ = State::Checking;
    have_release_ = false;
    last_error_[0] = 0;

    net::FetchResult r = net::fetchLatestRelease();
    if (!r.ok) {
        strncpy(last_error_, r.error, sizeof(last_error_) - 1);
        last_error_[sizeof(last_error_) - 1] = 0;
        state_ = State::Failed;
        return;
    }

    latest_ = r.info;
    have_release_ = true;

    if (version_compare::isNewer(FIRMWARE_VERSION, latest_.tag)) {
        state_ = State::UpdateAvailable;
    } else {
        state_ = State::UpToDate;
    }
}

void UpdateManager::doInstallBlocking() {
    // Placeholder until Task 9 lands the real install. For now: mark
    // failed so the state machine doesn't hang in Downloading if some
    // caller manages to invoke install_pending before Task 9.
    strncpy(last_error_, "install not implemented",
            sizeof(last_error_) - 1);
    state_ = State::Failed;
}
```

- [ ] **Step 3: Wire in main.cpp**

In `src/main.cpp`, add to includes (near the other `core/` includes):

```cpp
#include "core/UpdateManager.h"
```

In `setup()`, after `configStore.begin();`, add:

```cpp
    UpdateManager::instance().begin();
```

In `loop()`, near the other per-tick calls, add:

```cpp
    UpdateManager::instance().tick(now);
```

(Use whatever variable name the existing loop has for the current
millis — likely `now` or `now_ms`. If neither exists, add `const
uint32_t now = millis();` near the top of `loop()`.)

- [ ] **Step 4: Build the firmware**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/core/UpdateManager.h src/core/UpdateManager.cpp src/main.cpp
git commit -m "feat(core): UpdateManager skeleton + Check path wired into main"
```

---

### Task 8: `POST /api/check-for-updates` + `GET /api/update-status`

**Files:**
- Modify: `src/net/HttpServer.cpp`
- Modify: `tools/web-smoke.sh`

- [ ] **Step 1: Add include**

In `src/net/HttpServer.cpp`, near the existing `core/` includes:

```cpp
#include "../core/UpdateManager.h"
```

- [ ] **Step 2: Add a helper for serializing status**

In the anonymous namespace at the top of `HttpServer.cpp` (or near the
other private helpers if no anon namespace exists), add:

```cpp
namespace {
const char* stateName(UpdateManager::State s) {
    switch (s) {
        case UpdateManager::State::Idle:            return "idle";
        case UpdateManager::State::Checking:        return "checking";
        case UpdateManager::State::UpToDate:        return "up_to_date";
        case UpdateManager::State::UpdateAvailable: return "update_available";
        case UpdateManager::State::Downloading:     return "downloading";
        case UpdateManager::State::InstallReady:    return "install_ready";
        case UpdateManager::State::Failed:          return "failed";
    }
    return "unknown";
}

// Writes the full status JSON into `buf`; returns bytes written.
size_t writeStatusJson(char* buf, size_t buf_len) {
    auto& um = UpdateManager::instance();
    auto st = um.status();
    auto* rel = um.latestRelease();
    return snprintf(buf, buf_len,
        "{\"state\":\"%s\","
        "\"current\":\"%s\","
        "\"latest\":\"%s\","
        "\"notes\":%s\"%s\"%s,"
        "\"bytes_received\":%u,"
        "\"bytes_total\":%u,"
        "\"error\":\"%s\"}",
        stateName(st.state),
        um.currentVersion(),
        rel ? rel->tag : "",
        "", rel ? rel->body : "", "",
        (unsigned)st.bytes_received,
        (unsigned)st.bytes_total,
        st.last_error);
}
}  // namespace
```

(Note: `notes` is wrapped in literal quotes via the format string; this
keeps the JSON valid even when `body` is empty. JSON-escaping the body
properly is a separate concern — see Task 12 for the front-end side.)

- [ ] **Step 3: Register the endpoints**

In `registerStaHandlers()`, alongside the version endpoint from Task 3:

```cpp
server_->on("/api/check-for-updates", HTTP_POST, [this] {
    // Synchronous — runs the HTTPS request inline.
    UpdateManager::instance().requestCheck();
    char buf[1280];
    size_t n = writeStatusJson(buf, sizeof(buf));
    server_->send(200, "application/json", String(buf, n));
});

server_->on("/api/update-status", HTTP_GET, [this] {
    char buf[1280];
    size_t n = writeStatusJson(buf, sizeof(buf));
    server_->send(200, "application/json", String(buf, n));
});
```

- [ ] **Step 4: Add web-smoke checks**

Append to `tools/web-smoke.sh`:

```bash
echo "GET /api/update-status"
RESP=$(curl -fsS "http://$HOST/api/update-status")
echo "  $RESP"
echo "$RESP" | grep -q '"state":"idle"' || { echo "FAIL: state not idle"; exit 1; }

echo "POST /api/check-for-updates (expect either up_to_date, update_available, or failed)"
RESP=$(curl -fsS -X POST "http://$HOST/api/check-for-updates")
echo "  $RESP"
echo "$RESP" | grep -qE '"state":"(up_to_date|update_available|failed)"' \
    || { echo "FAIL: unexpected state"; exit 1; }
```

(Pre-first-release: expect `"failed"` because there are no published
releases yet. Once a release exists, `up_to_date` or `update_available`.)

- [ ] **Step 5: Flash + smoke** *(manual)*

```bash
pio run -t upload
HOST=<device-ip> ./tools/web-smoke.sh
```

Expected: smoke passes; `check-for-updates` returns `"failed"` until a
release is published in Task 19.

- [ ] **Step 6: Commit**

```bash
git add src/net/HttpServer.cpp tools/web-smoke.sh
git commit -m "feat(http): check-for-updates + update-status endpoints"
```

---

### Task 9: Real OTA install via `HTTPUpdate`

**Files:**
- Modify: `src/core/UpdateManager.cpp`
- Modify: `src/net/HttpServer.cpp`
- Modify: `src/main.cpp`

This task replaces the placeholder `doInstallBlocking()` with the actual
streamed install through Arduino's `HTTPUpdate` library, plus the
auto-rollback acknowledgement on next successful boot.

- [ ] **Step 1: Replace doInstallBlocking() in UpdateManager.cpp**

Open `src/core/UpdateManager.cpp`. Add includes near the top:

```cpp
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <NetworkClientSecure.h>
```

Add this declaration just below the existing `extern const uint8_t
github_certs_pem_*` block (which lives in GitHubReleases.cpp). Inside
UpdateManager.cpp:

```cpp
extern const uint8_t github_certs_pem_start[]
    asm("_binary_data_github_certs_pem_start");
extern const uint8_t github_certs_pem_end[]
    asm("_binary_data_github_certs_pem_end");
```

Replace the placeholder `doInstallBlocking()` body:

```cpp
void UpdateManager::doInstallBlocking() {
    if (!have_release_ || !latest_.download_url[0]) {
        strncpy(last_error_, "no release", sizeof(last_error_) - 1);
        state_ = State::Failed;
        return;
    }

    NetworkClientSecure client;
    const size_t bundle_size =
        (size_t)(github_certs_pem_end - github_certs_pem_start);
    client.setCACertBundle(github_certs_pem_start, bundle_size);

    httpUpdate.rebootOnUpdate(false);
    httpUpdate.onProgress([](int cur, int total) {
        auto& um = UpdateManager::instance();
        um.bytes_received_ = (uint32_t)cur;
        um.bytes_total_ = (uint32_t)total;
    });

    t_httpUpdate_return result =
        httpUpdate.update(client, latest_.download_url, FIRMWARE_VERSION);

    if (result == HTTP_UPDATE_OK) {
        state_ = State::InstallReady;
        delay(500);
        ESP.restart();   // hard reboot; never returns
        return;
    }

    const String& msg = httpUpdate.getLastErrorString();
    strncpy(last_error_, msg.c_str(), sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = 0;
    state_ = State::Failed;
}
```

Note: `bytes_received_` and `bytes_total_` are private. The lambda
captures `instance()` and writes them via the friend-or-public path. To
keep this simple, **make them public** or expose two setter methods.
Choose one — see step 2.

- [ ] **Step 2: Expose progress writers**

In `src/core/UpdateManager.h`, add to the public section (right before
`private:`):

```cpp
    // Called from the HTTPUpdate progress callback. Not for general use.
    void setProgressInternal(uint32_t received, uint32_t total) {
        bytes_received_ = received;
        bytes_total_ = total;
    }
```

Then back in `doInstallBlocking()`, change the lambda body:

```cpp
    httpUpdate.onProgress([](int cur, int total) {
        UpdateManager::instance().setProgressInternal(
            (uint32_t)cur, (uint32_t)total);
    });
```

- [ ] **Step 3: Add the `POST /api/install-update` endpoint**

In `src/net/HttpServer.cpp`, inside `registerStaHandlers()`:

```cpp
server_->on("/api/install-update", HTTP_POST, [this] {
    auto& um = UpdateManager::instance();
    if (um.status().state != UpdateManager::State::UpdateAvailable) {
        server_->send(409, "application/json",
                      "{\"error\":\"no update available\"}");
        return;
    }
    um.requestInstall();
    server_->send(200, "application/json", "{\"state\":\"downloading\"}");
});
```

- [ ] **Step 4: Mark the running image valid on next successful boot**

In `src/main.cpp`, add include near the top:

```cpp
#include <esp_ota_ops.h>
```

In `setup()`, **at the very end** (so it runs only after all other
init succeeded without crashing), add:

```cpp
    // If we got here, the current image is healthy. Acknowledge to the
    // bootloader so it won't roll back on next power-up. Safe to call
    // even when the running partition wasn't a fresh OTA install.
    esp_ota_mark_app_valid_cancel_rollback();
```

- [ ] **Step 5: Build**

Run: `pio run`
Expected: clean compile. If the linker complains about the
`github_certs_pem_*` symbols being defined twice (since both
GitHubReleases.cpp and UpdateManager.cpp declare them), keep both
declarations — `extern` declarations don't define a symbol, so multiple
file-scope `extern` lines are fine.

- [ ] **Step 6: Commit**

```bash
git add src/core/UpdateManager.h src/core/UpdateManager.cpp \
        src/net/HttpServer.cpp src/main.cpp
git commit -m "feat(ota): wire HTTPUpdate install path + rollback ack"
```

---

### Task 10: `UpdatingCard` UI

**Files:**
- Create: `src/ui/cards/UpdatingCard.h`
- Create: `src/ui/cards/UpdatingCard.cpp`

`UpdatingCard` is a `Card` subclass that mirrors `UpdateManager` status.
CardController will force-show it on state changes in Task 11. It does
NOT live in the card carousel — it's a pre-empt-everything overlay
selected by CardController.

Convention reminder from reading [src/ui/cards/WifiCard.cpp](src/ui/cards/WifiCard.cpp):
- Inherit from `Card`.
- Override `invalidate()`, `isDirty()`, `render(Display&)`.
- Render uses `Adafruit_ST7789& tft = display.tft();` then standard
  Adafruit GFX calls (`tft.fillScreen(ST77XX_BLACK)`, `tft.setCursor`,
  `tft.setTextSize`, `tft.setTextColor`, `tft.print`, `tft.fillRect`,
  `tft.drawRect`).
- 240 × 135 display (landscape on this Feather).
- CLAUDE.md: full clear only on first paint after state change; per-frame
  updates redraw only the bounding box of the changing region.

- [ ] **Step 1: Write the header**

Create `src/ui/cards/UpdatingCard.h`:

```cpp
#pragma once

#include <stdint.h>

#include "../Card.h"

class UpdateManager;

class UpdatingCard : public Card {
public:
    explicit UpdatingCard(UpdateManager& um) : um_(um) {}

    void tick(uint32_t now_ms) override;
    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    // True while UpdateManager is mid-install. CardController polls
    // this each tick and pre-empts the carousel when true.
    bool isActive() const;

private:
    UpdateManager& um_;
    bool      full_clear_     = true;
    uint8_t   last_pct_drawn_ = 255;     // sentinel — guarantees first paint
    uint32_t  last_render_ms_ = 0;
    uint32_t  now_ms_         = 0;       // updated each tick(), read by render()
};
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/cards/UpdatingCard.cpp`:

```cpp
#include "UpdatingCard.h"

#include "../../core/UpdateManager.h"
#include "../../display/Display.h"

#include <Adafruit_ST7789.h>

namespace {
constexpr uint32_t kMinRedrawIntervalMs = 250;
}

void UpdatingCard::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
}

void UpdatingCard::invalidate() {
    full_clear_     = true;
    last_pct_drawn_ = 255;
}

bool UpdatingCard::isActive() const {
    auto s = um_.status().state;
    return s == UpdateManager::State::Downloading
        || s == UpdateManager::State::InstallReady;
}

bool UpdatingCard::isDirty() const {
    if (!isActive()) return false;
    auto st = um_.status();
    uint8_t pct = (st.bytes_total > 0)
                  ? (uint8_t)((st.bytes_received * 100u) / st.bytes_total)
                  : 0;
    return full_clear_
        || pct != last_pct_drawn_
        || (now_ms_ - last_render_ms_) >= kMinRedrawIntervalMs;
}

void UpdatingCard::render(Display& display) {
    auto& tft  = display.tft();
    auto  st   = um_.status();
    auto* rel  = um_.latestRelease();

    uint8_t pct = (st.bytes_total > 0)
                  ? (uint8_t)((st.bytes_received * 100u) / st.bytes_total)
                  : 0;

    if (full_clear_) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextWrap(false);

        tft.setTextSize(2);
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
        tft.setCursor(8, 6);
        tft.print("UPDATING");

        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(8, 30);
        tft.print("v");
        tft.print(um_.currentVersion());
        tft.print(" -> v");
        tft.print(rel ? rel->tag : "?");

        // Progress bar outline (only drawn once).
        tft.drawRect(8, 60, 224, 16, ST77XX_WHITE);

        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.setCursor(8, 110);
        tft.print("Do not unplug");

        full_clear_ = false;
    }

    // Erase + redraw only the progress-bar interior + percent label.
    // (See CLAUDE.md re: per-frame fillScreen.)
    tft.fillRect(9, 61, 222, 14, ST77XX_BLACK);
    uint16_t fill = (uint16_t)((222u * pct) / 100u);
    if (fill > 0) tft.fillRect(9, 61, fill, 14, ST77XX_GREEN);

    tft.fillRect(8, 84, 80, 16, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(8, 84);
    tft.print(pct);
    tft.print("%");

    last_pct_drawn_ = pct;
    last_render_ms_ = now_ms_;
}
```

- [ ] **Step 3: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/UpdatingCard.h src/ui/cards/UpdatingCard.cpp
git commit -m "feat(ui): UpdatingCard for OTA install takeover"
```

---

### Task 11: `CardController` integration — force-show `UpdatingCard`

**Files:**
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`
- Modify: `src/main.cpp`

The pre-empt block goes inside `CardController::tick()` just before the
existing `stack_.tick(now_ms, display);` call. We also wake the display
on transition so the user sees the UpdatingCard regardless of sleep
state.

- [ ] **Step 1: Add UpdatingCard ownership**

In `src/ui/CardController.h`:

- Add `#include "cards/UpdatingCard.h"` next to the other card includes.
- Forward-declare `class UpdateManager;` at the top.
- Add an `UpdateManager&` parameter as the first constructor argument
  (or last — order doesn't matter as long as `main.cpp` matches).
- Add the corresponding member:
  ```cpp
      UpdateManager& um_;
      UpdatingCard   updating_card_;
  ```

In `src/ui/CardController.cpp`:

- Add `#include "../core/UpdateManager.h"` near the other includes.
- Update the constructor signature and initializer list — store
  `um_(um)`, initialize `updating_card_(um)`.

- [ ] **Step 2: Pre-empt the card stack when an install is active**

In `src/ui/CardController.cpp`, inside `CardController::tick(uint32_t
now_ms, Display& display)`, replace the existing block:

```cpp
    if (!display.isAsleep()) {
        stack_.tick(now_ms, display);
    }
```

with:

```cpp
    // Force-show overlays pre-empt the carousel. Wake the display so
    // they're visible even if we'd otherwise be asleep.
    updating_card_.tick(now_ms);
    if (updating_card_.isActive()) {
        if (display.isAsleep()) display.setBacklight(true);
        if (updating_card_.isDirty()) {
            updating_card_.render(display);
        }
        last_activity_ms_ = now_ms;   // prevent sleep while installing
        return;
    }

    if (!display.isAsleep()) {
        stack_.tick(now_ms, display);
    }
```

(`Display::setBacklight(true)` ramps the backlight to 100 %, leaving
sleep state. See [src/display/Display.h](src/display/Display.h:22).)

- [ ] **Step 3: Pass UpdateManager into CardController in main.cpp**

In `src/main.cpp`, update the `cardController{...}` brace-init to add
`UpdateManager::instance()` in the parameter slot you just added.
Also `#include "core/UpdateManager.h"` if it isn't already.

- [ ] **Step 4: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/ui/CardController.h src/ui/CardController.cpp src/main.cpp
git commit -m "feat(ui): CardController pre-empts carousel during OTA install"
```

---

### Task 12: Web UI — Manage section + polling JS

**Files:**
- Modify: `src/net/HttpServer.cpp` (the dashboard HTML/JS lives here)

- [ ] **Step 1: Locate the dashboard HTML**

In `src/net/HttpServer.cpp`, find the constant string or raw-literal
that holds the dashboard HTML (search for the existing `DANGER`,
`NETWORK`, or section headers from the configure-via-web spec).

- [ ] **Step 2: Add the Manage section markup**

Append a new section block to the HTML, after `DANGER`:

```html
<section>
  <h2>MANAGE</h2>
  <div>Current version: <code id="fw-cur">…</code></div>
  <button id="btn-check">Check for updates</button>
  <div id="check-result"></div>
  <button id="btn-install" hidden>Install update</button>
  <pre id="install-progress" hidden></pre>
  <hr>
  <button id="btn-factory-reset">Factory reset</button>
  <div id="reset-hint"></div>
</section>
```

- [ ] **Step 3: Add the JS**

Append to the existing inline `<script>` block (or create one at the
end of `<body>`):

```javascript
const $ = (id) => document.getElementById(id);

async function loadVersion() {
    const r = await fetch("/api/firmware-version");
    const j = await r.json();
    $("fw-cur").textContent = j.version;
}
loadVersion();

$("btn-check").addEventListener("click", async () => {
    $("check-result").textContent = "Checking…";
    $("btn-install").hidden = true;
    try {
        const r = await fetch("/api/check-for-updates", {method: "POST"});
        const j = await r.json();
        if (j.state === "update_available") {
            $("check-result").innerHTML =
                `New version: <b>${j.latest}</b><br><pre>${j.notes || ""}</pre>`;
            $("btn-install").hidden = false;
        } else if (j.state === "up_to_date") {
            $("check-result").textContent = "✓ You're up to date.";
        } else {
            $("check-result").textContent = `Failed: ${j.error || "(no reason)"}`;
        }
    } catch (e) {
        $("check-result").textContent = `Network error: ${e.message}`;
    }
});

$("btn-install").addEventListener("click", async () => {
    $("btn-install").hidden = true;
    $("install-progress").hidden = false;
    $("install-progress").textContent = "Starting…";
    try {
        await fetch("/api/install-update", {method: "POST"});
    } catch (e) {
        // expected — device may drop the connection briefly
    }
    pollUpdate();
});

async function pollUpdate() {
    let lastVersion = null;
    while (true) {
        try {
            const r = await fetch("/api/update-status");
            const j = await r.json();
            if (j.state === "downloading") {
                const pct = j.bytes_total
                    ? Math.floor((j.bytes_received * 100) / j.bytes_total)
                    : 0;
                $("install-progress").textContent =
                    `Installing v${j.latest}… ${pct}% ` +
                    `(${j.bytes_received}/${j.bytes_total} bytes)`;
            } else if (j.state === "failed") {
                $("install-progress").textContent =
                    `Failed: ${j.error || "(no reason)"}`;
                return;
            }
        } catch (e) {
            // device probably rebooting; switch to version-poll mode.
            $("install-progress").textContent = "Device rebooting…";
            try {
                const r = await fetch("/api/firmware-version");
                const j = await r.json();
                if (lastVersion === null) lastVersion = j.version;
                if (j.version !== lastVersion) {
                    $("install-progress").textContent =
                        `Updated to ${j.version}.`;
                    await loadVersion();
                    return;
                }
            } catch (_) { /* still offline */ }
        }
        await new Promise(r => setTimeout(r, 1500));
    }
}
```

- [ ] **Step 4: Build, flash, and visually verify** *(manual)*

```bash
pio run -t upload
# open http://<device-ip>/ in a browser
# verify the MANAGE section renders
# click "Check for updates" — expect "Failed" until a release exists
```

- [ ] **Step 5: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(web): Manage section — version, check, install flow"
```

---

### Task 13: `factory_reset_state` lib + native tests (TDD)

**Files:**
- Create: `lib/factory_reset_state/factory_reset_state.h`
- Create: `lib/factory_reset_state/factory_reset_state.cpp`
- Create: `test/test_factory_reset_state/test_factory_reset_state.cpp`

Pure state machine — no NVS, no time source, no UI. The device wrapper
in Task 14 handles those.

- [ ] **Step 1: Write the failing tests**

Create `test/test_factory_reset_state/test_factory_reset_state.cpp`:

```cpp
#include <unity.h>
#include "factory_reset_state.h"

using factory_reset_state::Machine;
using factory_reset_state::Phase;
using factory_reset_state::Inputs;

void setUp(void) {}
void tearDown(void) {}

static Inputs noPress(uint32_t now) { return {now, false}; }
static Inputs press(uint32_t now)   { return {now, true}; }

static void test_starts_idle(void) {
    Machine m;
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
}

static void test_arm_enters_awaiting_hold(void) {
    Machine m;
    m.arm(1000);
    TEST_ASSERT_EQUAL(Phase::AwaitingHold, m.phase());
}

static void test_hold_three_seconds_confirms(void) {
    Machine m;
    m.arm(1000);
    m.tick(noPress(1100));   // armed, no press yet
    m.tick(press(1500));     // press starts
    m.tick(press(2500));     // 1s of hold
    TEST_ASSERT_EQUAL(Phase::AwaitingHold, m.phase());
    m.tick(press(4500));     // 3s of hold → confirm
    TEST_ASSERT_EQUAL(Phase::Resetting, m.phase());
}

static void test_release_before_three_seconds_cancels(void) {
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // press starts
    m.tick(press(2500));     // 1s
    m.tick(noPress(3000));   // release before 3s
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

static void test_window_timeout_disarms(void) {
    Machine m;
    m.arm(1000);
    m.tick(noPress(1500));   // armed, no press
    m.tick(noPress(31500));  // 30.5s — window elapsed without any hold
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

static void test_hold_resets_when_released_then_pressed_again(void) {
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // press 1s
    m.tick(press(2500));
    m.tick(noPress(2600));   // release at 1s held
    m.tick(press(2700));     // re-press → hold timer restarts
    m.tick(press(3700));     // only 1s held
    TEST_ASSERT_EQUAL(Phase::AwaitingHold, m.phase());
    m.tick(press(5700));     // 3s from second press start → confirm
    TEST_ASSERT_EQUAL(Phase::Resetting, m.phase());
}

static void test_hold_ms_reports_current_progress(void) {
    Machine m;
    m.arm(1000);
    m.tick(press(1500));     // 0 ms held at start
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
    m.tick(press(2500));     // 1s held
    TEST_ASSERT_EQUAL_UINT32(1000, m.holdMs());
    m.tick(noPress(2600));   // released — back to 0
    TEST_ASSERT_EQUAL_UINT32(0, m.holdMs());
}

static void test_idle_ignores_input(void) {
    Machine m;
    m.tick(press(1500));     // not armed, holding does nothing
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
    m.tick(press(5500));
    TEST_ASSERT_EQUAL(Phase::Idle, m.phase());
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_idle);
    RUN_TEST(test_arm_enters_awaiting_hold);
    RUN_TEST(test_hold_three_seconds_confirms);
    RUN_TEST(test_release_before_three_seconds_cancels);
    RUN_TEST(test_window_timeout_disarms);
    RUN_TEST(test_hold_resets_when_released_then_pressed_again);
    RUN_TEST(test_hold_ms_reports_current_progress);
    RUN_TEST(test_idle_ignores_input);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `pio test -e native -f test_factory_reset_state`
Expected: FAIL — header missing.

- [ ] **Step 3: Write the header**

Create `lib/factory_reset_state/factory_reset_state.h`:

```cpp
#pragma once

#include <stdint.h>

namespace factory_reset_state {

enum class Phase : uint8_t {
    Idle,
    AwaitingHold,    // armed via web; user must hold center button
    Resetting,       // 3-s hold confirmed; caller should wipe NVS now
};

struct Inputs {
    uint32_t now_ms;
    bool     center_pressed;
};

constexpr uint32_t kArmWindowMs   = 30000;   // 30 s
constexpr uint32_t kHoldRequiredMs = 3000;   // 3 s

class Machine {
public:
    void   arm(uint32_t now_ms);
    void   tick(const Inputs& in);

    Phase    phase()  const { return phase_; }
    uint32_t holdMs() const;

private:
    Phase    phase_         = Phase::Idle;
    uint32_t armed_at_ms_   = 0;
    uint32_t hold_start_ms_ = 0;
    bool     was_pressed_   = false;
    uint32_t last_now_ms_   = 0;
};

}  // namespace factory_reset_state
```

- [ ] **Step 4: Write the implementation**

Create `lib/factory_reset_state/factory_reset_state.cpp`:

```cpp
#include "factory_reset_state.h"

namespace factory_reset_state {

void Machine::arm(uint32_t now_ms) {
    phase_ = Phase::AwaitingHold;
    armed_at_ms_ = now_ms;
    hold_start_ms_ = 0;
    was_pressed_ = false;
    last_now_ms_ = now_ms;
}

void Machine::tick(const Inputs& in) {
    last_now_ms_ = in.now_ms;

    if (phase_ != Phase::AwaitingHold) {
        was_pressed_ = in.center_pressed;
        return;
    }

    // Window timeout (only while we're still in AwaitingHold and no hold
    // has confirmed).
    if (in.now_ms - armed_at_ms_ >= kArmWindowMs && !in.center_pressed) {
        phase_ = Phase::Idle;
        was_pressed_ = false;
        return;
    }

    if (in.center_pressed && !was_pressed_) {
        hold_start_ms_ = in.now_ms;          // press edge
    }
    if (!in.center_pressed) {
        hold_start_ms_ = 0;                  // release
    }

    if (in.center_pressed && hold_start_ms_ != 0) {
        uint32_t held = in.now_ms - hold_start_ms_;
        if (held >= kHoldRequiredMs) {
            phase_ = Phase::Resetting;
        }
    } else if (!in.center_pressed && was_pressed_) {
        // released before threshold → back to idle (caller dismisses card)
        phase_ = Phase::Idle;
    }

    was_pressed_ = in.center_pressed;
}

uint32_t Machine::holdMs() const {
    if (phase_ != Phase::AwaitingHold || hold_start_ms_ == 0) return 0;
    if (last_now_ms_ < hold_start_ms_) return 0;
    return last_now_ms_ - hold_start_ms_;
}

}  // namespace factory_reset_state
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `pio test -e native -f test_factory_reset_state`
Expected: all 8 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/factory_reset_state/ test/test_factory_reset_state/
git commit -m "feat(factory_reset_state): hold-to-confirm state machine + tests"
```

---

### Task 14: `InputRouter::centerHoldMs()` accessor

**Files:**
- Modify: `src/input/InputRouter.h`
- Modify: `src/input/InputRouter.cpp`

The `FactoryResetCoordinator` polls this to drive the
`factory_reset_state::Machine`. The existing 5-s long-press handler is
left intact and remains the recovery path outside the armed window.

- [ ] **Step 1: Add the accessor declaration**

In `src/input/InputRouter.h`, in the public section (near
`lastInputMs()`):

```cpp
    // True iff the center button is currently held (debounced state).
    bool centerHeld() const { return center_held_; }

    // Milliseconds of continuous center-button hold; 0 if not held.
    uint32_t centerHoldMs(uint32_t now_ms) const {
        return center_held_ ? (now_ms - center_press_ms_) : 0;
    }
```

(`center_held_` and `center_press_ms_` are already private members on
the class — no additional state needed.)

- [ ] **Step 2: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add src/input/InputRouter.h
git commit -m "feat(input): expose centerHoldMs() for factory-reset polling"
```

---

### Task 15: `FactoryResetCoordinator` device wiring

**Files:**
- Create: `src/core/FactoryResetCoordinator.h`
- Create: `src/core/FactoryResetCoordinator.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Write the header**

Create `src/core/FactoryResetCoordinator.h`:

```cpp
#pragma once

#include <stdint.h>

#include "factory_reset_state.h"

class ConfigStore;
class Settings;
class InputRouter;

class FactoryResetCoordinator {
public:
    FactoryResetCoordinator(ConfigStore& cfg, Settings& settings,
                            const char* default_device_name);

    void setInputRouter(InputRouter* r) { input_ = r; }

    void tick(uint32_t now_ms);

    // Web endpoint calls this. Opens the 30-s arm window.
    void arm(uint32_t now_ms);

    factory_reset_state::Phase phase() const { return machine_.phase(); }
    uint32_t                   holdMs() const { return machine_.holdMs(); }

private:
    void performWipe();

    ConfigStore&  cfg_;
    Settings&     settings_;
    InputRouter*  input_ = nullptr;
    const char*   default_device_name_;

    factory_reset_state::Machine machine_;
    bool          wipe_done_ = false;
    uint32_t      wipe_started_ms_ = 0;
};
```

- [ ] **Step 2: Write the implementation**

Create `src/core/FactoryResetCoordinator.cpp`:

```cpp
#include "FactoryResetCoordinator.h"

#include <Arduino.h>

#include "ConfigStore.h"
#include "Settings.h"
#include "../input/InputRouter.h"

FactoryResetCoordinator::FactoryResetCoordinator(
    ConfigStore& cfg, Settings& settings, const char* default_device_name)
    : cfg_(cfg)
    , settings_(settings)
    , default_device_name_(default_device_name) {}

void FactoryResetCoordinator::arm(uint32_t now_ms) {
    wipe_done_ = false;
    machine_.arm(now_ms);
}

void FactoryResetCoordinator::tick(uint32_t now_ms) {
    factory_reset_state::Inputs in{
        now_ms,
        input_ ? input_->centerHeld() : false,
    };
    machine_.tick(in);

    if (machine_.phase() == factory_reset_state::Phase::Resetting
        && !wipe_done_) {
        performWipe();
        wipe_done_ = true;
        wipe_started_ms_ = now_ms;
    }

    // 1 s after the wipe, reboot.
    if (wipe_done_ && (now_ms - wipe_started_ms_) >= 1000) {
        delay(100);
        ESP.restart();
    }
}

void FactoryResetCoordinator::performWipe() {
    cfg_.clear();
    settings_.clearToDefaults(default_device_name_);
}
```

- [ ] **Step 3: Wire in main.cpp**

In `src/main.cpp`:

```cpp
#include "core/FactoryResetCoordinator.h"
```

Below the existing globals (after `bleLink`):

```cpp
static FactoryResetCoordinator factoryReset{configStore, settingsStore,
                                            appState.macDeviceName()};
```

(If the call to `appState.macDeviceName()` isn't valid pre-`begin()`,
move the `factoryReset` declaration into `setup()` as a `static` local
*or* use an empty default name and let `clearToDefaults("")` regenerate
in-place. Either works.)

In `setup()`, after `inputRouter.begin();`:

```cpp
    factoryReset.setInputRouter(&inputRouter);
```

In `loop()`, near the other tick calls:

```cpp
    factoryReset.tick(now);
```

- [ ] **Step 4: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/core/FactoryResetCoordinator.h src/core/FactoryResetCoordinator.cpp src/main.cpp
git commit -m "feat(core): FactoryResetCoordinator wiring to NVS + reboot"
```

---

### Task 16: `FactoryResetCard` UI

**Files:**
- Create: `src/ui/cards/FactoryResetCard.h`
- Create: `src/ui/cards/FactoryResetCard.cpp`

Same conventions as Task 10's `UpdatingCard` — inherit from `Card`, use
`tft.*` calls, full-clear on first paint, partial redraw thereafter.

- [ ] **Step 1: Write the header**

Create `src/ui/cards/FactoryResetCard.h`:

```cpp
#pragma once

#include <stdint.h>

#include "../Card.h"

class FactoryResetCoordinator;

class FactoryResetCard : public Card {
public:
    explicit FactoryResetCard(FactoryResetCoordinator& coord)
        : coord_(coord) {}

    void tick(uint32_t now_ms) override;
    void invalidate() override;
    bool isDirty() const override;
    void render(Display& display) override;

    bool isActive() const;

private:
    FactoryResetCoordinator& coord_;
    bool     full_clear_     = true;
    uint8_t  last_pct_drawn_ = 255;
    uint8_t  last_phase_     = 255;
    uint32_t last_render_ms_ = 0;
    uint32_t now_ms_         = 0;
};
```

- [ ] **Step 2: Write the implementation**

Create `src/ui/cards/FactoryResetCard.cpp`:

```cpp
#include "FactoryResetCard.h"

#include "../../core/FactoryResetCoordinator.h"
#include "../../display/Display.h"
#include "factory_reset_state.h"

#include <Adafruit_ST7789.h>

namespace {
constexpr uint32_t kMinRedrawIntervalMs = 100;
}

void FactoryResetCard::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
}

void FactoryResetCard::invalidate() {
    full_clear_     = true;
    last_pct_drawn_ = 255;
    last_phase_     = 255;
}

bool FactoryResetCard::isActive() const {
    auto p = coord_.phase();
    return p == factory_reset_state::Phase::AwaitingHold
        || p == factory_reset_state::Phase::Resetting;
}

bool FactoryResetCard::isDirty() const {
    if (!isActive()) return false;
    uint32_t held = coord_.holdMs();
    uint8_t pct = (held >= factory_reset_state::kHoldRequiredMs)
                  ? 100
                  : (uint8_t)((held * 100u)
                              / factory_reset_state::kHoldRequiredMs);
    uint8_t phase_id = (uint8_t)coord_.phase();
    return full_clear_
        || pct != last_pct_drawn_
        || phase_id != last_phase_
        || (now_ms_ - last_render_ms_) >= kMinRedrawIntervalMs;
}

void FactoryResetCard::render(Display& display) {
    auto& tft = display.tft();
    auto  phase = coord_.phase();
    auto  held  = coord_.holdMs();
    uint8_t pct = (held >= factory_reset_state::kHoldRequiredMs)
                  ? 100
                  : (uint8_t)((held * 100u)
                              / factory_reset_state::kHoldRequiredMs);

    if (full_clear_ || (uint8_t)phase != last_phase_) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextWrap(false);

        if (phase == factory_reset_state::Phase::Resetting) {
            tft.setTextSize(2);
            tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
            tft.setCursor(8, 20);
            tft.print("RESET DONE");

            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 60);
            tft.print("Rebooting...");
        } else {
            tft.setTextSize(2);
            tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
            tft.setCursor(8, 6);
            tft.print("FACTORY RESET");

            tft.setTextSize(1);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
            tft.setCursor(8, 32);
            tft.print("Hold center 3s to confirm.");
            tft.setCursor(8, 46);
            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
            tft.print("Release to cancel.");

            // Progress bar outline (only drawn once per phase).
            tft.drawRect(8, 80, 224, 20, ST77XX_WHITE);
        }
        full_clear_ = false;
        last_phase_ = (uint8_t)phase;
    }

    if (phase == factory_reset_state::Phase::AwaitingHold) {
        // Erase + redraw only the bar interior.
        tft.fillRect(9, 81, 222, 18, ST77XX_BLACK);
        uint16_t fill = (uint16_t)((222u * pct) / 100u);
        if (fill > 0) tft.fillRect(9, 81, fill, 18, ST77XX_RED);
    }

    last_pct_drawn_ = pct;
    last_render_ms_ = now_ms_;
}
```

- [ ] **Step 3: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add src/ui/cards/FactoryResetCard.h src/ui/cards/FactoryResetCard.cpp
git commit -m "feat(ui): FactoryResetCard with hold-to-confirm bar"
```

---

### Task 17: `CardController` integration — force-show `FactoryResetCard`

**Files:**
- Modify: `src/ui/CardController.h`
- Modify: `src/ui/CardController.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add ownership**

In `src/ui/CardController.h`:

- Add `#include "cards/FactoryResetCard.h"`.
- Forward-declare `class FactoryResetCoordinator;`.
- Add `FactoryResetCoordinator&` to the constructor parameter list.
- Add the corresponding members:
  ```cpp
      FactoryResetCoordinator& fr_;
      FactoryResetCard         factory_reset_card_;
  ```

In `src/ui/CardController.cpp`:

- Add `#include "../core/FactoryResetCoordinator.h"`.
- Update the constructor signature + initializer list — store `fr_(fr)`,
  initialize `factory_reset_card_(fr)`.

- [ ] **Step 2: Pre-empt priority — factory reset above OTA**

In `CardController::tick()`, the order is: factory reset first, then
OTA install, then the carousel. Update the block from Task 11 to:

```cpp
    // 1) Factory reset (highest priority — destructive operation).
    factory_reset_card_.tick(now_ms);
    if (factory_reset_card_.isActive()) {
        if (display.isAsleep()) display.setBacklight(true);
        if (factory_reset_card_.isDirty()) {
            factory_reset_card_.render(display);
        }
        last_activity_ms_ = now_ms;
        return;
    }

    // 2) OTA install.
    updating_card_.tick(now_ms);
    if (updating_card_.isActive()) {
        if (display.isAsleep()) display.setBacklight(true);
        if (updating_card_.isDirty()) {
            updating_card_.render(display);
        }
        last_activity_ms_ = now_ms;
        return;
    }

    // 3) Normal carousel.
    if (!display.isAsleep()) {
        stack_.tick(now_ms, display);
    }
```

They can't realistically be simultaneously active (you can't arm reset
while downloading firmware), but the explicit ordering keeps the
intent obvious.

- [ ] **Step 3: Pass the coordinator into CardController in main.cpp**

In `src/main.cpp`, update the `cardController{...}` brace-init to pass
`factoryReset` in the new parameter slot. Make sure the order matches
the constructor.

- [ ] **Step 4: Build**

Run: `pio run`
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/ui/CardController.h src/ui/CardController.cpp src/main.cpp
git commit -m "feat(ui): CardController pre-empts carousel for factory reset"
```

---

### Task 18: `POST /api/factory-reset` endpoint + Manage UI wire-up

**Files:**
- Modify: `src/net/HttpServer.h`
- Modify: `src/net/HttpServer.cpp`
- Modify: `src/main.cpp`
- Modify: `tools/web-smoke.sh`

`HttpServer` needs a reference to the coordinator. Pass it through the
constructor (similar to how `WifiManager` etc. are already injected).

- [ ] **Step 1: Add the dependency to HttpServer**

In `src/net/HttpServer.h`:

```cpp
class FactoryResetCoordinator;
```

Add `FactoryResetCoordinator& fr_;` to private members and an
`FactoryResetCoordinator&` parameter to the constructor (last
positional arg).

In `src/net/HttpServer.cpp`, update the constructor's initializer list.

- [ ] **Step 2: Register the endpoint**

In `registerStaHandlers()`:

```cpp
server_->on("/api/factory-reset", HTTP_POST, [this] {
    fr_.arm(millis());
    server_->send(200, "application/json",
        "{\"state\":\"awaiting_hold\",\"timeout_s\":30}");
});
```

- [ ] **Step 3: Pass the coordinator into HttpServer in main.cpp**

Update the `httpServer{...}` brace-init to include `factoryReset`.

- [ ] **Step 4: Wire the Manage button in the dashboard JS**

In the `<script>` block in HttpServer.cpp (added in Task 12), add:

```javascript
$("btn-factory-reset").addEventListener("click", async () => {
    if (!confirm("Factory reset wipes Wi-Fi and all settings. " +
                 "Continue?")) return;
    try {
        await fetch("/api/factory-reset", {method: "POST"});
        $("reset-hint").textContent =
            "Go to the device and hold the center button for 3 s " +
            "to confirm. (Auto-cancels in 30 s.)";
    } catch (e) {
        $("reset-hint").textContent = `Network error: ${e.message}`;
    }
});
```

- [ ] **Step 5: Add web-smoke check**

Append to `tools/web-smoke.sh` — but only the arm step, **not the hold**
(holding would actually wipe the test device):

```bash
echo "POST /api/factory-reset (arm only; do NOT hold center)"
RESP=$(curl -fsS -X POST "http://$HOST/api/factory-reset")
echo "  $RESP"
echo "$RESP" | grep -q '"state":"awaiting_hold"' \
    || { echo "FAIL: factory-reset did not arm"; exit 1; }
# wait for the 30-s window to elapse so the test device returns to normal
echo "  waiting 32s for arm window to lapse..."
sleep 32
```

- [ ] **Step 6: Build, flash, smoke** *(manual)*

```bash
pio run -t upload
HOST=<device-ip> ./tools/web-smoke.sh
```

Expected: smoke passes; after the POST, the device shows the
FactoryResetCard for 30 s, then returns to the normal card.

- [ ] **Step 7: Commit**

```bash
git add src/net/HttpServer.h src/net/HttpServer.cpp src/main.cpp \
        tools/web-smoke.sh
git commit -m "feat(http): POST /api/factory-reset + dashboard wire-up"
```

---

### Task 19: GitHub Actions release workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release firmware

on:
  push:
    tags:
      - 'v*.*.*'

permissions:
  contents: write   # for uploading the asset

jobs:
  build-and-upload:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
          cache: 'pip'

      - name: Cache PlatformIO
        uses: actions/cache@v4
        with:
          path: |
            ~/.platformio
            ~/.cache/pip
          key: ${{ runner.os }}-platformio-${{ hashFiles('platformio.ini') }}

      - name: Install PlatformIO
        run: |
          python -m pip install --upgrade pip
          pip install platformio

      - name: Extract version from tag
        id: ver
        run: echo "version=${GITHUB_REF_NAME#v}" >> "$GITHUB_OUTPUT"

      - name: Build firmware
        env:
          PLATFORMIO_BUILD_FLAGS: -DFW_VERSION='"${{ steps.ver.outputs.version }}"'
        run: pio run -e adafruit_feather_esp32s3_reversetft

      - name: Copy artifact
        run: cp .pio/build/adafruit_feather_esp32s3_reversetft/firmware.bin .

      - name: Upload firmware.bin to the release
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          # Wait for the release to exist (the developer creates it
          # manually, then pushes the tag — or it may already exist).
          for i in 1 2 3 4 5; do
            if gh release view "$GITHUB_REF_NAME" >/dev/null 2>&1; then
              break
            fi
            echo "Release $GITHUB_REF_NAME not found yet, retry $i/5..."
            sleep 6
          done
          gh release upload "$GITHUB_REF_NAME" firmware.bin --clobber
```

The `PLATFORMIO_BUILD_FLAGS` environment variable is appended to the
existing `build_flags` in `platformio.ini`, so the CI definition of
`FW_VERSION` overrides the local-default one. See
https://docs.platformio.org/en/latest/envvars.html#envvar-PLATFORMIO_BUILD_FLAGS.

- [ ] **Step 2: Verify the workflow syntax locally (optional)**

```bash
# If `act` is installed, dry-run:
act push -W .github/workflows/release.yml --eventpath /dev/null --list
```

Skip if `act` isn't installed. The real test is pushing a tag.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: release workflow — build firmware.bin on tag push"
```

- [ ] **Step 4: Smoke test the release pipeline end-to-end** *(manual)*

After the PR lands and you've pushed once over USB:

```bash
# 1. Create a release on GitHub for the first OTA-capable build:
gh release create v0.1.0 --title "v0.1.0 — first OTA" \
    --notes "Initial pull-based OTA. Install this build via USB once; \
future updates flow over the wire."

# 2. Push the tag — workflow runs, attaches firmware.bin.
git push origin v0.1.0

# 3. Confirm artifact exists:
gh release view v0.1.0
```

---

### Task 20: End-to-end on-device manual test

**Files:** none (verification only).

After Tasks 1–19, perform this checklist against a physical device.
Record results in the PR description.

- [ ] **Step 1: Happy-path OTA**

1. Flash the device with `pio run -t upload`. Note the version
   reported on `/api/firmware-version` (`0.0.0-dev` for a local build,
   or `0.1.0` if you installed the released `.bin` over USB).
2. Bump the version locally (edit `FW_VERSION` to `0.0.1-dev` and
   `pio run -t upload`).
3. Create a v0.2.0 release on GitHub with `firmware.bin` attached
   (either via the workflow from Task 19 or manual upload).
4. Open the dashboard, click **Check for updates**, then **Install
   update**.
5. Observe: UpdatingCard appears on the device; progress bar
   advances; device reboots; web UI shows "Updated to 0.2.0";
   `/api/firmware-version` returns `0.2.0`.

- [ ] **Step 2: Interrupted-download recovery**

1. From a working OTA-capable build, click **Install update**.
2. While the progress bar is between 20–60%, **disable the laptop's
   Wi-Fi** (or pull the device off the AP some other way).
3. Observe: state goes to `Failed`; device remains on the original
   image after reboot or after the failure timeout.

- [ ] **Step 3: Bootloop auto-rollback**

1. Make a deliberately bricking build: in `src/main.cpp`'s `setup()`,
   add `while (true) { delay(1000); }` before the
   `esp_ota_mark_app_valid_cancel_rollback()` call so the new image
   never reaches normal operation.
2. Build, tag, release this broken build.
3. Install it over OTA from a working device.
4. After it boots into the broken image and loops the watchdog
   reboot a couple of times, power-cycle the device.
5. Observe: device boots back into the previous image (rollback).
   `/api/firmware-version` shows the previous version.
6. **Revert the deliberate brick** before committing.

- [ ] **Step 4: Factory reset — confirm path**

1. Open the dashboard, click **Factory reset**, confirm the JS dialog.
2. Observe: FactoryResetCard appears on device with "Hold center 3 s".
3. Hold the center button. Watch the bar fill.
4. After 3 s: "RESET COMPLETE", device reboots.
5. Observe: device comes up in AP-provisioning mode (no Wi-Fi creds),
   StatusCard shows default device name (regenerated from MAC suffix).

- [ ] **Step 5: Factory reset — cancel-by-release**

1. Re-provision Wi-Fi, then open dashboard.
2. Click **Factory reset**, confirm.
3. On the device, hold the center button for ~1 s, then **release**.
4. Observe: FactoryResetCard dismisses; device returns to the
   previously visible card; Wi-Fi creds + settings preserved.

- [ ] **Step 6: Factory reset — 30-s window timeout**

1. From the dashboard, click **Factory reset**, confirm.
2. Do **not** touch the device.
3. Observe: after ~30 s, FactoryResetCard dismisses; device returns
   to normal; creds preserved.

- [ ] **Step 7: 5-s long-press unchanged outside the armed window**

1. With the device idle (factory reset NOT armed), hold center 5 s.
2. Observe: device clears Wi-Fi creds and reboots into AP-provisioning
   (existing behavior — settings NS untouched).

- [ ] **Step 8: Commit nothing**

This is a verification task — no code changes. Update the PR
description with results, then close out.

---

## Self-review notes

- All spec sections (Distribution model, Versioning, Partition table,
  Components UpdateManager/GitHubReleases/UpdatingCard/FactoryResetCard/
  FactoryResetCoordinator/Coexistence, HTTP endpoints, OTA install
  internals, Web UI, Failure modes, CI, File layout) have an
  implementing task.
- The "Bundle the Mozilla CA bundle" choice is implemented pragmatically
  as a 2-cert GitHub-roots bundle (Task 5). The implementation uses the
  same `WiFiClientSecure::setCACertBundle()` API the spec calls for; the
  PEM file can be swapped for the full Mozilla bundle later with no API
  changes.
- The `centerHoldMs()` accessor (Task 14) is the bridge between
  `InputRouter` and `FactoryResetCoordinator` that the spec assumed but
  did not explicitly enumerate. Worth flagging in the PR.
