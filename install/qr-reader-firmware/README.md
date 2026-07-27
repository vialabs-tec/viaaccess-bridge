# ViaAccess QR Reader firmware (ESP32-S3)

Firmware for the ESP32-S3 based QR Reader appliance. It is a port of
[`qr-reader-agent`](../qr-reader-agent) (Go, Raspberry Pi) to ESP-IDF, keeping the
same HTTP contract, the same `config.json` schema and the same Identity bridge
endpoints. Identity cannot tell the two products apart except through the
`X-ViaAccess-Agent-Version` header.

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
| Status LED | KY-016 RGB, common cathode (optional) |
| Clock | DS3231 module, I2C (optional, see below) |

### Factory pin map

Octal PSRAM claims GPIO 33-37 and SPI flash claims 26-32 on this module, so the
Raspberry Pi BCM map (relay 17, reed 4, REX 18, LED 22/27/23) is unusable here.
The map below also avoids the strapping pins (0, 3, 45, 46), the native USB pair
(19, 20) and the UART0 console (43, 44).

| Function | GPIO | Notes |
|---|---|---|
| Relay | 10 | low-level trigger, 3000 ms pulse |
| Door contact (reed) | 11 | active low, closed door pulls LOW |
| Exit button (REX) | 12 | active low |
| Status LED R / G / B | 4 / 5 / 6 | R stale, G online, B setup |
| Reader UART RX | 17 | to the module TX, 9600 8N1 |
| Reader UART TX | 18 | to the module RX, 9600 8N1 |
| RTC SDA / SCL | 8 / 9 | I2C, DS3231 at 0x68 |

The EP8280L ships in USB HID mode, which is what the Pi uses. Here it has to be in
TTL serial mode at 9600 baud, one scan per line; the Pi's keyboard-wedge path has
no equivalent on this board.

Defaults live in `components/viaaccess_core/include/viaaccess/config.hpp` and can
be overridden per install in the advanced section of `/setup`. The I2C pins are
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

### Wiring the EP8280L

The module ships as a USB HID keyboard ("USB-KBW"), which the S3 cannot host. Two
things are required before the reader works:

1. Switch it to TTL/RS232 output by scanning the configuration barcode from the
   EP8280L manual (or through the vendor SDK over USB, once, from a PC).
2. Wire the TTL header instead of the USB cable: 5 V, GND, module TX to GPIO 17,
   module RX to GPIO 18. Level shifting is unnecessary, the module drives 3.3 V
   logic, but the 5 V supply must be able to source ~150 mA plus the relay.

Until the module is switched over, `POST /scan` still exercises the whole
pipeline, so provisioning and homologation can be validated without the reader.

## Layout

```
CMakeLists.txt              project, firmware version stamp
partitions.csv              16 MB: 2x4 MB OTA app slots + 1.5 MB LittleFS
sdkconfig.defaults          S3 target, octal PSRAM, WDT, brownout, OTA rollback
components/viaaccess_core/  platform-free logic shared with the Go agent
main/                       ESP-IDF application
  app_main.cpp              boot order
  app_state.cpp             single owner of config and /health
  storage.cpp               LittleFS documents, NVS secrets
  config_json.cpp           config.json and Identity payload binding
  wifi_manager.cpp          SoftAP portal, station
  clock_service.cpp         DS3231 at boot, SNTP once online, clock trust
  ds3231_driver.cpp         I2C transport for the battery-backed clock
  http_server.cpp           local API on port 3710
  identity_client.cpp       redeem, claim, policy, device-config, commands
  scan_service.cpp          the single passage pipeline
  qr_reader.cpp             EP8280L over UART
  sync_task.cpp             60 s policy loop + command loop
  relay.cpp                 lock output
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
different partition table produces mount failures that look like bugs.

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
   `http://viaaccess-qr.local:3710/setup` on the LAN.
3. Open `/setup` and paste the provisioning URL or the `clm_` token from the
   dashboard. Provisioning derives the LAN hostname from the access point slug
   (`viaaccess-qr-{slug}.local`).

If the network is wrong or the password changed, five failed association attempts
bring the SoftAP back so no serial cable is needed.

### What the setup form asks for

The provisioning panel shows a single field in the normal path, and reveals the
other two only when they can actually be used:

| Field | When it shows |
|---|---|
| Provisioning URL or token | Always; the one thing a technician must paste |
| Identity URL | Only when the pasted text is not a full URL, since a bare `clm_` token carries no host |
| Factory PIN | Only when `/api/setup` answers `pinRequired`, which needs `setupPin` in `config.json` |

Prefer the full URL from the Identity admin panel: it already carries the host, so
the form stays at one field. If the claim answers with a loopback `identityUrl`
(a dev server behind `APP_URL=localhost`), the appliance keeps the host that
actually worked instead of storing an address it cannot reach.

The advanced block is collapsed on purpose. Opening it sends the whole hardware
map with the request, so an installer who wired something other than the factory
pins should open it; leaving it closed keeps the factory map and, on a
reprovision, whatever pins were already stored. Its fields are filled from
`/api/setup` on load, once, so opening the block on a reprovision does not quietly
push the factory pins over a custom panel.

The page has three tabs, and the wiring fields are the same set in all of them
(one `<template>`, cloned):

| Tab | For |
|---|---|
| Provisionar (QR) | The normal install: paste the claim, wiring optional in the advanced block |
| Manual | An `idb_` device key typed by hand, when no claim is available |
| Fiação | Wiring on an appliance already provisioned, keeping credentials and skipping Identity |

## HTTP surface

| Route | Purpose |
|---|---|
| `GET /health` | Posture, policy freshness, reader stats, last scan |
| `POST /scan` | Passage from an integrator or from homologation |
| `GET /setup`, `GET /wifi` | Technician pages |
| `GET /api/setup` | Current state for the pages (never returns the device key) |
| `POST /api/setup` | Manual configuration with an `idb_` device key |
| `POST /api/setup/provision` | Zero-touch claim with a `clm_` token |
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
the dashboard returning the appliance to setup mode, a network outage blocking
passage with `SYNC_STALE`, and (with the DS3231 fitted) a full power cut of a few
minutes after which `/health` must come back with `clock.source: "RTC"` before any
network is available.

## Persistence

| Data | Where | Why |
|---|---|---|
| `config.json` | LittleFS `/data` | Same schema as the Pi, minus the secrets |
| Policy snapshot | LittleFS `/data` | Restored on boot so `/health` knows policy age |
| Device key | NVS | A dumped filesystem image must not leak credentials |
| Wi-Fi password | NVS | Same reason |

Writes are atomic (temp file plus rename), so a power cut during a save cannot
leave a half-written config.

## Not in this scaffold

Deliberate gaps, listed so nobody assumes parity with the Pi:

- **Offline contingency.** The snapshot is fetched and stored, and the clock trust
  model that contingency depends on is in place, but local ticket verification,
  the nonce store and the outbox are not implemented. Contingency is therefore
  never entered: with Identity unreachable a scan is refused with `SYNC_STALE`
  (fail closed). The switch is `app::kLocalContingencySupported` in
  `main/app_state.hpp`.
- **Door contact, REX button and status LED drivers.** The pins, the config and
  the simulation endpoints exist; the GPIO interrupt handling and the LED state
  machine do not, so `/health` reports `driver: "pending"` for them.
- **OTA download.** The partition table reserves both slots and rollback is on,
  but an `update` command is acknowledged as unsupported rather than silently
  dropped.
- **NVS encryption.** Secrets are isolated in NVS so enabling encryption later
  does not change this code.
- **Never run on hardware.** The firmware builds clean for esp32s3 on ESP-IDF
  v5.5 (1.2 MB app, 71% of the slot free) and the host tests pass, but no board
  has been flashed yet, so nothing below the API contract has been observed in
  practice: PSRAM init, LittleFS on a real partition, the SoftAP portal, TLS to
  Identity, the UART framing and the DS3231 on the I2C bus are all unverified
  against silicon. The register level logic of the clock is unit tested, the
  transport is not.
