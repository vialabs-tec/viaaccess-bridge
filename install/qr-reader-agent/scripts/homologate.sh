#!/usr/bin/env bash
# Homologation checks for a running qr-reader-agent (Mac dev or Pi).
set -euo pipefail

AGENT_URL="${AGENT_URL:-http://127.0.0.1:3710}"
IDENTITY_URL="${IDENTITY_URL:-http://localhost:3100}"

echo "== Agent health =="
health="$(curl -sf "$AGENT_URL/health")"
echo "$health" | python3 -m json.tool 2>/dev/null || echo "$health"

configured="$(echo "$health" | python3 -c "import json,sys; print(json.load(sys.stdin).get('configured', False))" 2>/dev/null || echo "false")"
if [[ "$configured" != "True" && "$configured" != "true" ]]; then
  echo "WARN: agent not configured — open $AGENT_URL/setup and provision with a clm_ token"
  exit 0
fi

echo ""
echo "== Identity device-config (optional) =="
if [[ -n "${DEVICE_KEY:-}" ]]; then
  curl -sf -H "Authorization: Bearer $DEVICE_KEY" "$IDENTITY_URL/api/bridge/device-config" \
    | python3 -m json.tool 2>/dev/null || true
else
  echo "Skip (set DEVICE_KEY=idb_… to verify Identity device-config)"
fi

echo ""
echo "== Scan test (optional) =="
if [[ -n "${QR_URL:-}" ]]; then
  curl -sf -X POST "$AGENT_URL/scan" \
    -H "Content-Type: application/json" \
    -d "{\"qrUrl\":\"$QR_URL\"}" \
    | python3 -m json.tool 2>/dev/null || true
else
  echo "Skip (set QR_URL to a dynamic QR from the member PWA)"
fi

echo ""
echo "Homologation checks done."
