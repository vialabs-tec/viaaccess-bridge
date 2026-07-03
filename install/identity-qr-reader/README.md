# Leitor USB → ViaAccess Identity (QR dinâmico)

**Implementação de referência (Phase 1b), não produto acoplado.** Serve para homologar o contrato `POST /api/bridge/intent/redeem` e copiar o fluxo em produção. Integradores com catraca, SDK de fabricante ou firmware próprio devem adaptar ou reimplementar; a ViaLabs não garante compatibilidade com todo hardware via este script.

Dois modos no mesmo pacote:

| Modo | Comando | Entrada |
|------|---------|---------|
| **USB** (keyboard wedge) | `npm start` | stdin, uma URL por linha |
| **HTTP** (catraca / controlador) | `npm run start:http` | `POST /scan` com JSON |

```
Celular (PWA) → QR na tela → Leitor → bridge → Identity → ViaAccess
```

Não depende do pacote Frigate nem do Docker do bridge principal.

## Pré-requisitos

1. [ViaAccess Identity](https://github.com/vialabs-tec/viaaccess-identity) com ponto em modo **QR dinâmico no leitor**
2. Device key `idb_…` criada no admin do ponto (copie uma vez)
3. **USB:** leitor configurado como teclado (termina com Enter)
4. **HTTP:** controlador da catraca capaz de enviar a URL decodificada do QR para este adapter

## Setup comum

```bash
cd install/identity-qr-reader
cp .env.example .env
# Edite IDENTITY_URL e IDENTITY_DEVICE_KEY

npm install
```

### Modo USB

```bash
npm start
```

Deixe o terminal em foco (ou redirecione stdin). Cada scan envia uma linha com a URL do QR.

### Modo HTTP (catraca)

```bash
npm run start:http
```

O adapter escuta em `HTTP_HOST`:`HTTP_PORT` (padrão `0.0.0.0:3710`).

**Endpoint:** `POST /scan`

```json
{ "qrUrl": "https://identity.cliente/r/clxyz?t=AbCdEfGh" }
```

Aliases aceitos: `qr`, `payload`, ou corpo texto puro com a URL.

**Segurança (recomendado na LAN):** defina `WEBHOOK_SECRET` e envie header `X-Webhook-Secret` no POST.

**Destravar catraca:** se o controlador expõe um webhook local, configure `UNLOCK_WEBHOOK_URL`. Após redeem com `correlationOutcome: AUTHORIZED`, o bridge faz `POST` com:

```json
{
  "memberId": "…",
  "validationId": "…",
  "detectionId": "…",
  "correlationOutcome": "AUTHORIZED",
  "accessPointSlug": "entrada-principal"
}
```

Alternativa sem `UNLOCK_WEBHOOK_URL`: use `EMIT_DETECTION=true` e deixe o **VACP** do ViaAccess acionar `turnstile.unlock` (nível 02).

**Health:** `GET /health` → `{"ok":true}`

## Teste sem hardware

USB:

```bash
echo 'http://localhost:3100/r/clxyz123?t=AbCdEfGhIjKlMnOpQrStUv' | npm start
```

HTTP:

```bash
npm run start:http &
curl -s -X POST http://127.0.0.1:3710/scan \
  -H 'Content-Type: application/json' \
  -d '{"qrUrl":"http://localhost:3100/r/clxyz123?t=AbCdEfGhIjKlMnOpQrStUv"}'
```

## Variáveis

| Variável | Descrição |
|----------|-----------|
| `IDENTITY_URL` | URL do Identity (ex. `http://localhost:3100`) |
| `IDENTITY_DEVICE_KEY` | Chave `idb_…` do leitor |
| `EMIT_DETECTION` | `true` (padrão) emite detection após validation; `false` se houver câmera na porta |
| `DEBOUNCE_MS` | Ignora scans idênticos em sequência (padrão 2000) |
| `HTTP_HOST` | Bind do modo HTTP (padrão `0.0.0.0`) |
| `HTTP_PORT` | Porta do modo HTTP (padrão `3710`) |
| `WEBHOOK_SECRET` | Se definido, exige header `X-Webhook-Secret` |
| `UNLOCK_WEBHOOK_URL` | POST local após redeem autorizado (controlador da catraca) |
| `UNLOCK_ON_AUTHORIZED_ONLY` | `true` (padrão): só chama unlock se `AUTHORIZED` |

## Integração com fabricante

Este pacote **não** fala protocolo proprietário (serial, Wiegand, SDK fechado). O integrador:

1. Configura o firmware/SDK da catraca para enviar a **string do QR** (URL completa) para `POST /scan`, ou
2. Implementa um micro-serviço que traduz o evento do fabricante → mesmo JSON.

O módulo `src/redeem.ts` pode ser importado em adaptadores customizados.

## Raspberry Pi / systemd (USB)

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
