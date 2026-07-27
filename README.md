# ViaAccess Bridge

Sidecar que conecta **Frigate** ao [ViaAccess](https://viaaccess.dev) via [VADP](https://viaaccess.dev/docs/detection-providers/vadp).

```
Câmeras → Frigate → MQTT → viaaccess-bridge → POST /api/v1/detections → ViaAccess
```

O bridge **não** faz reconhecimento facial nem cadastro de usuários. Ele traduz eventos de zona do Frigate em detecções de passagem (`passage_detected`) para auditoria no ViaAccess.

## Instalação

Pacote pronto com Frigate + MQTT + bridge: **[install/frigate/](install/frigate/README.md)**.

Leitor de QR na catraca + QR dinâmico Identity (Phase 1b). Há **dois appliances** que
falam o mesmo contrato HTTP (porta 3710) e os mesmos endpoints `/api/bridge/*` do Identity;
o Identity só os distingue pelo header `X-ViaAccess-Agent-Version`:

- **[install/qr-reader-agent/](install/qr-reader-agent/README.md)**: agent Go no Raspberry Pi (`viaaccess-qr-agent`), com systemd
- **[install/qr-reader-firmware/](install/qr-reader-firmware/README.md)**: firmware ESP32-S3 (ESP-IDF), sem sistema operacional

Escolha por instalação:

| | Raspberry Pi (agent Go) | ESP32-S3 (firmware) |
|---|---|---|
| Leitor de QR | USB (HID) ou `POST /scan` | UART TTL 9600 (ex. EP8280L) ou `POST /scan` |
| Rede na primeira vez | Rede do Pi já configurada | SoftAP `viaaccess-qr-setup` → `/wifi` |
| Mapa de GPIO | BCM: relé 17, porta 4, REX 18, LED 22/27/23 | relé 10, porta 11, REX 12, LED 4/5/6 |
| Relógio | Hora do sistema (NTP) | SNTP; DS3231 opcional para hora confiável sem rede |
| Contingência offline | Implementada | Ainda não; falha fechado como `SYNC_STALE` |
| OTA de frota | Binário via comando `UPDATE` | Partições A/B prontas, download ainda não |

Provisionamento é igual nos dois: cole a URL de claim (`clm_…`) do admin do Identity em
`http://viaaccess-qr.local:3710/setup`.

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
