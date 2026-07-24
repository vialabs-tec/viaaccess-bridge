# Endurecimento de campo (armazenamento + energia)

Como operar o appliance QR em campo sem depender de SD genérico e fonte fraca.
O binário do agent (`install.sh`, OTA em `/var/lib/…`) **não muda** entre os perfis abaixo — o que muda é a imagem Linux e a alimentação.

## Fases

| Fase | Hardware | Storage | Energia |
|------|----------|---------|---------|
| **A — Piloto** | Pi 4 / Pi 5 + Bookworm 64-bit | SD industrial + menos writes + watchdog | Fonte 5 V sobrada; 12 V da trava separado |
| **B — Produto** | CM4 / CM5 + carrier | eMMC + root RO + data gravável | Mesmo + UPS curto com shutdown limpo |

GPIO de fábrica (relé 17, porta 4, REX 18, LEDs) permanece igual; ver [appliance-io-panel.md](./appliance-io-panel.md).

---

## Perfil A — Pi 4/5 + Raspberry Pi OS Bookworm (64-bit)

### Imagem e cartão

1. Imagem: **Raspberry Pi OS Lite (64-bit), Bookworm**.
2. Cartão: **industrial / high-endurance** (SanDisk Industrial, Swissbit, etc.), ≥ 16 GB. Evitar no-name.
3. Flash com Raspberry Pi Imager; habilite SSH só se o técnico precisar (produção prefere só LAN setup UI).

### `config.txt` (boot)

Em `/boot/firmware/config.txt` (Bookworm) ou `/boot/config.txt` (imagens antigas):

```ini
# Hardware watchdog (BCM)
dtparam=watchdog=on

# Evita under-voltage fantasma em cabos longos (ajuste se usar HAT UPS)
# avoid_warnings=1
```

Reinicie após alterar.

### Swap off

Swap no SD acelera desgaste e corrupção.

```bash
sudo dphys-swapfile swapoff || true
sudo systemctl disable --now dphys-swapfile || true
# ou:
sudo swapoff -a
sudo sed -i 's/^CONF_SWAPSIZE=.*/CONF_SWAPSIZE=0/' /etc/dphys-swapfile
```

Confirme: `free -h` sem Swap usado de forma persistente.

### Journal em RAM / teto de disco

```bash
sudo mkdir -p /etc/systemd/journald.conf.d
sudo tee /etc/systemd/journald.conf.d/viaaccess.conf >/dev/null <<'EOF'
[Journal]
Storage=volatile
RuntimeMaxUse=32M
SystemMaxUse=32M
EOF
sudo systemctl restart systemd-journald
```

`Storage=volatile` perde logs no reboot (aceitável em appliance; diagnóstico via `/health` + Identity).

### Watchdog systemd

Com `dtparam=watchdog=on` e `/dev/watchdog` presente:

```bash
sudo mkdir -p /etc/systemd/system.conf.d
sudo tee /etc/systemd/system.conf.d/viaaccess-watchdog.conf >/dev/null <<'EOF'
[Manager]
RuntimeWatchdogSec=15s
RebootWatchdogSec=3min
EOF
sudo systemctl daemon-reexec
```

Se o kernel travar, o SoC reinicia sozinho.

### fsck no boot

Garanta que a partição root tem `pass` ≥ 1 em `/etc/fstab` (padrão Bookworm já costuma ter). Após brownout, o primeiro boot repara o que der.

### Overlay root (opcional no piloto)

Root 100% read-only é o ideal, mas **exige** partição/overlay gravável para:

| Path | Quem escreve |
|------|----------------|
| `/etc/viaaccess-qr-reader/` | claim, `config.json`, `env` |
| `/var/lib/viaaccess-qr-reader/` | binário OTA, snapshot, outbox |

Sem planejar isso, **não** ligue `overlayroot` / “Make Read-only” do raspi-config: o setup UI e a OTA quebram.

Caminho seguro no piloto: **não** usar overlay ainda; só SD industrial + swap off + journal volátil + watchdog. Deixe overlay para o perfil B.

### Script (aplica o mínimo do perfil A)

No appliance, a partir do repo / pacote de install:

```bash
sudo ./scripts/harden-os.sh
```

Idempotente. Não altera `install.sh` nem o unit do agent.

### Install do agent (igual ao day 1)

```bash
sudo ./scripts/install.sh --binary bin/viaaccess-qr-agent-linux-arm64 --enable-status-led
```

Nada muda nos paths: config em `/etc/viaaccess-qr-reader/`, binário OTA em `/var/lib/viaaccess-qr-reader/bin/`.

---

## Perfil B — CM4 / CM5 + eMMC (produto)

Mesmo agent `linux-arm64`. Troca a placa, não o software.

### Hardware

| Item | Indicação |
|------|-----------|
| Módulo | CM4 ou CM5 **com eMMC** (não Lite+SD se for catálogo) |
| Carrier | Placa própria ou IO Board; mesmos BCM 4 / 17 / 18 / 22 / 27 / 23 |
| Boot | eMMC; sem cartão no campo |
| Watchdog | `dtparam=watchdog=on` + `RuntimeWatchdogSec` (igual perfil A) |

### Layout de disco sugerido

```text
p1  boot   FAT
p2  rootA  ext4   (sistema A — OTA de OS futuro)
p3  rootB  ext4   (sistema B — opcional; RAUC/Mender depois)
p4  data   ext4   montado em /var/lib/viaaccess-qr-reader e bind de /etc/viaaccess-qr-reader
```

Mínimo viável sem A/B de OS ainda:

- root em eMMC (ext4)
- **data** dedicada para `/var/lib/viaaccess-qr-reader` + `/etc/viaaccess-qr-reader` (bind mounts)
- root pode ir read-only depois que os binds existirem

Exemplo `/etc/fstab` (ajuste UUIDs):

```fstab
UUID=…-data  /mnt/viaaccess-data  ext4  defaults,nofail  0  2
/mnt/viaaccess-data/etc   /etc/viaaccess-qr-reader            none  bind,nofail  0  0
/mnt/viaaccess-data/var   /var/lib/viaaccess-qr-reader         none  bind,nofail  0  0
```

Crie os dirs na data **antes** do primeiro `install.sh`.

### OTA

| Camada | Hoje | Depois |
|--------|------|--------|
| Agent | Fleet OTA Identity (binário) | Mantém |
| OS | Flash manual / imagem golden | RAUC / Mender / balena (A/B) |

Não misture OTA de OS com a OTA do agent no mesmo arquivo; o agent só troca o binário sob `/var/lib/…`.

### `install.sh` no CM

Igual ao Pi. Confirme `uname -m` → `aarch64` e use o mesmo `viaaccess-qr-agent-linux-arm64`.

---

## Energia (A e B)

### Rails

```text
AC / PSU ──► 5 V ≥ 3 A (Pi 4) / conforme SoC ──► Pi / CM
AC / PSU ──► 12 V trava ──► LOCK NO/COM (módulo relé)     ← circuito separado
GPIO 17 ──► IN do módulo (sinal seco / TTL do módulo)
```

Não alimentar o Pi a partir da mesma gambiarra 12 V sem DC-DC dimensionado e filtrado.

### Cabo e fonte

- Fonte regulada 5 V com margem de corrente; cabo USB-C **curto**.
- Evitar hub USB barato entre fonte e Pi.
- Trava e bobina só no relé; pigtails GPIO **3–5 cm** ([appliance-io-panel.md](./appliance-io-panel.md)).

### UPS / brownout (recomendado a partir do 2º lote)

1. HAT ou módulo UPS 5 V com GPIO de “power failing”.
2. Serviço que, no sinal, roda `systemctl poweroff` (ou `shutdown -h now`).
3. Segure 30–60 s de autonomia — só para desligar limpo, não para operar a porta sem luz.

Exemplo mínimo (ajuste o GPIO do HAT):

```bash
# /etc/systemd/system/viaaccess-ups-shutdown.service — placeholder
# Dispare via gpio-script do fabricante do HAT; não invente pino sem datasheet.
```

### Sintomas de energia fraca

- Relâmpago / under-voltage no kernel (`Undervoltage detected`)
- SD corrompe após “apagão”
- Agent reinicia em loop na hora do pulso da trava → fonte 5 V fraca ou GND compartilhado ruim

---

## Checklist de campo

```text
Perfil A (piloto)
□ Bookworm Lite 64-bit
□ SD industrial
□ dtparam=watchdog=on
□ swap desligado
□ journald volatile / teto baixo
□ RuntimeWatchdogSec ativo
□ Fonte 5 V sobrada; 12 V trava separado
□ sudo ./scripts/harden-os.sh
□ sudo ./scripts/install.sh --binary … [--enable-status-led]

Perfil B (produto)
□ CM + eMMC (não SD de catálogo)
□ Partição data + binds para /etc e /var/lib viaaccess
□ Mesmo harden (swap/journal/watchdog) + UPS shutdown
□ Mesma pinagem de fábrica + bornes LOCK/DOOR/EXIT
□ Imagem golden documentada; OTA agent via Identity
```

## O que NÃO muda no agent

- Paths de config e OTA
- Unit systemd `viaaccess-qr-agent.service`
- Claim zero-touch / mDNS / GPIO de fábrica
- Artifact `viaaccess-qr-agent-linux-arm64`

Só a imagem e a alimentação mudam entre piloto e produto.
