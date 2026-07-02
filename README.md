# ViaAccess Bridge

Sidecar que conecta **Frigate** ao [ViaAccess](https://viaaccess.dev) via [VADP](https://viaaccess.dev/docs/detection-providers/vadp).

```
Câmeras → Frigate → MQTT → viaaccess-bridge → POST /api/v1/detections → ViaAccess
```

O bridge **não** faz reconhecimento facial nem cadastro de usuários. Ele traduz eventos de zona do Frigate em detecções de passagem (`passage_detected`) para auditoria no ViaAccess.

## Instalação

Pacote pronto com Frigate + MQTT + bridge: **[install/frigate/](install/frigate/README.md)**.

Leitor USB + QR dinâmico Identity (Phase 1b): **[install/identity-qr-reader/](install/identity-qr-reader/README.md)**.

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
