# Clawdmeter HTTP Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the BLE-based Clawdmeter adapter with an HTTP-based bridge that posts Anthropic API usage data to the device's existing web server, avoiding contention with Claude Desktop's BLE connection.

**Architecture:** The Python bridge polls the Anthropic API for rate-limit headers and POSTs `{"usage": {...}}` JSON to a new `POST /api/usage-update` endpoint on the device. The firmware parses the body through the existing `protocol_parse_line` function and fires `SnapshotReceived` so cards redraw. Claude Desktop keeps exclusive ownership of the BLE/NUS link.

**Tech Stack:** C++ / Arduino (firmware), Python 3.11+ with `httpx` (bridge), Unity (firmware C tests already cover `protocol_parse_line`), pytest (Python tests)

---

## File Map

| File | Change |
|------|--------|
| `src/net/HttpServer.h` | Add `EventBus&` param; change `const AppState&` → `AppState&` |
| `src/net/HttpServer.cpp` | Update constructor; add `#include`s; add `/api/usage-update` route |
| `src/main.cpp` | Pass `eventBus` to `HttpServer` constructor |
| `tools/clawdmeter_nus_bridge.py` | Full rewrite — drop BLE, add HTTP POST to device |
| `tools/test_clawdmeter_bridge.py` | New — pytest unit tests for the Python bridge |
| `README.md` | Add `/api/usage-update` to Web API table; document the bridge tool |

---

### Task 1: Open AppState write access and wire EventBus into HttpServer

**Files:**
- Modify: `src/net/HttpServer.h`
- Modify: `src/net/HttpServer.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Update HttpServer.h**

Replace the class declaration's constructor signature and member fields:

```cpp
// Forward-declare EventBus (add alongside the other forward declarations at top of file)
class EventBus;

// Updated constructor (was: const AppState& app)
HttpServer(WifiManager& wifi, AppState& app, EventBus& bus,
           ConfigStore& config, Settings& settings,
           FactoryResetCoordinator& fr);

// Updated private members (was: const AppState& app_)
AppState&  app_;
EventBus&  bus_;
```

- [ ] **Step 2: Update HttpServer.cpp constructor definition**

Add `#include "../core/EventBus.h"` alongside the existing includes at the top of `HttpServer.cpp`.

Find the constructor definition and update its signature and initialiser list:

```cpp
HttpServer::HttpServer(WifiManager& wifi, AppState& app, EventBus& bus,
                       ConfigStore& config, Settings& settings,
                       FactoryResetCoordinator& fr)
    : wifi_(wifi), app_(app), bus_(bus), config_(config),
      settings_(settings), fr_(fr),
      server_(nullptr), dns_(nullptr), role_(Role::NONE), boot_ms_(0) {}
```

- [ ] **Step 3: Update main.cpp instantiation**

Find the line:
```cpp
static HttpServer   httpServer{wifiManager, appState, configStore, settingsStore,
                               factoryReset};
```
Change it to:
```cpp
static HttpServer   httpServer{wifiManager, appState, eventBus, configStore,
                               settingsStore, factoryReset};
```

- [ ] **Step 4: Build to verify no compile errors**

```bash
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build succeeds. Fix any type errors before continuing.

- [ ] **Step 5: Commit**

```bash
git add src/net/HttpServer.h src/net/HttpServer.cpp src/main.cpp
git commit -m "refactor(http): open AppState write access and wire EventBus into HttpServer"
```

---

### Task 2: Add POST /api/usage-update firmware route

**Files:**
- Modify: `src/net/HttpServer.cpp`

- [ ] **Step 1: Add protocol.h include**

At the top of `HttpServer.cpp`, add:

```cpp
#include "protocol.h"
```

- [ ] **Step 2: Add the route inside registerStaHandlers()**

Add this block immediately before the `// ---- /api/settings/network` comment (after the bus-stops route):

```cpp
    // ---- /api/usage-update
    // Accepts {"usage":{"used":N,"remaining":N,"limit":N}} from the
    // Clawdmeter HTTP bridge. Parsed via protocol_parse_line so partial
    // fields accumulate without overwriting live BLE state.
    server_->on("/api/usage-update", HTTP_POST, [this]() {
        String body = server_->arg("plain");
        if (body.isEmpty()) {
            sendJsonError(server_, 400, "empty body");
            return;
        }
        if (!protocol_parse_line(body.c_str(), &app_.mutableStatus())) {
            sendJsonError(server_, 400, "invalid JSON");
            return;
        }
        bus_.publish(EventKind::SnapshotReceived);
        sendJsonOk(server_);
    });
```

- [ ] **Step 3: Build to verify no compile errors**

```bash
pio run -e adafruit_feather_esp32s3_reversetft
```

Expected: build succeeds with no new warnings.

- [ ] **Step 4: Smoke-test against a running device (if available)**

```bash
# Replace <device-ip> with the IP shown on the device's Wi-Fi card
curl -s -X POST http://<device-ip>/api/usage-update \
  -H "Content-Type: application/json" \
  -d '{"usage":{"used":4200,"remaining":5800,"limit":10000}}'
```

Expected response: `{"ok":true}`

- [ ] **Step 5: Commit**

```bash
git add src/net/HttpServer.cpp
git commit -m "feat(http): add POST /api/usage-update endpoint for Clawdmeter bridge"
```

---

### Task 3: Write Python bridge tests (TDD — write before the implementation)

**Files:**
- Create: `tools/test_clawdmeter_bridge.py`

- [ ] **Step 1: Create the test file**

```python
"""Tests for tools/clawdmeter_nus_bridge.py (HTTP version)."""
import importlib.util, sys, types, unittest
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

# ── load module without executing __main__ ────────────────────────────────────
spec = importlib.util.spec_from_file_location(
    "bridge", Path(__file__).parent / "clawdmeter_nus_bridge.py"
)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

# ── _extract_access_token ─────────────────────────────────────────────────────
class TestExtractToken(unittest.TestCase):
    def test_direct_json(self):
        blob = '{"accessToken":"sk-ant-abc123"}'
        self.assertEqual(mod._extract_access_token(blob), "sk-ant-abc123")

    def test_nested_json(self):
        blob = '{"claudeAiOauth":{"accessToken":"sk-ant-nested"}}'
        self.assertEqual(mod._extract_access_token(blob), "sk-ant-nested")

    def test_raw_token(self):
        self.assertEqual(mod._extract_access_token("sk-ant-rawtoken123456"), "sk-ant-rawtoken123456")

    def test_empty_returns_none(self):
        self.assertIsNone(mod._extract_access_token(""))

    def test_garbage_returns_none(self):
        self.assertIsNone(mod._extract_access_token("not json and not a token!"))

# ── poll_usage — raw token headers ───────────────────────────────────────────
class TestPollUsageRawHeaders(unittest.IsolatedAsyncioTestCase):
    async def test_uses_raw_token_headers_when_present(self):
        resp = MagicMock()
        resp.status_code = 200
        resp.headers = {
            "anthropic-ratelimit-tokens-limit":     "10000",
            "anthropic-ratelimit-tokens-remaining":  "6000",
            "anthropic-ratelimit-tokens-used":       "4000",
        }
        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            result = await mod.poll_usage("fake-token")

        self.assertEqual(result, {"usage": {"used": 4000, "remaining": 6000, "limit": 10000}})

# ── poll_usage — unified fallback ─────────────────────────────────────────────
class TestPollUsageUnifiedFallback(unittest.IsolatedAsyncioTestCase):
    async def test_falls_back_to_unified_utilization(self):
        resp = MagicMock()
        resp.status_code = 200
        resp.headers = {
            "anthropic-ratelimit-unified-5h-utilization": "0.45",
        }
        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            result = await mod.poll_usage("fake-token")

        self.assertEqual(result, {"usage": {"used": 45, "remaining": 55, "limit": 100}})

    async def test_returns_none_when_no_usable_headers(self):
        resp = MagicMock()
        resp.status_code = 200
        resp.headers = {}
        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            result = await mod.poll_usage("fake-token")

        self.assertIsNone(result)

    async def test_returns_none_on_api_error(self):
        resp = MagicMock()
        resp.status_code = 401
        resp.text = "Unauthorized"
        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            result = await mod.poll_usage("fake-token")

        self.assertIsNone(result)

# ── post_usage ────────────────────────────────────────────────────────────────
class TestPostUsage(unittest.IsolatedAsyncioTestCase):
    async def test_posts_json_to_device(self):
        resp = MagicMock()
        resp.status_code = 200
        resp.text = '{"ok":true}'

        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            payload = {"usage": {"used": 100, "remaining": 900, "limit": 1000}}
            ok = await mod.post_usage("192.168.1.42", payload)

        self.assertTrue(ok)
        mock_client.post.assert_called_once_with(
            "http://192.168.1.42/api/usage-update",
            json=payload,
            timeout=10.0,
        )

    async def test_returns_false_on_non_200(self):
        resp = MagicMock()
        resp.status_code = 503
        resp.text = "offline"

        with patch("httpx.AsyncClient") as mock_client_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__  = AsyncMock(return_value=False)
            mock_client.post       = AsyncMock(return_value=resp)
            mock_client_cls.return_value = mock_client

            ok = await mod.post_usage("192.168.1.42", {"usage": {}})

        self.assertFalse(ok)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests — expect ImportError or AttributeError (functions don't exist yet)**

```bash
cd /Users/lionel/Documents/code/desktop-buddy
python3 -m pytest tools/test_clawdmeter_bridge.py -v 2>&1 | head -40
```

Expected: failures like `AttributeError: module 'bridge' has no attribute 'poll_usage'` or `'post_usage'`. That's the red state.

---

### Task 4: Rewrite clawdmeter_nus_bridge.py as HTTP bridge

**Files:**
- Modify: `tools/clawdmeter_nus_bridge.py`

- [ ] **Step 1: Replace the file content**

```python
#!/usr/bin/env python3
"""Clawdmeter → desktop-buddy HTTP bridge.

Polls the Anthropic API for rate-limit usage and POSTs it to the
desktop-buddy device's /api/usage-update endpoint over HTTP (Wi-Fi).
This avoids contention with Claude Desktop's BLE connection.

Usage:
    python3 clawdmeter_nus_bridge.py --host <device-ip> [--interval 60]

Dependencies (install once):
    pip install httpx

On macOS the Claude OAuth token is read from the Keychain
(service "Claude Code-credentials"). On Linux it is read from
~/.claude/.credentials.json — the same locations Claude Code uses.
"""

import argparse
import asyncio
import getpass
import json
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

import httpx

DEFAULT_POLL_INTERVAL = 60  # seconds
TICK = 5

KEYCHAIN_SERVICE = "Claude Code-credentials"
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"

API_URL = "https://api.anthropic.com/v1/messages"
API_HEADERS = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta": "oauth-2025-04-20",
    "Content-Type": "application/json",
    "User-Agent": "claude-code/2.1.5",
}
# One token of Haiku — essentially free; we only need the response headers.
API_BODY = {
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 1,
    "messages": [{"role": "user", "content": "hi"}],
}


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ── token reading ─────────────────────────────────────────────────────────────

def _extract_access_token(blob: str) -> str | None:
    """Pull the accessToken from a credentials blob (direct, nested, regex, or raw)."""
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        for v in data.values():
            if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
                return v["accessToken"]
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    if m:
        return m.group(1)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return blob
    return None


def _read_token_keychain() -> str | None:
    try:
        out = subprocess.run(
            ["security", "find-generic-password", "-s", KEYCHAIN_SERVICE,
             "-a", getpass.getuser(), "-w"],
            check=True, capture_output=True, text=True, timeout=10,
        )
        return _extract_access_token(out.stdout)
    except (subprocess.CalledProcessError, FileNotFoundError,
            subprocess.TimeoutExpired) as e:
        log(f"Keychain read failed: {e}")
        return None


def _read_token_file() -> str | None:
    try:
        return _extract_access_token(CREDENTIALS_PATH.read_text())
    except OSError as e:
        log(f"Error reading credentials: {e}")
        return None


def read_token() -> str | None:
    return _read_token_keychain() if sys.platform == "darwin" else _read_token_file()


# ── API polling ───────────────────────────────────────────────────────────────

async def poll_usage(token: str) -> dict | None:
    """Hit the Anthropic API and return a desktop-buddy usage payload, or None."""
    hdrs = {**API_HEADERS, "Authorization": f"Bearer {token}"}
    try:
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.post(API_URL, headers=hdrs, json=API_BODY)
    except httpx.HTTPError as e:
        log(f"API call failed: {e}")
        return None
    if resp.status_code >= 400:
        log(f"API HTTP {resp.status_code}: {resp.text[:200]}")
        return None

    h = resp.headers

    def hdr_int(name: str) -> int | None:
        v = h.get(name)
        try:
            return int(v) if v is not None else None
        except ValueError:
            return None

    def hdr_float(name: str) -> float | None:
        v = h.get(name)
        try:
            return float(v) if v is not None else None
        except ValueError:
            return None

    # Prefer raw token counts — maps directly to the firmware's usage struct.
    limit     = hdr_int("anthropic-ratelimit-tokens-limit")
    remaining = hdr_int("anthropic-ratelimit-tokens-remaining")
    used      = hdr_int("anthropic-ratelimit-tokens-used")

    if limit is not None and remaining is not None:
        if used is None:
            used = limit - remaining
        log(f"Token usage  used={used} remaining={remaining} limit={limit}")
        return {"usage": {"used": used, "remaining": remaining, "limit": limit}}

    # Fall back to unified-5h utilization ratio (expressed as out of 100).
    util = hdr_float("anthropic-ratelimit-unified-5h-utilization")
    if util is None:
        log("No usable rate-limit headers in response")
        return None
    used      = round(util * 100)
    limit     = 100
    remaining = limit - used
    log(f"Unified-5h utilization  used={used}% limit={limit}")
    return {"usage": {"used": used, "remaining": remaining, "limit": limit}}


# ── HTTP post to device ───────────────────────────────────────────────────────

async def post_usage(host: str, payload: dict) -> bool:
    """POST payload to the device's /api/usage-update endpoint."""
    url = f"http://{host}/api/usage-update"
    try:
        async with httpx.AsyncClient(timeout=10.0) as http:
            resp = await http.post(url, json=payload)
        if resp.status_code != 200:
            log(f"Device returned HTTP {resp.status_code}: {resp.text[:100]}")
            return False
        return True
    except httpx.HTTPError as e:
        log(f"Device POST failed: {e}")
        return False


# ── poll loop ─────────────────────────────────────────────────────────────────

async def run(host: str, poll_interval: int) -> None:
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()

    def _stop(*_: object) -> None:
        log("Stopping")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            signal.signal(sig, _stop)

    log(f"=== desktop-buddy Clawdmeter bridge  host={host}  interval={poll_interval}s ===")

    last_poll = 0.0
    while not stop_event.is_set():
        if time.time() - last_poll >= poll_interval:
            token = read_token()
            if not token:
                log("No token; skipping poll")
            else:
                payload = await poll_usage(token)
                if payload is not None:
                    log(f"Posting: {json.dumps(payload, separators=(',', ':'))}")
                    if await post_usage(host, payload):
                        last_poll = time.time()
        try:
            await asyncio.wait_for(stop_event.wait(), timeout=TICK)
        except asyncio.TimeoutError:
            pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Bridge Clawdmeter-style API polling to a desktop-buddy device over HTTP"
    )
    parser.add_argument("--host", required=True, metavar="IP_OR_HOSTNAME",
                        help="device IP or hostname (shown on the Wi-Fi card)")
    parser.add_argument("--interval", metavar="SECS", type=int,
                        default=DEFAULT_POLL_INTERVAL,
                        help=f"seconds between polls (default: {DEFAULT_POLL_INTERVAL})")
    args = parser.parse_args()
    try:
        asyncio.run(run(args.host, args.interval))
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run tests — expect all to pass**

```bash
python3 -m pytest tools/test_clawdmeter_bridge.py -v
```

Expected output (all green):
```
PASSED tools/test_clawdmeter_bridge.py::TestExtractToken::test_direct_json
PASSED tools/test_clawdmeter_bridge.py::TestExtractToken::test_nested_json
PASSED tools/test_clawdmeter_bridge.py::TestExtractToken::test_raw_token
PASSED tools/test_clawdmeter_bridge.py::TestExtractToken::test_empty_returns_none
PASSED tools/test_clawdmeter_bridge.py::TestExtractToken::test_garbage_returns_none
PASSED tools/test_clawdmeter_bridge.py::TestPollUsageRawHeaders::test_uses_raw_token_headers_when_present
PASSED tools/test_clawdmeter_bridge.py::TestPollUsageUnifiedFallback::test_falls_back_to_unified_utilization
PASSED tools/test_clawdmeter_bridge.py::TestPollUsageUnifiedFallback::test_returns_none_when_no_usable_headers
PASSED tools/test_clawdmeter_bridge.py::TestPollUsageUnifiedFallback::test_returns_none_on_api_error
PASSED tools/test_clawdmeter_bridge.py::TestPostUsage::test_posts_json_to_device
PASSED tools/test_clawdmeter_bridge.py::TestPostUsage::test_returns_false_on_non_200
```

If any test fails, fix `clawdmeter_nus_bridge.py` until all pass before continuing.

- [ ] **Step 3: Commit**

```bash
git add tools/clawdmeter_nus_bridge.py tools/test_clawdmeter_bridge.py
git commit -m "feat(tools): rewrite Clawdmeter bridge to use HTTP instead of BLE"
```

---

### Task 5: Update README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add /api/usage-update to the Web API table**

Find the table row:
```markdown
| `/api/settings/bus-stops` | POST | Save bus stop codes and labels |
```

Add after it:
```markdown
| `/api/usage-update` | POST | Push `{"usage":{"used":N,"remaining":N,"limit":N}}` from the Clawdmeter bridge |
```

- [ ] **Step 2: Add a Clawdmeter bridge section**

After the `## Run Tests` section, add:

```markdown
## Clawdmeter Bridge

The `tools/clawdmeter_nus_bridge.py` script polls your Anthropic API rate-limit
headers and forwards them to the device over HTTP, keeping the BLE link free for
Claude Desktop.

```sh
pip install httpx
python3 tools/clawdmeter_nus_bridge.py --host <device-ip>
```

The device IP is shown on the Wi-Fi card. The script reads your Claude OAuth
token from the macOS Keychain (`Claude Code-credentials`) or from
`~/.claude/.credentials.json` on Linux — no configuration needed.

Optional flag: `--interval <seconds>` (default 60).
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document /api/usage-update endpoint and Clawdmeter HTTP bridge"
```

---

## Unresolved Questions

- The device's Wi-Fi IP can change on DHCP. Consider whether a `--host` flag is sufficient or if mDNS discovery (`claude-buddy-XXXX.local`) should be added in a follow-up.
- The firmware currently has no authentication on any HTTP endpoint. The usage-update route inherits this — acceptable for a local-network device, but worth noting.
