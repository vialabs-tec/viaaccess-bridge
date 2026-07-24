#!/usr/bin/env bash
# Apply pilot-level OS hardening for ViaAccess QR appliance on Raspberry Pi OS Bookworm.
# Does not enable overlayroot (that would break setup + fleet OTA without a data partition).
#
# Usage:
#   sudo ./scripts/harden-os.sh
#   sudo ./scripts/harden-os.sh --dry-run
set -euo pipefail

DRY_RUN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      sed -n '2,8p' "$0"
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

run() {
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "dry-run: $*"
  else
    "$@"
  fi
}

write_file() {
  local path="$1"
  local content="$2"
  if [[ "$DRY_RUN" -eq 1 ]]; then
    echo "dry-run: write ${path}"
    echo "$content" | sed 's/^/  | /'
    return
  fi
  mkdir -p "$(dirname "$path")"
  printf '%s\n' "$content" > "$path"
}

echo "== ViaAccess QR: OS harden (perfil A / piloto) =="

# --- swap ---
if command -v dphys-swapfile >/dev/null 2>&1; then
  if [[ -f /etc/dphys-swapfile ]]; then
    if grep -q '^CONF_SWAPSIZE=' /etc/dphys-swapfile; then
      run sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=0/' /etc/dphys-swapfile
    else
      if [[ "$DRY_RUN" -eq 1 ]]; then
        echo "dry-run: append CONF_SWAPSIZE=0 to /etc/dphys-swapfile"
      else
        echo 'CONF_SWAPSIZE=0' >> /etc/dphys-swapfile
      fi
    fi
  fi
  run dphys-swapfile swapoff 2>/dev/null || true
  run systemctl disable --now dphys-swapfile 2>/dev/null || true
else
  run swapoff -a 2>/dev/null || true
fi

# --- journald ---
write_file /etc/systemd/journald.conf.d/viaaccess.conf \
'[Journal]
Storage=volatile
RuntimeMaxUse=32M
SystemMaxUse=32M'
if [[ "$DRY_RUN" -eq 0 ]]; then
  systemctl restart systemd-journald
fi

# --- systemd hardware watchdog ---
write_file /etc/systemd/system.conf.d/viaaccess-watchdog.conf \
'[Manager]
RuntimeWatchdogSec=15s
RebootWatchdogSec=3min'

# --- boot: enable BCM watchdog if config.txt exists ---
BOOT_CFG=""
for candidate in /boot/firmware/config.txt /boot/config.txt; do
  if [[ -f "$candidate" ]]; then
    BOOT_CFG="$candidate"
    break
  fi
done

if [[ -n "$BOOT_CFG" ]]; then
  if grep -qE '^[[:space:]]*dtparam=watchdog=on' "$BOOT_CFG"; then
    echo "ok: watchdog already in ${BOOT_CFG}"
  else
    if [[ "$DRY_RUN" -eq 1 ]]; then
      echo "dry-run: append dtparam=watchdog=on to ${BOOT_CFG}"
    else
      printf '\n# ViaAccess QR appliance — hardware watchdog\ndtparam=watchdog=on\n' >> "$BOOT_CFG"
      echo "updated ${BOOT_CFG} (reboot required for /dev/watchdog)"
    fi
  fi
else
  echo "warn: no config.txt found — set dtparam=watchdog=on manually on CM/custom images"
fi

if [[ "$DRY_RUN" -eq 0 ]]; then
  systemctl daemon-reexec 2>/dev/null || true
fi

echo
echo "done."
echo "next:"
echo "  1. reboot if config.txt changed (watchdog device)"
echo "  2. confirm: free -h  (swap ~0) && ls -l /dev/watchdog*"
echo "  3. install agent: sudo ./scripts/install.sh --binary bin/viaaccess-qr-agent-linux-arm64"
echo "  4. power: 5V PSU with headroom; keep 12V lock rail separate"
echo "docs: docs/field-hardening.md"
