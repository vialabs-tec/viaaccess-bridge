# JLCPCB PCBA — carrier-v0

Order the **bare board + assembled** passives, NPN drivers, sockets, and connectors.
Modules (DevKit, relay board, buzzer, LED, RTC) still plug in by hand.

**No BSS138 level-shifter** on this revision — relay/buzzer use `Q1`/`Q2`.

## Files

| File | Role |
|---|---|
| `bom-jlcpcb.csv` | BOM (`Comment`, `Designator`, `Footprint`, `LCSC Part #`) |
| `cpl-jlcpcb.csv` | Centroid / pick & place (mm, Top) |
| `../gerbers/*.zip` | You create this from KiCad after regenerate |

## 1. Export Gerbers (KiCad)

1. Regenerate if needed: `python3 ../gen_pcb.py`
2. Open `carrier-v0.kicad_pcb` → **Fill all zones** → DRC clean.
3. **File → Fabrication Outputs → Gerbers** → layers  
   `F.Cu` `B.Cu` `F.SilkS` `B.SilkS` `F.Mask` `B.Mask` `Edge.Cuts`
4. **Generate Drill Files** (Excellon).
5. Zip the `gerbers/` folder.

## 2. Quote on [JLCPCB](https://cart.jlcpcb.com/pt/quote)

1. Product: **PCB/PCBA Padrão**.
2. Upload Gerber ZIP → **90 × 60 mm**, **2 layers**, **1.6 mm**, HASL, 1 oz, qty **5**.
3. Enable **PCB Assembly** → **Standard** (THT + SMT), **Top side**.
4. Upload `bom-jlcpcb.csv` + `cpl-jlcpcb.csv` → **Process BOM & CPL**.
   CPL Y is **flipped** for JLCPCB (`y' = 60 − y` from KiCad); do not re-export
   raw KiCad positions without that transform.

## 3. Assign LCSC parts

Pre-filled suggestions (verify stock + footprint in library):

| Designator | LCSC / search |
|---|---|
| C2 | `C49678` 100 nF 0805 |
| R3 | `C3016734` or any **10kΩ 0805** Basic in stock — **reject 5.1k** |
| C1 | Search `1000uF 16V` radial, pitch **5.0 mm**; **+ = left** |
| J1 | `C7429632` XH 2P wafer pitch **2.5 mm** |
| J2 | `C474953` screw 3P 5.08 |
| J3, J4 | `C8465` screw 2P 5.08 |
| J5 | `C53447001` screw 4P 5.08 |
| J8A, J8B | Two **1×22** female TH (`C7499337`); centroids **22.86 mm** apart (DevKit 0.9″). Same LCSC twice is correct |
| J9, J10 | `C2937625` header 1×3 |
| J12 | `C492401` header 1×2 |
| J13, J14 | `C2691448` header 1×4 |
| Q1, Q2 | `C5330385` 2N2222A TO-92 — confirm **CBE** vs silk |
| R1, R2 | Search `1k` axial pitch ~7.6 mm; else DNP and hand-solder |

**Do not assemble:** ESP32 DevKit, relay module, buzzer, LED, RTC.

## 4. Firmware polarity after PCBA

- Relay: **active-high unchecked** (`IN` idle low via Q2).
- Buzzer: **active-high checked** (GPIO high turns Q1 on → buzz).

## 5. Polarity / orientation

- **C1:** stripe / minus → **right** (GND); plus → **left** (P5V).
- **Q1/Q2:** flat face toward silk `CBE`; pads L→R = **C B E**.
- **J3/J4** (`C8465` 2P): CPL rotation **180°** so wire entry faces the board edge
  (same as J2/J5). Pads/nets unchanged — only the housing orientation flips.
