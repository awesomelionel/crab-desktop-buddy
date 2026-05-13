#!/usr/bin/env bash
# Curl smoke-test for the Configure-via-Web surface. Set DEVICE_HOST to
# the device's IP (or hostname) before running. Each step prints a
# header and the response.
#
# Usage:
#   DEVICE_HOST=192.168.1.42 ./tools/web-smoke.sh
#
# Verifies happy paths only — server-side validation and the /api/actions/*
# reboot endpoints are deliberately skipped (they restart the device).

set -euo pipefail

HOST="${DEVICE_HOST:-claude-buddy.local}"
BASE="http://${HOST}"

step() { printf '\n\033[1;33m== %s ==\033[0m\n' "$*"; }

step "GET /api/firmware-version"
RESP=$(curl -fsS "$BASE/api/firmware-version")
echo "$RESP" | jq .
echo "$RESP" | grep -q '"version"' || { echo "FAIL: no version key"; exit 1; }

step "GET /api/status"
curl -fsS "$BASE/api/status" | jq .

step "GET /api/settings"
curl -fsS "$BASE/api/settings" | jq .

step "POST /api/scan (kick off a Wi-Fi scan)"
curl -fsS -X POST "$BASE/api/scan" | jq .

step "GET /api/networks (after a brief wait)"
sleep 3
curl -fsS "$BASE/api/networks" | jq .

step "POST /api/settings/device (rename to 'smoketest', live=30, sleep=0, dim=60)"
curl -fsS -X POST "$BASE/api/settings/device" \
  --data-urlencode "name=smoketest" \
  --data-urlencode "live_timeout_s=30" \
  --data-urlencode "sleep_timeout_s=0" \
  --data-urlencode "dim_timeout_s=60" \
  --data-urlencode "dim_level_pct=25" \
  --data-urlencode "full_level_pct=100" | jq .

step "POST /api/settings/cards (default order: status, eyes, wifi)"
curl -fsS -X POST "$BASE/api/settings/cards" \
  -d "enabled_mask=7" \
  -d "order=0" -d "order=1" -d "order=2" \
  -d "boot_card=0" | jq .

step "GET /api/settings (verify changes stuck)"
curl -fsS "$BASE/api/settings" | jq .

step "GET /api/update-status (expect idle)"
RESP=$(curl -fsS "$BASE/api/update-status")
echo "$RESP" | jq .
echo "$RESP" | grep -q '"state":"idle"' || { echo "FAIL: state not idle"; exit 1; }

step "POST /api/check-for-updates (expect up_to_date, update_available, or failed)"
RESP=$(curl -fsS -X POST "$BASE/api/check-for-updates")
echo "$RESP" | jq .
echo "$RESP" | grep -qE '"state":"(up_to_date|update_available|failed)"' \
    || { echo "FAIL: unexpected state"; exit 1; }

step "POST /api/factory-reset (arm only; do NOT hold center)"
RESP=$(curl -fsS -X POST "$BASE/api/factory-reset")
echo "$RESP" | jq .
echo "$RESP" | grep -q '"state":"awaiting_hold"' \
    || { echo "FAIL: factory-reset did not arm"; exit 1; }
echo "  waiting 32s for arm window to lapse..."
sleep 32

step "Done. Skipped: /api/actions/{reboot,reset-settings,forget-wifi} —"
step "those reboot the device. Run them manually to verify."
