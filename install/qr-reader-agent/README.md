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
make test           # testes unitários (também rodam no CI do GitHub)
make dev            # desenvolvimento no Mac (ver abaixo)
make homologate     # checklist de health/scan (ver abaixo)
```

CI: workflow `.github/workflows/qr-reader-agent.yml` executa `go test` e `go build` em push/PR que alteram esta pasta.

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

1. Em até ~60s (sync de policy ou device-config) ou no próximo `POST /scan`, o agent detecta `401` / `BRIDGE_DISABLED`.
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

## Operação contínua (sync com Identity)

Depois de provisionado, o agent mantém contato periódico com o Identity. **Não é necessário reiniciar** o serviço quando defaults mudam na nuvem ou quando a chave é revogada (neste caso ele volta ao `/setup` sozinho).

A cada **~60 segundos** (e uma vez ao entrar em modo ONLINE), o loop de sync executa:

| Etapa | Endpoint Identity | Efeito no appliance |
|-------|-------------------|---------------------|
| 1. Policy | `GET /api/bridge/policy-snapshot` | Atualiza quem pode passar em modo offline (`policy-snapshot.json`) |
| 2. Device config | `GET /api/bridge/device-config` | Ajusta parâmetros operacionais em `config.json` (ver abaixo) |
| 3. Outbox | `POST /api/bridge/contingency/flush` | Reenvia passagens gravadas offline, se houver |

```
  provisionado (idb_…)                    Identity
        │                                    │
        ├──── policy sync ──────────────────►│ grants + ticketVerify
        ├──── device-config (ETag) ─────────►│ debounce, contingência…
        ├──── redeem (/scan) ───────────────►│ validation + detection
        └──── outbox flush ─────────────────►│ após modo offline
```

### Config remota (`device-config`)

O Identity define os **defaults operacionais** do leitor. O agent autentica com `idb_…` e usa `If-None-Match` (ETag) para evitar transferir o mesmo JSON repetidamente (`304 Not Modified`).

**Campos que o agent aplica** quando mudam no Identity:

| Campo | Efeito prático |
|-------|----------------|
| `debounceMs` | Tempo mínimo entre dois scans do mesmo QR |
| `emitDetection` | Registrar detection no ViaAccess após validation |
| `contingency.enabled` | Permitir passagem offline com snapshot local |
| `contingency.onlineRedeemTimeoutMs` | Quanto esperar a rede antes de tentar offline |
| `contingency.maxPolicyStaleHours` | Idade máxima do snapshot antes de bloquear |
| `unlockOnAuthorizedOnly` | Só acionar relé/webhook em `AUTHORIZED` |
| `accessPointSlug` | Ponto vinculado à device key |

**Permanecem só no appliance** (configurados no `/setup` ou JSON local): relé GPIO, `unlockWebhookUrl`, `httpPort`, PIN de fábrica.

Quando o Identity envia config nova:

1. Grava em `config.json` (ou `config.dev.json` em dev).
2. Recarrega handlers HTTP **sem reiniciar** o processo.
3. Log: `device config applied: debounceMs=… emitDetection=… contingency=…`

### Chave inválida → modo setup automático

Respostas `401` ou `403` com `BRIDGE_DISABLED` em policy sync, device-config, redeem (`/scan`) ou outbox flush disparam:

1. Limpeza de `deviceKey` e `configured: false` no JSON local.
2. Rotas voltam para `/setup` (UI de provisionamento).
3. Log: `device key invalid (…) — setup mode at http://…:3710/setup`.

Provisionar de novo com um **novo** token `clm_…` (ver [Reprovisionar após revogar a chave](#reprovisionar-após-revogar-a-chave)).

## Desenvolvimento no Mac

```bash
cd install/qr-reader-agent
make dev      # sobe com config.dev.json + estado em .dev/
make test     # testes unitários
make homologate  # health (+ scan opcional com QR_URL=…)
```

No macOS use arquivos locais em `.dev/` em vez de `/etc/viaaccess-qr-reader/`.

**Primeiro boot (testar provisionamento QR):** apague ou não crie `config.dev.json`, rode `make dev`, abra `http://127.0.0.1:3710/setup` e provisione com um `clm_…` do admin (`http://localhost:3100`).

**Já configurado** — crie `config.dev.json` (gitignored) e use `make dev` ou `make run` (com `--stdin`).

Homologação rápida com leitor já ativo (`scripts/homologate.sh`):

| Variável | Obrigatório | Função |
|----------|-------------|--------|
| `AGENT_URL` | não (padrão `http://127.0.0.1:3710`) | Base do agent |
| `IDENTITY_URL` | não (padrão `http://localhost:3100`) | Base do Identity |
| `DEVICE_KEY` | não | Testa `GET /api/bridge/device-config` |
| `QR_URL` | não | Simula `POST /scan` com QR dinâmico do PWA |

```bash
export DEVICE_KEY=idb_…
export QR_URL='https://…'
make homologate
```

Saída esperada com tudo ok: `configured: true`, `operationMode: ONLINE`, e scan com `correlationOutcome: AUTHORIZED`.

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

Config remota e ciclo de sync: ver [Operação contínua](#operação-contínua-sync-com-identity).

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
| Device config sync | — | `internal/syncclient` (`GET /api/bridge/device-config`) |
| Provisionamento QR | — | `internal/setup` (`POST /api/setup/provision`) |
| Revogação → setup | — | `internal/server/app.go` (hot reload) |

## Ver também

- Identity: `GET /api/bridge/device-config`, `POST /api/bridge/provision/claim`, `POST /api/bridge/intent/redeem` — OpenAPI em viaaccess-identity
- [identity-qr-bridge.md](https://github.com/vialabs-tec/viaaccess-identity/blob/main/docs/installation/identity-qr-bridge.md)
