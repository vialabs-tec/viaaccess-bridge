# ViaAccess Bridge

Sidecar Docker que conecta **Frigate** (imagem oficial) ao **ViaAccess Cloud** via VADP.

```
Câmeras → Frigate → MQTT → viaaccess-bridge → POST /api/v1/detections → ViaAccess Cloud
```

## Instalação no cliente

Use o pacote pronto em **[install/frigate/](install/frigate/README.md)** (compose + `.env` + template Frigate).

Imagem publicada: **`ghcr.io/vialabs-tec/viaaccess-bridge`**

```bash
cd install/frigate
cp .env.example .env   # API key + mapping
docker compose pull
docker compose up -d
```

## Desenvolvimento

```bash
npm install
cp .env.example .env
npm run dev
npm test
```

Build local da imagem:

```bash
npm run docker:build
```

## Variáveis

| Variável | Descrição |
|----------|-----------|
| `VIAACCESS_API_URL` | URL do ViaAccess Cloud |
| `VIAACCESS_API_KEY` | API key do tenant (`vac_…`) |
| `FRIGATE_MQTT_URL` | Broker MQTT (no compose: `mqtt://mqtt:1883`) |
| `FRIGATE_BASE_URL` | API HTTP do Frigate (snapshots) |
| `FRIGATE_ACCESS_POINT_MAP` | JSON: câmera + zona → slug do access point |
| `OUTBOX_PATH` | Fila de retry quando o cloud está offline |
| `BRIDGE_STATUS_PATH` | Arquivo JSON de status (opcional) |

## CI / publicação

O workflow [`.github/workflows/publish-image.yml`](.github/workflows/publish-image.yml) publica em `ghcr.io/<owner>/viaaccess-bridge` (owner = dono do repositório no GitHub) em push na `main` e em tags `v*`.

## Licença

Proprietário Via Labs.
