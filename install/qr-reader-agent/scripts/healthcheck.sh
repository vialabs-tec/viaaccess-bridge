#!/usr/bin/env bash
# Boot / oneshot health probe for viaaccess-qr-agent.
# Succeeds when GET /health returns JSON (including setup mode where ok=false).
set -euo pipefail

AGENT_URL="${AGENT_URL:-http://127.0.0.1:3710}"
RETRIES="${HEALTHCHECK_RETRIES:-30}"
SLEEP_SEC="${HEALTHCHECK_SLEEP_SEC:-2}"

for ((i = 1; i <= RETRIES; i++)); do
  if body="$(curl -sf --max-time 3 "${AGENT_URL}/health" 2>/dev/null)"; then
    if echo "$body" | python3 -c 'import json,sys; d=json.load(sys.stdin); assert "configured" in d and "operationMode" in d' 2>/dev/null; then
      mode="$(echo "$body" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("operationMode",""))' 2>/dev/null || true)"
      echo "healthcheck ok operationMode=${mode}"
      echo "$body" | python3 -m json.tool 2>/dev/null || echo "$body"
      exit 0
    fi
  fi
  echo "healthcheck attempt ${i}/${RETRIES}: waiting for ${AGENT_URL}/health"
  sleep "$SLEEP_SEC"
done

echo "healthcheck failed: ${AGENT_URL}/health not ready after ${RETRIES} attempts" >&2
exit 1
