# ViaAccess QR Reader Agent (Go)

Daemon de borda para o **appliance ViaAccess QR Reader**: lê QR dinâmico do Identity, faz `POST /api/bridge/intent/redeem`, opcionalmente aciona relé GPIO e webhook de unlock.

Runtime de produção no Raspberry Pi (setup UI, policy sync, OTA, door contact, exit button, systemd).

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

Sem `config.json` com `"configured": true`, o agent sobe em **modo setup** e anuncia mDNS na LAN:

```text
http://viaaccess-qr.local:3710/setup
```

Se `.local` não resolver no celular/notebook, use `http://<ip>:3710/setup`. No claim, o hostname
passa a ser `viaaccess-qr-{slug-do-ponto}` (ex. `viaaccess-qr-entrada-principal.local`). Override
opcional em Configuração avançada (segundo leitor no mesmo ponto) ou `MDNS_HOSTNAME` antes do claim.

Config padrão em produção: `/etc/viaaccess-qr-reader/config.json` (permissões `0600`).

PIN de fábrica (opcional): variável `SETUP_PIN` ou flag `--setup-pin`.

### Fluxo recomendado: QR zero-touch (sem SSH, sem GPIO no formulário)

Um leitor novo é provisionado sem SSH e sem escolher pinos. O mapa elétrico de fábrica
(relé GPIO 17, sensor porta GPIO 4, botão saída GPIO 18, LED R/G/B 22/27/23) é aplicado automaticamente no claim.

```
Admin (Identity)                    Appliance (rede local)
     │                                      │
     │  Provisionar (QR) → clm_… (~15 min)  │
     │ ───────────────────────────────────► │  :3710/setup → cole claim
     │                                      │  POST Identity /api/bridge/provision/claim
     │                                      │  recebe idb_… + defaults operacionais
     │                                      │  grava GPIO de fábrica → ONLINE
```

**1. Admin (Identity)**

1. Admin → ponto → **Leitores (bridge)** (perfis com passagem pelo app, app+QR ou QR no leitor).
2. Informe o nome do leitor → **Provisionar (QR)**.
3. Escaneie o QR ou copie a URL `…/bridge/provision?t=clm_…` (válida por ~15 minutos).

Cada token `clm_…` é **uso único**: provisiona um appliance. Para outro leitor, gere outro QR.

**2. Appliance**

1. Conecte o Pi/leitor na rede (mesma LAN do técnico), com a fiação padrão do produto.
2. Abra `http://viaaccess-qr.local:3710/setup` (fallback: `http://<ip>:3710/setup`).
3. Aba **Provisionar (QR)** → cole a **URL completa** ou só o token `clm_…`.
   - Se colar só `clm_…`, informe também a URL do Identity.
4. **Provisionar** (não é preciso abrir GPIO). Fiação diferente: **Configuração avançada**.
5. O agent **ativa o modo operação automaticamente** (não precisa reiniciar o processo).

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

### Reset manual (trocar de cliente ou reprovisionar)

Se você tem o hardware mas não sabe a qual tenant pertence, ou quer instalá-lo em outro cliente **sem** depender da revogação no admin:

```bash
cd install/qr-reader-agent
sudo ./scripts/reset-setup.sh
```

O script:

1. Mostra `identityUrl`, `deviceId` e `accessPointSlug` atuais (para identificar o vínculo).
2. Pede confirmação (use `--yes` para pular).
3. Para o `viaaccess-qr-agent` (systemd), limpa credenciais em `config.json`, remove `IDENTITY_DEVICE_KEY` / `IDENTITY_URL` de `/etc/viaaccess-qr-reader/env` se existirem, e apaga snapshot/outbox locais do tenant anterior.
4. Reinicia o serviço; o appliance fica em `http://<ip>:3710/setup`.

Opções úteis:

| Flag | Uso |
|------|-----|
| `--yes` | Sem prompt (automação) |
| `--dev` | `config.dev.json` + `.dev/` (Mac, sem systemd) |
| `--no-clear-state` | Mantém `policy-snapshot.json` e outbox |
| `--no-restart` | Só altera arquivos; reinicie o serviço você mesmo |

Desenvolvimento no Mac: `make reset-setup` (equivale a `./scripts/reset-setup.sh --dev --yes`).

Depois do reset, gere um **novo** `clm_…` no admin do cliente destino e provisione em `/setup`.

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
| 1. Policy | `GET /api/bridge/policy-snapshot` | Atualiza grants, `ticketVerify` e `edgePolicy` (regras ViaAccess para contingência) em `policy-snapshot.json` |
| 2. Device config | `GET /api/bridge/device-config` | Ajusta parâmetros operacionais em `config.json` (ver abaixo) |
| 2b. Remote commands | `GET /api/bridge/commands` (+ ack) | `UNLOCK` / `UPDATE` (OTA); poll adaptativo via `pollAfterMs` (~10s idle / ~2s após enqueue) |
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

**Permanecem só no appliance** (mapa de fábrica no claim, ou **Configuração avançada** / JSON local):
relé GPIO, sensor de porta, Status LED, `unlockWebhookUrl`, `httpPort`, PIN de fábrica.

Mapa de fábrica (zero-touch):

| Função | BCM | Notas |
|--------|-----|--------|
| Relé | 17 | `enabled`, pulso 3000 ms |
| Sensor porta (MC38) | 4 | `activeLow` |
| Botão saída (REX) | 18 | `activeLow`, pino físico 12 |
| LED KY-016 R/G/B | 22 / 27 / 23 | status online / contingência / setup |

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

**Já configurado** — crie `config.dev.json` (gitignored) e use `make run` ou `make dev` (ambos usam `.dev/` + `--stdin` para leitor USB).

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
| `MDNS_ENABLED` | `true` (padrão) anuncia `hostname.local` na LAN |
| `MDNS_HOSTNAME` | Padrão `viaaccess-qr` → `http://viaaccess-qr.local:3710/setup` |
| `STATUS_LED_ENABLED` | `true` para módulo KY-016 (SETUP / ONLINE / SYNC_STALE) |
| `STATUS_LED_RED_PIN` / `GREEN` / `BLUE` | Canais R/G/B do KY-016 em `gpiochip0` (padrões 22 / 27 / 23) |
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

Scanners HID aparecem como teclado. Sob **systemd**, `--stdin` **não** recebe essas teclas (o serviço não tem o TTY do USB). O unit de produção usa `--hid-auto`, que abre `/dev/input/by-id/*-event-kbd` com `EVIOCGRAB`.

O agent redescobre o HID periodicamente quando nenhum scanner está disponível e reconecta automaticamente após desconectar/conectar ou trocar o scanner. Em modo auto, qualquer keyboard wedge único é aceito; se houver vários teclados, fixe `HID_SCANNER_DEVICE`.

Após provisionar pelo `/setup` sem reiniciar, o agent também sobe o HID automaticamente. Com binário antigo, reinicie o serviço:

```bash
sudo systemctl restart viaaccess-qr-agent
journalctl -u viaaccess-qr-agent -n 50 --no-pager
# espere: hid scanner active on /dev/input/...  ou  scan inputs active (... hid=...)
```

```bash
# Produção / Pi (systemd já passa --hid-auto)
./bin/viaaccess-qr-agent --config ./config.json --hid-auto

# Fixar dispositivo (se houver vários teclados)
./bin/viaaccess-qr-agent --config ./config.json \
  --hid-device /dev/input/by-id/usb-*-event-kbd

# Dev no Mac / pipe
./bin/viaaccess-qr-agent --config ./config.json --stdin
```

O usuário do serviço (`viaaccess`) precisa do grupo `input` (o `install.sh` e o unit com `SupplementaryGroups=input` já fazem isso).

Se `hid-auto` falhar com “multiple keyboards”, liste e fixe:

```bash
ls -l /dev/input/by-id/*-event-kbd
# em /etc/viaaccess-qr-reader/env:
# HID_SCANNER_DEVICE=/dev/input/by-id/usb-SEU_LEITOR-event-kbd
```

### Catraca / controlador HTTP

```bash
curl -s -X POST http://127.0.0.1:3710/scan \
  -H 'Content-Type: application/json' \
  -d '{"qrUrl":"https://identity.cliente/r/clxyz?t=AbCd"}'
```

## Relé GPIO (Linux)

Com `relay.enabled: true` no config, o agent pulsa a linha em `gpiochip0` no offset configurado (`relayGpioPin`). Em Raspberry Pi, confira o offset com `gpioinfo` (BCM 17 nem sempre é offset 17).

O relé só dispara quando o redeem retorna `correlationOutcome: AUTHORIZED` (padrão `unlockOnAuthorizedOnly: true`), **ou** no botão de saída (REX) / comando remoto `UNLOCK`. Regras do ViaAccess no ponto, como **`after_hours`**, bloqueiam a autorização fora do horário (online e offline via `edgePolicy` no policy snapshot). O mesmo vale para VACP em `authorized_entry`. O REX **não** passa por validação de membro.

Em desenvolvimento (macOS) ou sem GPIO, usa driver simulado (log).

## Sensor de porta MC38 (reed / door contact)

Com `doorContact.enabled: true`, o agent observa um reed switch NF (ex.: MC38) e reporta
`opened` / `closed` / `held_open` ao Identity (`POST /api/bridge/door-contact/events`).

Padrão: **GPIO 4** (pino físico 7), GND no pino 9, `activeLow: true`
(porta fechada = contato fechado = linha em LOW com pull-up).
Evite os pinos do relé (17), do reed (4), do botão de saída (18) e do LED KY-016 (22/27/23).

| MC38 | Pi (BCM) | Physical |
|------|----------|----------|
| COM / um fio | GPIO 4 | pin 7 |
| outro fio | GND | pin 9 (ou 6 / 14 / 20) |

Opcional: resistor série ~1kΩ no fio do GPIO. Dupont / Wago / borne.
Para case com borne externo (parafusável como o módulo de relé), use o layout
[`docs/appliance-io-panel.md`](docs/appliance-io-panel.md) + SVG 1:1
[`docs/door-terminal-board.svg`](docs/door-terminal-board.svg).

Config JSON:

```json
"doorContact": {
  "enabled": true,
  "gpioPin": 4,
  "activeLow": true,
  "debounceMs": 50,
  "heldOpenAfterMs": 60000,
  "simulated": false
}
```

Env: `DOOR_CONTACT_*` ainda sobrescreve no boot (avançado). No caminho zero-touch o claim
já habilita o sensor no GPIO 4; use **Configuração avançada** no `/setup` só para pinos ou simulação.

Homologação sem hardware: marque **Simular** no setup, ou `simulated: true` / `POST /api/door-contact/sim`.

```bash
curl -s -X POST http://127.0.0.1:3710/api/door-contact/sim -d '{"state":"open"}'
curl -s -X POST http://127.0.0.1:3710/api/door-contact/sim -d '{"state":"closed"}'
```

`/health` inclui `doorContact: { enabled, state, simulated }`.

## Botão de saída (REX / Request-to-Exit)

Com `exitButton.enabled: true`, um botão momentâneo **do lado de dentro** abre a porta
sem QR: notifica Identity e pulsa o **mesmo** relé `LOCK`.

Fluxo:

1. Debounce do press (GPIO) → evento `pressed`
2. `POST /api/bridge/exit-button/events` `{ kind, at }` — Identity abre janela de graça
   (o `opened` do reed seguinte **não** deve gerar alerta de invasão)
3. Pulso do relé (e webhook de unlock, se configurado) — funciona mesmo se o Identity estiver offline

Padrão: **GPIO 18** (pino físico 12), GND compartilhado, `activeLow: true`
(botão para GND = pressionado = LOW com pull-up).
Evite os pinos do relé (17), do reed (4) e do LED KY-016 (22/27/23).

| Botão | Pi (BCM) | Physical |
|-------|----------|----------|
| um fio | GPIO 18 | pin 12 |
| outro fio | GND | pin 6 / 9 / 14 / 20 |

Config JSON:

```json
"exitButton": {
  "enabled": true,
  "gpioPin": 18,
  "activeLow": true,
  "debounceMs": 50,
  "cooldownMs": 3000,
  "simulated": false
}
```

Env: `EXIT_BUTTON_*`. Zero-touch já habilita no GPIO 18; use **Configuração avançada** no `/setup` para pinos ou simulação.

Homologação:

```bash
curl -s -X POST http://127.0.0.1:3710/api/exit-button/sim -d '{"state":"pressed"}'
curl -s -X POST http://127.0.0.1:3710/api/exit-button/sim -d '{"state":"idle"}'
```

`/health` inclui `exitButton: { enabled, state, simulated, gpioPin }`.

**Identity (necessário):** endpoint `POST /api/bridge/exit-button/events` deve registrar a saída
e abrir a mesma janela de correlação usada após redeem/`UNLOCK`, para que
`door-contact` `opened` não dispare alerta de invasão. `held_open` continua válido.
Detalhes: [`docs/exit-button-identity.md`](docs/exit-button-identity.md).

## Status LED KY-016 (SETUP / ONLINE / SYNC_STALE)

Módulo **KY-016** (RGB 5mm, cátodo comum, resistores 1kΩ na placa). Com `STATUS_LED_ENABLED=true`:

| Modo | Canal KY-016 | Padrão |
|------|--------------|--------|
| `ONLINE` | **G** verde | Fixo |
| `SYNC_STALE` | **R** vermelho | Fixo |
| `CONTINGENCY` | **R** vermelho | Pisca |
| `SETUP` | **B** azul | Pisca |

Ligação padrão:

| KY-016 | Pi (BCM) | Physical |
|--------|----------|----------|
| GND | GND | pin 6 |
| R | 22 | pin 15 |
| G | 27 | pin 13 |
| B | 23 | pin 16 |

Sem GPIO (Mac), o driver simulado só registra no log. `/health` inclui `statusLed` (`module: KY-016`).

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

## Install no Pi (day 1)

No Mac (cross-compile):

```bash
make release   # gera bin/viaaccess-qr-agent-linux-arm64
```

No Pi (copiar o repo ou só `bin/` + `scripts/` + `systemd/`):

```bash
sudo ./scripts/install.sh --binary bin/viaaccess-qr-agent-linux-arm64
# com LEDs de estado:
sudo ./scripts/install.sh --binary bin/viaaccess-qr-agent-linux-arm64 --enable-status-led
```

O script:

1. Instala o binário em `/var/lib/viaaccess-qr-reader/bin/viaaccess-qr-agent` (dono `viaaccess`, necessário para OTA)
2. Symlink em `/usr/local/bin/viaaccess-qr-agent`
3. Cria usuário `viaaccess`, dirs em `/etc/viaaccess-qr-reader/`
4. Habilita `viaaccess-qr-agent.service` + `viaaccess-qr-agent-health.service` (health no boot)
5. Sobe o serviço (`http://<ip>:3710/setup` se ainda não provisionado)

### Fleet OTA

Com Identity configurado (`BRIDGE_OTA_VERSION`, `BRIDGE_OTA_DOWNLOAD_URL`, `BRIDGE_OTA_SHA256`), o admin enfileira **Atualizar software** no painel. O agent:

1. Faz poll de `UPDATE` com `{ version, url, sha256 }`
2. Baixa o binário (HTTPS), verifica SHA-256, troca o arquivo sob `/var/lib/…` e faz ack
3. Sai com código 0; systemd `Restart=always` sobe a nova versão

Rede do cliente: saída HTTPS para o Identity **e** para o host do artifact (CDN/GitHub Releases). Sem VPN/SSH.

Publique um release e configure o Identity:

```bash
make VERSION=1.4.0 release
shasum -a 256 bin/viaaccess-qr-agent-linux-arm64
# suba o binário; no Identity:
# BRIDGE_OTA_VERSION=1.4.0
# BRIDGE_OTA_DOWNLOAD_URL=https://…/viaaccess-qr-agent-linux-arm64
# BRIDGE_OTA_SHA256=<hex>
```

Readers já instalados com binário só em `/usr/local/bin` precisam de um `install.sh` desta versão (uma vez) para migrar o path gravável.

Healthcheck no boot: `scripts/healthcheck.sh` (JSON `/health` com `configured` + `operationMode`).

```bash
systemctl status viaaccess-qr-agent viaaccess-qr-agent-health
journalctl -u viaaccess-qr-agent -u viaaccess-qr-agent-health -f
```

## Capacidades

| Área | Pacote |
|------|--------|
| Redeem Identity | `internal/redeem` |
| Debounce + `POST /scan` | `internal/scan` |
| Unlock webhook / GPIO relay | `internal/unlock`, setup relay |
| USB keyboard wedge | `--hid-auto` / `--hid-device` (evdev); `--stdin` for dev pipes |
| Contingency verify | `internal/contingency` |
| Policy sync + flush | `internal/syncclient` |
| Device config sync | `internal/syncclient` (`GET /api/bridge/device-config`) |
| Provisionamento QR | `internal/setup` (`POST /api/setup/provision`) |
| Revogação → setup | `internal/server/app.go` (hot reload) |
| Status LED | `internal/statusled` |
| Door contact (MC38) | `internal/doorcontact` |
| Exit button (REX) | `internal/exitbutton` |
| Fleet OTA | `scripts/install.sh`, Identity enqueue |
| Install + health boot | `scripts/install.sh`, `*-health.service` |

## Ver também

- Identity: `GET /api/bridge/device-config`, `POST /api/bridge/provision/claim`, `POST /api/bridge/intent/redeem`, `POST /api/bridge/door-contact/events`, `POST /api/bridge/exit-button/events` — OpenAPI em viaaccess-identity
- [identity-qr-bridge.md](https://github.com/vialabs-tec/viaaccess-identity/blob/main/docs/installation/identity-qr-bridge.md)
