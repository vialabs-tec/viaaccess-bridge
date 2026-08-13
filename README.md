# ViaAccess Bridge

Sidecar que conecta **Frigate** ao [ViaAccess](https://viaaccess.dev) via [VADP](https://viaaccess.dev/docs/detection-providers/vadp).

```
Câmeras → Frigate → MQTT → viaaccess-bridge → POST /api/v1/detections → ViaAccess
```

O bridge **não** faz reconhecimento facial nem cadastro de usuários. Ele traduz eventos de zona do Frigate em detecções de passagem (`passage_detected`) para auditoria no ViaAccess.

## Instalação

Pacote pronto com Frigate + MQTT + bridge: **[install/frigate/](install/frigate/README.md)**.

Leitor de QR na catraca + QR dinâmico Identity (Phase 1b): appliance
**[install/qr-reader-firmware/](install/qr-reader-firmware/README.md)** (ESP32-S3, ESP-IDF).
Contrato HTTP na porta 3710 e endpoints `/api/bridge/*` do Identity.

O agent Go em [install/qr-reader-agent/](install/qr-reader-agent/README.md) (Raspberry Pi)
é **legado**: código permanece no repo, CI desligado, sem novas instalações nem OTA de frota.

| | ESP32-S3 (suportado) |
|---|---|
| Leitor de QR | UART TTL 9600 (ex. EP8280L) ou `POST /scan` |
| Rede na primeira vez | SoftAP `viaaccess-setup` → `/wifi` |
| Mapa de GPIO | relé 10, porta 11, REX 12, LED 4/5/6 |
| Relógio | SNTP; DS3231 opcional para hora confiável sem rede |
| Contingência offline | Implementada (inclui `after_hours`) |
| OTA de frota | App image via comando `UPDATE` (`BRIDGE_OTA_*`) |

Provisionamento: cole a URL de claim (`clm_…`) do admin do Identity em
`http://viaaccess.local:3710/setup`.

Imagem Docker:

```text
ghcr.io/vialabs-tec/viaaccess-bridge:latest
```

```bash
cd install/frigate
cp .env.example .env   # API key + mapping
docker compose pull
docker compose up -d
```

Crie o tenant, o ponto de acesso (slug) e a API key no painel ViaAccess antes de subir o compose. Documentação completa: [viaaccess.dev/docs](https://viaaccess.dev/docs).

## Variáveis

| Variável | Descrição |
|----------|-----------|
| `VIAACCESS_API_URL` | URL da API ViaAccess |
| `VIAACCESS_API_KEY` | API key do tenant (`vac_…`) |
| `FRIGATE_MQTT_URL` | Broker MQTT (no compose: `mqtt://mqtt:1883`) |
| `FRIGATE_BASE_URL` | API HTTP do Frigate (snapshots) |
| `FRIGATE_ACCESS_POINT_MAP` | JSON: câmera + zona → slug do access point |
| `presenceSessionGapSeconds` | (por mapping) debounce de movimento contínuo no interior — ver README |
| `OUTBOX_PATH` | Fila de retry quando a API está offline |
| `BRIDGE_STATUS_PATH` | Arquivo JSON de status (opcional) |

## Desenvolvimento

```bash
npm install
cp .env.example .env
npm run dev
npm test
```

Build local da imagem: `npm run docker:build`

## Licença

Software proprietário © Via Labs. Uso sujeito aos termos do serviço ViaAccess.
