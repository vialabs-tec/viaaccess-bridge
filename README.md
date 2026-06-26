# ViaAccess Bridge

Sidecar container that connects **Frigate** (official image) to **ViaAccess Cloud** via VADP.

```
Cameras → Frigate → MQTT (frigate/events) → viaaccess-bridge → POST /api/v1/detections → ViaAccess Cloud
```

ViaAccess Cloud never speaks Frigate natively; it only accepts VADP envelopes.

## Quick start (Docker)

1. Copy `docker-compose.example.yml` and `.env.example` to your deployment folder.
2. Set `VIAACCESS_API_URL`, `VIAACCESS_API_KEY`, and `FRIGATE_ACCESS_POINT_MAP`.
3. Run `docker compose up -d`.

Image: `vialabs/viaaccess-bridge`

## Environment

| Variable | Description |
|----------|-------------|
| `VIAACCESS_API_URL` | ViaAccess Cloud base URL |
| `VIAACCESS_API_KEY` | Tenant API key (`vac_…`) |
| `FRIGATE_MQTT_URL` | MQTT broker (default `mqtt://127.0.0.1:1883`) |
| `FRIGATE_MQTT_TOPIC_PREFIX` | Topic prefix (default `frigate`) |
| `FRIGATE_BASE_URL` | Frigate HTTP API for snapshot URLs |
| `FRIGATE_ACCESS_POINT_MAP` | JSON array: camera + zone → access point slug |
| `OUTBOX_PATH` | Retry queue file when cloud is unreachable |
| `OUTBOX_FLUSH_INTERVAL_MS` | Retry interval (default 30000) |
| `BRIDGE_STATUS_PATH` | Optional JSON status file (used by viaaccess-lab) |

### Mapping example

```json
[
  {
    "accessPoint": "entrada-principal",
    "camera": "portao-principal",
    "zone": "entrada",
    "labels": ["person"]
  }
]
```

## Local development

```bash
npm install
cp .env.example .env   # or point --env-file to viaaccess-lab/.env
npm run dev
```

From **viaaccess-lab**:

```bash
npm run frigate:bridge
```

## Build image

```bash
npm run build
npm pack ../viaaccess/packages/client -o viaaccess-client.tgz
docker build -t vialabs/viaaccess-bridge:local .
```

## Tests

```bash
npm test
```
