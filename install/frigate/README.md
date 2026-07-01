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
  },
  {
    "accessPoint": "interior-principal",
    "camera": "portao-principal",
    "zone": "interior",
    "labels": ["person"],
    "presenceSessionGapSeconds": 20
  }
]
```

- **Porta:** cada entrada na zona gera detecção (correlação com validação no ViaAccess).
- **Interior:** `presenceSessionGapSeconds` agrupa movimento contínuo; só a abertura de sessão vai para o ViaAccess (regra `presence_after_entry`).

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

## Desenvolvimento local (mercadinho)

Cenário de teste com **ViaAccess no host** (`localhost:3002`), **Identity** opcional (`localhost:3100`) e stack Frigate+bridge em Docker.

### 1. ViaAccess (host)

```bash
cd viaaccess && npm run dev   # :3002
```

No dashboard, crie dois pontos:

| Slug | Papel |
|------|--------|
| `entrada-principal` | Porta — validação Identity + `authorized_entry` |
| `interior-principal` | Salão — regra `presence_after_entry` (desative `unauthorized_entry`) |

Gere uma API key (`vac_…`) com acesso aos dois slugs.

Regra no interior (`presence_after_entry`), exemplo de params:

```json
{
  "entryAccessPointSlug": "entrada-principal",
  "entryWithinSeconds": 120,
  "providerActionsOnAllowed": ["lighting.on"],
  "providerActionsOnDenied": ["siren.play"]
}
```

### 2. Bridge (Docker)

```bash
cd install/frigate
cp .env.local.example .env
docker compose up -d --build
```

A imagem `ghcr.io/vialabs-tec/viaaccess-bridge` é privada. Em dev o compose **faz build local** (`viaaccess-bridge:local`). Produção no cliente: `docker login ghcr.io` e defina `VIAACCESS_BRIDGE_IMAGE` no `.env`.

| Serviço | URL |
|---------|-----|
| Frigate UI | http://localhost:5050 |
| ViaAccess API (do container) | `http://host.docker.internal:3002` |

Logs: `docker compose logs -f viaaccess-bridge`

Se você alterou o código do bridge localmente:

```bash
cd ../.. && npm run docker:build
# No .env: VIAACCESS_BRIDGE_IMAGE="ghcr.io/vialabs-tec/viaaccess-bridge:local"
cd install/frigate && docker compose up -d viaaccess-bridge
```

### 3. Frigate

**Webcam do Mac (dev, sem câmera IP):**

```bash
cd install/frigate
docker compose up -d
# Terminal 2 — publica webcam no MediaMTX (porta 8555)
npm run webcam:stream
```

Stack: **ffmpeg (host) → MediaMTX (:8555) → go2rtc → Frigate**. Reinicie após mudar config: `docker compose restart frigate`

Variáveis opcionais: `WEBCAM_INPUT="0:none"`, `WEBCAM_FRAMERATE=30`, `WEBCAM_LISTEN_PORT=8555`.

Se aparecer `Input/output error`, conceda acesso à **Câmera** para o Terminal em Ajustes do Sistema → Privacidade e Segurança → Câmera.

O `config.yml` usa uma câmera (`portao-principal`) com zonas `entrada` e `interior` no mesmo stream (webcam dev). Ajuste zonas em http://localhost:5050.

**Câmeras IP (produção):** use `frigate/config/config.production.example.yml` como referência e substitua os paths RTSP no `config.yml` (remova a seção `go2rtc` se não precisar).

- Confirme que eventos MQTT batem com o mapping: zona `entrada` → `entrada-principal`; zona `interior` → `interior-principal` (debounce 20s).

### 4. Fluxo de teste

1. **Entrada autorizada:** Identity (ou `POST /api/v1/validations`) em `entrada-principal` → pessoa na zona `entrada` → evento autorizado na porta.
2. **Interior com entrada recente:** dentro de 120s, movimento na zona `interior` → `presence_after_entry` allowed (uma detecção por sessão de ~20s).
3. **Intrusão:** movimento no interior **sem** validação recente na porta → alerta / ações `OnDenied`.

### 5. Identity (opcional)

```bash
cd viaaccess-identity && docker compose up postgres -d && npm run dev   # :3100
```

Admin → ViaAccess: cole a mesma `vac_…`, sincronize pontos, conceda permissão em `entrada-principal` aos membros de teste.

## Integração de software

Quem integra apenas identidade (ERP, app mobile) **não** instala este pacote. Use o SDK ou a API ViaAccess:

- `POST /api/v1/validations` — autorizar passagem
- Webhooks — `access.authorized` / `access.unauthorized`

Documentação: [viaaccess.dev/docs](https://viaaccess.dev/docs)
