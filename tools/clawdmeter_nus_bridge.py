#!/usr/bin/env python3
"""Clawdmeter → desktop-buddy NUS bridge.

Polls the Anthropic API for rate-limit usage (the same way Clawdmeter does),
then forwards it to a desktop-buddy device over Nordic UART Service (NUS) in
the desktop-buddy JSON protocol format.

Usage:
    python3 clawdmeter_nus_bridge.py [--device Claude-XXXX] [--interval 60]

Dependencies (install once):
    pip install bleak httpx

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
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

# Nordic UART Service (NUS) — what desktop-buddy's BleLink speaks
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # host → device (write)
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # device → host (notify)

DEVICE_NAME_PREFIX = "Claude-"
DEFAULT_POLL_INTERVAL = 60  # seconds
TICK = 5                    # inner wait granularity
SCAN_TIMEOUT = 10.0

KEYCHAIN_SERVICE = "Claude Code-credentials"
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"
SAVED_ADDR_FILE = Path.home() / ".config" / "claude-buddy-bridge" / "ble-address"

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

# Conservative chunk size matching the default ATT MTU (23 bytes, 20 usable).
# BleLink on the device accumulates bytes into a line buffer, so chunking is fine.
NUS_CHUNK = 20


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# ── token reading ────────────────────────────────────────────────────────────

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


# ── BLE address cache ────────────────────────────────────────────────────────

_MAC_RE = re.compile(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")
_CB_RE  = re.compile(r"[0-9A-Fa-f]{8}-(?:[0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}$")


def load_cached_address() -> str | None:
    if not SAVED_ADDR_FILE.exists():
        return None
    addr = SAVED_ADDR_FILE.read_text().strip()
    if _MAC_RE.match(addr) or _CB_RE.match(addr):
        return addr
    log("Cached address malformed, discarding")
    SAVED_ADDR_FILE.unlink(missing_ok=True)
    return None


def save_address(addr: str) -> None:
    SAVED_ADDR_FILE.parent.mkdir(parents=True, exist_ok=True)
    SAVED_ADDR_FILE.write_text(addr)


# ── device discovery ─────────────────────────────────────────────────────────

async def scan_for_device(exact_name: str | None) -> str | None:
    target = exact_name or DEVICE_NAME_PREFIX
    log(f"Scanning for '{target}{'*' if not exact_name else ''}' ({SCAN_TIMEOUT}s)...")
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT)
    for d in devices:
        if not d.name:
            continue
        match = (d.name == exact_name) if exact_name else d.name.startswith(DEVICE_NAME_PREFIX)
        if match:
            log(f"Found: {d.name} ({d.address})")
            return d.address
    return None


# ── API polling ──────────────────────────────────────────────────────────────

async def poll_usage(token: str) -> dict | None:
    """Hit the Anthropic API and return a desktop-buddy usage payload, or None on failure."""
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

    # Prefer raw token counts (per-request headers) — maps directly to the
    # firmware's usage struct fields.
    limit     = hdr_int("anthropic-ratelimit-tokens-limit")
    remaining = hdr_int("anthropic-ratelimit-tokens-remaining")
    used      = hdr_int("anthropic-ratelimit-tokens-used")

    if limit and remaining is not None:
        if used is None:
            used = limit - remaining
        log(f"Token usage  used={used} remaining={remaining} limit={limit}")
    else:
        # Fall back to the unified-5h utilization ratio that Clawdmeter uses.
        # Express as out of 100 so the firmware renders the bar as a percentage.
        util = hdr_float("anthropic-ratelimit-unified-5h-utilization")
        if util is None:
            log("No usable rate-limit headers in response")
            return None
        used      = round(util * 100)
        limit     = 100
        remaining = limit - used
        log(f"Unified-5h utilization  used={used}% limit={limit}")

    return {"usage": {"used": used, "remaining": remaining, "limit": limit}}


# ── NUS write ────────────────────────────────────────────────────────────────

async def write_payload(client: BleakClient, payload: dict) -> bool:
    """Serialise payload to a newline-terminated JSON line and send over NUS."""
    line = json.dumps(payload, separators=(",", ":")) + "\n"
    data = line.encode()
    try:
        for i in range(0, len(data), NUS_CHUNK):
            await client.write_gatt_char(NUS_RX_CHAR_UUID, data[i:i + NUS_CHUNK],
                                         response=False)
        return True
    except BleakError as e:
        log(f"BLE write failed: {e}")
        return False


# ── connection loop ──────────────────────────────────────────────────────────

async def connect_and_run(address: str, poll_interval: int,
                          stop_event: asyncio.Event) -> bool:
    log(f"Connecting to {address}...")
    client = BleakClient(address)
    try:
        await client.connect()
    except (BleakError, asyncio.TimeoutError) as e:
        log(f"Connection failed: {e}")
        return False
    if not client.is_connected:
        log("Connection failed (not connected after attempt)")
        return False

    log("Connected")
    last_poll   = 0.0
    ever_ok     = False

    try:
        while client.is_connected and not stop_event.is_set():
            if time.time() - last_poll >= poll_interval:
                token = read_token()
                if not token:
                    log("No token; skipping poll")
                else:
                    payload = await poll_usage(token)
                    if payload is not None:
                        log(f"Sending: {json.dumps(payload, separators=(',', ':'))}")
                        if await write_payload(client, payload):
                            last_poll = time.time()
                            ever_ok   = True
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=TICK)
            except asyncio.TimeoutError:
                pass
    finally:
        try:
            await client.disconnect()
        except BleakError:
            pass

    log("Device disconnected" if not stop_event.is_set() else "Stopping")
    return ever_ok


# ── main ─────────────────────────────────────────────────────────────────────

async def run(exact_name: str | None, poll_interval: int) -> None:
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

    label = exact_name or f"{DEVICE_NAME_PREFIX}*"
    log(f"=== desktop-buddy Clawdmeter bridge  device={label}  interval={poll_interval}s ===")

    backoff = 1
    while not stop_event.is_set():
        address = load_cached_address()
        if not address:
            address = await scan_for_device(exact_name)
            if address:
                save_address(address)
            else:
                log(f"Device not found, retrying in {backoff}s...")
                try:
                    await asyncio.wait_for(stop_event.wait(), timeout=backoff)
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 2, 60)
                continue

        ok = await connect_and_run(address, poll_interval, stop_event)
        if not ok:
            log("Invalidating cached address")
            SAVED_ADDR_FILE.unlink(missing_ok=True)
        backoff = min(backoff * 2, 60) if not ok else 1
        if not stop_event.is_set():
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=backoff)
            except asyncio.TimeoutError:
                pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Bridge Clawdmeter-style API polling to a desktop-buddy device over NUS"
    )
    parser.add_argument(
        "--device", metavar="NAME",
        help="exact BLE device name to connect to (default: any Claude-XXXX)",
    )
    parser.add_argument(
        "--interval", metavar="SECS", type=int, default=DEFAULT_POLL_INTERVAL,
        help=f"seconds between API polls (default: {DEFAULT_POLL_INTERVAL})",
    )
    args = parser.parse_args()
    try:
        asyncio.run(run(args.device, args.interval))
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
