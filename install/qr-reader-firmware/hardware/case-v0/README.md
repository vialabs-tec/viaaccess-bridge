# Case v0 — enclosure for carrier-v0

Pilot plastic case around the **90 × 60 mm** carrier PCB. Complements KiCad
(`../carrier-v0/`) the same way FreeCAD / OpenSCAD complements a PCB tool: this
folder is the mechanical source of truth for holes, glands, and standoffs.

Electrical / field rules: [`docs/carrier-v0.md`](../../docs/carrier-v0.md)
(*Case cable entry and strain relief*).

## Tools

| Tool | Role |
|---|---|
| **FreeCAD** (recommended) | Refine the case, import KiCad STEP, export STEP/STL for fab or print |
| **OpenSCAD** | Parametric skeleton in-repo (`case-v0.scad`) — versionable, regenerates STL |
| **KiCad 3D Viewer** | Board-only preview (`../carrier-v0-3d-preview/`), not the enclosure |

Install FreeCAD: [freecad.org](https://www.freecad.org/). OpenSCAD optional for
quick STL from the `.scad` file.

## FreeCAD workflow (sign-off path)

1. In KiCad (carrier or 3D preview): **File → Export → STEP** → save as
   `imports/carrier-v0.step` (gitignored; regenerate locally).
2. Open FreeCAD → create/open `case-v0.FCStd` (local; see `.gitignore`).
3. **File → Import** the STEP (`CarrierSTEP`) and `case-v0-base.stl`.
4. Select **CarrierSTEP** → **Placement** (verified fit against case-v0.scad defaults
   `wall=2.4`, `clearance_xy=10`, `standoff_h=7`):

   | Property | Value |
   |---|---|
   | Axis | `0, 0, 1` (Z) |
   | Angle | `180°` |
   | **x** | **102.4** mm (`wall + clearance_w + 90`) |
   | **y** | **24.4** mm (`wall + clearance_s` — **do not** add `+ 60`) |
   | **z** | **9.4** mm (`floor + standoff`) |

   KiCad STEP comes in beside the case, flipped 180°, and sitting on Z=0. The
   table above rotates it, lifts it onto the PCB bosses, and slides it into the
   cavity. Adding `pcb_h` (+60) to **y** pushes the board out of the box.
   `clearance_s` is larger (22 mm) so LED / RTC / relay fit south of the PCB.
5. Check: field terminals face the PG wall; four M3 holes hit the four short
   bosses; USB side faces the USB slot; LED/RTC bosses meet module holes;
   relay sits in the cradle.
6. Export **STL** (FDM pilot) or **STEP** (machined / vendor).

Do not commit large binary `.FCStd` / `.step` / `.stl` unless the team agrees;
keep parameters and this README in git.

## OpenSCAD skeleton (interior included)

The `.scad` models the **inside** of the base, not only the outer shell:

| Feature | Detail |
|---|---|
| PCB bosses ×4 | At carrier M3 holes; height `standoff_h` (7 mm) |
| Heat-set M3 inserts | Pocket Ø4.0 × 5 mm in each PCB + lid boss (`use_heatset=true`) |
| Lid bosses ×4 | Corners (separate from PCB mounts), full cavity height |
| Lid holes | M3 clearance + countersink, aligned to lid bosses |
| **KY-016 LED** | Optional — 2× M2 bosses south of J13 (default status LED is DevKit WS2812) |
| **DS3231 RTC** | 2× M2 bosses south of J14 — module has screw holes |
| **Relay 1 ch** | Cradle + zip-tie slots (module has **no** screw holes) |
| Cable-tie anchors ×4 | Near PG wall for strain relief before screw terminals |
| PG / DC / USB | Same outer cutouts as the checklist below |

Tune `led_holes_pcb` / `rtc_holes_pcb` / `relay_*` in the `.scad` after measuring your
exact modules against the FreeCAD STEP (clone boards vary by a few mm).

```bash
# macOS — app binary if `openscad` is not on PATH:
OPENSCAD=/Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD
cd hardware/case-v0

"$OPENSCAD" -o case-v0.stl -D 'part="preview"' case-v0.scad
"$OPENSCAD" -o case-v0-base.stl -D 'part="base"' case-v0.scad
"$OPENSCAD" -o case-v0-lid.stl -D 'part="lid"' case-v0.scad
# half-cut view to inspect bosses in FreeCAD:
"$OPENSCAD" -o case-v0-cutaway.stl -D 'part="base_cutaway"' case-v0.scad
```

Import `case-v0-base.stl` (or cutaway) into FreeCAD next to `CarrierSTEP`. Set
`use_heatset=false` in the `.scad` if you prefer plain through-holes + nuts.

## Carrier → case dimensions

| Item | Value |
|---|---|
| PCB outline | **90 × 60 mm**, 1.6 mm thick |
| Mounting holes (M3) | `(3.5, 3.5)`, `(86.5, 3.5)`, `(3.5, 56.5)`, `(86.5, 56.5)` mm from PCB origin |
| Hole drill on PCB | Ø 3.2 mm (pad 4.5 mm) |
| Standoffs | M3, metal or brass, height ~**6–8 mm** under PCB (clear TO-92 / solder) |
| DevKit orientation | Component-side up, **USB to the left** (carrier silk) |
| Field screw blocks | Inside, on carrier field edge — lid must open for service |

Suggested outer envelope (skeleton defaults): ~**110 × 80 × 45 mm** (OD) with
~2.4 mm walls — enough for glands, DC jack, USB flash access, and module height.
Adjust after the first STEP fit check.

## Hole / cutout checklist

Mark each hole on the case drawing before print or machining.

### Cable wall (field jackets — outside)

| # | Cutout | Panel hole (approx.) | Notes |
|---|---|---|---|
| 1 | PG7 READER | Ø **12.5 mm** | 4×26 AWG manga ~4.3 mm OD |
| 2 | PG7 DOOR | Ø **12.5 mm** | Reed 2-core |
| 3 | PG7 REX | Ø **12.5 mm** | Exit button 2-core |
| 4 | PG9 LOCK | Ø **15.2 mm** | Upsize to PG11 (Ø ~18.6) if strike cable is fat |

Order on the wall (left → right, facing outside): READER · DOOR · REX · LOCK.
Keep glands near the internal screw terminals; leave room for a service loop +
cable tie **before** each block.

### Power / service wall

| # | Cutout | Size | Notes |
|---|---|---|---|
| 5 | DC jack 5.5×2.1 panel (P4) | Ø **~11 mm** (confirm jack datasheet) | Rabicho XH → carrier J1; not a field screw terminal |
| 6 | USB (DevKit flash/monitor) | Slot ~**9 × 3.5 mm** (USB-C) or larger relief | Bench only — not a field port; align to DevKit USB when seated |

### Lid / body

| # | Feature | Notes |
|---|---|---|
| 7 | Four M3 standoff bosses | Coincident with PCB holes; bosses tall enough for 6–8 mm standoff + nut/screw |
| 8 | Lid screws | Separate from PCB mounts (e.g. 4× M3 into corner bosses) |
| 9 | Status LED window in lid | Over DevKit WS2812 (default) or KY-016 if fitted |
| 10 | LED + RTC M2 bosses | South of PCB; screw modules down after plugging headers |
| 11 | Relay cradle | Seat module, zip-tie through side slots |
| 12 | No external screw terminals | Per carrier-v0 case rule |

## Strain relief (install)

Same as the electrical doc:

```text
PG gland (jacket clamped) → service loop → cable tie → screw terminal
```

Pull test: tugging the cable outside must not move the conductor in the terminal.

## Suggested FreeCAD tree

```text
case-v0.FCStd
├── Body_Base          shell + bosses + PG/DC/USB pockets
├── Body_Lid           lid + optional LED window
├── Import_CarrierSTEP reference (hidden for export)
└── Spreadsheet        wall, clearances, gland diameters (optional)
```

## Out of scope (v0)

- IP65 waterproofing validation
- Injection-mold tooling
- Reader (EP8280L) housing — separate door-side mount
- On-board custom PCB / PoE
