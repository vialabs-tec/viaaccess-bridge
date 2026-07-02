# Leitor USB → ViaAccess Identity (QR dinâmico)

Referência mínima para **Phase 1b**: leitor USB tipo *keyboard wedge* escaneia o QR do celular e chama `POST /api/bridge/intent/redeem` no Identity.

```
Celular (PWA) → QR na tela → Leitor USB → este script → Identity → ViaAccess validation
```

Não depende do pacote Frigate nem do Docker do bridge principal.

## Pré-requisitos

1. [ViaAccess Identity](https://github.com/vialabs-tec/viaaccess-identity) com ponto em modo **QR dinâmico no leitor**
2. Device key `idb_…` criada no admin do ponto (copie uma vez)
3. Leitor USB configurado como teclado (termina a leitura com Enter)

## Setup

```bash
cd install/identity-qr-reader
cp .env.example .env
# Edite IDENTITY_URL e IDENTITY_DEVICE_KEY

npm install
npm start
```

Deixe o terminal em foco (ou redirecione stdin do dispositivo no Linux). Cada scan envia uma linha com a URL do QR.

## Teste sem hardware

Com o PWA aberto na tela do QR, copie a URL ou simule:

```bash
echo 'http://localhost:3100/r/clxyz123?t=AbCdEfGhIjKlMnOpQrStUv' | npm start
```

Saída esperada:

```text
2026-… OK validation=… member=… correlation=…
```

## Variáveis

| Variável | Descrição |
|----------|-----------|
| `IDENTITY_URL` | URL do Identity (ex. `http://localhost:3100`) |
| `IDENTITY_DEVICE_KEY` | Chave `idb_…` do leitor |
| `EMIT_DETECTION` | `true` (padrão) emite detection `identity-qr` após validation; use `false` se houver câmera na porta |
| `DEBOUNCE_MS` | Ignora scans idênticos em sequência (padrão 2000) |

## Raspberry Pi / systemd (opcional)

Exemplo de unit que mantém o script rodando e lê do stdin ligado ao leitor:

```ini
[Unit]
Description=Identity QR USB reader
After=network-online.target

[Service]
WorkingDirectory=/opt/viaaccess-bridge/install/identity-qr-reader
EnvironmentFile=/opt/viaaccess-bridge/install/identity-qr-reader/.env
ExecStart=/usr/bin/npm start
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

No Linux, alguns integradores mapeiam o leitor para `/dev/input` ou usam `evdev`; este script cobre o caso mais comum (HID → stdin).

## Desenvolvimento

```bash
npm test
```

## Ver também

- Identity OpenAPI: `POST /api/bridge/intent/redeem`
- [viaaccess-identity/docs/installation/identity-qr-bridge.md](https://github.com/vialabs-tec/viaaccess-identity/blob/main/docs/installation/identity-qr-bridge.md)
