# Botão de saída (REX) — contrato Identity

O appliance ViaAccess QR Reader envia um evento quando o botão físico de saída
(Request-to-Exit) é pressionado:

```http
POST /api/bridge/exit-button/events
Authorization: Bearer idb_…
X-ViaAccess-Exit-Button-Enabled: true
Content-Type: application/json

{ "kind": "pressed", "at": "2026-07-23T12:00:00.000000000Z" }
```

## Semântica esperada no Identity

1. Registrar auditoria `EXIT_REQUEST` no access point do device (sem `memberId`).
2. Abrir **janela de graça** (mesma usada após redeem `AUTHORIZED` / comando `UNLOCK`)
   para que o próximo `POST /api/bridge/door-contact/events` com `kind: opened`
   **não** gere alerta de invasão / abertura forçada.
3. **Não** emitir detecção de entrada autorizada (`authorized_entry`) — saída livre
   não deve alimentar regras Frigate `presence_after_entry` como se fosse entrada.
4. `held_open` do door-contact continua válido após a janela (porta aberta demais).

O agent pulsa o relé **mesmo se este POST falhar** (egresso offline). Idealmente o
Identity aceita o evento com atraso curto e correlaciona pelo timestamp `at`.
