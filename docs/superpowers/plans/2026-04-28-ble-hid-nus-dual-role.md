# BLE dual role: NUS buddy + HID keyboard — implementation plan

> **Context:** The device already exposes Nordic UART Service (NUS) for JSON snapshots and (per `docs/superpowers/specs/2026-04-27-permission-buttons-design.md`) may send permission decisions back over TX notify. This plan adds an **optional BLE HID keyboard** on the **same** peripheral so the user can inject keystrokes when protocol-based approval is unavailable — without a separate firmware or reboot, subject to stack and host constraints.

**Goal:** One paired BLE device, one connection from the host when possible: **custom protocol for buddy state + permissions**, plus **HID reports** for deliberate fallback typing.

**Related docs:** `src/ble_bridge.*`, `docs/superpowers/specs/2026-04-27-permission-buttons-design.md`, `docs/superpowers/plans/2026-04-27-permission-buttons.md`.

---

## Non-goals

- Replacing protocol permission responses with HID as the primary path.
- Auto-approving CLI prompts without user action.
- Supporting every OS keyboard layout with typed letters in v1 (prefer Enter / arrows / Tab).
- Guaranteed single-connection multiplexing on every host stack (document fallbacks).

---

## Architecture (target)

### GATT layout

- **Existing:** NUS service (`6e400001-…`) with RX write + TX notify.
- **New:** **HID over GATT** — Boot Keyboard Input Report (or standard keyboard report), HID Report Map, HID Information, optional Battery (skip v1).
- **Single** `BLEServer` with **multiple** services; one advertising payload listing both (or primary service + appearance).

### Firmware modules (suggested split)

| Unit | Responsibility |
|------|------------------|
| `ble_bridge` (extend) or `ble_hid` (new) | Register HID service, map logical actions → HID reports, rate-limit / arm gate |
| `main` / card UI | Menu or gesture to **arm HID** for one shot; show clear on-screen state |
| Host (Claude Desktop) | Unchanged for permissions: still consumes NUS JSON |

### HID safety gate (required)

- **Default:** HID injection **disarmed**.
- **Arm:** e.g. user navigates to **“HID: Ready”** in a settings card and presses center to arm **one shot**, or **hold center 1s** to arm next action only.
- **Disarm:** automatically after one injected macro, or after timeout (e.g. 10s), or on disconnect.
- **Rate limit:** minimum interval between HID bursts (e.g. 2s).

---

## UX flow — expected behaviors

### One-time pairing

1. User powers device; firmware advertises name `Claude-XXYY` (or chosen prefix).
2. User adds device in **OS Bluetooth settings**.
3. **Expected:** OS shows one device; pairing succeeds; OS may install **HID keyboard** driver in background.
4. User enables **Hardware Buddy** in Claude Desktop; app connects via BLE (NUS).
5. **Expected:** Snapshots and footer `LIVE` behave as today; HID does not send keys until armed.

### Normal permission (protocol path)

1. Desktop sends snapshot with `prompt`.
2. Device shows permission UI; user navigates **Approve / Deny / Dismiss** with buttons.
3. **Expected:** Device sends `{"cmd":"permission",…}` on NUS TX notify; **no HID keys** are sent.
4. Desktop clears prompt; device returns to status/eyes UI.

### Fallback: HID macro (armed)

1. User focuses target window (e.g. terminal running `claude`).
2. User **arms** HID on device (see gate above).
3. User triggers **Approve macro** or **Deny macro** (dedicated button combo or menu selection + confirm).
4. **Expected:** Device sends a **fixed** sequence of HID reports (e.g. Enter, or ArrowDown + Enter — **configurable in firmware constants** after you validate against your CLI build).
5. **Expected:** After one macro, HID **disarms**; serial log line for debugging.

### Error and edge cases

| Condition | Expected behavior |
|-----------|-------------------|
| HID fired while wrong app focused | **User responsibility** in v1; optional future: only allow HID when `prompt.present` and live (tighter coupling). |
| BLE disconnect | NUS + HID both drop; permission UI hides per existing spec; HID disarmed. |
| Notify queue full (NUS TX) | Existing plan: drop, desktop re-prompts. |
| HID report send fails | Log, disarm, show brief error flash on TFT. |
| Stack rejects dual services | Document fallback: build flag `ENABLE_BLE_HID=0` or split advertising (last resort). |

---

## Implementation phases

### Phase 0 — Prerequisites

- [ ] Land or verify **permission-buttons** pipeline: `ble_write_line`, `prompt_ui`, protocol `prompt` parsing (per `2026-04-27-permission-buttons` plan).
- [ ] Confirm desktop receives permission JSON end-to-end.

### Phase 1 — HID service skeleton (no user trigger)

- [ ] Add HID service + report map + input report characteristic per ESP32 Arduino BLE HID examples (or NimBLE if you migrate — pick one stack and stay consistent with current `BLEDevice`).
- [ ] Advertise **Appearance** or **HID Generic** as appropriate; ensure device still pairs.
- [ ] On boot, send **no** key reports; verify OS does not see phantom keys.
- [ ] **Acceptance:** Pair on macOS; System Settings shows keyboard; NUS still connects from Claude Desktop; existing status UI unchanged.

### Phase 2 — Arm gate + one macro

- [ ] Implement **armed / disarmed** state machine (timeout + single-shot).
- [ ] Implement **one** macro: e.g. “press Enter” or “Down + Enter” as constants in `hid_macros.h`.
- [ ] Wire to a **explicit** UI path (new settings card or two-button chord) — no accidental fire.
- [ ] **Acceptance:** With TextEdit focused, armed macro produces visible effect once; disarms after; serial logs state transitions.

### Phase 3 — Productize UX

- [ ] On-screen label: `HID: OFF` / `HID: 1 SHOT` / `SENT`.
- [ ] Optional: map **Approve** on permission screen to protocol only; map **long-press** to HID only if you want both (dangerous — default should stay protocol-only on that screen).
- [ ] Document in README: pairing, arming, focus warning, macro meaning.

### Phase 4 — Hardening

- [ ] Rate limiting and debounce for HID.
- [ ] Consider gating HID: `prompt.present && live` only (stricter safety, weaker “generic terminal” use).
- [ ] If dual-service instability observed: feature flag to disable HID at compile time.

---

## Testing checklist (manual)

1. **Regression:** NUS snapshots, heartbeat, `LIVE`/`OFFLN`, permission JSON approve/deny/dismiss (no HID).
2. **HID idle:** No keys for 5 minutes with terminal focused.
3. **HID armed:** Macro fires once; disarms; no repeat until re-armed.
4. **Disconnect:** HID disarmed; reconnect; NUS resumes.
5. **Stress:** Rapid arm/disarm; confirm no stuck “key down” (always send key-up reports).

---

## Open decisions (resolve before Phase 2)

- Exact **macro bytes** for your CLI (record once from real prompt UI).
- **Arm gesture** vs **settings card** (recommend card + confirm for fewer accidents).
- Whether HID is allowed **only** when a permission prompt is visible on device (safer) vs always when armed (more flexible).

---

## File touch list (expected)

- `src/ble_bridge.cpp` / `.h` — extend or split `ble_hid.cpp` / `.h`
- `src/main.cpp` — UI for arm state, optional new card
- `platformio.ini` — only if new lib or compile flags
- `README.md` — user-facing HID instructions (when Phase 3 done)

---

## Success criteria (summary)

- Single device pairing supports **both** Claude Desktop NUS traffic and OS HID when enabled.
- Permission handling **defaults** to **protocol**, not keystrokes.
- HID injection requires **explicit arm** and is **bounded** (single shot + timeout + rate limit).
- Documented expected behaviors match manual test checklist.
