// Case v0 — parametric shell for carrier-v0 (90 × 60 mm PCB).
// Interior: PCB M3 bosses, lid bosses, PG strain-relief ties,
//           KY-016 + DS3231 M2 screw bosses, relay cradle (no module holes).
// Dimensions: millimetres. PCB origin = board bottom-left on the standoff plane.
//
// Carrier headers (PCB xy): J13 LED (12, 5.5), J14 RTC (28, 5.5), J9 RELAY (44, 5.5).
// Modules hang off the −Y edge into clearance_s — tune hole xy after your STEP fit.
//
// Export:
//   openscad -o case-v0-base.stl -D 'part="base"' case-v0.scad
//   openscad -o case-v0-lid.stl  -D 'part="lid"'  case-v0.scad
//   openscad -o case-v0.stl      -D 'part="preview"' case-v0.scad

/* [Which part] */
part = "preview"; // ["preview", "base", "lid", "base_cutaway"]

/* [PCB] */
pcb_w = 90;
pcb_h = 60;
pcb_thickness = 1.6;
standoff_h = 7;
pcb_boss_od = 9;
pcb_boss_id = 3.2;
use_heatset = true;
insert_od_m3 = 4.0;
insert_od_m2 = 3.2;          // short M2 brass insert
insert_depth = 5.0;
mounts = [
  [3.5, 3.5],
  [86.5, 3.5],
  [3.5, 56.5],
  [86.5, 56.5]
];

/* [Outer shell — asymmetric Y: more room on module (−Y) side] */
wall = 2.4;
rim = 1.6;
clearance_w = 10;            // −X (USB / DC)
clearance_e = 10;            // +X
clearance_s = 22;            // −Y — LED / RTC / relay bodies past PCB edge
clearance_n = 10;            // +Y — field terminals / PG service loop
inner_z = 42;
lid_h = 3.0;
lid_clearance = 0.35;
floor_t = wall;

/* [Lid screws] */
lid_boss_od = 10;
lid_boss_inset = 6;
lid_screw_clear = 3.2;
lid_countersink_d = 6.0;
lid_countersink_h = 1.6;

/* [Cable wall — +Y] */
pg7 = 12.5;
pg9 = 15.2;
gland_z = 18;
gland_spacing = 18;
gland_ds = [pg7, pg7, pg7, pg9];

/* [Power wall — −X] */
dc_jack_d = 11;
dc_jack_z = 14;
usb_w = 10;
usb_h = 4.5;
usb_z = 22;
usb_y = 30;

/* [Cable-tie anchors near +Y] */
tie_count = 4;
tie_slot_w = 3.5;
tie_slot_h = 2.2;
tie_bridge = 2.0;

/* [Daughter modules — PCB coordinates; headers at y=5.5]
   Boss top ≈ carrier top + male header stack (module PCB sits on pins). */
module_boss_h = standoff_h + pcb_thickness + 8.5; // ~17.1 mm from floor top of boss
module_boss_od = 7;
// KY-016 — two M2 holes south of J13 (12, 5.5); ~12 mm spacing — measure your board
led_holes_pcb = [
  [6, -6],
  [18, -6]
];
// ZS-042 DS3231 — two M2/M2.5 holes south of J14 (28, 5.5); ~28 mm span — measure
rtc_holes_pcb = [
  [14, -8],
  [42, -8]
];
// 1-ch 5 V relay — no screw holes: cradle + zip-tie. Body ~50×26, south of J9 (44, 5.5)
relay_center_pcb = [44, -10];
relay_pocket_w = 52;         // X
relay_pocket_d = 28;         // Y
relay_wall = 2.0;
relay_inner_w = 46;
relay_inner_d = 22;

/* [Preview] */
show_pcb_ghost = true;
show_module_ghosts = true;

inner_w = pcb_w + clearance_w + clearance_e;
inner_d = pcb_h + clearance_s + clearance_n;
outer_w = inner_w + 2 * wall;
outer_d = inner_d + 2 * wall;
outer_h = floor_t + inner_z + rim;

// PCB origin in case coordinates
function pcb_xy(m) = [wall + clearance_w + m[0], wall + clearance_s + m[1]];

function lid_boss_xy(i) =
  let (
    xs = [wall + lid_boss_inset, outer_w - wall - lid_boss_inset],
    ys = [wall + lid_boss_inset, outer_d - wall - lid_boss_inset]
  )
  [xs[i % 2], ys[floor(i / 2)]];

module heatset_bore(h_boss, od) {
  translate([0, 0, h_boss - insert_depth])
    cylinder(h = insert_depth + 0.05, d = od, $fn = 32);
  translate([0, 0, -0.1])
    cylinder(h = h_boss + 0.2, d = 2.0, $fn = 24);
}

module clearance_bore(h_boss, id) {
  translate([0, 0, -0.1])
    cylinder(h = h_boss + 0.2, d = id, $fn = 24);
}

module round_boss(h, od, insert_od) {
  difference() {
    union() {
      cylinder(h = h, d = od, $fn = 36);
      cylinder(h = 1.6, d1 = od + 3, d2 = od, $fn = 36);
    }
    if (use_heatset) heatset_bore(h, insert_od);
    else clearance_bore(h, insert_od - 0.8);
  }
}

module pcb_mount_bosses() {
  for (m = mounts) {
    xy = pcb_xy(m);
    translate([xy[0], xy[1], floor_t])
      round_boss(standoff_h, pcb_boss_od, insert_od_m3);
  }
}

module lid_mount_bosses() {
  h = inner_z;
  for (i = [0:3]) {
    xy = lid_boss_xy(i);
    translate([xy[0], xy[1], floor_t])
      round_boss(h, lid_boss_od, insert_od_m3);
  }
}

module module_screw_bosses() {
  // LED + RTC — M2 heat-set; screw from module top into boss
  for (m = concat(led_holes_pcb, rtc_holes_pcb)) {
    xy = pcb_xy(m);
    translate([xy[0], xy[1], floor_t])
      round_boss(module_boss_h, module_boss_od, insert_od_m2);
  }
}

module relay_cradle() {
  // Open toward +Y (toward J9 header). Zip-tie slots on ±X walls.
  c = pcb_xy(relay_center_pcb);
  translate([
    c[0] - relay_pocket_w / 2,
    c[1] - relay_pocket_d / 2,
    floor_t
  ]) {
    difference() {
      cube([relay_pocket_w, relay_pocket_d, module_boss_h]);
      // pocket
      translate([
        (relay_pocket_w - relay_inner_w) / 2,
        (relay_pocket_d - relay_inner_d) / 2,
        1.2
      ])
        cube([relay_inner_w, relay_inner_d, module_boss_h]);
      // open face toward PCB / header (+Y in cradle local = higher y)
      translate([
        (relay_pocket_w - relay_inner_w) / 2,
        relay_pocket_d / 2,
        1.2
      ])
        cube([relay_inner_w, relay_pocket_d / 2 + 0.1, module_boss_h]);
      // zip-tie slots left/right
      for (sx = [relay_wall, relay_pocket_w - relay_wall - tie_slot_w]) {
        translate([sx, relay_pocket_d / 2 - 4, module_boss_h - 6])
          cube([tie_slot_w, 8, 4]);
      }
    }
  }
}

module cable_tie_anchors() {
  n = tie_count;
  usable = inner_w - 16;
  step = usable / (n - 1);
  y = outer_d - wall - 6;
  z = floor_t + 8;
  for (i = [0:n - 1]) {
    x = wall + 8 + i * step;
    translate([x - 4, y - 3, z]) {
      difference() {
        cube([8, 6, tie_bridge + tie_slot_h]);
        translate([1.5, -0.1, tie_bridge])
          cube([tie_slot_w, 6.2, tie_slot_h + 0.2]);
        translate([8 - 1.5 - tie_slot_w, -0.1, tie_bridge])
          cube([tie_slot_w, 6.2, tie_slot_h + 0.2]);
      }
    }
  }
}

module cable_wall_holes() {
  n = len(gland_ds);
  total = (n - 1) * gland_spacing;
  start_x = (outer_w - total) / 2;
  for (i = [0:n - 1]) {
    translate([start_x + i * gland_spacing, outer_d + 0.1, gland_z])
      rotate([90, 0, 0])
        cylinder(h = wall + 0.4, d = gland_ds[i], $fn = 48);
  }
}

module power_wall_holes() {
  translate([-0.1, outer_d / 2, dc_jack_z])
    rotate([0, 90, 0])
      cylinder(h = wall + 0.4, d = dc_jack_d, $fn = 48);
  translate([-0.1, usb_y - usb_w / 2, usb_z - usb_h / 2])
    cube([wall + 0.4, usb_w, usb_h]);
}

module base_shell() {
  difference() {
    cube([outer_w, outer_d, outer_h]);
    translate([wall, wall, floor_t])
      cube([inner_w, inner_d, inner_z + rim + 0.1]);
    cable_wall_holes();
    power_wall_holes();
  }
  pcb_mount_bosses();
  lid_mount_bosses();
  module_screw_bosses();
  relay_cradle();
  cable_tie_anchors();
}

module lid() {
  // DevKitC-1 WS2812 sits near the USB end of the module (PCB left / −X).
  led_xy = pcb_xy([18, 30]);
  difference() {
    cube([outer_w, outer_d, lid_h]);
    // status LED window over onboard WS2812 (KY-016 optional elsewhere)
    translate([led_xy[0] - 5, led_xy[1] - 5, -0.1])
      cube([10, 10, lid_h + 0.2]);
    for (i = [0:3]) {
      xy = lid_boss_xy(i);
      translate([xy[0], xy[1], -0.1])
        cylinder(h = lid_h + 0.2, d = lid_screw_clear, $fn = 24);
      translate([xy[0], xy[1], lid_h - lid_countersink_h])
        cylinder(h = lid_countersink_h + 0.05, d1 = lid_screw_clear, d2 = lid_countersink_d, $fn = 32);
    }
  }
  translate([wall + lid_clearance, wall + lid_clearance, -rim + 0.15])
    difference() {
      cube([
        inner_w - 2 * lid_clearance,
        inner_d - 2 * lid_clearance,
        rim
      ]);
      translate([1.4, 1.4, -0.1])
        cube([
          inner_w - 2 * lid_clearance - 2.8,
          inner_d - 2 * lid_clearance - 2.8,
          rim + 0.2
        ]);
      for (i = [0:3]) {
        xy = lid_boss_xy(i);
        translate([xy[0] - (wall + lid_clearance), xy[1] - (wall + lid_clearance), -0.1])
          cylinder(h = rim + 0.2, d = lid_boss_od + 1.2, $fn = 32);
      }
    }
}

module pcb_ghost() {
  color([0.2, 0.6, 0.3, 0.35])
    translate([wall + clearance_w, wall + clearance_s, floor_t + standoff_h])
      cube([pcb_w, pcb_h, pcb_thickness]);
}

module module_ghosts() {
  // rough bodies for FreeCAD clearance check
  color([0.9, 0.5, 0.1, 0.35]) {
    xy = pcb_xy([12, -8]);
    translate([xy[0] - 8, xy[1] - 6, floor_t + module_boss_h])
      cube([16, 14, 8]); // KY-016
  }
  color([0.2, 0.4, 0.9, 0.35]) {
    xy = pcb_xy([28, -10]);
    translate([xy[0] - 16, xy[1] - 8, floor_t + module_boss_h])
      cube([38, 22, 12]); // ZS-042
  }
  color([0.5, 0.5, 0.5, 0.35]) {
    xy = pcb_xy(relay_center_pcb);
    translate([xy[0] - 25, xy[1] - 13, floor_t + 1.2])
      cube([50, 26, 18]); // relay
  }
}

module base_cutaway() {
  difference() {
    base_shell();
    translate([outer_w / 2, -1, -1])
      cube([outer_w, outer_d + 2, outer_h + 2]);
  }
}

if (part == "base") {
  base_shell();
} else if (part == "lid") {
  lid();
} else if (part == "base_cutaway") {
  base_cutaway();
  if (show_pcb_ghost) pcb_ghost();
  if (show_module_ghosts) module_ghosts();
} else {
  base_shell();
  translate([0, 0, outer_h + 3]) lid();
  if (show_pcb_ghost) pcb_ghost();
  if (show_module_ghosts) module_ghosts();
}
