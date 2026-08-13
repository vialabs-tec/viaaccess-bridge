# SoftAP TLS materials

Factory self-signed ECDSA P-256 certificate for the local HTTPS setup portal.

- **CN / SAN:** `192.168.4.1`, `viaaccess.local`
- Regenerated with `../../scripts/gen-softap-certs.sh`
- Embedded into the app image via `main/CMakeLists.txt` (`EMBED_FILES`)

Browsers show a trust warning; accept it once on SoftAP commissioning. Identity
does not need this cert — it is only for phone ↔ SoftAP TLS.
