#!/usr/bin/env bash
# Lists AVFoundation video devices on macOS (for WEBCAM_DEVICE / WEBCAM_INPUT).
set -euo pipefail

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found. Install with: brew install ffmpeg" >&2
  exit 1
fi

echo "AVFoundation devices (stderr is normal; use index or name in WEBCAM_INPUT):"
echo ""

ffmpeg -hide_banner -f avfoundation -list_devices true -i "" 2>&1 \
  | sed -n '/AVFoundation video devices:/,/AVFoundation audio devices:/p' \
  | sed '$d' \
  || true

echo ""
echo "Examples:"
echo "  npm run webcam:stream"
echo "  WEBCAM_INPUT=\"0\" npm run webcam:stream"
echo "  WEBCAM_FRAMERATE=auto WEBCAM_INPUT=\"0\" npm run webcam:stream"
