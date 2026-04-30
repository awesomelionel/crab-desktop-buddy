# Architecture refactor + Wi‑Fi plan

This doc captures two related plans:

1. **Architecture refactor** — replicate the deskhog MVC‑ish separation in this codebase.
2. **Wi‑Fi capability** — add Wi‑Fi STA + captive portal provisioning, display IP on device.

The Wi‑Fi work depends on the refactor. Do the refactor first; Wi‑Fi lands as new modules + a card on top.

---

## Part 1: Architecture refactor

### Current state

- `src/main.cpp` is a 447‑line monolith: globals, render functions, button polling, line parsing, and 3 hardcoded "cards" (`CARD_STATUS`, `CARD_EYES`, `CARD_NAV_TEST`) all live in `loop()`. Switching cards is a `currentCard` enum + a `paint_current_card()` if/else.
- `lib/` already has clean modules: `buttons`, `prompt_ui`, `protocol`, `state` — these are good and we keep them. They have tests under `test/`.
- `src/ble_bridge.{h,cpp}` and `src/eyes.{h,cpp}` are fine, just need owners.

### Target shape (mirrors deskhog at our scale)

We are not running LVGL or FreeRTOS tasks — single Arduino loop with `Adafruit_GFX`. We copy deskhog's **separation of concerns**, not its threading.

| deskhog | ours |
|---|---|
| `DisplayInterface` (LVGL + buffers) | `display/Display` — owns `tft`, screen dims, `init()` |
| `InputHandler` interface | `ui/InputHandler` — `bool handleButton(ButtonEvent)` returns true if consumed |
| `Card` (LVGL obj + InputHandler) | `ui/Card` — `virtual void render(Display&)`, `virtual bool isDirty()`, plus `InputHandler` |
| `CardNavigationStack` | `ui/CardStack` — vector of `Card*`, current index, prev/next, dispatches button to active card first |
| `CardController` | `ui/CardController` — owns the stack, registers cards, handles cross‑card events (e.g. prompt overrides nav) |
| `EventQueue` | `core/EventBus` — small synchronous pub/sub (no FreeRTOS); used for `SnapshotReceived`, `LinkLive/Dead`, `PromptShow/Hide` |
| `posthog/PostHogClient` | `net/BleLink` — wraps `ble_bridge` + line buffering + `protocol_parse_line`, publishes `SnapshotReceived` to the bus |
| (n/a) | `core/AppState` — single owner of `ClaudeStatus`, `BuddyState`, `lastSnapshotMs`, `deviceName`, `isLive()` |

### File layout

```
src/
  main.cpp                     // ~50 lines: setup() wires everything, loop() ticks subsystems
  core/
    AppState.{h,cpp}           // status, buddy state, isLive(), device name
    EventBus.{h,cpp}           // tiny pub/sub, std::function callbacks
  display/
    Display.{h,cpp}            // owns Adafruit_ST7789, init, getters W/H
  net/
    BleLink.{h,cpp}            // wraps ble_bridge + line parser, emits SnapshotReceived
  input/
    InputRouter.{h,cpp}        // wraps lib/buttons, emits ButtonEvent
  ui/
    InputHandler.h             // interface
    Card.h                     // base: render(Display&) + InputHandler + isDirty()
    CardStack.{h,cpp}          // navigation, draws active card, routes buttons
    CardController.{h,cpp}     // builds + registers cards, listens to bus
    cards/
      StatusCard.{h,cpp}       // moves render_status() here, reads AppState
      EyesCard.{h,cpp}         // wraps eyes.cpp anim + dirty-detection
      NavTestCard.{h,cpp}      // moves render_nav_test() here
      PromptCard.{h,cpp}       // wraps lib/prompt_ui; pushed/popped by controller on prompt visible
lib/                           // unchanged: buttons, prompt_ui, protocol, state
test/                          // existing test_* dirs unchanged; add test_card_stack later
```

### Migration order (each step compiles + boots)

1. **Extract `Display`** — move `tft`, `initDisplay()`, W/H constants out of `main.cpp`. `main.cpp` holds a `Display display;` and passes a reference around. No behavior change.
2. **Extract `AppState`** — move `status`, `currentState`, `lastSnapshotMs`, `deviceName`, `isLive()`, `initDeviceName()`. Render functions take `const AppState&`.
3. **Introduce `Card` base + `CardStack`** — move the 3 render funcs into `StatusCard`, `EyesCard`, `NavTestCard`. Each owns its own dirty‑tracking state (the per‑card `lastDrawn*` globals migrate into the cards). `CardStack` replaces `currentCard` enum + `paint_current_card()` + `poll_nav()`.
4. **Add `InputRouter`** — wraps `buttons_step` and the raw `digitalRead` in `poll_nav`. Emits a normalized `ButtonEvent` to `CardStack`, which gives the active card first refusal via `InputHandler::handleButton`. NavTestCard returns `false` to fall through to nav; StatusCard returns `false`; EyesCard returns `false`.
5. **Wrap prompt as `PromptCard`** — `CardController` listens for `prompt_ui_view().visible` going true/false and pushes/pops `PromptCard` to the front of the stack, reproducing today's "prompt overrides nav" behavior. Prompt button consumption stays in the card (returns `true`).
6. **Extract `BleLink`** — move the `lineBuf` parsing out of `loop()`. On a successful parse, `BleLink` writes into `AppState` (or emits `SnapshotReceived` and `AppState` listens). Outgoing `ble_write_line` is called by `PromptCard` via a `BleLink&` reference passed in.
7. **Add minimal `EventBus`** — only after step 6 so we have ≥2 producers (BleLink, PromptCard) and ≥2 consumers (AppState, CardController). Don't build it earlier; you'll over‑abstract.
8. **`main.cpp` final shape** — construct `Display`, `AppState`, `EventBus`, `BleLink`, `InputRouter`, `CardController`. `setup()` calls `begin()` on each. `loop()` calls `bleLink.tick(); inputRouter.tick(now); cardController.tick(now); pace();`.

### Trade‑offs

- **No threading.** Deskhog uses FreeRTOS tasks because LVGL + WiFi + HTTP are heavy. We don't need that. Skipping it keeps the controller a synchronous tick — much simpler, easier to test.
- **Don't copy `EventQueue` verbatim.** Deskhog's is a FreeRTOS queue with mutex‑guarded callbacks. A 30‑line synchronous `std::vector<std::function>` per event type is enough here.
- **Keep `lib/` C‑style modules.** They're tested. Wrap them, don't rewrite as classes — you'd lose the tests for no gain.
- **Dirty tracking belongs on cards, not the loop.** Today `main.cpp` tracks `lastDrawnState`, `lastEyesH`, `lastPromptId` etc. as globals. Each card owning its own `isDirty()` is the single biggest simplification.
- **`Card` vs `InputHandler`.** Deskhog separates them because LVGL owns rendering and the handler is a mixin. We can collapse them — `Card` is both renderable and input‑handling.

### Risk

Step 5 (prompt as a card) is the biggest risk. Today the prompt is implicit: when `pv.visible` goes true, `loop()` short‑circuits nav. As a stack‑pushed card it's cleaner, but the underlying card (e.g. EyesCard) must re‑invalidate when the prompt pops. Plan: invalidate the new‑top card on every push/pop in `CardStack`.

---

## Part 2: Wi‑Fi capabilities

### What deskhog does

- **First boot:** no creds → device starts a SoftAP. User joins it, browser hits captive portal, submits SSID + password.
- **`ConfigManager`** persists creds in NVS (ESP32 preferences).
- **`WifiInterface`** reads creds, connects in STA mode, manages reconnect, publishes events on the bus.
- **`CaptivePortal`** runs an HTTP server in STA mode for a web UI (insights config, OTA, etc.).
- **`ProvisioningCard`** shows AP SSID + portal URL until creds exist, then shows STA IP + status once connected.

### New modules

| Module | Job |
|---|---|
| `core/ConfigStore` | Wraps `Preferences` (NVS). `getSsid/getPassword/setCreds/clear`. |
| `net/WifiManager` | Owns the Wi‑Fi state machine: `BOOT → AP_PROVISIONING → STA_CONNECTING → STA_CONNECTED ↔ STA_RECONNECT`. Emits events; exposes `getIp()`, `getMode()`, `getSsid()`. |
| `net/HttpServer` | Two roles, same `WebServer` instance: (1) captive portal endpoints in AP mode (`/`, `/save`, generic redirect), (2) JSON API in STA mode (start with `GET /status`). |
| `ui/cards/WifiCard` | New card. Three render states keyed off `WifiManager` state: AP (show SSID + `192.168.4.1`), connecting (dots), connected (show SSID + IP). |

`AppState` does **not** absorb Wi‑Fi fields. `WifiCard` reads from `WifiManager` directly so `AppState` stays focused on Claude/protocol state.

### Provisioning UX

Two viable flows:

1. **Always provision via captive portal.** No creds → AP. Has creds → STA only. To re‑provision, user holds a button to clear creds (or hits `/factory_reset` from the LAN once connected). This is what deskhog does.
2. **Button‑triggered AP.** Boot always tries STA; long‑press a button drops to AP mode regardless. Better when networks change often.

**Decision:** option (1) for parity, plus a button escape hatch to clear creds. Two sources of truth for "should I be in AP mode" is messy.

### State machine

```
            no creds                      creds saved
  BOOT ────────────────► AP_PROVISIONING ──────────────► STA_CONNECTING
                                  ▲                            │
                                  │ user hits /save            │ WL_CONNECTED
                                  │                            ▼
                                  │                      STA_CONNECTED
                                  │                            │
                                  │ creds cleared              │ disconnect
                                  └────────────────────────────┘
                                                STA_RECONNECT (backoff)
                                                       │
                                                       ▼
                                                 STA_CONNECTING
```

Tick `WifiManager` once per `loop()`. Use `WiFi.onEvent()` to flip states; don't poll `WiFi.status()` more than necessary.

### File additions

```
src/
  core/ConfigStore.{h,cpp}
  net/WifiManager.{h,cpp}
  net/HttpServer.{h,cpp}
  ui/cards/WifiCard.{h,cpp}
```

Dependencies (`platformio.ini`): `WiFi.h`, `WebServer.h`, `DNSServer.h` are bundled with the ESP32 Arduino core — no new libs needed for the basic version.

### Migration order (after refactor steps 1–4)

Each step boots and is testable on hardware before moving on.

1. **`ConfigStore`** — read/write SSID + password to NVS. Test by writing in `setup()` and reading back.
2. **`WifiManager` STA‑only path** — hardcode a test SSID/password in code, get to `STA_CONNECTED`, log IP over serial. Don't add AP yet. This validates the event hookup and reconnect.
3. **`WifiCard`** — render the three states. Add it to `CardStack`. Now you can see IP on screen.
4. **`HttpServer` STA mode** — add `GET /status` returning `{state, msg, ip, uptime}`. Curl the device from your laptop. Feature works for "known network" case.
5. **Captive portal** — add `AP_PROVISIONING` state. `WifiManager` starts SoftAP + DNS server, `HttpServer` serves a tiny HTML form on `/`, `POST /save` writes to `ConfigStore` and triggers `WifiManager::reboot()`.
6. **Clear‑creds escape hatch** — long‑press (e.g. center button 5s) calls `ConfigStore::clear()` and reboots.

Shippable after step 4 if you don't need provisioning yet (hardcode creds during dev). Steps 5–6 are polish.

### Trade‑offs

- **Power.** Wi‑Fi STA is the dominant draw. Today you have light sleep available; with Wi‑Fi up you lose much of that benefit unless you use modem sleep (`WiFi.setSleep(true)`, default on STA). USB‑powered desk buddy is fine; battery isn't.
- **Memory.** `WebServer` + `DNSServer` + WPA supplicant pushes RAM use up. The Adafruit ESP32‑S3 Reverse TFT Feather has 8MB PSRAM, so this is fine — but it confirms the synchronous EventBus is the right choice; don't add FreeRTOS queues just for Wi‑Fi.
- **Security.** Captive portal sends the password as plaintext over an open AP. Standard for hobby ESP32 projects. Lock down later with WPA2 on the AP if needed.
- **mDNS.** Optional but worth it: `MDNS.begin("claude-buddy")` lets you reach `http://claude-buddy.local` instead of memorizing the IP. ~1KB of RAM, big UX win. Add in step 4.
- **HTTP vs WebSocket.** `GET /status` is fine for v1. If you later want push from device to browser, upgrade to AsyncWebServer + WebSocket. Don't do that yet.
- **Card visibility.** Two options for `WifiCard` placement: (a) always present in the stack alongside the others; (b) only present in AP mode, auto‑pops once STA connects. (b) matches deskhog's `ProvisioningCard`. **Decision:** start with (a) — simpler, and you'll want to see the IP on demand anyway.

---

## Combined order of work

1. Refactor steps 1–4 (Display, AppState, Card/CardStack, InputRouter).
2. Wi‑Fi steps 1–4 (ConfigStore, WifiManager STA, WifiCard, HttpServer `/status`).
3. Refactor steps 5–8 (PromptCard, BleLink, EventBus, lean main.cpp).
4. Wi‑Fi steps 5–6 (captive portal, clear‑creds).

Refactor steps 5–8 can swap with Wi‑Fi steps 1–4 if you'd rather see Wi‑Fi on screen first. The hard ordering is: refactor 1–4 before any Wi‑Fi work, captive portal after `HttpServer` exists.
