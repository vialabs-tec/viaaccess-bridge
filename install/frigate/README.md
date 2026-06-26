# Instalação ViaAccess + Frigate

Pacote para rodar na **rede local do cliente**: Frigate + MQTT + **viaaccess-bridge** → ViaAccess.

## Pré-requisitos

- Docker e Docker Compose
- Câmera IP com RTSP (ou NVR compatível)
- Tenant no ViaAccess com API key (`vac_…`) e ponto de acesso criado (slug, ex. `entrada-principal`)

## Passo a passo

### 1. Copiar arquivos

Copie esta pasta `install/frigate/` para o servidor do cliente (ex. `/opt/viaaccess-frigate`).

### 2. Configurar câmera no Frigate

Edite `frigate/config/config.yml`:

- `path`: URL RTSP da câmera
- Nome da câmera (`portao-principal`) deve bater com o mapping do bridge
- Ajuste a zona `entrada` se necessário (coordenadas no Frigate UI)

### 3. Configurar o bridge

```bash
cp .env.example .env
```

Preencha:

| Variável | Valor |
|----------|--------|
| `VIAACCESS_API_URL` | URL da API (ex. `https://api.viaaccess.com.br`) |
| `VIAACCESS_API_KEY` | API key do tenant |
| `FRIGATE_ACCESS_POINT_MAP` | JSON: câmera + zona → slug do access point |

Exemplo de mapping:

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

### 4. Subir

```bash
docker compose pull
docker compose up -d
```

- Frigate UI: http://IP-DO-SERVIDOR:5050
- Logs do bridge: `docker compose logs -f viaaccess-bridge`

### 5. Validar

1. No seu app de gestão, registre uma validação de entrada no ponto de acesso (`POST /api/v1/validations`).
2. Provoke passagem na zona da câmera.
3. Confira no ViaAccess: acesso **autorizado** ou alerta.

## Atualizar o bridge

```bash
docker compose pull viaaccess-bridge
docker compose up -d viaaccess-bridge
```

Imagem padrão: `ghcr.io/vialabs-tec/viaaccess-bridge:latest`. Para outra tag ou registry, defina `VIAACCESS_BRIDGE_IMAGE` no `.env`.

## Integração de software

Quem integra apenas identidade (ERP, app mobile) **não** instala este pacote. Use o SDK ou a API ViaAccess:

- `POST /api/v1/validations` — autorizar passagem
- Webhooks — `access.authorized` / `access.unauthorized`

Documentação: [viaaccess.dev/docs](https://viaaccess.dev/docs)
