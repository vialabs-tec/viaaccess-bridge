# LAN HTTPS materials

Factory self-signed ECDSA P-256 certificate for local HTTPS on **STA/LAN**
(`https://viaaccess.local/`, `curl -k`). SoftAP does **not** bind `:443` —
phones must use HTTP `:80` on `viaaccess-setup`.

- **CN / SAN:** `192.168.4.1`, `viaaccess.local`
- Regenerated with `../../scripts/gen-softap-certs.sh`
- Embedded into the app image via `main/CMakeLists.txt` (`EMBED_FILES`)

Browsers show a trust warning on LAN HTTPS. Identity does not need this cert.
