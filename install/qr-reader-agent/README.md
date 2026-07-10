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

## Primeiro boot (setup)

Sem `config.json` configurado, o agent sobe em modo setup:

1. Conecte o appliance na rede
2. Abra `http://<ip>:3710/setup`
3. Informe URL do Identity e device key `idb_…` (admin do ponto → QR dinâmico no leitor)
4. Salve e reinicie o serviço

Config padrão: `/etc/viaaccess-qr-reader/config.json` (permissões `0600`).

PIN de fábrica (opcional): variável `SETUP_PIN` ou flag `--setup-pin`.

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
| `GET` | `/health` | Status (rede, Identity, último scan) |
| `POST` | `/scan` | Body JSON `{ "qrUrl": "…" }` |
| `GET` | `/setup` | UI de provisionamento (só sem config) |

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
| Setup / health / GPIO | — | `internal/setup`, `internal/server`, `internal/relay` |

## Ver também

- Identity: `POST /api/bridge/intent/redeem` — OpenAPI em viaaccess-identity
- [identity-qr-bridge.md](https://github.com/vialabs-tec/viaaccess-identity/blob/main/docs/installation/identity-qr-bridge.md)
