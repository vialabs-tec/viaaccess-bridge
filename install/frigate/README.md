# Instalação ViaAccess + Frigate (cliente)

Pacote para rodar na **rede do condomínio / cliente**: Frigate oficial + MQTT + **viaaccess-bridge** → ViaAccess Cloud.

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
| `VIAACCESS_API_URL` | URL do cloud (ex. `https://api.viaaccess.com.br`) |
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

1. No sistema de gestão (ClubeVia, etc.), registre uma validação de entrada no ponto de acesso.
2. Provoke passagem na zona da câmera.
3. Confira no ViaAccess: acesso **autorizado** ou alerta.

## Atualizar o bridge

```bash
docker compose pull viaaccess-bridge
docker compose up -d viaaccess-bridge
```

Imagem: `ghcr.io/vialabs-tec/viaaccess-bridge` (tags `latest` ou versão semver).

## Integração de software (Identity Provider)

O ERP / app do clube **não** instala este pacote. Ele só chama:

- `POST /api/v1/validations` — quando o sócio valida QR
- Webhooks ou SSE — para receber `access.authorized` / `access.unauthorized`

Documentação: painel ViaAccess → `/docs`
