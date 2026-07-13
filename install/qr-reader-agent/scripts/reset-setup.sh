#!/usr/bin/env bash
# Reset qr-reader-agent to setup mode (reprovision on another tenant or access point).
set -euo pipefail

CONFIG_PATH="${VIAACCESS_QR_CONFIG:-/etc/viaaccess-qr-reader/config.json}"
ENV_PATH="${VIAACCESS_QR_ENV:-/etc/viaaccess-qr-reader/env}"
POLICY_PATH="${VIAACCESS_QR_POLICY:-/etc/viaaccess-qr-reader/policy-snapshot.json}"
OUTBOX_PATH="${VIAACCESS_QR_OUTBOX:-/etc/viaaccess-qr-reader/outbox.json}"
NONCE_PATH="${VIAACCESS_QR_NONCE:-/etc/viaaccess-qr-reader/consumed-intents.json}"
SERVICE_NAME="${VIAACCESS_QR_SERVICE:-viaaccess-qr-agent}"
HTTP_HOST="${HTTP_HOST:-127.0.0.1}"
HTTP_PORT="${HTTP_PORT:-3710}"

SKIP_CONFIRM=false
CLEAR_STATE=true
RESTART_SERVICE=true
DEV_MODE=false

usage() {
  cat <<'EOF'
Uso: scripts/reset-setup.sh [opções]

Limpa credenciais do appliance (device key, slug) e reabre o modo /setup.
Preserva relay GPIO, porta HTTP e demais ajustes locais.

Opções:
  --yes, -y           Não pedir confirmação
  --dev               Usa config.dev.json e estado em .dev/ (desenvolvimento no Mac)
  --no-clear-state    Não apaga policy-snapshot, outbox e consumed-intents
  --no-restart        Não para/reinicia o serviço systemd
  -h, --help          Mostra esta ajuda

Variáveis (produção no Pi):
  VIAACCESS_QR_CONFIG   Caminho do config.json (padrão: /etc/viaaccess-qr-reader/config.json)
  VIAACCESS_QR_ENV      EnvironmentFile do systemd (padrão: /etc/viaaccess-qr-reader/env)
  VIAACCESS_QR_SERVICE  Nome do unit systemd (padrão: viaaccess-qr-agent)

Exemplos:
  sudo ./scripts/reset-setup.sh
  sudo ./scripts/reset-setup.sh --yes
  ./scripts/reset-setup.sh --dev --yes
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes|-y) SKIP_CONFIRM=true ;;
    --dev) DEV_MODE=true ;;
    --no-clear-state) CLEAR_STATE=false ;;
    --no-restart) RESTART_SERVICE=false ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Opção desconhecida: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [[ "$DEV_MODE" == true ]]; then
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
  CONFIG_PATH="${VIAACCESS_QR_CONFIG:-$ROOT/config.dev.json}"
  ENV_PATH=""
  POLICY_PATH="${VIAACCESS_QR_POLICY:-$ROOT/.dev/policy-snapshot.json}"
  OUTBOX_PATH="${VIAACCESS_QR_OUTBOX:-$ROOT/.dev/outbox.json}"
  NONCE_PATH="${VIAACCESS_QR_NONCE:-$ROOT/.dev/consumed-intents.json}"
  RESTART_SERVICE=false
fi

require_python3() {
  if ! command -v python3 >/dev/null 2>&1; then
    echo "Erro: python3 é necessário para editar o config.json." >&2
    exit 1
  fi
}

read_config_summary() {
  if [[ ! -f "$CONFIG_PATH" ]]; then
    echo "(config não encontrado — será criado no próximo provisionamento)"
    return
  fi
  python3 - "$CONFIG_PATH" <<'PY'
import json, sys
path = sys.argv[1]
try:
    with open(path) as f:
        cfg = json.load(f)
except Exception as exc:
    print(f"(não foi possível ler {path}: {exc})")
    raise SystemExit(0)
fields = [
    ("configured", cfg.get("configured")),
    ("identityUrl", cfg.get("identityUrl") or "—"),
    ("deviceId", cfg.get("deviceId") or "—"),
    ("accessPointSlug", cfg.get("accessPointSlug") or "—"),
    ("provisionedAt", cfg.get("provisionedAt") or "—"),
]
for key, value in fields:
    print(f"  {key}: {value}")
PY
}

reset_config_json() {
  require_python3
  local dir
  dir="$(dirname "$CONFIG_PATH")"
  mkdir -p "$dir"
  if [[ ! -f "$CONFIG_PATH" ]]; then
    python3 - "$CONFIG_PATH" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, "w") as f:
    json.dump({"configured": False}, f, indent=2)
    f.write("\n")
import os
os.chmod(path, 0o600)
PY
    return
  fi
  python3 - "$CONFIG_PATH" <<'PY'
import json, os, sys
path = sys.argv[1]
with open(path) as f:
    cfg = json.load(f)
cfg["configured"] = False
cfg["deviceKey"] = ""
cfg["deviceId"] = ""
cfg["provisionedAt"] = ""
cfg["accessPointSlug"] = ""
with open(path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
os.chmod(path, 0o600)
PY
}

strip_env_overrides() {
  local path="$1"
  [[ -n "$path" && -f "$path" ]] || return 0
  python3 - "$path" <<'PY'
import sys
path = sys.argv[1]
keys = {"IDENTITY_DEVICE_KEY", "IDENTITY_URL"}
lines = []
removed = []
with open(path) as f:
    for line in f:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            lines.append(line)
            continue
        name = stripped.split("=", 1)[0].strip()
        if name in keys:
            removed.append(name)
            continue
        lines.append(line)
with open(path, "w") as f:
    f.writelines(lines)
for name in removed:
    print(f"  removido de {path}: {name}")
PY
}

clear_state_files() {
  local removed=0
  for path in "$POLICY_PATH" "$OUTBOX_PATH" "$NONCE_PATH"; do
    if [[ -f "$path" ]]; then
      rm -f "$path"
      echo "  removido: $path"
      removed=1
    fi
  done
  if [[ "$removed" -eq 0 ]]; then
    echo "  (nenhum arquivo de estado local encontrado)"
  fi
}

systemd_active() {
  command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files "$SERVICE_NAME.service" >/dev/null 2>&1
}

stop_service() {
  if [[ "$RESTART_SERVICE" != true ]]; then
    return 0
  fi
  if ! systemd_active; then
    echo "  systemd não disponível ou unit ausente — pule reinício manual se o agent estiver rodando"
    return 0
  fi
  if systemctl is-active --quiet "$SERVICE_NAME"; then
    systemctl stop "$SERVICE_NAME"
    echo "  serviço $SERVICE_NAME parado"
  fi
}

start_service() {
  if [[ "$RESTART_SERVICE" != true ]]; then
    return 0
  fi
  if ! systemd_active; then
    return 0
  fi
  systemctl start "$SERVICE_NAME"
  echo "  serviço $SERVICE_NAME iniciado"
}

read_http_endpoint() {
  if [[ -f "$CONFIG_PATH" ]]; then
    local parsed
    parsed="$(python3 - "$CONFIG_PATH" <<'PY' 2>/dev/null || true
import json, sys
with open(sys.argv[1]) as f:
    cfg = json.load(f)
host = (cfg.get("httpHost") or "0.0.0.0").strip()
port = int(cfg.get("httpPort") or 3710)
if host in ("0.0.0.0", "::", ""):
    host = "127.0.0.1"
print(f"{host}:{port}")
PY
)" || true
    if [[ -n "$parsed" ]]; then
      HTTP_HOST="${parsed%%:*}"
      HTTP_PORT="${parsed##*:}"
    fi
  fi
}

if [[ "$DEV_MODE" != true && "$(id -u)" -ne 0 ]]; then
  echo "Em produção, execute com sudo: sudo ./scripts/reset-setup.sh" >&2
  exit 1
fi

echo "== Reset para modo setup =="
echo "Config: $CONFIG_PATH"
[[ -n "$ENV_PATH" ]] && echo "Env:    $ENV_PATH"
echo ""
echo "Estado atual:"
read_config_summary
echo ""

if [[ "$SKIP_CONFIRM" != true ]]; then
  echo "Isso desvincula o leitor do tenant atual. Será necessário um novo token clm_… no admin."
  read -r -p "Continuar? [y/N] " answer
  case "${answer:-}" in
    y|Y|yes|YES) ;;
    *)
      echo "Cancelado."
      exit 0
      ;;
  esac
fi

stop_service

echo ""
echo "== Limpando credenciais =="
reset_config_json
echo "  config atualizado: configured=false, deviceKey/deviceId/slug limpos"

if [[ -n "$ENV_PATH" ]]; then
  echo ""
  echo "== Removendo overrides no env =="
  strip_env_overrides "$ENV_PATH" || true
fi

if [[ "$CLEAR_STATE" == true ]]; then
  echo ""
  echo "== Limpando estado offline =="
  clear_state_files
fi

start_service

read_http_endpoint
SETUP_URL="http://${HTTP_HOST}:${HTTP_PORT}/setup"

echo ""
echo "== Próximos passos =="
echo "1. No admin do Identity (cliente novo): ponto → Leitores → Provisionar (QR) → clm_…"
echo "2. Abra $SETUP_URL na rede local e cole o token."
echo "3. Valide: curl -s http://${HTTP_HOST}:${HTTP_PORT}/health"
echo ""
if [[ "$DEV_MODE" == true ]]; then
  echo "Modo dev: reinicie 'make dev' se o agent já estava rodando."
fi
echo "Reset concluído."
