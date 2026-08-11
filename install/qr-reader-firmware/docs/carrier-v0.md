# Carrier v0 (pre-custom PCB)

Board that replaces Dupont spaghetti for **pilot installs**, before a full custom PCB.
Keep the ESP32-S3 DevKit + modules as daughterboards; the carrier does power star,
discrete NPN drivers, headers, and field screw terminals.

Target: one PCB (JLCPCB) that fits inside the case, with **field wires on
screw terminals** and **modules on pin headers**.

KiCad 8 project (board + fab notes): [`hardware/carrier-v0/`](../hardware/carrier-v0/).
Board outline: **90 × 60 mm**, 2-layer. The EP8280L mounts **outside** the case
(door side) and lands on the field **READER** screw terminal.

Enclosure (FreeCAD / OpenSCAD skeleton, PG + DC + USB + standoff checklist):
[`hardware/case-v0/`](../hardware/case-v0/).

**Case rule (pilot standard):** screw terminals stay **inside** the enclosure.
Field cables enter only through **cable glands (prensa-cabo PG)**. Do not mount
live screw blocks on the outside face of the case — a pull on the jacket must
load the gland / case, never the conductor under the screw.

## Design rules

1. One **5 V star**: supply, electrolytic (470–1000 µF), ESP `5V`/`VIN`, relay coil rail,
   buzzer rail, scanner 5 V.
2. Common **GND** everywhere (star, ESP, modules, field returns).
3. Factory GPIO map stays fixed (see README). Do not renumber for v0.
4. Lock/strike power never shares the appliance 5 V rail (dry contacts only).
5. No BSS138 level-shifter module — GPIO drives loads through **2N2222** open-collectors.

## Power inlet (case panel, not a field terminal)

Panel-mount **5.5 × 2.1 mm** DC jack (P4 fêmea com rabicho), embutido na case.
O rabicho termina em housing **XH fêmea**; na placa, **J1 DCIN** é o wafer XH
macho 2 vias (passo **2,5 mm**, mercado “XH2.54”): pino 1 `+5V`, pino 2 `GND`.
Fonte ≥ 2 A em 5 V (ou conversor 12→5 V antes do jack). Centro do plug = +5 V
(padrão mais comum; confirme a fonte).

## Field screw terminals (inside the case)

Use 5.08 mm pluggable blocks on the **carrier field edge**, accessible when the
lid is open. Labels in Portuguese on the silk / label plate. Terminals are **not**
external binding posts.

| Block | Label | Pins | Wire from site | Carrier connects to |
|---|---|---|---|---|
| **LOCK** | `COM` / `NO` (/ `NC` optional) | 2–3 | Strike / maglock circuit (own supply) | Relay module dry contacts |
| **DOOR** | `REED` / `GND` | 2 | MC38 (closed = door shut) | GPIO 11 + GND (pull-up in firmware) |
| **REX** | `REX` / `GND` | 2 | Momentary exit button | GPIO 12 + GND |
| **READER** | `5V` / `GND` / `TX` / `RX` | 4 | EP8280L TTL (outside case / door) | Star 5 V, GND, GPIO 17 (ESP RX), GPIO 18 (ESP TX) |

Notes:

- `LOCK` carries only the strike loop. Never land 12 V strike supply on **J1 DCIN** / the 5 V star.
- Optional third lock pin `NC` only if the site needs fail-safe wiring.
- Prefer ferrules on stranded conductors; torque the screws — that improves contact,
  it does **not** replace strain relief on the cable jacket.

## Case cable entry and strain relief

Standard install for every pilot case:

1. Drill the **cable wall** (opposite or beside the field-edge terminals) for
   nylon **PG** glands — one gland per field cable (or one gland per jacketed
   multi-core; do not share LOCK with the 5 V reader loom).
2. Pass the jacket through the gland, tighten the nut so a firm pull does not
   slide the cable.
3. Inside: leave a short **service loop**, then a nylon cable tie to a standoff,
   boss, or adhesive mount **before** the screw terminal.
4. Land conductors on LOCK / DOOR / REX / READER. Reader shield/drain → `GND`
   at the carrier end only.

Suggested gland sizes (nylon PG, IP68-style, black):

| Cable | Typical OD | Gland |
|---|---|---|
| READER — 4×26 AWG shielded manga (~4.3 mm) | ~4–5 mm | **PG7** (or PG9 if the jacket is thicker) |
| DOOR — 2-core reed | ~3–5 mm | **PG7** |
| REX — 2-core button | ~3–5 mm | **PG7** |
| LOCK — strike loop (site dependent) | often thicker | **PG9** / **PG11** |

Panel layout (cable wall, outside view):

```text
 ┌──────────────────────────────────────────┐
 │  [PG READER] [PG DOOR] [PG REX] [PG LOCK] │  ← jackets enter here
 │                                          │
 │   (DC jack 5.5×2.1 on another face)      │
 │   (USB for flash only — not a field port)│
 └──────────────────────────────────────────┘
```

Inside the same wall, keep glands close to the carrier field edge so each cable
runs a few centimetres to its block:

```text
 glands → service loop → cable tie → screw terminal
```

### READER cable (field pinout)

Shielded 4-core, ≤ 3–5 m (e.g. Tiaflex 4×26 AWG). EP8280L in **TTL/RS232**
9600 8N1 — not USB keyboard / CDC. Cross TX/RX at the terminal.

| Color (4×26 Tiaflex) | Signal | Module | Carrier READER |
|---|---|---|---|
| Red | 5 V | 5V | `5V` |
| Brown | GND | GND | `GND` (+ braid/drain) |
| Orange | TX from reader | TX | `RX` (GPIO 17) |
| Yellow | RX to reader | RX | `TX` (GPIO 18) |

Module end: XH 2.5 mm housing matching the EP8280L header (populate only the
four signal pins if the plug is 8-way), or solder + heat-shrink for early pilots.
Kit USB–serial cable stays on the bench for configuration only.

## Internal headers / sockets (stay in the case)

| Header | Pitch / pins | Module | Carrier nets |
|---|---|---|---|
| **J1 DCIN** | XH 2P wafer 2,5 mm | Rabicho XH fêmea do jack P4 | `+5V`, `GND` → power star |
| **U1 ESP (J8A/J8B)** | Two 1×22, centers 0.9\" apart | ESP32-S3-DevKitC-1 | `5V`, `3V3`, `GND`, GPIO 4–12, 17, 18 on J8A; USB left |
| **J9 RELAY** | 1×3 | 5 V relay module | `VCC`→5 V, `GND`, `IN`← open-collector `Q2` (+ `R3` pull-up to 5 V) |
| **J12 BUZZ** | 1×2 | SFM-20B / 2-wire active | `+`→5 V; `−`← `Q1` collector |
| **J13 LED** | 1×4 | KY-016 (optional) | Only if not using DevKit WS2812; `GND`, R←GPIO4, G←GPIO5, B←GPIO6 |
| **J14 RTC** | 1×4 | DS3231 (ZS-042) | `VCC`→3V3, `GND`, SDA←GPIO8, SCL←GPIO9 |

### Discrete drivers

| Load | GPIO | Parts | `/setup` polarity |
|---|---|---|---|
| Relay `IN` | 10 | R2 1k → Q2 base; Q2 sinks `IN`; R3 10k pulls `IN` to 5 V | Active-high **unchecked** |
| Buzzer `−` | 7 | R1 1k → Q1 base; Q1 sinks buzzer `−` | Active-high **checked** |

## Power star (on carrier copper)

```text
J1 DCIN.+5V ──┬── C1 electrolytic 470–1000 µF ( + to 5V, − to GND )
              ├── C2 100 nF
              ├── U1 ESP 5V / VIN
              ├── J9 RELAY VCC
              ├── J12 BUZZ +
              ├── READER 5V (field)
              └── R3 → RELAY_IN (pull-up)

J1 DCIN.GND ──┬── C1 − / C2
              ├── all module GNDs
              ├── DOOR.GND, REX.GND
              └── Q1 / Q2 emitters
```

`3V3` comes **only** from the ESP module regulator (RTC, optional opto `VCC` on
JD-VCC relays). Do not feed heavy coils from `3V3`.

## Suggested mechanical layout (top view)

```text
 ┌─────────────────────────────────────────────┐
 │  PG glands on cable wall (outside jackets)  │
 │  [LOCK] [DOOR] [REX] [READER]  ← inside     │
 │                                             │
 │  DevKitC-1 (USB← left)     │  Q2/R2 relay  │
 │                            │  Q1/R1 buzz   │
 │  LED RTC RELAY             │  J1 DCIN      │
 │                            │  C1 / C2      │
 └─────────────────────────────────────────────┘
```

## BOM extras (carrier only)

| Item | Qty | Notes |
|---|---|---|
| Carrier PCB | 1 | JLCPCB 2-layer OK |
| Screw terminal 2P | 2 | DOOR, REX |
| Screw terminal 2P or 3P | 1 | LOCK |
| Screw terminal 4P | 1 | READER (EP8280L) — inside case |
| DC jack 5.5×2.1 panel (P4 + rabicho XH fêmea) | 1 | Embutido na case → encaixa em J1 |
| Nylon PG7 cable gland | 3 | READER, DOOR, REX (size up if jacket needs PG9) |
| Nylon PG9 or PG11 cable gland | 1 | LOCK (match strike cable OD) |
| Cable-tie mounts / P-clips | 4 | One per field cable, inside, before terminals |
| Ferrules (assortment) | 1 kit | Stranded ends into screw blocks |
| 470–1000 µF / ≥10 V electrolytic | 1 | Star (C1) |
| 100 nF 0805 | 1 | C2 |
| 2N2222 TO-92 | 2 | Q1 buzzer, Q2 relay |
| 1 kΩ | 2 | R1, R2 base |
| 10 kΩ 0805 | 1 | R3 `RELAY_IN` pull-up |
| XH 2P wafer + pin/female headers | as needed | J1 DCIN + modules + J8A/J8B |
| Standoffs / case | 1 set | USB + DC jack accessible; PG wall for field cables |

## Build checklist

1. Continuity: star 5 V not shorted to GND; all GNDs common.
2. Power only via DC jack → J1: measure ~5 V on star, ~3.3 V on ESP `3V3`.
3. Idle: `RELAY_IN` low (Q2 on), relay LED off; buzzer silent.
4. Pulse: unlock → relay click; held-open → buzzer cadence.
5. Case: PG glands fitted; jackets clamped; cable ties before terminals; no
   external screw blocks.
6. Pull test: firm tug on each field cable must not move the conductor in the
   terminal (gland / tie takes the load).
7. Field: reed and REX on screw blocks; strike only on LOCK dry contacts;
   READER shield to `GND` at carrier only.

## Out of scope for v0

- On-board ESP32-S3 chip + antenna (that is the full custom PCB)
- PoE, RS-485, battery UPS
- Changing factory GPIO numbers
- Injection-mold tooling / IP65 sign-off (case-v0 is a pilot print / machine shell)
