# ViaAccess QR Reader Agent (Go)

Daemon de borda para o **appliance ViaAccess QR Reader**: lê QR dinâmico do Identity, faz `POST /api/bridge/intent/redeem`, opcionalmente aciona relé GPIO e webhook de unlock.

O pacote TypeScript em [`../identity-qr-reader`](../identity-qr-reader) permanece como **referência de contrato** para homologação. Este agent é o runtime de produção no Raspberry Pi.

```
Celular (PWA) → QR → leitor USB/HTTP → viaaccess-qr-agent → Identity → ViaAccess
```

## Build

Requisitos: Go 1.22+

```bash
cd install/qr-reader-agent
make build          # bin/viaaccess-qr-agent (host)
make arm64          # bin/viaaccess-qr-agent-linux-arm64 (Pi)
make test
```

## Primeiro boot (provisionamento)

Sem `config.json` com `"configured": true`, o agent sobe em **modo setup** e expõe a UI em `http://<ip>:3710/setup`.

Config padrão em produção: `/etc/viaaccess-qr-reader/config.json` (permissões `0600`).

PIN de fábrica (opcional): variável `SETUP_PIN` ou flag `--setup-pin`.

### Fluxo recomendado: QR zero-SSH

Um leitor novo é provisionado sem SSH. O admin gera um token de uso único; o técnico cola no appliance na rede local.

```
Admin (Identity)                    Appliance (rede local)
     │                                      │
     │  Provisionar (QR) → clm_… (~15 min)  │
     │ ───────────────────────────────────► │  :3710/setup → aba Provisionar
     │                                      │  POST Identity /api/bridge/provision/claim
     │                                      │  recebe idb_… + defaults
     │                                      │  entra em ONLINE (sem reinício)
```

**1. Admin (Identity)**

1. Ponto em modo **QR dinâmico no leitor** (`DYNAMIC_QR_AT_READER`).
2. Admin → ponto → **Leitores (bridge)**.
3. Informe o nome do leitor → **Provisionar (QR)**.
4. Escaneie o QR ou copie a URL `…/bridge/provision?t=clm_…` (válida por ~15 minutos).

Cada token `clm_…` é **uso único**: provisiona um appliance. Para outro leitor, gere outro QR.

**2. Appliance**

1. Conecte o Pi/leitor na rede (mesma LAN do técnico).
2. Abra `http://<ip-do-appliance>:3710/setup`.
3. Aba **Provisionar (QR)**.
4. Cole a **URL completa** ou só o token `clm_…`.
   - Se colar só `clm_…`, informe também a URL do Identity.
5. Ajuste relé GPIO se necessário → **Provisionar e testar**.
6. O agent **ativa o modo operação automaticamente** (não precisa reiniciar o processo).

**3. Validar**

```bash
curl -s http://<ip>:3710/health
```

Esperado: `"configured": true`, `"operationMode": "ONLINE"`, `"identityReachable": true`.

Teste de passagem (simula leitor HTTP):

```bash
curl -s -X POST http://<ip>:3710/scan \
  -H 'Content-Type: application/json' \
  -d '{"qrUrl":"URL_DO_QR_DINAMICO_DO_PWA"}'
```

Resposta esperada com acesso liberado: `"correlationOutcome": "AUTHORIZED"`.

### Fallback: provisionamento manual

Se o QR expirou ou não puder ser escaneado:

1. Admin → **Nova chave** → copie `idb_…` (exibida uma vez).
2. `http://<ip>:3710/setup` → aba **Manual**.
3. URL do Identity, device key `idb_…`, slug do ponto (opcional se já vinculado à chave).
4. Salve; o agent volta ao modo operação automaticamente.

### Reprovisionar após revogar a chave

Se o admin **revogar** a device key `idb_…` no Identity:

1. Em até ~60s (sync de policy) ou no próximo `POST /scan`, o agent detecta `401` / `BRIDGE_DISABLED`.
2. Ele **limpa a chave local**, grava `configured: false` e reabre **`/setup`** sem reiniciar o processo.
3. No admin, gere um **novo** QR (**Provisionar (QR)**). Tokens `clm_…` antigos não podem ser reutilizados.
4. No appliance, abra `http://<ip>:3710/setup` → cole o novo `clm_…` → provisionar.

Log esperado: `device key invalid (…) — setup mode at http://…:3710/setup`.

`/health` em modo setup:

```json
{"ok": false, "configured": false, "setupRequired": true}
```

### Endpoints de setup (modo não configurado)

| Método | Path | Uso |
|--------|------|-----|
| `GET` | `/setup` | UI web (abas Provisionar e Manual) |
| `GET` | `/api/setup/status` | PIN obrigatório? |
| `POST` | `/api/setup/provision` | Consome `clm_…` via Identity e grava config |
| `POST` | `/api/setup` | Salva config manual (`idb_…`) |

## Desenvolvimento no Mac

No macOS use arquivos locais em `.dev/` em vez de `/etc/viaaccess-qr-reader/`:

```bash
cd install/qr-reader-agent
mkdir -p .dev
```

**Primeiro boot (testar provisionamento QR):**

```bash
go run ./cmd/viaaccess-qr-agent \
  --config ./.dev/config.json \
  --policy ./.dev/policy-snapshot.json \
  --outbox ./.dev/outbox.json \
  --nonce ./.dev/consumed-intents.json
```

Abra `http://127.0.0.1:3710/setup`, provisione com um `clm_…` do admin local (`http://localhost:3100`). O modo operação ativa sozinho após salvar.

**Já configurado** — crie `config.dev.json` (gitignored) e use:

```bash
make run
```

Equivalente a `go run ./cmd/viaaccess-qr-agent --config ./config.dev.json --stdin`.

Exemplo de `config.dev.json`:

```json
{
  "configured": true,
  "identityUrl": "http://localhost:3100",
  "deviceKey": "idb_…",
  "accessPointSlug": "main-entrance",
  "emitDetection": true,
  "httpHost": "127.0.0.1",
  "httpPort": 3710,
  "relay": { "enabled": false }
}
```

No Mac, `relaySimulated: true` em `/health` é esperado (sem GPIO físico).

## Modo operação

Variáveis em `/etc/viaaccess-qr-reader/env` (opcional, sobrescrevem JSON):

| Variável | Descrição |
|----------|-----------|
| `IDENTITY_URL` | URL do Identity |
| `IDENTITY_DEVICE_KEY` | Chave `idb_…` |
| `EMIT_DETECTION` | `true` (padrão) emite detection após validation |
| `HTTP_HOST` / `HTTP_PORT` | Bind HTTP (padrão `0.0.0.0:3710`) |
| `WEBHOOK_SECRET` | Exige header `X-Webhook-Secret` no `POST /scan` |
| `UNLOCK_WEBHOOK_URL` | POST local após `AUTHORIZED` |
| `UNLOCK_ON_AUTHORIZED_ONLY` | `true` (padrão) |
| `RELAY_ENABLED` | `true` para GPIO |
| `RELAY_GPIO_PIN` | Padrão `17` |
| `STDIN_SCANNER` | `true` com `--stdin` no systemd para leitor USB |

### Endpoints locais

| Método | Path | Uso |
|--------|------|-----|
| `GET` | `/health` | Modo operação, sync, outbox, último scan — ver [docs/contingency-mode.md](docs/contingency-mode.md) |
| `POST` | `/scan` | Body JSON `{ "qrUrl": "…" }` |
| `GET` | `/setup` | UI de provisionamento (só sem config) |
| `POST` | `/api/setup/provision` | Provisionamento via token `clm_…` (só sem config) |

### Leitor USB (keyboard wedge)

```bash
./bin/viaaccess-qr-agent --config ./config.json --stdin
```

Ou via systemd (`--stdin` no unit file).

### Catraca / controlador HTTP

```bash
curl -s -X POST http://127.0.0.1:3710/scan \
  -H 'Content-Type: application/json' \
  -d '{"qrUrl":"https://identity.cliente/r/clxyz?t=AbCd"}'
```

## Relé GPIO (Linux)

Com `relay.enabled: true` no config, o agent pulsa a linha em `gpiochip0` no offset configurado (`relayGpioPin`). Em Raspberry Pi, confira o offset com `gpioinfo` (BCM 17 nem sempre é offset 17).

Em desenvolvimento (macOS) ou sem GPIO, usa driver simulado (log).

## Modo contingência (porta sem WAN)

Celular com 4G continua emitindo QR no Identity. O appliance:

1. Tenta **redeem online** (timeout 3s).
2. Se a rede falhar e o **policy snapshot** estiver fresh → modo `CONTINGENCY`.
3. Se o snapshot estiver expirado → `SYNC_STALE` (passagem bloqueada).

Arquivos em `/etc/viaaccess-qr-reader/`:

- `policy-snapshot.json` — último sync de grants + chave `ticketVerify` (sync automático a cada 60s)
- `outbox.json` — eventos pendentes para flush no Identity
- `consumed-intents.json` — anti-replay local por `intentId`

Documentação completa: [docs/contingency-mode.md](docs/contingency-mode.md).

O QR dinâmico inclui `st` (JWT assinado pelo Identity). Em contingência, o agent valida `st` contra o snapshot local sem chamar a rede.

## systemd

```bash
sudo install -m 0755 bin/viaaccess-qr-agent-linux-arm64 /usr/local/bin/viaaccess-qr-agent
sudo install -d -m 0750 /etc/viaaccess-qr-reader
sudo install -m 0640 systemd/viaaccess-qr-reader.env.example /etc/viaaccess-qr-reader/env
sudo install systemd/viaaccess-qr-agent.service /etc/systemd/system/
sudo systemctl enable --now viaaccess-qr-agent
```

## Paridade com identity-qr-reader (TypeScript)

| Comportamento | TS reference | Go agent |
|---------------|--------------|----------|
| Redeem Identity | `redeem.ts` | `internal/redeem` |
| Debounce + `/scan` | `turnstile-http.ts` | `internal/scan` |
| Unlock webhook | `unlock-webhook.ts` | `internal/unlock` |
| USB stdin | `scan-redeem.ts` | `--stdin` |
| Contingency verify | — | `internal/contingency` |
| Policy sync + flush | — | `internal/syncclient` |
| Provisionamento QR | — | `internal/setup` (`POST /api/setup/provision`) |

## Ver também

- Identity: `POST /api/bridge/provision/claim`, `POST /api/bridge/intent/redeem` — OpenAPI em viaaccess-identity
- [identity-qr-bridge.md](https://github.com/vialabs-tec/viaaccess-identity/blob/main/docs/installation/identity-qr-bridge.md)
