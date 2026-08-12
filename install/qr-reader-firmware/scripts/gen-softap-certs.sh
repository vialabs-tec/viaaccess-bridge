#!/usr/bin/env bash
# Regenerate the factory SoftAP self-signed certificate embedded in firmware.
# Browsers will warn once; that is expected for offline commissioning.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/main/certs"
mkdir -p "$out"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout "$out/prvtkey.pem" -out "$out/servercert.pem" \
  -days 8250 -nodes \
  -subj "/O=ViaAccess/CN=192.168.4.1" \
  -addext "subjectAltName=IP:192.168.4.1,DNS:viaaccess-qr.local"
chmod 600 "$out/prvtkey.pem"
openssl x509 -in "$out/servercert.pem" -noout -subject -dates -ext subjectAltName
echo "Wrote $out/servercert.pem and $out/prvtkey.pem — rebuild firmware to embed."
