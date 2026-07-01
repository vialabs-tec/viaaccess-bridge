#!/usr/bin/env bash
# Mac webcam → ffmpeg push → MediaMTX (:8555) → Frigate go2rtc
set -euo pipefail

DEVICE="${WEBCAM_DEVICE:-0}"
STREAM="${WEBCAM_STREAM:-portao-principal}"
RTSP_PORT="${WEBCAM_RTSP_PORT:-8555}"
SIZE="${WEBCAM_SIZE:-1280x720}"
FRAMERATE="${WEBCAM_FRAMERATE:-30}"
WEBCAM_INPUT="${WEBCAM_INPUT:-${DEVICE}:none}"
PUBLISH_URL="rtsp://127.0.0.1:${RTSP_PORT}/${STREAM}"

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found. Install with: brew install ffmpeg" >&2
  exit 1
fi

echo "Input: avfoundation ${WEBCAM_INPUT} @ ${SIZE} ${FRAMERATE}fps (uyvy422)"
echo "Publishing: ${PUBLISH_URL}"
echo "(requires: docker compose up -d mediamtx frigate)"
echo "Press Ctrl+C to stop."
echo ""

set +e
ffmpeg -hide_banner -loglevel warning \
  -f avfoundation -framerate "${FRAMERATE}" -video_size "${SIZE}" -pixel_format uyvy422 \
  -i "${WEBCAM_INPUT}" \
  -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -g "${FRAMERATE}" \
  -f rtsp -rtsp_transport tcp "${PUBLISH_URL}"
status=$?
set -e

if [[ "${status}" -ne 0 ]]; then
  echo "" >&2
  echo "ffmpeg exited with ${status}." >&2
  echo "Start MediaMTX: docker compose up -d mediamtx" >&2
  echo "Grant Camera to Terminal: System Settings → Privacy & Security → Camera" >&2
  exit "${status}"
fi
