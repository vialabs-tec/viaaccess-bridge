# ViaAccess QR Reader firmware (ESP32-S3)

Firmware for the ESP32-S3 based QR Reader appliance. It is a port of
[`qr-reader-agent`](../qr-reader-agent) (Go, Raspberry Pi) to ESP-IDF, keeping the
same HTTP contract, the same `config.json` schema and the same Identity bridge
endpoints. Identity cannot tell the two products apart except through the
`X-ViaAccess-Agent-Version` header. Authenticated bridge calls also send
`X-ViaAccess-Relay-Pulse-Ms` so Identity can align unlock countdown and
door-contact windows with the local `relay.pulseMs` from `/setup`.

Why a second implementation instead of cross-compiling the agent: the S3 has no
Linux, no USB host and no Ethernet MAC, so the three platform-bound layers had to
be rewritten (Wi-Fi provisioning, GPIO through the IDF drivers, the scanner over
TTL UART instead of USB HID). Everything above those layers, the passage
decision, is shared logic and lives in `components/viaaccess_core`, unit tested on
the host.

## Hardware

| Item | Part |
|---|---|
| Board | ESP32-S3 N16R8 (16 MB flash, 8 MB **octal** PSRAM) |
| Reader | EP8280L barcode module, **TTL mode** |
| Lock | Relay module, 5 V coil, dry contact to the strike |
| Door sensor | MC38 reed switch (optional) |
| Exit button | Momentary REX button (optional) |
| Status LED | DevKit onboard WS2812 (default); KY-016 RGB optional |
| Buzzer | Active 5 V buzzer via transistor (optional) |
| Clock | DS3231 module, I2C (optional, see below) |

Pilot installs without a full custom PCB: see [carrier v0](docs/carrier-v0.md)
and the [case v0](hardware/case-v0/) enclosure skeleton (FreeCAD / OpenSCAD).
(field screw terminals vs internal module headers, 5 V star, discrete NPN drivers).

### Factory pin map

Octal PSRAM claims GPIO 33-37 and SPI flash claims 26-32 on this module, so the
Raspberry Pi BCM map (relay 17, reed 4, REX 18, LED 22/27/23) is unusable here.
The map below also avoids the strapping pins (0, 3, 45, 46), the native USB pair
(19, 20) and the UART0 console (43, 44).

| Function | GPIO | Notes |
|---|---|---|
| Relay | 10 | low-level trigger, **15000 ms** pulse by default (matches Identity app countdown). Appliances that already saved NVS keep the old value until `/setup` is saved again. |
| Door contact (reed) | 11 | active low, closed door pulls LOW |
| Exit button (REX) | 12 | active low; **disabled by default** until enabled in Fiação |
| Status LED | GPIO 38 (WS2812 onboard) | KY-016 on 4/5/6 optional |
| Buzzer | 7 | 3-pin module, low-level trigger on I/O |
| Reader UART RX | 17 | to the module TX, 9600 8N1 |
| Reader UART TX | 18 | to the module RX, 9600 8N1 |
| RTC SDA / SCL | 8 / 9 | I2C, DS3231 at 0x68 |

The EP8280L ships in USB HID mode, which is what the Pi uses. Here it has to be in
TTL serial mode at 9600 baud, one scan per line; the Pi's keyboard-wedge path has
no equivalent on this board.

Defaults live in `components/viaaccess_core/include/viaaccess/config.hpp` and can
be overridden per install on the **Fiação** tab of `/setup`. The I2C pins are
read at boot, so changing them takes effect after a reset.

### Battery-backed clock (DS3231)

Optional part, and the firmware is built to run with or without it. What it buys:
the ESP32-S3 has no clock of its own, so after a power cut it counts from the
epoch until SNTP answers. Every offline decision is time dependent (the passage
ticket expires 45 s after issue, the policy snapshot has a maximum age, and the
audit timestamp queued for Identity is what will be recorded), and a clock running
behind real time makes an expired ticket look valid.

So the trust model is explicit: `clock.source` in `/health` is `NONE`, `RTC` or
`NETWORK`, and with `NONE` the offline path is refused even when the snapshot is
fresh. Without the module the appliance works normally while the network is there
and simply blocks passage after a power cut until SNTP returns.

Wiring: `VCC` to 3.3 V, `GND`, `SDA` to GPIO 8, `SCL` to GPIO 9. The ZS-042 module
carries its own 4.7k pull-ups. Its charging circuit expects a **rechargeable
LIR2032**; fitting a CR2032 without removing the charging resistor is the one
mistake that ruins these boards. `SQW` and `32K` are unused.

The oscillator stop flag is read at every boot, so a dead cell shows up as
`clock.rtc.oscillatorStopped` in `/health` and as a warning on `/setup` instead of
a wrong time being trusted. Each SNTP sync writes the corrected time back into the
chip, which is what keeps the drift bounded and the flag clear.

### Wiring the relay

`/setup` → **Fiação** picks how long the coil stays active after authorization
(`relay.unlockMode` + `relay.pulseMs`):

| Tipo de trava | `unlockMode` | Default `pulseMs` | Behaviour |
|---|---|---|---|
| Fechadura elétrica / strike | `pulse` | 500 | Short solenoid pulse |
| Eletroímã / maglock (fail-safe) | `hold` | 15000 | Timed unlock window (factory default) |
| Manter até fechar | `until_closed` | 30000 | Stay unlocked until reed open→close; value is max safety timeout |

Identity still reads `X-ViaAccess-Relay-Pulse-Ms` for the app countdown and
`door_opened` window (for `until_closed`, that header is the max timeout).

The trigger polarity describes the board, it is not a safety preference, and it is
the one detail that fails dangerously when it disagrees with the hardware. A module
that switches on a **low** input (`IN` pulled to GND, the usual opto-isolated board
and what the factory default assumes) idles with the GPIO HIGH and the coil off.
Leave *Módulo aciona em nível alto* checked on such a board and the idle level
inverts: `IN` sits at GND for as long as the appliance is powered and idle, the
coil stays energized and the door is released, with the "pulse" being the only
moment it locks.

Do not assume `IN` is pulled up on the module. The reference board is not: with the
wire off the GPIO it energized on its own. So the coil closes during the boot
window before `relay.cpp` configures the pin, after a crash that reverts the GPIO
to an input, and if the wire ever works loose in the panel. Fit a **10k from `IN`
to 3.3 V**, which holds the line at the idle level whenever nothing is driving it
and costs nothing when the firmware is.

Wiring: `VCC` to **5 V**, `GND` shared with the board, `IN` to GPIO 10. No level
shifter is needed on a low-triggered module, since the GPIO only sinks the trigger
current to GND. Budget ~190 mA for the coil while energized; with the EP8280L on
the same rail, USB power from a laptop is marginal and a proper 5 V supply belongs
in the panel.

Two traps on where that 5 V comes from, both confirmed on the bench with an
ESP32-S3 N16R8 clone. The header pin silkscreened `5V in` **fed nothing outward**:
the module was completely dead on it and came alive on `3V3`, because the pin sits
behind a diode into the regulator so an external supply cannot backfeed USB. Check
the pin before blaming the module, and use the module's own LED as the indicator,
since these boards have no separate power LED. And never leave the coil on `3V3`:
it does energize, which makes it a useful diagnostic, but ~190 mA out of the LDO
next to the radio browned the board out mid-test and reset it.

### Powering in the field

One 5 V supply runs the whole appliance, board and relay module together. The
brownout on the bench was not one supply being asked for too much: the coil was
hanging off `3V3`, the LDO that also feeds the radio. On the 5 V rail those ~190 mA
are unremarkable. And because `5V in` does not source power, the field topology is
the bench one reversed, with the supply feeding that pin instead of being tapped
from it:

| From | To |
|---|---|
| Supply +5 V | Board `5V in`, and the module's `VCC` in parallel from the supply |
| Supply GND | Board `GND` and the module's `GND`, common ground is what the trigger references |
| GPIO 10 | Module `IN`, with the 10k to 3.3 V |

Then drop the USB cable: one source at a time, USB only on the bench for flash and
monitor. Budget ~500 mA steady for the board and the EP8280L, peaking near 850 mA
while the coil is energized, so a 2 A supply leaves real margin. Fit a 470 µF to
1000 µF electrolytic across 5 V and GND close to the board, since the coil's inrush
is the transient that already reset this hardware once.

The lock is the exception and the one place a second supply belongs. A 12 V strike
or maglock takes its own supply, switched by the dry contacts, never the appliance's
5 V: a solenoid pulls amps on engage and would brown the MCU out at exactly the
moment the door is meant to open. A single 12 V panel supply with a 12 V to 5 V
converter for the electronics works as well, as long as the strike hangs off the
12 V side and the appliance only ever sees the converter output.

Trigger polarity says nothing about what a power cut does, which is a separate
decision on the contact side. With no power the coil is de-energized whatever the
polarity is, so the relay rests with `COM` closed on `NC` and open on `NO`. What
the door does then follows from the lock: a fail-secure strike stays locked
without power, a maglock or fail-safe strike releases, which is sometimes what
fire code demands on an escape route. An entrance normally pairs a fail-secure
strike with `COM` plus `NO`, so a cut supply leaves the circuit open and the door
locked. The relay never carries the appliance's own supply.

Test with nothing screwed into the terminals first, watching the module LED and
listening for the click. The idle state is what matters: LED off and silence
between pulses. If the module stays dark even when triggered, it has no power, and
a dark LED at idle proves nothing on its own. Then note that a made-up QR **will
not** move the relay, since
the pulse only follows an `AUTHORIZED` redeem and Identity rejects an unknown
intent. Three things do drive it:

| How | What it proves |
|---|---|
| `POST /api/exit-button/sim` `{"state":"pressed"}` | The GPIO, polarity and pulse width, with no network in the way. Needs the REX simulated flag on. |
| **Abrir porta** in the Identity admin | The `UNLOCK` command loop, ack included |
| A member's rotating QR | The whole path, redeem to strike |

Changing polarity or the simulated flags is a wiring save, which on a provisioned
appliance is the **Fiação** tab of `/setup` (`POST /api/setup/hardware`). It keeps
the device key, the Identity URL and the Wi-Fi credentials, and it never calls
Identity, so a pin can be corrected during an outage without a new claim from the
admin.

### Wiring the door contact (MC38)

Reed switch, normally closed when the door is shut. No polarity: either wire to
**GPIO 11**, the other to **GND**. The firmware enables the internal pull-up, so
closed door = LOW and open door = HIGH (`activeLow: true`). A series ~1 kΩ on the
GPIO leg is optional protection.

| MC38 | ESP32-S3 |
|---|---|
| One wire | GPIO 11 |
| Other wire | GND |

On boot the first reading is seeded without posting, so a door already open does
not look like a forced entry. After debounce (50 ms) the appliance posts
`opened` / `closed` to Identity, and `held_open` once the door has stayed open
past `heldOpenAfterMs` (60 s by default). `/health` reports
`doorContact.state` as `open` / `closed` / `unknown`.

Homologation without the reed: **Fiação** → mark *Simular*, then

```bash
curl -s -X POST http://viaaccess-qr-<slug>.local:3710/api/door-contact/sim \
  -H 'Content-Type: application/json' -d '{"state":"open"}'
curl -s -X POST http://viaaccess-qr-<slug>.local:3710/api/door-contact/sim \
  -H 'Content-Type: application/json' -d '{"state":"closed"}'
```

The sim endpoint only flips the virtual reed; the watcher still debounces and
POSTs, so `held_open` fires the same way as with hardware.

### Wiring the exit button (REX)

Momentary button on the secure side of the door. One leg to **GPIO 12**, the other
to **GND**. Internal pull-up, `activeLow: true` (LOW = pressed). Debounce 50 ms,
cooldown 3000 ms so a held or bouncing press cannot re-fire.

On a stable press the appliance notifies Identity (`POST /api/bridge/exit-button/events`)
to open the same grace window as redeem / UNLOCK, then pulses the relay. Egress is
local-first: a failed notify still unlocks; only a revoked device key aborts before
the pulse. Boot seeds without unlocking, so a stuck button does not open the door.

Homologation without the button: **Fiação** → *Simular* on the REX block, then

```bash
curl -s -X POST http://viaaccess-qr-<slug>.local:3710/api/exit-button/sim \
  -H 'Content-Type: application/json' -d '{"state":"pressed"}'
# release so the next press can arm again
curl -s -X POST http://viaaccess-qr-<slug>.local:3710/api/exit-button/sim \
  -H 'Content-Type: application/json' -d '{"state":"idle"}'
```

### DevKit BOOT button (GPIO 0)

The ESP32-S3-DevKitC-1 **BOOT** button is sampled after app start (strapping is
not touched during reset). Gestures are exclusive in time:

| Gesture | Action |
|---|---|
| 1 click | Announce posture: success beep = `ONLINE`, fail beeps = setup / contingency / stale |
| 2 clicks | Synthetic REX — only when **Fiação** has REX enabled **and** Simular checked |
| 3 clicks | Force SoftAP `viaaccess-qr-setup` immediately (join and open `http://192.168.4.1:3710/setup` or `/wifi`) without clearing the device key. Required to change setup after the reader is provisioned (LAN/STA writes are rejected). SoftAP auto-closes in ~10 min if STA stays up. |
| Hold 2 s | Warning cue (keep holding) |
| Hold 5 s | Factory reset: clear Identity credentials **and** Wi-Fi, then reboot into SoftAP |

REX (GPIO 12 and BOOT) is **off by default**. Enable it under `/setup` → **Fiação**
when a physical exit button is wired; turn on Simular for bench / BOOT 2-click unlock.
Do not use BOOT as the production exit button; field REX stays on GPIO 12. The
RST button only resets the chip and is not read by firmware.

### Status LED (onboard WS2812 by default)

The ESP32-S3-DevKitC-1 already has an addressable RGB LED. The appliance uses it
for status so the external KY-016 module is optional.

| Board revision | WS2812 GPIO |
|---|---|
| DevKitC-1 **v1.1** (default) | **38** |
| DevKitC-1 v1.0 | 48 |

| Mode | Color | Pattern |
|---|---|---|
| `ONLINE` | Green | Solid |
| `SYNC_STALE` | Red | Solid |
| `CONTINGENCY` | Red | Blink |
| `SETUP` | Blue | Blink |

`/health` reports `statusLed.module: "WS2812"` (or `"KY-016"`). `/setup` → **Fiação**
can switch driver, GPIO, and brightness.

Optional **KY-016** (common cathode) if the LED must sit off the DevKit:

| KY-016 | ESP32-S3 |
|---|---|
| GND | GND |
| R | GPIO 4 |
| G | GPIO 5 |
| B | GPIO 6 |

Set driver to `ky016` in `/setup` (or `statusLed.driver` in `config.json`).

### Wiring the buzzer

Common 3-pin active module (`VCC`, `GND`, `I/O`) with the driver on the board.
Factory default is **low-level trigger** (GPIO LOW = on, HIGH = idle), same idea
as the relay module: the inverted setting leaves the line half-driven and causes
idle hiss.

| Module | ESP32-S3 / supply |
|---|---|
| `VCC` | 5 V star in the panel; `3V3` is fine on a USB bench |
| `GND` | GND (common) |
| `I/O` | GPIO 7 |

| Cue | Sound |
|---|---|
| Door **held open** past `heldOpenAfterMs` (default 60 s) | Repeating long beep until the door closes |
| Authorized scan / REX | One short beep |
| Denied / blocked scan | Two short beeps |

The held-open alarm is the main reason to fit a buzzer: Identity still gets
`door_held_open`, and the panel itself calls attention on site. Disable or move
the pin under `/setup` → **Fiação**.

### Wiring the EP8280L

The module ships as a USB HID keyboard ("USB-KBW"), which the S3 cannot host. Two
things are required before the reader works:

1. Switch it to TTL/RS232 output by scanning the configuration barcode from the
   EP8280L manual (or through the vendor SDK over USB, once, from a PC).
2. Wire the TTL header instead of the USB cable: 5 V, GND, module TX to GPIO 17,
   module RX to GPIO 18. Level shifting is unnecessary, the module drives 3.3 V
   logic, but the 5 V supply must be able to source ~150 mA plus the relay.
   On the pilot case the READER (and DOOR / REX / LOCK) screw terminals stay
   **inside**; jackets enter through **PG glands** with a cable tie before the
   block so a pull cannot yank the wire out of the screw — details and pin
   colors in [`docs/carrier-v0.md`](docs/carrier-v0.md).

Until the module is switched over, `POST /scan` still exercises the whole
pipeline, so provisioning and homologation can be validated without the reader.

### BLE proximity beacon

When Identity device-config includes `bleBeacon` (access point BLE proximity
enabled), the ESP32-S3 advertises an iBeacon-compatible manufacturer payload with
ViaAccess company ID `0x5641` ("VA") — not Apple `0x004C`, because iOS
CoreBluetooth hides true iBeacon mfg data from apps (so the member app would
never see the door). UUID / major / minor / measured power come from Identity.
No extra wiring: the radio is on-chip. `/health` reports
`bleBeacon: { advertising, uuid, major, minor }` while enabled, or
`bleBeacon: null` when Identity clears proximity. If NimBLE is unavailable at
build or init time the appliance logs a warning and keeps serving passages
without advertising.

## Layout

```
CMakeLists.txt              project, firmware version stamp
partitions.csv              16 MB: 2x4 MB OTA app slots + 1.5 MB LittleFS
sdkconfig.defaults          S3 target, octal PSRAM, NVS HMAC encryption, OTA rollback
components/viaaccess_core/  platform-free logic shared with the Go agent
main/                       ESP-IDF application
  app_main.cpp              boot order
  app_state.cpp             single owner of config and /health
  ble_beacon.cpp            NimBLE proximity beacon from device-config
  storage.cpp               LittleFS documents, NVS secrets
  config_json.cpp           config.json and Identity payload binding
  wifi_manager.cpp          SoftAP portal, station
  clock_service.cpp         DS3231 at boot, SNTP once online, clock trust
  ds3231_driver.cpp         I2C transport for the battery-backed clock
  http_server.cpp           local HTTP API on port 3710 (+ optional HTTPS :443)
  identity_client.cpp       redeem, claim, policy, device-config, commands
  scan_service.cpp          the single passage pipeline
  qr_reader.cpp             EP8280L over UART
  sync_task.cpp             60 s policy loop + command loop
  relay.cpp                 lock output
  door_contact.cpp          MC38 reed (GPIO or simulated)
  exit_button.cpp           REX button (GPIO or simulated)
  service_button.cpp        DevKit BOOT multi-gesture (status / SoftAP / REX / factory)
  status_led.cpp            Status RGB (WS2812 onboard or KY-016)
  buzzer.cpp                active buzzer feedback (GPIO 7)
  web/                      embedded setup and Wi-Fi pages
scripts/homologate.sh       field checklist against a flashed appliance
test/host/                  host unit tests for viaaccess_core
```

## Toolchain

ESP-IDF **v5.5** or newer (the project uses `esp_netif_sntp` and the v5 UART API).
Once per workstation:

```bash
brew install cmake ninja dfu-util python@3.12
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3
```

`install.sh` builds its own virtualenv, so the system Python version does not
matter as long as `python@3.12` is on the PATH for it to pick up. Every new shell
needs `. ~/esp/esp-idf/export.sh` before `idf.py`.

For a build without touching the workstation (and what CI does), use the official
image instead:

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:release-v5.5 \
  bash -c 'idf.py set-target esp32s3 && idf.py build'
```

Docker Desktop on macOS cannot pass the USB port through, so flashing still needs
a local `esptool.py` (see below).

## Build and flash

```bash
. ~/esp/esp-idf/export.sh
cd install/qr-reader-firmware
idf.py set-target esp32s3        # only the first time, or after a fullclean
idf.py build                     # ~1.2 MB app, 71% of the 4 MB slot free
idf.py -p /dev/cu.usbmodem* erase-flash    # first flash of a board only
idf.py -p /dev/cu.usbmodem* flash monitor
```

`erase-flash` matters on a board that ran other firmware: a stale NVS or a
different partition table produces mount failures that look like bugs. After
moving to HMAC-encrypted NVS, a unit that still has plaintext NVS will erase that
partition on first boot and need `/setup` again (the HMAC eFuse key, once burned,
survives `erase-flash`).

The version is stamped by CMake and reported in `/health` as `agentVersion`,
which is also what Identity records per reader:

```bash
idf.py -DVIAACCESS_FIRMWARE_VERSION=1.0.0 build
```

Release builds come from CI on a `qr-reader-firmware-v*` tag; the artifact
contains the app image, the bootloader and the partition table, which is
everything `esptool.py` needs to flash a unit with no ESP-IDF installed:

```bash
pip install esptool
esptool.py -p /dev/cu.usbmodem* --chip esp32s3 -b 460800 write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0xf000 ota_data_initial.bin \
  0x20000 viaaccess-qr-firmware.bin
```

`ota_data_initial.bin` is not optional on a fresh board: it initializes the OTA
data partition so the bootloader knows to run `ota_0`. Flash mode, size and
frequency are left at `keep` so esptool reads them from the image header instead
of drifting from `sdkconfig.defaults`. All four offsets come from
`flasher_args.json`, which ships in the same artifact.

## Host tests

The shared logic compiles for macOS and Linux with no ESP-IDF installed:

```bash
make -C test/host
```

106 tests cover config normalization, operation mode, debounce, redeem
classification, claim parsing, hostname derivation, UART line framing, command
backoff, RFC 3339 handling, clock trust and the DS3231 register encoding. Most of
them mirror the `_test.go` files of the Go agent, so a behavior change on one
product shows up as a failing test on the other; the clock ones have no
counterpart because the Pi gets its date from the distribution.

## First boot

1. The appliance raises an open SoftAP named `viaaccess-qr-setup`.
2. Join it and open `http://192.168.4.1:3710/wifi`, pick the network and submit.
   The portal drops as soon as the station associates, which is the success
   signal; the appliance is then reachable at
   `http://viaaccess-qr.local:3710/setup` on the LAN (optional `https://…/` on port 443 with `curl -k`).
3. Open `/setup` and paste the provisioning URL or the `clm_` token from the
   dashboard. Provisioning derives the LAN hostname from the access point slug
   (`viaaccess-qr-{slug}.local`).

If the network is wrong or the password changed, five failed association attempts
bring the SoftAP back so no serial cable is needed. Three clicks on the DevKit
**BOOT** button force that portal immediately (see above).

### What the setup form asks for

The provisioning panel shows a single field in the normal path, and reveals the
other two only when they can actually be used:

| Field | When it shows |
|---|---|
| Provisioning URL or token | Always; the one thing a technician must paste |
| Identity URL | Only when the pasted text is not a full URL, since a bare `clm_` token carries no host |
| Setup PIN | After provision: required on every local write. Claim from Identity sets a 6-digit PIN; manual path asks for one on first save. Legacy units without a PIN get `pinSetupRequired` until they set one. |

Prefer the full URL from the Identity admin panel: it already carries the host, so
the form stays at one field. If the claim answers with a loopback `identityUrl`
(a dev server behind `APP_URL=localhost`), the appliance keeps the host that
actually worked instead of storing an address it cannot reach.

GPIO, LED and buzzer live only on the **Fiação** tab (`POST /api/setup/hardware`).
Optional mDNS hostname (`.local`) is on Provisionar, Manual and Fiação.
Provisionar / Manual apply the factory pin map (and keep any pins already stored
on reprovision) without sending GPIO fields. Fiação is filled from `/api/setup`
on load, once, so a refresh does not fight typing.

| Tab | For |
|---|---|
| Provisionar (QR) | Normal install: paste the claim; factory GPIO; optional `.local` hostname |
| Manual | An `idb_` device key typed by hand; optional `.local` hostname |
| Fiação | GPIO / LED / buzzer (+ hostname); keeps credentials and skips Identity |

## HTTP surface

| Route | Purpose |
|---|---|
| `GET /health` | Posture, policy freshness, reader stats, last scan |
| `POST /scan` | Passage from an integrator or from homologation |
| `GET /setup`, `GET /wifi` | Technician pages |
| `GET /api/setup` | Current state for the pages (never returns the device key or setup PIN). Includes `pinRequired` / `pinSetupRequired`. |
| `POST /api/setup` | Manual configuration with an `idb_` device key |
| `POST /api/setup/provision` | Zero-touch claim with a `clm_` token (saves after claim; no second Identity ping) |
| `POST /api/setup/hardware` | Wiring only, no credentials and no Identity round-trip |
| `GET /api/setup/wifi/scan` | Nearby networks, strongest first |
| `POST /api/setup/wifi` | Store credentials and reconnect |
| `POST /api/door-contact/sim` | `{"state":"open"\|"closed"}`, simulation only |
| `POST /api/exit-button/sim` | `{"state":"pressed"\|"idle"}`, simulation only |

One intentional difference from the Pi: there the operational routes are only
registered once the appliance is provisioned, so `/scan` answers 404 in setup
mode. Here it answers `503` with `code: "SETUP_REQUIRED"`, which is easier to read
in the field than a bare 404.

## Homologation

`scripts/homologate.sh` runs the whole checklist. It is read-only unless `QR_URL`
is set, so it is safe against a door in service:

```bash
# Reader still on the SoftAP, before provisioning
READER_URL=http://192.168.4.1:3710 ./scripts/homologate.sh

# Provisioned reader on the LAN, including a real passage and the debounce window
READER_URL=http://viaaccess-qr.local:3710 \
QR_URL='<QR dinâmico do app do associado>' \
  ./scripts/homologate.sh
```

It checks posture (`ONLINE`, Identity reachable, policy not stale), the hardware
reported by `/health` including the clock source and the DS3231 temperature, that
an unknown QR is refused, that a real QR opens the door and that the same QR inside
the debounce window is swallowed locally. `DEVICE_KEY` and `IDENTITY_URL`
additionally dump the Identity device-config.

Four items still need someone at the door and are printed as a reminder at the
end: a scan through the EP8280L itself rather than curl, a device key revoked in
the dashboard returning the appliance to setup mode, a network outage exercising
`CONTINGENCY` (fresh policy + trusted clock) or `SYNC_STALE` when those are
missing, and (with the DS3231 fitted) a full power cut of a few minutes after
which `/health` must come back with `clock.source: "RTC"` before any network is
available.

## Persistence

| Data | Where | Why |
|---|---|---|
| `config.json` | LittleFS `/data` | Same schema as the Pi, minus the secrets |
| Policy snapshot | LittleFS `/data` | Restored on boot so `/health` knows policy age and CONTINGENCY can verify offline |
| Consumed intents | LittleFS `/data` | Anti-replay nonce store for offline tickets |
| Outbox | LittleFS `/data` | Offline passages waiting for Identity flush |
| Device key | NVS (encrypted) | A dumped flash image must not yield usable credentials |
| Wi-Fi password | NVS (encrypted) | Same reason |

Writes are atomic (temp file plus rename), so a power cut during a save cannot
leave a half-written config.

### NVS encryption

The default NVS partition is encrypted with XTS-AES. Keys are derived at runtime
from an HMAC key in eFuse `BLOCK_KEY0` (`CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=0`), so
there is no `nvs_keys` partition and full flash encryption is not required.

On the first boot of an encrypted build, if `KEY0` is empty the firmware
generates an `HMAC_UP` key and burns it into that eFuse block. That burn is
permanent for the chip. Later `erase-flash` clears NVS contents but keeps the
HMAC key, so the same board re-encrypts NVS with the same derived keys.

Upgrading a unit that still has **plaintext** NVS from an older firmware: flash
the new image and power on once. Boot erases NVS when decryption fails, then
reopens it encrypted. Re-run `/setup` (Wi-Fi + device key / claim) afterward.

Optional factory burn of a known HMAC key (skip runtime generation):

```bash
espefuse.py -p /dev/cu.usbmodem* burn_key BLOCK_KEY0 hmac_key_file.bin HMAC_UP
```

## Offline contingency

When Identity is unreachable (redeem timeout / status 0 / 5xx) and the appliance
is in `CONTINGENCY` mode, a scan is validated locally instead of being refused:

1. Parse the QR URL (`/r/{intentId}?st=…`)
2. Verify the HS256 passage JWT against `ticketVerify` from the policy snapshot
3. Check access-point slug, grant version, member grant list and ticket expiry
4. Mark the intent consumed (nonce store) and enqueue an outbox event
5. Pulse the relay / unlock webhook with a synthetic `AUTHORIZED` redeem

`CONTINGENCY` requires `contingency.enabled`, a fresh policy snapshot, and a
trusted clock (`RTC` or `NETWORK`). Without those, the posture stays
`SYNC_STALE` and the scan is refused (fail closed). Client 4xx from online redeem
never falls through to contingency.

When the network returns, the policy sync loop posts pending outbox events to
`POST /api/bridge/contingency/flush` and clears the local queue when Identity
reports `flushed > 0`. `/health` exposes `contingency.localVerify: "ready"` and
`outbox.pending`.

Offline `after_hours` follows the Pi agent: when the policy snapshot carries
`edgePolicy.rules.after_hours`, contingency refuses with `AFTER_HOURS` outside
the allowed window. Known zones (`America/Sao_Paulo`, `UTC`) use a fixed offset
table; unknown timezones fail open, matching Go when `LoadLocation` fails.

## OTA updates

Identity enqueues `UPDATE` with `{ version, url, sha256 }`. The appliance:

1. Skips the download when `version` already matches `agentVersion` (ack ok)
2. HTTPS-downloads the **app image** (`viaaccess-qr-firmware.bin`) into the
   inactive OTA slot while hashing
3. Aborts without changing the boot partition on SHA-256 mismatch or write error
4. On success: points the bootloader at the new slot, acks Identity, reboots
5. On the next boot `esp_ota_mark_app_valid_cancel_rollback()` confirms the image
   after Wi-Fi/HTTP are up; a crash before that rolls back automatically

### Publishing a release

```bash
# on viaaccess-bridge main
VERSION=1.0.0
git tag "qr-reader-firmware-v${VERSION}"
git push origin "qr-reader-firmware-v${VERSION}"
```

CI builds the image, uploads an Actions artifact, and creates a **GitHub Release**
on that tag with `viaaccess-qr-firmware.bin` (+ `.sha256`, flash helpers). Copy
the Identity block from the release notes into `.env`:

```bash
BRIDGE_OTA_VERSION=1.0.0
BRIDGE_OTA_DOWNLOAD_URL=https://github.com/vialabs-tec/viaaccess-bridge/releases/download/qr-reader-firmware-v1.0.0/viaaccess-qr-firmware.bin
BRIDGE_OTA_SHA256=<from release notes or .sha256 asset>
BRIDGE_OTA_URL_ALLOWLIST=https://github.com/vialabs-tec/
```

`BRIDGE_OTA_VERSION` must match the CI-stamped `VIAACCESS_FIRMWARE_VERSION`
(the part after `qr-reader-firmware-v`). This is the only fleet OTA path going
forward.

GitHub release URLs respond with HTTPS **302** to `release-assets.githubusercontent.com`
and a long `Location` header. The OTA client follows redirects explicitly and uses
4 KB HTTP buffers so that hop succeeds; without it the appliance acks
`OTA HTTP 302` and stays on the previous image.

## Local setup security (shipped)

After claim, mutating `/api/setup*` requires:

1. **Setup PIN** (from Identity claim or set on the manual path).
2. **SoftAP portal up** (BOOT 3-click / STA failures / first boot). SoftAP forced
   while STA is up is **held** until TTL (~10 min) so GOT_IP does not kick the phone.
3. **URL on SoftAP** — `http://192.168.4.1:3710/setup` on Wi‑Fi `viaaccess-qr-setup`
   (`Host: 192.168.4.1`). SoftAP uses **HTTP on purpose**: mobile browsers often
   hard-fail self-signed HTTPS on captive SoftAP networks (“site unavailable”).
   Optional HTTPS on `:443` (`https://192.168.4.1/setup`, `curl -k`) for LAN/scripts.
   `*.local` / STA IP stay **read-only** even while SoftAP is active.

## Next evolution

- **Trusted SoftAP TLS** (per-device cert / TOFU in the admin or member app) so
  phones can use HTTPS on SoftAP without a hard fail. Until then HTTP SoftAP +
  PIN + Host gate remain the commissioning path.
- **Drop the `Host: 192.168.4.1` heuristic** once SoftAP TLS identity is strong
  enough that LAN cannot be mistaken for the portal.

## Not in this scaffold

Deliberate gaps, listed so nobody assumes parity with the Pi:

- **Full flash encryption / secure boot.** NVS secrets are encrypted via HMAC;
  the rest of flash (app, LittleFS) is not ciphertext at rest.
- **Full IANA timezone database.** Offline `after_hours` covers the zones ViaAccess
  ships today (`America/Sao_Paulo`, `UTC`); other IANA ids fail open until added
  to the offset table.
