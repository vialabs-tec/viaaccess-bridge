#!/usr/bin/env python3
"""Generate carrier-v0.kicad_pcb with channel routing (F stubs + B.Cu buses).

Re-run:
  python3 gen_pcb.py
Then: close PCB editor → reopen → Fill all zones → DRC.
"""

from __future__ import annotations

import heapq
import uuid
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

OUT = Path(__file__).with_name("carrier-v0.kicad_pcb")
BOARD_W = 90.0
BOARD_H = 60.0
# False = corridor autorouter (preferred). True = pads only if autoroute fails DRC.
UNROUTED = False
GRID = 0.5
CLEAR = 0.32


def uid() -> str:
    return str(uuid.uuid4())


NETS = [
    "GND",
    "P5V",
    "P3V3",
    "GPIO4",
    "GPIO5",
    "GPIO6",
    "GPIO7",
    "GPIO8",
    "GPIO9",
    "GPIO10",
    "GPIO11",
    "GPIO12",
    "GPIO17",
    "GPIO18",
    "RELAY_IN",
    "LOCK_COM",
    "LOCK_NO",
    "LOCK_NC",
    "BUZZ_SW",
    "Q1_BASE",
    "Q2_BASE",
]
NET_ID = {n: i + 1 for i, n in enumerate(NETS)}

W = {
    "P5V": 0.5,
    "P3V3": 0.4,
    "LOCK_COM": 0.4,
    "LOCK_NO": 0.4,
    "LOCK_NC": 0.4,
    "GND": 0.35,
}
DEFAULT_W = 0.3


@dataclass
class Pad:
    num: str
    dx: float
    dy: float
    net: str | None
    shape: str = "circle"
    size: float = 1.7
    drill: float = 1.0
    smd: bool = False


@dataclass
class Footprint:
    ref: str
    value: str
    kind: str
    x: float
    y: float
    pads: list[Pad]
    silk: list[tuple[float, float, float, float]] = field(default_factory=list)


def header_pads(nets: list[str | None], pitch: float = 2.54) -> list[Pad]:
    n = len(nets)
    start = -((n - 1) * pitch) / 2
    return [Pad(str(i + 1), start + i * pitch, 0.0, net) for i, net in enumerate(nets)]


def terminal_pads(nets: list[str | None], pitch: float = 5.08) -> list[Pad]:
    n = len(nets)
    start = -((n - 1) * pitch) / 2
    return [
        Pad(str(i + 1), start + i * pitch, 0.0, net, shape="rect", size=2.8, drill=1.5)
        for i, net in enumerate(nets)
    ]


def silk_box(w: float, h: float):
    hw, hh = w / 2, h / 2
    return [(-hw, -hh, hw, -hh), (hw, -hh, hw, hh), (hw, hh, -hw, hh), (-hw, hh, -hw, -hh)]


def xh_2p_pads(nets: list[str | None]) -> list[Pad]:
    """JST-XH / market XH2.54 2P board wafer — pitch 2.5 mm (not 2.54)."""
    assert len(nets) == 2
    return [
        Pad("1", -1.25, 0.0, nets[0], size=1.8, drill=0.9),
        Pad("2", 1.25, 0.0, nets[1], size=1.8, drill=0.9),
    ]


# DevKitC-1 row centers are 22.86 mm apart (0.9"); module center stays at y=28.
DEVKIT_CX = 33.0
DEVKIT_CY = 28.0
DEVKIT_ROW = 22.86 / 2  # 11.43 mm from module center to each 1x22


def devkitc1_row_nets() -> tuple[list[str | None], list[str | None]]:
    """Nets for DevKitC-1 rows (USB end = pin 1). Official v1.1 Header Block.

    Top = board J1, bottom = board J3. Only factory GPIOs + power are netted;
    other pads stay NC (mechanical keepouts). Single 3V3 pad netted (pin 2 NC).
    """
    # J1 pin 1 → 22 (USB → far end)
    j1: list[str | None] = [
        "P3V3",  # 1 3V3
        None,  # 2 3V3
        None,  # 3 RST/EN
        "GPIO4",
        "GPIO5",
        "GPIO6",
        "GPIO7",
        None,  # 8 GPIO15
        None,  # 9 GPIO16
        "GPIO17",
        "GPIO18",
        "GPIO8",
        None,  # 13 GPIO3
        None,  # 14 GPIO46
        "GPIO9",
        "GPIO10",
        "GPIO11",
        "GPIO12",
        None,  # 19 GPIO13
        None,  # 20 GPIO14
        "P5V",
        "GND",  # 22 — carrier GND star
    ]
    # J3: all NC on carrier (DevKit bonds GNDs; avoids B.Cu islands)
    j3: list[str | None] = [None] * 22
    return j1, j3


def socket_1x22_pads(nets: list[str | None]) -> list[Pad]:
    """Single-row female 1x22, pitch 2.54 mm, pin 1 at USB (west) end."""
    assert len(nets) == 22
    pitch = 2.54
    start = -((22 - 1) * pitch) / 2
    return [Pad(str(i + 1), start + i * pitch, 0.0, net) for i, net in enumerate(nets)]


def smd0805(net_a: str, net_b: str) -> list[Pad]:
    return [
        Pad("1", -0.95, 0, net_a, shape="roundrect", size=1.0, drill=0, smd=True),
        Pad("2", 0.95, 0, net_b, shape="roundrect", size=1.0, drill=0, smd=True),
    ]


def build_parts() -> list[Footprint]:
    # 90×60 mm: DevKit left; NPN drivers + power island on the right (was 100 mm)
    j1_nets, j3_nets = devkitc1_row_nets()
    parts: list[Footprint] = [
        # Field terminals: equal ~3 mm gap between silk boxes (widths 15 / 9 / 9 / 20)
        # READER at 70: P5V pin clears DevKit J3 far-end NC (~x59.7)
        Footprint("J2", "LOCK", "Terminal_3P", 25.5, 53, terminal_pads(["LOCK_COM", "LOCK_NO", "LOCK_NC"]), silk_box(15, 7)),
        Footprint("J3", "DOOR", "Terminal_2P", 40.5, 53, terminal_pads(["GPIO11", "GND"]), silk_box(9, 7)),
        Footprint("J4", "REX", "Terminal_2P", 52.5, 53, terminal_pads(["GPIO12", "GND"]), silk_box(9, 7)),
        Footprint("J5", "READER", "Terminal_4P", 70.0, 53, terminal_pads(["P5V", "GND", "GPIO17", "GPIO18"]), silk_box(20, 7)),
        # ESP32-S3-DevKitC-1 — two 1x22 females (JLCPCB has no 0.9" dual-row)
        # Centers 22.86 mm apart; USB end to the left (tick on J8A)
        Footprint(
            "J8A",
            "DevKit-J1",
            "PinSocket_1x22",
            DEVKIT_CX,
            round(DEVKIT_CY - DEVKIT_ROW, 3),
            socket_1x22_pads(j1_nets),
            silk_box(56, 3.2) + [(-28.5, -1.6, -28.5, 1.6)],
        ),
        Footprint(
            "J8B",
            "DevKit-J3",
            "PinSocket_1x22",
            DEVKIT_CX,
            round(DEVKIT_CY + DEVKIT_ROW, 3),
            socket_1x22_pads(j3_nets),
            silk_box(56, 3.2),
        ),
        # Top row over the DevKit
        Footprint("J13", "LED", "PinHeader_1x04", 12, 5.5, header_pads(["GND", "GPIO4", "GPIO5", "GPIO6"]), silk_box(11, 3.2)),
        Footprint("J14", "RTC", "PinHeader_1x04", 28, 5.5, header_pads(["P3V3", "GND", "GPIO8", "GPIO9"]), silk_box(11, 3.2)),
        Footprint("J9", "RELAY", "PinHeader_1x03", 44, 5.5, header_pads(["P5V", "GND", "RELAY_IN"]), silk_box(9, 3.2)),
        # Dry contacts next to LOCK
        Footprint("J10", "RELAY_DRY", "PinHeader_1x03", 12, 46, header_pads(["LOCK_COM", "LOCK_NO", "LOCK_NC"]), silk_box(9, 3.2)),
        # Right island by net locality, kept east of DevKit pads (x≳62):
        #   1) relay under J9  2) buzzer mid  3) DCIN/C1/C2 star on the edge
        Footprint("R3", "10k", "R_0805", 50, 9, smd0805("P5V", "RELAY_IN")),
        Footprint(
            "Q2",
            "2N2222",
            "TO92",
            66,
            14,
            [Pad("1", -2.54, 0, "RELAY_IN"), Pad("2", 0, 0, "Q2_BASE"), Pad("3", 2.54, 0, "GND")],
        ),
        Footprint(
            "R2",
            "1k",
            "R_Axial",
            68,
            20,
            [Pad("1", -3.81, 0, "GPIO10", size=1.6, drill=0.8), Pad("2", 3.81, 0, "Q2_BASE", size=1.6, drill=0.8)],
        ),
        Footprint(
            "R1",
            "1k",
            "R_Axial",
            67,
            32,
            [Pad("1", -3.81, 0, "GPIO7", size=1.6, drill=0.8), Pad("2", 3.81, 0, "Q1_BASE", size=1.6, drill=0.8)],
        ),
        Footprint(
            "Q1",
            "2N2222",
            "TO92",
            76,
            32,
            [Pad("1", -2.54, 0, "BUZZ_SW"), Pad("2", 0, 0, "Q1_BASE"), Pad("3", 2.54, 0, "GND")],
        ),
        # BUZZ header top-right — P5V from DCIN star, BUZZ_SW drops to Q1
        Footprint("J12", "BUZZ", "PinHeader_1x02", 84, 12, header_pads(["P5V", "BUZZ_SW"]), silk_box(6.5, 3.2)),
        # C1 mid-east (not south of DCIN): keeps bulk can clear of field connector case cutout
        Footprint(
            "C1",
            "1000uF/16V",
            "C_Radial",
            84,
            24,
            [Pad("1", -2.5, 0, "P5V", size=2.2, drill=1.2), Pad("2", 2.5, 0, "GND", size=2.2, drill=1.2)],
        ),
        # XH wafer (male pins) — mates with female XH housing on the panel-jack pigtail
        Footprint(
            "J1",
            "DCIN",
            "XH_1x02",
            84,
            40,
            xh_2p_pads(["P5V", "GND"]),
            silk_box(7.5, 6.0) + [(-2.0, -3.0, -2.0, -1.5)],  # pin-1 (+5V) tick
        ),
        Footprint("C2", "100n", "C_0805", 72, 40, smd0805("P5V", "GND")),
    ]
    for x, y in ((3.5, 3.5), (86.5, 3.5), (3.5, 56.5), (86.5, 56.5)):
        parts.append(Footprint(f"H{x}_{y}", "M3", "MountingHole", x, y, [Pad("", 0, 0, None, size=4.5, drill=3.2)]))
    return parts


def pad_abs(fp: Footprint, pad: Pad) -> tuple[float, float]:
    return (round(fp.x + pad.dx, 3), round(fp.y + pad.dy, 3))


def index_pads(parts: list[Footprint]):
    """ref.pin -> (x, y, net, size, smd)"""
    by_ref: dict[str, dict[str, tuple]] = defaultdict(dict)
    by_net: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for fp in parts:
        for pad in fp.pads:
            if not pad.num and not pad.net:
                continue
            xy = pad_abs(fp, pad)
            by_ref[fp.ref][pad.num] = (xy[0], xy[1], pad.net, pad.size, pad.smd)
            if pad.net:
                by_net[pad.net].append(xy)
    return by_ref, by_net


class Copper:
    def __init__(self):
        self.segments: list[tuple] = []  # x1,y1,x2,y2,w,nid,net,layer
        self.vias: list[tuple] = []  # x,y,nid,net

    def seg(self, x1, y1, x2, y2, net, layer="F.Cu", w=None):
        if abs(x1 - x2) < 0.01 and abs(y1 - y2) < 0.01:
            return
        ww = w if w is not None else W.get(net, DEFAULT_W)
        self.segments.append((x1, y1, x2, y2, ww, NET_ID[net], net, layer))

    def via(self, x, y, net):
        self.vias.append((x, y, NET_ID[net], net))

    def wire(self, net: str, pts: list[tuple[float, float, str]], w=None):
        """pts: (x, y, 'F'|'B'). Layer change inserts a via at the shared point."""
        ww = w if w is not None else W.get(net, DEFAULT_W)
        for i in range(len(pts) - 1):
            x1, y1, l1 = pts[i]
            x2, y2, l2 = pts[i + 1]
            layer1 = "F.Cu" if l1 == "F" else "B.Cu"
            layer2 = "F.Cu" if l2 == "F" else "B.Cu"
            if l1 != l2:
                # via at end of current / start of next (same coords expected)
                self.via(x1, y1, net)
                if abs(x1 - x2) > 0.01 or abs(y1 - y2) > 0.01:
                    self.seg(x1, y1, x2, y2, net, layer2, ww)
            else:
                self.seg(x1, y1, x2, y2, net, layer1, ww)


def _uniq_pts(pts: list[tuple[float, float]]) -> list[tuple[float, float]]:
    out: list[tuple[float, float]] = []
    for p in pts:
        if all(abs(p[0] - q[0]) > 0.05 or abs(p[1] - q[1]) > 0.05 for q in out):
            out.append(p)
    return out


def _mst(pts: list[tuple[float, float]]):
    pts = _uniq_pts(pts)
    if len(pts) < 2:
        return []
    used = {pts[0]}
    rest = set(pts[1:])
    edges = []
    while rest:
        best = None
        best_d = 1e9
        for a in used:
            for b in rest:
                d = abs(a[0] - b[0]) + abs(a[1] - b[1])
                if d < best_d:
                    best_d = d
                    best = (a, b)
        assert best
        edges.append(best)
        used.add(best[1])
        rest.remove(best[1])
    return edges


def _dist_point_seg(px, py, x1, y1, x2, y2) -> float:
    vx, vy = x2 - x1, y2 - y1
    L2 = vx * vx + vy * vy
    if L2 < 1e-12:
        return ((px - x1) ** 2 + (py - y1) ** 2) ** 0.5
    t = max(0.0, min(1.0, ((px - x1) * vx + (py - y1) * vy) / L2))
    return ((px - (x1 + t * vx)) ** 2 + (py - (y1 + t * vy)) ** 2) ** 0.5


def route_board(parts: list[Footprint]) -> tuple[Copper, list]:
    """Corridor A*: cheap paths in empty bands + side gutters; F/B with vias."""
    global UNROUTED
    P, by_net = index_pads(parts)
    cu = Copper()
    notes: list[str] = []

    def pref(ref: str, num: str) -> tuple[float, float]:
        return P[ref][num][0], P[ref][num][1]

    routed_segs: list[tuple] = []  # x1,y1,x2,y2,w,net,layer_idx
    routed_vias: list[tuple] = []  # x,y,r,net
    escape_keepouts: list[tuple[float, float, float, str]] = []

    def stitch_gnd(x1, y1, x2, y2):
        """GND pour stitch on F.Cu — also reserved in the autorouter keepouts."""
        w = W.get("GND", DEFAULT_W)
        cu.seg(x1, y1, x2, y2, "GND", "F.Cu", w)
        routed_segs.append((x1, y1, x2, y2, w, "GND", 0))

    # Stitch C2 / C1 / Q1 / Q2 GND into pour (never toward a sibling P5V pad)
    gx, gy = pref("C2", "2")
    stitch_gnd(gx, gy, gx + 2.5, gy)
    cu.via(gx + 2.5, gy, "GND")
    routed_vias.append((gx + 2.5, gy, 0.85, "GND"))
    c1x, c1y = pref("C1", "2")
    stitch_gnd(c1x, c1y, c1x - 2.5, c1y)
    cu.via(c1x - 2.5, c1y, "GND")
    routed_vias.append((c1x - 2.5, c1y, 0.85, "GND"))
    # Q1 / Q2 emitters → short F.Cu stubs into B.Cu pour
    qx, qy = pref("Q1", "3")
    stitch_gnd(qx, qy, qx, qy + 2.5)
    cu.via(qx, qy + 2.5, "GND")
    routed_vias.append((qx, qy + 2.5, 0.85, "GND"))
    q2x, q2y = pref("Q2", "3")
    stitch_gnd(q2x, q2y, q2x + 2.5, q2y)
    cu.via(q2x + 2.5, q2y, "GND")
    routed_vias.append((q2x + 2.5, q2y, 0.85, "GND"))
    if UNROUTED:
        notes.append("UNROUTED: signal copper omitted")
        return cu, notes

    pads = []
    for fp in parts:
        for pad in fp.pads:
            x, y = pad_abs(fp, pad)
            if not pad.net:
                # NC DevKit/socket pads and mounting holes still block copper
                if fp.kind.startswith("Mounting") or "Socket" in fp.kind:
                    # Inflate slightly — KiCad also expands solder mask around PTH
                    pads.append((x, y, pad.size / 2 + 0.2, "__HOLE__", False))
                continue
            pads.append((x, y, pad.size / 2, pad.net, pad.smd))

    # Corridors: DevKit left (~x9–63); right island drivers/DCIN/C1 (~x74–96)
    h_bands = [(2.0, 4.0), (7.5, 12.0), (16.0, 20.0), (26.0, 34.0), (38.0, 42.0), (47.0, 50.0), (56.0, 58.5)]
    v_bands = [(2.0, 5.5), (62.0, 66.0), (85.0, 88.0)]

    def in_corridor(x: float, y: float) -> bool:
        for y0, y1 in h_bands:
            if y0 <= y <= y1 and 2.0 <= x <= BOARD_W - 2.0:
                return True
        for x0, x1 in v_bands:
            if x0 <= x <= x1 and 2.0 <= y <= BOARD_H - 2.0:
                return True
        return False

    nx = int(BOARD_W / GRID) + 1
    ny = int(BOARD_H / GRID) + 1

    def cell(x: float, y: float) -> tuple[int, int]:
        return (
            max(0, min(nx - 1, int(round(x / GRID)))),
            max(0, min(ny - 1, int(round(y / GRID)))),
        )

    def obstructed(
        x: float, y: float, layer: int, net: str, w: float, start, goal, via_r: float = 0.0
    ) -> bool:
        # Allow standing on own endpoints
        if (x - start[0]) ** 2 + (y - start[1]) ** 2 < 0.55**2:
            return False
        if (x - goal[0]) ** 2 + (y - goal[1]) ** 2 < 0.55**2:
            return False
        need = w / 2 + CLEAR + via_r
        for px, py, pr, pnet, smd in pads:
            if pnet == net:
                continue
            if smd and layer != 0:
                continue
            # 0805 + large terminal pads — inflate keepout (KiCad mask is stricter than CLEAR)
            pad_need = need + (0.55 if smd else 0.0) + (0.45 if pr >= 1.3 else 0.0)
            if (x - px) ** 2 + (y - py) ** 2 < (pr + pad_need) ** 2:
                return True
        for vx, vy, vr, vnet in list(routed_vias) + escape_keepouts:
            if vnet == net:
                continue
            if (x - vx) ** 2 + (y - vy) ** 2 < (vr + need) ** 2:
                return True
        for x1, y1, x2, y2, tw, tnet, tlayer in routed_segs:
            if tnet == net or tlayer != layer:
                continue
            if _dist_point_seg(x, y, x1, y1, x2, y2) < (tw / 2 + need):
                return True
        if x < 1.5 or y < 1.5 or x > BOARD_W - 1.5 or y > BOARD_H - 1.5:
            return True
        return False

    def astar(start, goal, net, w) -> list[tuple[float, float, int]] | None:
        sc = cell(*start)
        gc = cell(*goal)
        starts = [(sc[0], sc[1], 0), (sc[0], sc[1], 1)]
        open_h: list[tuple[float, int, tuple[int, int, int]]] = []
        seq = 0
        best: dict[tuple[int, int, int], float] = {}
        came: dict[tuple[int, int, int], tuple[int, int, int] | None] = {}
        for st in starts:
            best[st] = 0.0
            came[st] = None
            heapq.heappush(open_h, (0.0, seq, st))
            seq += 1

        def h(n):
            return abs(n[0] - gc[0]) + abs(n[1] - gc[1])

        while open_h:
            _, _, cur = heapq.heappop(open_h)
            if (cur[0], cur[1]) == gc:
                path = []
                n: tuple[int, int, int] | None = cur
                while n is not None:
                    path.append((n[0] * GRID, n[1] * GRID, n[2]))
                    n = came.get(n)
                path.reverse()
                return path
            cost = best[cur]
            # via (account for 0.8 mm via copper vs foreign tracks/pads)
            other = (cur[0], cur[1], 1 - cur[2])
            xc, yc = cur[0] * GRID, cur[1] * GRID
            if not obstructed(xc, yc, other[2], net, w, start, goal, via_r=0.4):
                if not obstructed(xc, yc, cur[2], net, w, start, goal, via_r=0.4):
                    nc = cost + 7.0
                    if other not in best or nc < best[other]:
                        best[other] = nc
                        came[other] = cur
                        heapq.heappush(open_h, (nc + h(other), seq, other))
                        seq += 1
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nxt = (cur[0] + dx, cur[1] + dy, cur[2])
                if not (0 <= nxt[0] < nx and 0 <= nxt[1] < ny):
                    continue
                xx, yy = nxt[0] * GRID, nxt[1] * GRID
                if obstructed(xx, yy, nxt[2], net, w, start, goal):
                    continue
                step = 1.0 if in_corridor(xx, yy) else 2.2
                # Prefer F.Cu; allow leaving corridors so nets can drop straight to pads
                if nxt[2] == 1:
                    step += 0.15
                nc = cost + step
                if nxt not in best or nc < best[nxt]:
                    best[nxt] = nc
                    came[nxt] = cur
                    heapq.heappush(open_h, (nc + h(nxt), seq, nxt))
                    seq += 1
        return None

    def on_pth_pad(x: float, y: float, net: str) -> bool:
        """True when (x,y) sits on a through-hole pad (already F↔B); skip extra via."""
        for px, py, pr, pnet, smd in pads:
            if smd or pnet != net:
                continue
            if (x - px) ** 2 + (y - py) ** 2 < (pr * 0.85) ** 2:
                return True
        return False

    def place_via(x: float, y: float, net: str):
        if on_pth_pad(x, y, net):
            return
        if any(abs(x - vx) < 0.2 and abs(y - vy) < 0.2 and vn == net for vx, vy, _, vn in routed_vias):
            return
        cu.via(x, y, net)
        routed_vias.append((x, y, 0.85, net))

    def commit(path: list[tuple[float, float, int]], start, goal, net, w):
        # Escape stubs are on F.Cu: if the path arrives/leaves on B, stitch with a via
        if path[0][2] == 1:
            place_via(start[0], start[1], net)
        if path[-1][2] == 1:
            place_via(goal[0], goal[1], net)
        pts = [(start[0], start[1], path[0][2])] + path + [(goal[0], goal[1], path[-1][2])]
        i = 0
        while i < len(pts) - 1:
            x1, y1, l1 = pts[i]
            x2, y2, l2 = pts[i + 1]
            if l1 != l2:
                place_via(x1, y1, net)
                i += 1
                continue
            j = i + 1
            while j < len(pts) - 1 and pts[j][2] == l1 and pts[j + 1][2] == l1:
                if pts[i][0] == pts[j][0] == pts[j + 1][0] or pts[i][1] == pts[j][1] == pts[j + 1][1]:
                    j += 1
                else:
                    break
            x2, y2, _ = pts[j]
            layer = "F.Cu" if l1 == 0 else "B.Cu"
            cu.seg(x1, y1, x2, y2, net, layer, w)
            routed_segs.append((x1, y1, x2, y2, w, net, l1))
            i = j

    order = [
        "P5V",
        "P3V3",
        "LOCK_COM",
        "LOCK_NO",
        "LOCK_NC",
        "GPIO4",
        "GPIO5",
        "GPIO6",
        "GPIO8",
        "GPIO9",
        "GPIO7",
        "GPIO10",
        "RELAY_IN",
        "BUZZ_SW",
        "Q1_BASE",
        "Q2_BASE",
        "GPIO11",
        "GPIO12",
        "GPIO17",
        "GPIO18",
    ]

    pad_repl: dict[tuple[str, str], tuple[float, float]] = {}

    # Field DOOR/REX: rise straight up from signal pin (avoid sibling GND pad)
    field_escape_y = 46.0
    for ref, pin, net in (("J3", "1", "GPIO11"), ("J4", "1", "GPIO12")):
        x, y = pref(ref, pin)
        w = W.get(net, DEFAULT_W)
        cu.seg(x, y, x, field_escape_y, net, "F.Cu", w)
        routed_segs.append((x, y, x, field_escape_y, w, net, 0))
        pad_repl[(ref, pin)] = (x, field_escape_y)

    # Field READER: stub every signal clear of sibling terminal pads (5.08 mm pitch, 2.8 mm pads)
    x5, y5 = pref("J5", "1")  # P5V — rise then west, away from GND pad
    w5 = W.get("P5V", DEFAULT_W)
    cu.seg(x5, y5, x5, 49.0, "P5V", "F.Cu", w5)
    routed_segs.append((x5, y5, x5, 49.0, w5, "P5V", 0))
    cu.seg(x5, 49.0, x5 - 3.0, 49.0, "P5V", "F.Cu", w5)
    routed_segs.append((x5, 49.0, x5 - 3.0, 49.0, w5, "P5V", 0))
    pad_repl[("J5", "1")] = (x5 - 3.0, 49.0)
    # UART: rise then east into gutter at staggered Y (no shared corridor)
    # South band is free with C1 at y=24 — park UART tips clear of DCIN/C2
    for net, pin, tx, ty in (("GPIO17", "3", 86.0, 46.0), ("GPIO18", "4", 86.0, 48.5)):
        x, y = pref("J5", pin)
        w = W.get(net, DEFAULT_W)
        mid_y = ty
        cu.seg(x, y, x, mid_y, net, "F.Cu", w)
        routed_segs.append((x, y, x, mid_y, w, net, 0))
        cu.seg(x, mid_y, tx, ty, net, "F.Cu", w)
        routed_segs.append((x, mid_y, tx, ty, w, net, 0))
        pad_repl[("J5", pin)] = (tx, ty)

    # DevKit J8A (J1 row): stub south into under-module corridor before MST
    esp_escape_y = DEVKIT_CY
    for pin, net in (
        ("1", "P3V3"),
        ("4", "GPIO4"),
        ("5", "GPIO5"),
        ("6", "GPIO6"),
        ("7", "GPIO7"),
        ("10", "GPIO17"),
        ("11", "GPIO18"),
        ("12", "GPIO8"),
        ("15", "GPIO9"),
        ("16", "GPIO10"),
        ("17", "GPIO11"),
        ("18", "GPIO12"),
        ("21", "P5V"),
    ):
        x, y = pref("J8A", pin)
        w = W.get(net, DEFAULT_W)
        cu.seg(x, y, x, esp_escape_y, net, "F.Cu", w)
        routed_segs.append((x, y, x, esp_escape_y, w, net, 0))
        pad_repl[("J8A", pin)] = (x, esp_escape_y)

    # Escape tips: keepout for foreign nets only (owner net may still land there)
    for (ref, pin), (ex, ey) in pad_repl.items():
        owner = P[ref][pin][2]
        if owner:
            escape_keepouts.append((ex, ey, 0.85, owner))

    def remap(pts: list[tuple[float, float]]) -> list[tuple[float, float]]:
        out = []
        for p in pts:
            replaced = False
            for (ref, pin), esc in pad_repl.items():
                jx, jy = pref(ref, pin)
                if abs(p[0] - jx) < 0.2 and abs(p[1] - jy) < 0.2:
                    out.append(esc)
                    replaced = True
                    break
            if not replaced:
                out.append(p)
        return out

    failed = []
    for net in order:
        pts = list(by_net.get(net, []))
        if net == "P5V":
            pts = pts + [pref("C2", "1")]
        pts = remap(pts)
        w = W.get(net, DEFAULT_W)
        for a, b in _mst(pts):
            path = astar(a, b, net, w)
            if path is None:
                path = astar(a, b, net, max(0.25, w - 0.1))
            if path is None:
                failed.append((net, a, b))
                continue
            commit(path, a, b, net, w)

    if failed:
        notes.append(f"autoroute failed edges={len(failed)}")
        for f in failed[:8]:
            notes.append(f"  fail {f}")
        # Fall back to unrouted copper (keep GND via only)
        cu2 = Copper()
        cu2.seg(gx, gy, gx + 2.0, gy, "GND", "F.Cu")
        cu2.via(gx + 2.0, gy, "GND")
        notes.append("FALLBACK unrouted due to failed edges")
        return cu2, notes

    notes.append(f"autoroute ok segs={len(cu.segments)} vias={len(cu.vias)}")
    return cu, notes


def emit_fp(fp: Footprint) -> str:
    lines = [
        f'  (footprint "{fp.kind}"',
        '    (layer "F.Cu")',
        f"    (uuid {uid()})",
        f"    (at {fp.x} {fp.y})",
        "    (attr smd)" if fp.kind in ("C_0805", "R_0805") else "    (attr through_hole)",
    ]
    if fp.kind.startswith("Mounting"):
        lines[-1] = "    (attr through_hole exclude_from_pos_files exclude_from_bom)"

    ref_x, ref_y = 0.0, -2.8
    if fp.kind.startswith("Terminal"):
        ref_y = -4.8
    elif fp.kind == "C_Radial":
        # West of can — east would clip the board edge at x=90
        ref_x, ref_y = -5.5, 0.0
    elif fp.kind in ("C_0805", "R_0805"):
        ref_y = -1.8
    elif fp.kind == "TO92":
        # South of body — west would hit R1/R2; east shares space with CBE
        ref_x, ref_y = 0.0, 3.2
    elif fp.kind == "R_Axial":
        ref_y = -2.2
    elif fp.kind.startswith("PinSocket"):
        # North of strip — clears pad masks
        ref_y = -2.4 if fp.ref == "J8A" else 2.4
    elif fp.kind == "XH_1x02":
        # South of the 6 mm housing silk (half-height 3.0)
        ref_y = 4.8
    elif fp.kind.startswith("PinHeader") and fp.ref in ("J9", "J12", "J13", "J14"):
        # Ref south of the header — pin legends sit on the north edge
        ref_y = 2.8
    elif len(fp.pads) > 6:
        ref_y = -3.2

    if not fp.kind.startswith("Mounting"):
        fab_y = abs(ref_y) if ref_y != 0 else 5.5
        lines += [
            f'    (fp_text reference "{fp.ref}" (at {ref_x} {ref_y}) (layer "F.SilkS") (uuid {uid()})',
            "      (effects (font (size 1 1) (thickness 0.15)))",
            "    )",
            f'    (fp_text value "{fp.value}" (at 0 {fab_y}) (layer "F.Fab") (uuid {uid()})',
            "      (effects (font (size 0.8 0.8) (thickness 0.12)))",
            "    )",
        ]

    for x1, y1, x2, y2 in fp.silk:
        lines.append(
            f'    (fp_line (start {x1} {y1}) (end {x2} {y2}) '
            f'(stroke (width 0.12) (type solid)) (layer "F.SilkS"))'
        )
    if fp.kind == "C_Radial":
        lines.append('    (fp_circle (center 0 0) (end 4 0) (stroke (width 0.12) (type solid)) (fill no) (layer "F.SilkS"))')
        lines.append('    (fp_line (start -1.5 -5.5) (end 1.5 -5.5) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))')
    if fp.kind == "TO92":
        lines.append('    (fp_line (start -2.8 1.6) (end 2.8 1.6) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))')
        lines.append(
            f'    (fp_text user "CBE" (at 4.5 0) (layer "F.SilkS") (uuid {uid()})'
            " (effects (font (size 0.8 0.8) (thickness 0.12))))"
        )

    for pad in fp.pads:
        net_s = f' (net {NET_ID[pad.net]} "{pad.net}")' if pad.net else ""
        if pad.smd:
            lines.append(
                f'    (pad "{pad.num}" smd roundrect (at {pad.dx} {pad.dy}) '
                f'(size 1.0 1.25) (layers "F.Cu" "F.Mask" "F.Paste") (roundrect_rratio 0.25){net_s})'
            )
        elif fp.kind.startswith("Mounting"):
            lines.append('    (pad "" thru_hole circle (at 0 0) (size 4.5 4.5) (drill 3.2) (layers "*.Cu" "*.Mask"))')
        else:
            shape = "rect" if pad.shape == "rect" else "circle"
            lines.append(
                f'    (pad "{pad.num}" thru_hole {shape} (at {pad.dx} {pad.dy}) '
                f'(size {pad.size} {pad.size}) (drill {pad.drill}) (layers "*.Cu" "*.Mask"){net_s})'
            )
    lines.append("  )")
    return "\n".join(lines)


def emit_segment(x1, y1, x2, y2, w, nid, layer) -> str:
    return (
        "  (segment\n"
        f"    (start {round(x1, 3)} {round(y1, 3)})\n"
        f"    (end {round(x2, 3)} {round(y2, 3)})\n"
        f"    (width {w})\n"
        f'    (layer "{layer}")\n'
        f"    (net {nid})\n"
        f"    (uuid {uid()})\n"
        "  )"
    )


def emit_via(x, y, nid) -> str:
    return (
        "  (via\n"
        f"    (at {round(x, 3)} {round(y, 3)})\n"
        "    (size 0.8)\n"
        "    (drill 0.4)\n"
        '    (layers "F.Cu" "B.Cu")\n'
        f"    (net {nid})\n"
        f"    (uuid {uid()})\n"
        "  )"
    )


def emit_text(txt: str, x: float, y: float, size: float = 0.9) -> str:
    size = max(size, 0.8)
    return (
        f'  (gr_text "{txt}"\n'
        f"    (at {x} {y})\n"
        f'    (layer "F.SilkS")\n'
        f"    (uuid {uid()})\n"
        f"    (effects (font (size {size} {size}) (thickness 0.15)) (justify left))\n"
        "  )"
    )


def local_drc(parts: list[Footprint], cu: Copper) -> list[str]:
    """Catch shorts before KiCad."""
    pads = []
    for fp in parts:
        for pad in fp.pads:
            x, y = pad_abs(fp, pad)
            if not pad.net:
                if fp.kind.startswith("Mounting") or "Socket" in fp.kind:
                    pads.append((x, y, pad.size / 2 + 0.2, "__NC__", False))
                continue
            pads.append((x, y, pad.size / 2, pad.net, pad.smd))
    vias = [(x, y, 0.4, net) for x, y, _, net in cu.vias]  # radius ≈ via size/2
    errs = []

    def dist_ps(px, py, x1, y1, x2, y2):
        vx, vy = x2 - x1, y2 - y1
        L2 = vx * vx + vy * vy
        if L2 < 1e-12:
            return ((px - x1) ** 2 + (py - y1) ** 2) ** 0.5
        t = max(0, min(1, ((px - x1) * vx + (py - y1) * vy) / L2))
        return ((px - (x1 + t * vx)) ** 2 + (py - (y1 + t * vy)) ** 2) ** 0.5

    for x1, y1, x2, y2, w, _nid, net, layer in cu.segments:
        for px, py, pr, pnet, smd in pads:
            if pnet == net:
                continue
            if smd and layer != "F.Cu":
                continue
            d = dist_ps(px, py, x1, y1, x2, y2)
            if d < pr + w / 2 + CLEAR - 0.01:
                errs.append(f"track {net}@{layer} too close to pad {pnet} d={d:.3f}")
        for vx, vy, vr, vnet in vias:
            if vnet == net:
                continue
            d = dist_ps(vx, vy, x1, y1, x2, y2)
            # via present on both layers
            if d < vr + w / 2 + CLEAR - 0.01:
                errs.append(f"track {net}@{layer} too close to via {vnet} d={d:.3f} @({vx},{vy})")

    # via to foreign pad
    for vx, vy, vr, vnet in vias:
        for px, py, pr, pnet, smd in pads:
            if pnet == vnet:
                continue
            d = ((vx - px) ** 2 + (vy - py) ** 2) ** 0.5
            if d < pr + vr + CLEAR - 0.01:
                errs.append(f"via {vnet} too close to pad {pnet} d={d:.3f}")

    # via to via
    for i, (x1, y1, r1, n1) in enumerate(vias):
        for x2, y2, r2, n2 in vias[i + 1 :]:
            if n1 == n2:
                continue
            d = ((x1 - x2) ** 2 + (y1 - y2) ** 2) ** 0.5
            # 0.8 mm via size → 0.4 r; 0.2 mm clearance → min center dist 1.0
            if d < 0.98:
                errs.append(f"vias {n1}/{n2} too close d={d:.3f}")

    # track crossings different nets same layer
    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def crosses(a, b, c, d):
        return orient(a, b, c) * orient(a, b, d) < 0 and orient(c, d, a) * orient(c, d, b) < 0

    segs = cu.segments
    for i, a in enumerate(segs):
        for b in segs[i + 1 :]:
            if a[7] != b[7] or a[6] == b[6]:
                continue
            A, B, C, D = (a[0], a[1]), (a[2], a[3]), (b[0], b[1]), (b[2], b[3])
            ea = {(round(A[0], 2), round(A[1], 2)), (round(B[0], 2), round(B[1], 2))}
            eb = {(round(C[0], 2), round(C[1], 2)), (round(D[0], 2), round(D[1], 2))}
            if ea & eb:
                continue
            if crosses(A, B, C, D):
                errs.append(f"cross {a[6]}/{b[6]} on {a[7]}")
    return errs


def generate():
    parts = build_parts()
    cu, notes = route_board(parts)
    errs = local_drc(parts, cu)
    if errs and len(cu.segments) > 2:
        notes.append(f"local_drc={len(errs)} (kept routed; review in KiCad)")
        for e in errs[:15]:
            notes.append(f"  {e}")

    out: list[str] = []
    out.append("(kicad_pcb")
    out.append("  (version 20240108)")
    out.append('  (generator "viaaccess-carrier-v0-script")')
    out.append('  (generator_version "8.0")')
    out.append("  (general (thickness 1.6) (legacy_teardrops no))")
    out.append('  (paper "A4")')
    out.append("  (layers")
    for layer in [
        '(0 "F.Cu" signal)',
        '(31 "B.Cu" signal)',
        '(32 "B.Adhes" user "B.Adhesive")',
        '(33 "F.Adhes" user "F.Adhesive")',
        '(34 "B.Paste" user)',
        '(35 "F.Paste" user)',
        '(36 "B.SilkS" user "B.Silkscreen")',
        '(37 "F.SilkS" user "F.Silkscreen")',
        '(38 "B.Mask" user)',
        '(39 "F.Mask" user)',
        '(40 "Dwgs.User" user "User.Drawings")',
        '(41 "Cmts.User" user "User.Comments")',
        '(42 "Eco1.User" user "User.Eco1")',
        '(43 "Eco2.User" user "User.Eco2")',
        '(44 "Edge.Cuts" user)',
        '(45 "Margin" user)',
        '(46 "B.CrtYd" user "B.Courtyard")',
        '(47 "F.CrtYd" user "F.Courtyard")',
        '(48 "B.Fab" user)',
        '(49 "F.Fab" user)',
    ]:
        out.append(f"    {layer}")
    out.append("  )")
    out.append("  (setup (pad_to_mask_clearance 0.05) (allow_soldermask_bridges_in_footprints no)")
    out.append(
        '    (pcbplotparams (layerselection 0x00010fc_ffffffff) (plot_on_all_layers_selection 0x00000000_00000000)'
        " (disableapertmacros no) (usegerberextensions no) (usegerberattributes yes)"
        " (usegerberadvancedattributes yes) (creategerberjobfile yes)"
        " (dashed_line_dash_ratio 12) (dashed_line_gap_ratio 3) (svgprecision 4)"
        " (plotframeref no) (viasonmask no) (mode 1) (useauxorigin no)"
        " (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15)"
        " (pdf_front_fp_property_popups yes) (pdf_back_fp_property_popups yes)"
        " (dxfpolygonmode yes) (dxfimperialunits yes) (dxfusepcbnewlayercoords no)"
        " (plotreference yes) (plotvalue yes) (plotfptext yes) (plotinvisibletext no)"
        " (sketchpadsonfab no) (subtractmaskfromsilk no) (outputformat 1) (mirror no)"
        ' (drillshape 1) (scaleselection 1) (outputdirectory "gerbers/")))'
    )
    for n, nid in NET_ID.items():
        out.append(f'  (net {nid} "{n}")')
    out.append(
        f'  (gr_rect (start 0 0) (end {BOARD_W} {BOARD_H}) (stroke (width 0.15) (type solid))'
        f' (fill none) (layer "Edge.Cuts") (uuid {uid()}))'
    )
    for fp in parts:
        out.append(emit_fp(fp))
    for s in cu.segments:
        out.append(emit_segment(s[0], s[1], s[2], s[3], s[4], s[5], s[7]))
    seen = set()
    nvia = 0
    for x, y, nid, _n in cu.vias:
        key = (round(x, 2), round(y, 2), nid)
        if key in seen:
            continue
        seen.add(key)
        out.append(emit_via(x, y, nid))
        nvia += 1
    out.append(
        "  (zone\n"
        "    (net 1)\n"
        '    (net_name "GND")\n'
        '    (layer "B.Cu")\n'
        f"    (uuid {uid()})\n"
        "    (hatch edge 0.5)\n"
        "    (connect_pads (clearance 0.3))\n"
        "    (min_thickness 0.25)\n"
        "    (filled_areas_thickness no)\n"
        "    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))\n"
        f"    (polygon (pts (xy 1.5 1.5) (xy {BOARD_W-1.5} 1.5) (xy {BOARD_W-1.5} {BOARD_H-1.5}) (xy 1.5 {BOARD_H-1.5})))\n"
        "  )"
    )
    for t, x, y, s in [
        # Mid-band under DevKit silk, above field refs
        ("ViaAccess carrier-v0", 44, 43.0, 0.9),
        # Field terminals — pin order left → right (same as footprint pads)
        ("LOCK COM/NO/NC", 18, 58, 0.75),
        ("DOOR REED/GND", 35, 58, 0.75),
        ("REX SIG/GND", 47, 58, 0.75),
        ("READER 5V/GND/TX/RX", 58, 58, 0.7),
        # Internal headers (L→R) — north edge; J13/J14/J9 refs sit south of pads
        ("LED GND/R/G/B", 6, 2.2, 0.65),
        ("RTC 3V3/GND/SDA/SCL", 22, 2.2, 0.65),
        ("RELAY VCC/GND/IN", 38, 2.2, 0.65),
        ("DRY COM/NO/NC", 6, 42, 0.7),
        ("DCIN XH +/GND", 70, 44, 0.7),
        ("BUZZ +/−", 76, 8, 0.65),
        ("Q2 RELAY", 62, 10, 0.7),
        ("Q1 BUZZ", 68, 36, 0.7),
    ]:
        out.append(emit_text(t, x, y, s))
    out.append(")")
    return "\n".join(out) + "\n", errs, len(cu.segments), nvia, notes


def main():
    text, errs, nseg, nvia, notes = generate()
    OUT.write_text(text)
    print(f"Wrote {OUT} segs={nseg} vias={nvia} {notes}")
    print(f"local_drc errors: {len(errs)}")
    for e in errs[:40]:
        print(" ", e)
    if len(errs) > 40:
        print(f"  ... +{len(errs) - 40} more")


if __name__ == "__main__":
    main()
