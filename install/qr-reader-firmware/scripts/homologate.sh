#!/usr/bin/env bash
# Homologation checks for a flashed ViaAccess QR Reader appliance (ESP32-S3).
#
# Same checks as qr-reader-agent/scripts/homologate.sh plus what only exists on
# this product: Wi-Fi phase, UART reader stats and the SoftAP provisioning step.
# Read-only unless QR_URL is set, so it is safe to run against a door in service.
#
#   READER_URL=http://viaaccess-qr.local:3710 ./scripts/homologate.sh
#   QR_URL='https://…' ./scripts/homologate.sh   # also exercises a real passage
set -euo pipefail

READER_URL="${READER_URL:-http://viaaccess-qr.local:3710}"
IDENTITY_URL="${IDENTITY_URL:-}"

pass=0
fail=0

jq_get() {
  # Reads a dotted path out of a JSON document without requiring jq.
  python3 -c '
import json, sys
doc = json.load(sys.stdin)
for key in sys.argv[1].split("."):
    if not isinstance(doc, dict):
        doc = None
        break
    doc = doc.get(key)
print("" if doc is None else doc)
' "$1" 2>/dev/null || echo ""
}

check() {
  local label="$1" actual="$2" expected="$3"
  if [[ "$actual" == "$expected" ]]; then
    printf '  PASS  %-34s %s\n' "$label" "$actual"
    pass=$((pass + 1))
  else
    printf '  FAIL  %-34s %s (esperado: %s)\n' "$label" "${actual:-<vazio>}" "$expected"
    fail=$((fail + 1))
  fi
}

show() { printf '  ----  %-34s %s\n' "$1" "${2:-<vazio>}"; }

echo "== 1. Alcance e postura =="
if ! health="$(curl -sf --max-time 10 "$READER_URL/health")"; then
  echo "  FAIL  leitor inacessível em $READER_URL"
  echo ""
  echo "Se o leitor ainda não tem Wi-Fi, entre na rede viaaccess-qr-setup e use"
  echo "READER_URL=http://192.168.4.1:3710"
  exit 1
fi

configured="$(printf '%s' "$health" | jq_get configured)"
show "firmware" "$(printf '%s' "$health" | jq_get agentVersion)"
show "wifi" "$(printf '%s' "$health" | jq_get wifi.phase) $(printf '%s' "$health" | jq_get wifi.ip)"
show "mDNS" "$(printf '%s' "$health" | jq_get mdns.url)"

if [[ "$configured" != "True" ]]; then
  echo "  WARN  leitor em modo setup"
  echo ""
  echo "Abra $READER_URL/setup e provisione com o token clm_ do dashboard,"
  echo "depois rode este script novamente."
  exit 0
fi

check "configured" "$configured" "True"
check "ok" "$(printf '%s' "$health" | jq_get ok)" "True"
check "operationMode" "$(printf '%s' "$health" | jq_get operationMode)" "ONLINE"
check "identityReachable" "$(printf '%s' "$health" | jq_get identityReachable)" "True"
check "policySync.stale" "$(printf '%s' "$health" | jq_get policySync.stale)" "False"
show "policySync.syncedAt" "$(printf '%s' "$health" | jq_get policySync.syncedAt)"
show "memberGrantCount" "$(printf '%s' "$health" | jq_get policySync.memberGrantCount)"

echo ""
echo "== 2. Hardware =="
show "relaySimulated" "$(printf '%s' "$health" | jq_get relaySimulated)"
show "leitor UART pronto" "$(printf '%s' "$health" | jq_get qrReader.driverReady)"
show "leituras / linhas perdidas" \
  "$(printf '%s' "$health" | jq_get qrReader.scans) / $(printf '%s' "$health" | jq_get qrReader.droppedLines)"
show "contato de porta" "$(printf '%s' "$health" | jq_get doorContact.driver)"
show "botoeira" "$(printf '%s' "$health" | jq_get exitButton.driver)"

clock_source="$(printf '%s' "$health" | jq_get clock.source)"
rtc_present="$(printf '%s' "$health" | jq_get clock.rtc.present)"
show "origem da hora" "$clock_source"
show "relógio de bateria" "$rtc_present"
show "temperatura do relógio" "$(printf '%s' "$health" | jq_get clock.rtc.temperatureC)"
check "hora confiável" "$(printf '%s' "$health" | jq_get clock.trusted)" "True"

# Without the DS3231 the appliance still works online, but a power cut leaves it
# with no clock, and contingency is refused until the network returns.
if [[ "$rtc_present" != "True" ]]; then
  echo "  WARN  sem DS3231: após queda de energia a contingência fica bloqueada"
elif [[ "$(printf '%s' "$health" | jq_get clock.rtc.oscillatorStopped)" == "True" ]]; then
  echo "  WARN  bateria do DS3231 esgotada: troque a célula (LIR2032 ou CR2032)"
fi

# droppedLines above zero on a fresh appliance is almost always the module baud
# rate, the single most common wiring mistake with the EP8280L in TTL mode.
if [[ "$(printf '%s' "$health" | jq_get qrReader.droppedLines)" != "0" ]]; then
  echo "  WARN  linhas descartadas: confira o baud do módulo (padrão 9600)"
fi

echo ""
echo "== 3. QR inválido (deve bloquear) =="
invalid_status="$(curl -s -o /dev/null -w '%{http_code}' --max-time 15 \
  -X POST "$READER_URL/scan" -H 'Content-Type: application/json' \
  -d '{"qrUrl":"https://exemplo.invalido/qr/homologacao"}')"
if [[ "$invalid_status" == "4"* ]]; then
  printf '  PASS  %-34s HTTP %s\n' "recusa QR desconhecido" "$invalid_status"
  pass=$((pass + 1))
else
  printf '  FAIL  %-34s HTTP %s (esperado 4xx)\n' "recusa QR desconhecido" "$invalid_status"
  fail=$((fail + 1))
fi

echo ""
echo "== 4. Passagem real =="
if [[ -z "${QR_URL:-}" ]]; then
  echo "  SKIP  defina QR_URL com um QR dinâmico do app do associado"
else
  first="$(curl -s --max-time 20 -X POST "$READER_URL/scan" \
    -H 'Content-Type: application/json' -d "{\"qrUrl\":\"$QR_URL\"}")"
  check "primeira leitura ok" "$(printf '%s' "$first" | jq_get ok)" "True"
  check "scanPath" "$(printf '%s' "$first" | jq_get scanPath)" "online"
  show "correlationOutcome" "$(printf '%s' "$first" | jq_get redeem.correlationOutcome)"
  show "relé" "$(printf '%s' "$first" | jq_get relay.ok)"

  # The same QR inside the debounce window must be swallowed locally instead of
  # reaching Identity, which is what protects the door from a double read.
  second="$(curl -s --max-time 20 -X POST "$READER_URL/scan" \
    -H 'Content-Type: application/json' -d "{\"qrUrl\":\"$QR_URL\"}")"
  check "segunda leitura em debounce" "$(printf '%s' "$second" | jq_get ignored)" "True"
fi

echo ""
echo "== 5. Identity device-config =="
if [[ -n "${DEVICE_KEY:-}" && -n "$IDENTITY_URL" ]]; then
  curl -sf -H "Authorization: Bearer $DEVICE_KEY" "$IDENTITY_URL/api/bridge/device-config" \
    | python3 -m json.tool 2>/dev/null || echo "  WARN  device-config não respondeu"
else
  echo "  SKIP  defina DEVICE_KEY=idb_… e IDENTITY_URL para conferir o device-config"
fi

echo ""
echo "$pass verificações ok, $fail falhas"
echo ""
echo "Ainda manual (precisa de alguém na porta):"
echo "  - leitura pelo módulo EP8280L (não só por curl) abre a fechadura"
echo "  - chave revogada no dashboard devolve o leitor ao modo setup"
echo "  - queda de rede bloqueia a passagem com SYNC_STALE (fail closed)"
echo "  - com DS3231: desligar tudo por 5 min e religar sem rede mantém clock.source=RTC"

[[ "$fail" -eq 0 ]]
