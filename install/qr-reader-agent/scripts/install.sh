#!/usr/bin/env bash
# Install viaaccess-qr-agent on a Raspberry Pi (or Linux arm64/amd64 host).
# Usage:
#   sudo ./scripts/install.sh [--binary PATH] [--no-start] [--enable-status-led]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY=""
NO_START=0
ENABLE_STATUS_LED=0
PREFIX="${PREFIX:-/usr/local}"
ETC_DIR="${ETC_DIR:-/etc/viaaccess-qr-reader}"
LIB_DIR="${PREFIX}/lib/viaaccess-qr-reader"
# Writable by service user so fleet OTA can replace the binary without root.
VAR_DIR="${VAR_DIR:-/var/lib/viaaccess-qr-reader}"
BIN_DIR="${VAR_DIR}/bin"
LEGACY_BIN="${PREFIX}/bin/viaaccess-qr-agent"
SERVICE_USER="${SERVICE_USER:-viaaccess}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)
      BINARY="${2:-}"
      shift 2
      ;;
    --no-start)
      NO_START=1
      shift
      ;;
    --enable-status-led)
      ENABLE_STATUS_LED=1
      shift
      ;;
    -h|--help)
      sed -n '2,6p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $1" >&2
      exit 1
      ;;
  esac
done

if [[ "$(id -u)" -ne 0 ]]; then
  echo "run as root (sudo)" >&2
  exit 1
fi

arch="$(uname -m)"
if [[ -z "$BINARY" ]]; then
  case "$arch" in
    aarch64|arm64)
      BINARY="${ROOT}/bin/viaaccess-qr-agent-linux-arm64"
      ;;
    x86_64|amd64)
      if [[ -x "${ROOT}/bin/viaaccess-qr-agent" ]]; then
        BINARY="${ROOT}/bin/viaaccess-qr-agent"
      else
        BINARY="${ROOT}/bin/viaaccess-qr-agent-linux-amd64"
      fi
      ;;
    *)
      echo "unsupported arch ${arch}; pass --binary" >&2
      exit 1
      ;;
  esac
fi

if [[ ! -x "$BINARY" ]]; then
  echo "binary not found or not executable: $BINARY" >&2
  echo "build with: make arm64   (or make build)" >&2
  exit 1
fi

if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
  useradd --system --home "$ETC_DIR" --shell /usr/sbin/nologin "$SERVICE_USER"
fi
if getent group gpio >/dev/null 2>&1; then
  usermod -aG gpio "$SERVICE_USER" || true
fi
if getent group input >/dev/null 2>&1; then
  usermod -aG input "$SERVICE_USER" || true
fi

install -d -m 0755 "$LIB_DIR" "${PREFIX}/bin"
install -d -m 0755 -o "$SERVICE_USER" -g "$SERVICE_USER" "$VAR_DIR" "$BIN_DIR"
install -d -m 0750 -o "$SERVICE_USER" -g "$SERVICE_USER" "$ETC_DIR"
install -m 0755 -o "$SERVICE_USER" -g "$SERVICE_USER" "$BINARY" "${BIN_DIR}/viaaccess-qr-agent"
install -m 0755 "${ROOT}/scripts/healthcheck.sh" "${LIB_DIR}/healthcheck.sh"
install -m 0644 "${ROOT}/systemd/viaaccess-qr-agent.service" /etc/systemd/system/viaaccess-qr-agent.service
install -m 0644 "${ROOT}/systemd/viaaccess-qr-agent-health.service" /etc/systemd/system/viaaccess-qr-agent-health.service

# Convenience symlink for PATH; real binary is under VAR_DIR for OTA.
if [[ -e "$LEGACY_BIN" && ! -L "$LEGACY_BIN" ]]; then
  rm -f "$LEGACY_BIN"
fi
ln -sfn "${BIN_DIR}/viaaccess-qr-agent" "$LEGACY_BIN"

if [[ ! -f "${ETC_DIR}/env" ]]; then
  install -m 0640 -o "$SERVICE_USER" -g "$SERVICE_USER" \
    "${ROOT}/systemd/viaaccess-qr-reader.env.example" "${ETC_DIR}/env"
fi
if [[ ! -f "${ETC_DIR}/config.json" ]]; then
  # Empty appliance: first boot opens /setup
  printf '%s\n' '{"configured":false}' > "${ETC_DIR}/config.json"
  chown "$SERVICE_USER:$SERVICE_USER" "${ETC_DIR}/config.json"
  chmod 0600 "${ETC_DIR}/config.json"
fi

if [[ "$ENABLE_STATUS_LED" -eq 1 ]]; then
  if ! grep -q '^STATUS_LED_ENABLED=' "${ETC_DIR}/env" 2>/dev/null; then
    printf '\nSTATUS_LED_ENABLED=true\n' >> "${ETC_DIR}/env"
  else
    sed -i 's/^#\?STATUS_LED_ENABLED=.*/STATUS_LED_ENABLED=true/' "${ETC_DIR}/env"
  fi
fi

systemctl daemon-reload
systemctl enable viaaccess-qr-agent.service viaaccess-qr-agent-health.service

if [[ "$NO_START" -eq 0 ]]; then
  systemctl restart viaaccess-qr-agent.service
  systemctl start viaaccess-qr-agent-health.service || {
    echo "warn: health unit failed — check: journalctl -u viaaccess-qr-agent -u viaaccess-qr-agent-health" >&2
  }
fi

echo "installed ${BIN_DIR}/viaaccess-qr-agent (symlink ${LEGACY_BIN})"
echo "config ${ETC_DIR}/ (open http://<ip>:3710/setup if not provisioned)"
echo "status: systemctl status viaaccess-qr-agent viaaccess-qr-agent-health"
