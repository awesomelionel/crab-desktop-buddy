# Configure via Web — Design

Lets a user on the same Wi-Fi change device settings from `http://<device-ip>/`. Settings, status, and Wi-Fi reprovisioning all live on the existing dashboard page.

This is the first of three planned "what can the user do on the web" sub-projects. **Manage** (OTA, factory reset over the wire) and **Personalize** (eyes customization) follow once this lands and the settings infrastructure is in place.

## Goals

- Per-device prefs (name, timeouts, cards, boot card) editable without reflashing.
- Wi-Fi reprovisioning without holding the center button (button stays as the recovery path).
- Reboot / reset-settings / forget-Wi-Fi as explicit actions on the page.
- Settings infrastructure (`core/Settings` + EventBus signal) reusable by future Personalize + Manage work.

## Non-goals

- Authentication. The page is reachable by anyone on the LAN; same trust model as the open captive-portal AP. A future `Lock` spec covers this.
- OTA firmware update. That's the Manage spec.
- Eye colors, behaviors, custom expressions. That's the Personalize spec.
- Settings sync across multiple devices.

## Settings model

A new `core/Settings.{h,cpp}` parallels `ConfigStore`. Owns typed in-memory state and persists to the NVS namespace `settings`, separate from `wifi-creds` so a Wi-Fi factory reset doesn't nuke device prefs (and vice-versa).

| Field | Type | Default | Range |
|---|---|---|---|
| `device_name` | `char[16]` | `Claude-XXXX` (last 2 MAC bytes) | 1–15 chars, ASCII printable (32–126) |
| `sleep_timeout_s` | `uint16_t` | `0` (never) | `0`, or `30…3600` |
| `live_timeout_s` | `uint16_t` | `30` | `5…300` |
| `cards_enabled` | bitmask | all on except `NavTest` | per-card bool |
| `cards_order` | `uint8_t[]` | `Status, Eyes, Wifi` | permutation of the enabled set |
| `boot_card_id` | `uint8_t` | `Status` | one of `cards_enabled` |

`Settings::begin()` runs once in `setup()` and loads the NVS-stored values (or applies defaults). `Settings::setX(...)` setters validate, persist immediately to NVS, then publish `EventKind::SettingsChanged` on the EventBus.

Card identity is a stable enum — `enum class CardId : uint8_t { Status=0, Eyes, Wifi, NavTest, Count_ }`. The order/enabled fields use these ids so renaming or reordering enum values is a migration concern (call out in the implementation plan).

## Page structure

`/` becomes the dashboard *and* the settings page (the user's pick). Layout, top to bottom:

```
HEADER     <device-name>        [LIVE] / [OFFLN]
           <ssid> · <ip>

STATUS     state · total · running · waiting
(read)     last msg
(poll 3s)

DEVICE     Name:           [_____________]
           Live timeout:   [30] s
           Sleep timeout:  [0] s   (0 = never)
                                                 [ Save ]

CARDS      ☑ Status   ↑↓
           ☑ Eyes     ↑↓
           ☑ Wifi     ↑↓
           ☐ NavTest  ↑↓ (dev)
           Boot card: [Status ▼]
                                                 [ Save ]

NETWORK    SSID:     [scan dropdown ▼] [↻]
           Password: [_______]
           (saving reboots the device)
                                                 [ Save ]

DANGER     [ Reboot ]  [ Reset settings ]  [ Forget Wi-Fi ]
```

Status pulls from the existing JSON every 3 s. Each settings section has its own Save button (per-section save was the chosen interaction model — batches NVS writes naturally and matches the existing captive-portal `/save` pattern).

The captive portal page (AP mode) stays minimal and posts to the same `POST /api/settings/network` endpoint as the inline form, so the network-save handler is shared.

## Endpoints

| Method+Path | Body | Purpose |
|---|---|---|
| `GET /` | – | Server-rendered HTML with current values pre-filled (server-side render so it's useful before JS runs; JS then keeps live values fresh). |
| `GET /api/status` | – | Existing `/status` JSON, moved under `/api/` for consistency. |
| `GET /api/settings` | – | Current `Settings` as JSON. |
| `POST /api/settings/device` | form | `name`, `live_timeout_s`, `sleep_timeout_s` |
| `POST /api/settings/cards` | form | `enabled[]`, `order[]`, `boot_card` |
| `POST /api/settings/network` | form | `ssid`, `pass` (existing `/save` moves here, with reboot) |
| `POST /api/scan` | – | Trigger Wi-Fi rescan (was `/scan`). |
| `GET /api/networks` | – | Scan results (was `/networks`). |
| `POST /api/actions/reboot` | – | `ESP.restart()` after 500 ms flush. |
| `POST /api/actions/reset-settings` | – | NVS `settings` clear + reboot (Wi-Fi creds preserved). |
| `POST /api/actions/forget-wifi` | – | NVS `wifi-creds` clear + reboot (lands in AP). |

The legacy `GET /status`, `GET /networks`, `POST /scan`, `POST /save` paths are kept as 301 redirects to the `/api/...` versions for one release so any docs or curl scripts still work.

## Settings application

How each save propagates at runtime:

| Setting | Effect | Reboot? |
|---|---|---|
| `device_name` | `BleLink` listens for `SettingsChanged`, calls a new `ble_bridge::ble_set_device_name(...)` which restarts BLE advertising under the new name. StatusCard footer re-reads on next render. Connected client sees a transient disconnect on rename — acceptable. | No |
| `live_timeout_s` | `AppState::isLive()` reads `settings_.live_timeout_s` instead of the constant `LIVE_TIMEOUT_MS`. | No |
| `sleep_timeout_s` | `Display::tick(now_ms, last_input_ms)` turns backlight off (`digitalWrite(TFT_BACKLITE, LOW)`) after the timeout; any button event in `InputRouter` updates `last_input_ms` and turns the backlight on. `0` disables the timer. | No |
| `cards_enabled` / `cards_order` / `boot_card` | `CardController::rebuildStack()` clears the stack, re-adds cards in the new order, sets `CardStack::index_` to the boot card position. Active card invalidates so the next render is clean. | No |
| `Network` save | Settings persisted, response sent, `delay(500)`, `ESP.restart()` (existing behavior). | Yes |
| `Reset settings` | `Settings::clear()`, reboot. | Yes |
| `Forget Wi-Fi` | `ConfigStore::clear()`, reboot. | Yes |

### Fallback-to-AP after failed reconnects

To keep inline Wi-Fi reconfiguration from bricking the user out, `WifiManager` adds a `reconnect_attempts_` counter:

- Increment on each `STA_CONNECTING → STA_RECONNECT` transition.
- Reset to `0` on any successful `STA_CONNECTED`.
- When it crosses **5**, transition to `AP_PROVISIONING` instead of scheduling another `STA_CONNECTING`. The user reconnects to the AP and uses the captive portal.

This makes "type wrong password in the inline form" a recoverable mistake — with the existing exponential backoff (2 s, 4 s, 8 s, 16 s, 30 s) the 5th failed attempt hits at ≈ 60 s after the save, then the device drops to AP and the captive portal becomes reachable, without the user needing the center-button hold.

## EventBus

Adds one event kind: `EventKind::SettingsChanged`. No payload — subscribers re-read whatever fields they care about and compare to their cached value before reacting. Cheaper than per-field events; matches the existing pattern (`SnapshotReceived` / `WifiConnected` are similarly payload-free).

Subscribers:

- `BleLink`: re-reads `device_name`, calls `ble_set_device_name` if changed.
- `CardController`: re-reads cards config, calls `rebuildStack()` if changed.
- `Display`: re-reads `sleep_timeout_s`.
- `AppState`: no subscriber — `isLive()` reads at call time.

## Validation

Server is source of truth. Each `POST /api/settings/*` handler validates before persisting; rejection is `400 application/json` with `{"ok":false,"error":"..."}`.

| Field | Rule |
|---|---|
| `device_name` | 1–15 chars, ASCII 32–126 |
| `live_timeout_s` | 5 ≤ x ≤ 300 |
| `sleep_timeout_s` | x == 0 or 30 ≤ x ≤ 3600 |
| `cards_enabled` | at least one card |
| `cards_order` | permutation of the enabled set, no duplicates, no unknown ids |
| `boot_card` | present in `cards_enabled`; otherwise silently default to the first enabled card |
| `ssid` | 1–32 chars (existing rule) |
| `pass` | 0–64 chars (existing rule) |

Success returns `200 application/json` `{"ok":true}` plus the resulting settings JSON so the page can re-render the form with the actually-applied values (handles the `boot_card` silent default, etc.). The page surfaces errors inline next to the relevant Save button.

Client-side `<input>` `pattern`/`min`/`max` attributes mirror these rules so the happy path is nicer, but the server doesn't trust them.

## Security & threat model

- **No auth.** The dashboard is open on the LAN. Same trust model as the open captive-portal AP. Adding HTTP basic-auth or a "lock" PIN is a future spec.
- **CSRF.** Same-origin only — the device serves both the page and the API, no cross-origin write expected. Not adding CSRF tokens for v1.
- **Open AP during reprovisioning.** Unchanged from current behavior.

## File layout

```
lib/
  settings/                       # NEW — pure validation + JSON, native-testable
    settings.h                    # Settings struct, validators, serializer
    settings.cpp
src/
  core/
    Settings.{h,cpp}              # NEW — Arduino-side wrapper: NVS + EventBus
  net/
    HttpServer.{h,cpp}            # adds /api/settings/* + /api/actions/* handlers
    WifiManager.{h,cpp}           # adds reconnect-attempts counter, fallback-to-AP
  display/
    Display.{h,cpp}               # adds tick(now, last_input_ms) for sleep
  input/
    InputRouter.{h,cpp}           # exposes lastInputMs() for Display::tick
  ui/
    CardController.{h,cpp}        # rebuildStack() driven by SettingsChanged
test/
  test_settings/                  # NEW — native unit tests for lib/settings
    test_settings.cpp
```

## Testing strategy

**Native unit tests** (covers the bulk of risk):

- `lib/settings`: validation rules, JSON round-trip, default values, the cards-permutation invariant. Run via `pio test -e native`.

**Hardware integration** (verified by hand):

- Save device name → BLE peripheral advertises new name, StatusCard footer updates.
- Save live/sleep timeouts → live badge flips correctly; backlight goes off and wakes on button.
- Save cards config → carousel reflects new order, NavTest hidden, boot card respected on next reboot.
- Save network with wrong password → device reboots, fails to associate, falls back to AP after ~30 s, captive portal works.
- Reset settings → device prefs back to defaults, Wi-Fi still works.
- Forget Wi-Fi → device reboots into AP, settings preserved.

Curl smoke tests for the `/api/...` surface live in `tools/web-smoke.sh` documented alongside this spec; CI does not run them (no device on CI).

## Open questions / decisions punted

- Backlight PWM (display brightness slider) was deliberately dropped from this scope — TFT backlight is currently a digital pin, not PWM-wired. If we want graded brightness later, that's a hardware-side change and a separate setting.
- "Reorder cards" interaction is up/down arrows. Drag-to-reorder is fashionable but adds a JS dep; revisit if v1 feels clunky.
- `device_name` rename causes a brief BLE disconnect of any connected desktop client. Acceptable for v1; if it becomes annoying we'd consider deferring the rename until the next disconnect.
