# desktop-buddy

ESP32-S3 firmware for the [Adafruit Feather ESP32-S3 Reverse TFT](https://www.adafruit.com/product/5691). It connects to Claude Desktop over BLE, shows live session state on the built-in TFT, lets you approve or deny permission prompts from the device, and exposes a local Web UI for Wi-Fi, settings, OTA updates, and reset actions.

## Features

### Claude Session Display

- **Card carousel** navigated by the onboard buttons.
- **Status card** with Claude state, total/running/waiting counts, latest message, usage percentage/bar, battery, and live/offline footer.
- **Animated eyes card** for `DISCONNECTED`, `IDLE`, `WORKING`, `WAITING`, and completion feedback.
- **Wi-Fi card** with current connection state, device URL, and QR code.
- **Prompt overlay** that takes over when Claude Desktop is waiting on a permission decision.

### Permission Prompts

When Claude Desktop sends a snapshot with a `prompt`, the device shows a decision UI:

- **Approve** sends `{"cmd":"permission","id":"...","decision":"once"}`.
- **Deny** sends `{"cmd":"permission","id":"...","decision":"deny"}`.
- **Dismiss** hides the prompt locally without sending a decision.
- Dismissed prompt ids stay collapsed so the same prompt does not keep taking over the screen.
- If the prompt disappears or the BLE link goes offline, the device clears the prompt UI.

### BLE / Claude Desktop

- Nordic UART Service (NUS) BLE bridge.
- Advertises as a MAC-derived `Claude-XXXX` device.
- Re-advertises automatically after disconnects.
- Handles MTU fragmentation and newline-delimited JSON snapshots.
- Uses a configurable heartbeat timeout to mark Claude as offline.

### Wi-Fi + Web UI

The device hosts a local Web UI:

- In station mode: `http://<device-ip>/`
- In first-run or forgotten-Wi-Fi mode: open AP `claude-buddy-XXXX`, captive portal at `http://192.168.4.1/`

From the Web UI you can:

- Change device name.
- Adjust Claude live timeout.
- Configure screen sleep, dim timeout, dim brightness, and full brightness.
- Enable, disable, order, and choose the boot card.
- Scan and save Wi-Fi credentials.
- Reboot, reset settings, or forget Wi-Fi.
- Check for and install firmware updates from GitHub Releases.
- Arm a factory reset that must be confirmed on-device.

### OTA Updates

- Manual pull-based updates from GitHub Releases.
- Checks `https://api.github.com/repos/awesomelionel/desktop-buddy/releases/latest`.
- Installs the release asset named `firmware.bin`.
- Uses TLS validation and streams the GitHub asset directly into the inactive OTA partition.
- Marks successful boots valid with ESP32 OTA rollback support.
- Shows install progress on both the Web UI and an on-device update card.

Release tags should use `vMAJOR.MINOR.PATCH`, for example `v0.1.5`. CI builds tagged releases and uploads `firmware.bin`.

### Factory Reset

- Web UI **Factory reset** arms a 30-second confirmation window.
- The device shows a full-screen confirmation card.
- Holding the center button for 3 seconds wipes Wi-Fi credentials and settings, then reboots.
- Holding center for 5 seconds outside that armed flow still forgets Wi-Fi as a recovery path.

### Hardware Support

- MAX17048 battery gauge polling with battery percentage and charging state in the footer.
- Backlight dim/off behavior based on input and activity.
- Three onboard buttons:
  - D2: previous/up
  - D0 / BOOT: next/down
  - D1: center/select

## Hardware

| Component | Part |
|---|---|
| MCU + display | Adafruit Feather ESP32-S3 Reverse TFT (#5691) |
| Display | 1.14" ST7789 TFT, 135x240 px |
| Battery gauge | MAX17048 |
| Input | 3 onboard buttons: D2, D0, D1 |

## Build & Flash

Install PlatformIO, then build and upload:

```sh
pio run -e adafruit_feather_esp32s3_reversetft -t upload
pio device monitor -b 115200
```

If `pio` is not on PATH but PlatformIO is installed in its default location (`~/.platformio` on macOS/Linux, `%USERPROFILE%\.platformio` on Windows):

```sh
"$HOME/.platformio/penv/bin/pio" run -e adafruit_feather_esp32s3_reversetft -t upload
"$HOME/.platformio/penv/bin/pio" device monitor -b 115200
```

On Windows (PowerShell), use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e adafruit_feather_esp32s3_reversetft -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

## Pair with Claude Desktop

1. In Claude Desktop, enable developer mode from **Help -> Troubleshooting -> Enable Developer Mode**.
2. Open **Developer -> Open Hardware Buddy...**.
3. Click **Connect** and select `Claude-XXXX`.

After pairing, the device advertises again after disconnects and Claude Desktop can reconnect in the background.

## First-Time Wi-Fi Setup

1. Flash the firmware.
2. Join the open Wi-Fi network named `claude-buddy-XXXX`.
3. Open `http://192.168.4.1/` if the captive portal does not open automatically.
4. Pick your Wi-Fi network, enter the password, and save.
5. The device reboots and shows its LAN URL on the Wi-Fi card.

## OTA Test Flow

After the OTA-capable firmware has been flashed once over USB:

1. Create/push a GitHub release tag such as `v0.1.6`.
2. Confirm the release has a `firmware.bin` asset.
3. Open the device Web UI.
4. Click **Check for updates**.
5. Click **Install update**.
6. Watch the device reboot and verify the new firmware version in the Web UI or serial monitor.

Useful serial logs:

```text
[ota] resolving https://github.com/...
[ota] probe code=302
[ota] redirect -> https://release-assets.githubusercontent.com/...
[ota] download code=200 len=...
[ota] install OK; rebooting
```

If installation fails, the Web UI reports the latest OTA error string, such as `probe http 403`, `download http 404`, `no content length`, or `bad firmware header`.

## Web API

Key endpoints served in station mode:

| Endpoint | Method | Purpose |
|---|---:|---|
| `/api/status` | GET | Wi-Fi, uptime, device name, live state, Claude counters/message |
| `/api/settings` | GET | Current persisted settings |
| `/api/settings/device` | POST | Device name, live timeout, sleep/dim/backlight levels |
| `/api/settings/cards` | POST | Enabled cards, card order, boot card |
| `/api/settings/network` | POST | Save Wi-Fi credentials and reboot |
| `/api/networks` | GET | Wi-Fi scan results |
| `/api/scan` | POST | Start Wi-Fi scan |
| `/api/firmware-version` | GET | Current firmware version |
| `/api/check-for-updates` | POST | Fetch latest GitHub release metadata |
| `/api/update-status` | GET | OTA state, versions, release notes, progress, last error |
| `/api/install-update` | POST | Start OTA install |
| `/api/factory-reset` | POST | Arm on-device factory reset confirmation |
| `/api/actions/reboot` | POST | Reboot |
| `/api/actions/reset-settings` | POST | Reset settings namespace and reboot |
| `/api/actions/forget-wifi` | POST | Clear Wi-Fi credentials and reboot |

## Protocol

Claude Desktop sends newline-delimited JSON snapshots over BLE. The parser accepts partial updates and preserves missing fields.

Important fields:

- `total`, `running`, `waiting`
- `msg`
- `tokens_today` as the legacy daily-token display fallback
- optional `usage` object with `used`, `remaining`, and/or `limit` for the Status card usage bar
- `prompt`
- heartbeat snapshots used for live/offline state

Malformed or non-JSON lines are ignored. Fixed-size buffers bound RAM usage on the device.

## Run Tests

Host-side Unity tests live under `test/`:

```sh
pio test -e native
```

The ESP32 firmware build is the main integration check:

```sh
pio run -e adafruit_feather_esp32s3_reversetft
```

There is also a Web smoke script for a running device:

```sh
DEVICE_HOST=<device-ip> ./tools/web-smoke.sh
```

## Code Structure

```text
src/
  main.cpp                         boot, main loop, wiring
  ble_bridge.*                     BLE NUS bridge
  core/
    AppState.*                     shared runtime state
    ConfigStore.*                  Wi-Fi credential storage
    Settings.*                     persisted user settings
    UpdateManager.*                GitHub release check + OTA install
    FactoryResetCoordinator.*      web-armed hold-to-confirm reset
  net/
    WifiManager.*                  STA/AP Wi-Fi state machine
    HttpServer.*                   Web UI + JSON API
    GitHubReleases.*               GitHub releases HTTPS client
  ui/
    CardController.*               card carousel, overlays, backlight
    cards/                         status, eyes, Wi-Fi, OTA, reset cards
  hal/
    Battery.*                      MAX17048 polling
  input/
    InputRouter.*                  button routing and long holds

lib/
  protocol/                        Claude snapshot parser
  state/                           Claude state derivation
  prompt_ui/                       prompt decision state machine
  settings/                        settings model + validation
  version_compare/                 semver comparison
  github_releases_parse/           GitHub release JSON parser
  factory_reset_state/             pure reset confirmation state machine
  backlight/                       dim/off logic
  buttons/                         button helpers

test/
  test_*                           Unity tests for pure libraries
```
