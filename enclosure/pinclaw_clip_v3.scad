// ============================================================
// Pinclaw Clip v3.0 — Magnetic Lapel Mic Design
// ============================================================
//
// Design concept (like Humane AI Pin / lavalier mic):
//   FRONT PLATE (main unit) ← magnets → BACK PLATE (magnet disc)
//            ↕ clothing fabric sandwiched between ↕
//   USB-C cable exits from front plate edge
//
// Two separate pieces + magnets hold through fabric.
// Cable serves as tether, data, and charging.
//
// Display modes:
//   0 = assembled (front + back + components, semi-transparent)
//   1 = front plate only (for printing)
//   2 = back plate only (for printing)
//   3 = exploded view (default)
//   4 = components only (layout check)
//   5 = cross-section
//   6 = wearable simulation (fabric between plates)
// ============================================================

$fn = 60;

/* [Display] */
display_mode = 3;

// ============================================================
// MAIN DIMENSIONS
// ============================================================

/* [Front Plate Shape] */
// Using teardrop/egg: hull of large circle + small circle
// Wide end (battery side)
wide_r = 16;           // radius of wide end circle
wide_y = 0;            // position
// Narrow end (USB-C cable exit)
narrow_r = 9;          // radius of narrow end
narrow_y = 22;         // distance from wide center to narrow center
// This creates a shape roughly 44mm long x 32mm wide

/* [Front Plate Dimensions] */
front_h = 11;          // total front plate height/thickness
wall = 1.5;            // shell wall thickness
dome_h = 1.5;          // slight dome on top surface
edge_r = 1.0;          // edge rounding

/* [Back Plate] */
back_dia = 25;         // back plate diameter
back_h = 3;            // back plate thickness
back_edge_r = 1.0;
magnet_dia = 8;        // neodymium disc magnet diameter
magnet_h = 2;          // magnet height
magnet_count = 3;      // number of magnets
magnet_ring_r = 7;     // distance from center to magnet center

/* [Cable Exit] */
cable_dia = 3.5;       // USB-C cable outer diameter
cable_exit_y = 0;      // exits from narrow end
cable_strain_l = 6;    // strain relief length
cable_strain_w = 7;    // strain relief width

/* [Magnets in Front Plate] */
// Same magnet specs, positions match back plate
front_magnet_ring_r = 7;

// ============================================================
// HARDWARE COMPONENTS
// ============================================================

/* [XIAO nRF52840 Sense] */
// 21 x 17.5 x 4.5mm (with components)
xiao_l = 21;
xiao_w = 17.5;
xiao_pcb_h = 1.0;
xiao_total_h = 4.5;
// Position: in the wide area, USB-C faces narrow end
xiao_x = 0;
xiao_y = -1;           // shifted towards wide end
xiao_z = 0;            // sits on top of battery

// USB-C on XIAO
usbc_w = 9.0;
usbc_h = 3.2;

/* [Tactile Switch 4x4x0.8mm] */
btn_l = 4;
btn_w = 4;
btn_h = 0.8;
// Big button cap on top of front plate
btn_cap_dia = 14;      // large, easy to press
btn_cap_h = 0.8;
btn_hole_dia = 5;

/* [LiPo Battery 502030] */
// 30 x 20 x 5mm
bat_l = 30;
bat_w = 20;
bat_h = 5;
// Position: bottom layer, centered in wide area
bat_x = 0;
bat_y = -2;

/* [Vibration Motor LCM-0827] */
// 8mm dia x 2.7mm coin
vib_dia = 8;
vib_h = 2.7;

/* [WS2812B LED] */
led_size = 5;          // 5x5mm
led_h = 1.6;

/* [Tolerances] */
tol = 0.3;

// ============================================================
// 2D SHAPE: Teardrop / Egg profile
// ============================================================

module egg_2d() {
    hull() {
        // Wide end (battery area)
        translate([0, wide_y])
            circle(r=wide_r);
        // Narrow end (cable exit)
        translate([0, narrow_y])
            circle(r=narrow_r);
    }
}

module egg_3d(h) {
    linear_extrude(height=h)
        egg_2d();
}

// Egg with minkowski rounding on edges
module egg_rounded(h, r) {
    minkowski() {
        egg_3d(h - r*2);
        sphere(r=r);
    }
}

// ============================================================
// FRONT PLATE (Main Unit)
// ============================================================

module front_plate() {
    floor_z = wall;
    bat_top = floor_z + bat_h;          // 6.5mm
    xiao_top = bat_top + xiao_total_h;  // 11mm

    difference() {
        union() {
            // Outer shell with slight dome
            hull() {
                egg_3d(front_h - dome_h);
                // Dome: slightly smaller egg at top
                translate([0, 0, front_h - dome_h])
                    scale([0.97, 0.97, 1])
                        egg_3d(dome_h);
            }

            // Strain relief bump for cable exit
            translate([0, narrow_y + narrow_r - 2, front_h/2])
                rotate([-90, 0, 0])
                    scale([1, 0.7, 1])
                        cylinder(d=cable_strain_w,
                                h=cable_strain_l, $fn=24);
        }

        // Hollow interior
        translate([0, 0, wall])
            offset_egg_3d(front_h, -wall);

        // --- Openings ---

        // Button hole (center-top, slightly towards wide end)
        translate([0, 2, -0.1])
            cylinder(d=btn_hole_dia, h=front_h + dome_h + 1);

        // Microphone holes (array near XIAO mic position)
        // Main mic hole
        translate([4, xiao_y + 3, -0.1])
            cylinder(d=1.8, h=wall + 0.2, $fn=20);
        // Secondary holes for better pickup
        for (dx = [-2, 0, 2])
            for (dy = [-2, 0, 2])
                if (dx != 0 || dy != 0)
                    translate([4 + dx*2, xiao_y + 3 + dy*2, -0.1])
                        cylinder(d=0.8, h=wall + 0.2, $fn=12);

        // LED window (small, on the side near narrow end)
        translate([0, narrow_y - 2, front_h * 0.7])
            rotate([-90, 0, 0])
                cylinder(d=3, h=narrow_r + wall + 1, $fn=20);

        // Cable hole (through strain relief, narrow end)
        translate([0, narrow_y + narrow_r - 3, front_h/2])
            rotate([-90, 0, 0])
                cylinder(d=cable_dia + tol,
                        h=cable_strain_l + 5, $fn=24);

        // USB-C port internal clearance (XIAO USB faces narrow end)
        translate([-usbc_w/2 - tol, xiao_y + xiao_l/2,
                   wall + bat_h + xiao_pcb_h - 0.5])
            cube([usbc_w + tol*2, 15, usbc_h + tol*2]);

        // Magnet pockets (recessed from bottom)
        for (i = [0:magnet_count-1]) {
            angle = i * (360/magnet_count) + 90;
            translate([front_magnet_ring_r * cos(angle),
                      front_magnet_ring_r * sin(angle) + 3,
                      -0.1])
                cylinder(d=magnet_dia + tol, h=magnet_h + 0.2, $fn=30);
        }

        // Speaker grille (bottom, under battery area offset)
        translate([-8, -6, -0.1])
            for (dx = [-4:2.5:4])
                for (dy = [-4:2.5:4])
                    if (dx*dx + dy*dy < 25)
                        translate([dx, dy, 0])
                            cylinder(d=1.0, h=wall + 0.2, $fn=12);
    }

    // --- Internal mounting ---

    // Battery cradle (thin walls around battery)
    translate([bat_x, bat_y, wall]) {
        difference() {
            translate([-(bat_l/2 + 1), -(bat_w/2 + 1), 0])
                cube([bat_l + 2, bat_w + 2, bat_h]);
            translate([-bat_l/2, -bat_w/2, -0.1])
                cube([bat_l, bat_w, bat_h + 0.2]);
        }
    }

    // XIAO standoff posts (on top of battery cradle walls)
    for (dx = [-xiao_w/2 + 2, xiao_w/2 - 2])
        for (dy = [-xiao_l/2 + 2, xiao_l/2 - 2])
            translate([dx + xiao_x, dy + xiao_y,
                      wall + bat_h])
                cylinder(d=2, h=0.5, $fn=12);

    // Button support pillar (from XIAO top to shell ceiling)
    translate([0, 2, wall + bat_h + xiao_total_h])
        cylinder(d=btn_hole_dia - 1, h=0.5, $fn=24);
}

// Helper: offset egg (smaller egg for cavity)
module offset_egg_3d(h, offset) {
    hull() {
        translate([0, wide_y])
            cylinder(r=wide_r + offset, h=h);
        translate([0, narrow_y])
            cylinder(r=narrow_r + offset, h=h);
    }
}

// ============================================================
// BACK PLATE (Magnet Disc)
// ============================================================

module back_plate() {
    difference() {
        // Simple rounded disc
        hull() {
            cylinder(d=back_dia, h=back_h - back_edge_r);
            translate([0, 0, back_edge_r])
                cylinder(d=back_dia, h=back_h - back_edge_r*2);
        }

        // Magnet pockets (recessed from top = clothing side)
        for (i = [0:magnet_count-1]) {
            angle = i * (360/magnet_count) + 90;
            translate([magnet_ring_r * cos(angle),
                      magnet_ring_r * sin(angle),
                      back_h - magnet_h])
                cylinder(d=magnet_dia + tol, h=magnet_h + 0.1, $fn=30);
        }

        // Center dimple (alignment mark / aesthetic)
        translate([0, 0, back_h - 0.3])
            cylinder(d=3, h=0.4, $fn=20);
    }
}

// ============================================================
// HARDWARE COMPONENT MODELS
// ============================================================

module comp_xiao() {
    z_base = wall + bat_h;
    translate([xiao_x, xiao_y, z_base]) {
        // PCB
        color("DarkGreen", 0.9)
            translate([-xiao_w/2, -xiao_l/2, 0])
                cube([xiao_w, xiao_l, xiao_pcb_h]);

        // Components on top
        color("DimGray", 0.9) {
            // nRF52840 SoC (shielded module)
            translate([-4, -3, xiao_pcb_h])
                cube([8, 8, 2.2]);
            // PDM Microphone
            translate([4, -1.5, xiao_pcb_h])
                cube([3, 3, 1.5]);
            // IMU
            translate([-6, 3, xiao_pcb_h])
                cube([3.5, 3, 1.2]);
        }

        // USB-C (faces narrow end = +Y direction)
        color("Silver", 0.9)
            translate([-usbc_w/2, xiao_l/2 - 1, xiao_pcb_h - 0.5])
                cube([usbc_w, 7, usbc_h]);

        // Castellated pads (both long edges)
        color("Gold", 0.8)
            for (i = [0:6])
                for (side = [-1, 1]) {
                    translate([side * (xiao_w/2 - 0.6),
                              -xiao_l/2 + 2 + i * 2.54,
                              0])
                        cube([1.2, 1.2, xiao_pcb_h]);
                }

        // Board buttons
        color("White", 0.8) {
            // Reset
            translate([-xiao_w/2 + 1, -1, xiao_pcb_h])
                cube([2, 2.5, 0.8]);
            // User
            translate([xiao_w/2 - 3, -1, xiao_pcb_h])
                cube([2, 2.5, 0.8]);
        }
    }
}

module comp_battery() {
    translate([bat_x, bat_y, wall]) {
        // Cell body
        color("RoyalBlue", 0.85)
            translate([-bat_l/2, -bat_w/2, 0])
                cube([bat_l, bat_w, bat_h]);
        // Protection PCB + wires
        color("DarkRed", 0.7)
            translate([-bat_l/2 - 2, -3, 0.5])
                cube([2, 6, 3]);
        // JST wires
        color("Red") translate([-bat_l/2 - 2, -1, 2])
            cube([3, 0.8, 0.4]);
        color("Black") translate([-bat_l/2 - 2, 1, 2])
            cube([3, 0.8, 0.4]);
    }
}

module comp_tact_switch() {
    z = wall + bat_h + xiao_total_h;
    translate([0, 2, z]) {
        // Switch body
        color("Silver", 0.9)
            translate([-btn_l/2, -btn_w/2, 0])
                cube([btn_l, btn_w, btn_h]);
        // Actuator
        color("DarkGray")
            cylinder(d=2, h=btn_h + 0.2, $fn=16);
    }
}

module comp_button_cap() {
    z = front_h - btn_cap_h;
    translate([0, 2, z]) {
        color("DarkSlateGray", 0.85) {
            // Flat disc with dome
            cylinder(d=btn_cap_dia, h=btn_cap_h, $fn=48);
            translate([0, 0, btn_cap_h])
                scale([1, 1, 0.25])
                    sphere(d=btn_cap_dia, $fn=48);
            // Plunger shaft going down
            translate([0, 0, -2])
                cylinder(d=3.5, h=2, $fn=16);
        }
    }
}

module comp_vib_motor() {
    // Tucked in corner of wide end
    translate([-10, -8, wall]) {
        color("OrangeRed", 0.85)
            cylinder(d=vib_dia, h=vib_h, $fn=30);
        // Wires
        color("Red", 0.6)
            translate([0, vib_dia/2, vib_h/2])
                rotate([90, 0, 90])
                    cylinder(d=0.5, h=4, $fn=8);
    }
}

module comp_led() {
    // Near the narrow end, visible through LED window
    translate([0, narrow_y - 5, wall + bat_h]) {
        color("White", 0.9)
            translate([-led_size/2, -led_size/2, 0])
                cube([led_size, led_size, led_h]);
        // Glow
        color("Cyan", 0.2)
            translate([0, 0, led_h])
                sphere(d=4, $fn=16);
    }
}

module comp_magnets_front() {
    for (i = [0:magnet_count-1]) {
        angle = i * (360/magnet_count) + 90;
        translate([front_magnet_ring_r * cos(angle),
                  front_magnet_ring_r * sin(angle) + 3,
                  0])
            color("Gray", 0.7)
                cylinder(d=magnet_dia, h=magnet_h, $fn=24);
    }
}

module comp_magnets_back() {
    for (i = [0:magnet_count-1]) {
        angle = i * (360/magnet_count) + 90;
        translate([magnet_ring_r * cos(angle),
                  magnet_ring_r * sin(angle),
                  back_h - magnet_h])
            color("Gray", 0.7)
                cylinder(d=magnet_dia, h=magnet_h, $fn=24);
    }
}

module comp_cable() {
    // USB-C cable exiting from narrow end
    translate([0, narrow_y + narrow_r + cable_strain_l - 2, front_h/2])
        rotate([-90, 0, 0]) {
            // Cable
            color("DarkGray", 0.7)
                cylinder(d=cable_dia, h=30, $fn=16);
            // USB-C plug end
            color("Silver", 0.8)
                translate([0, 0, 28])
                    scale([1, 1, 1])
                        cylinder(d=cable_dia + 2, h=12, $fn=20);
        }
}

module all_front_components() {
    comp_battery();
    comp_xiao();
    comp_tact_switch();
    comp_vib_motor();
    comp_led();
    comp_magnets_front();
}

// ============================================================
// FABRIC (for wearable simulation)
// ============================================================

module fabric_layer() {
    color("Khaki", 0.4)
        translate([-30, -20, -1.5])
            cube([60, 60, 1.5]);
}

// ============================================================
// DISPLAY MODES
// ============================================================

if (display_mode == 0) {
    // --- Assembled view ---
    // Front plate (semi-transparent)
    color("DimGray", 0.25) front_plate();
    all_front_components();
    comp_button_cap();
    comp_cable();
    // Back plate below (after fabric gap)
    translate([0, 3, -(3 + back_h)])  // 3mm gap for fabric
        color("DimGray", 0.5) back_plate();
    translate([0, 3, -(3 + back_h)])
        comp_magnets_back();
}

if (display_mode == 1) {
    // --- Front plate only (for printing) ---
    front_plate();
}

if (display_mode == 2) {
    // --- Back plate only (for printing) ---
    back_plate();
}

if (display_mode == 3) {
    // --- Exploded view ---
    // Button cap (top)
    translate([0, 0, 22]) comp_button_cap();

    // Front plate shell
    translate([0, 0, 8])
        color("DimGray", 0.3) front_plate();

    // Components (in layout position)
    all_front_components();

    // Cable
    translate([0, 0, 8]) comp_cable();

    // Fabric
    translate([0, 0, -8]) fabric_layer();

    // Back plate
    translate([0, 3, -18])
        color("DimGray", 0.5) back_plate();
    translate([0, 3, -18])
        comp_magnets_back();
}

if (display_mode == 4) {
    // --- Components only (check layout) ---
    all_front_components();
    comp_button_cap();
    // Ghost outline of shell
    %front_plate();
}

if (display_mode == 5) {
    // --- Cross-section ---
    difference() {
        union() {
            color("DimGray", 0.3) front_plate();
            all_front_components();
            comp_button_cap();
        }
        // Cut front half
        translate([-50, -50, -1])
            cube([50, 100, front_h + 5]);
    }
}

if (display_mode == 6) {
    // --- Wearable simulation ---
    // Front plate on top of fabric
    translate([0, 0, 1.5])  // fabric thickness
    {
        color("DimGray", 0.35) front_plate();
        all_front_components();
        comp_button_cap();
        comp_cable();
    }
    // Fabric
    fabric_layer();
    // Back plate under fabric
    translate([0, 3, -back_h])  {
        color("DimGray", 0.6) back_plate();
        comp_magnets_back();
    }
}

// ============================================================
// Console Output
// ============================================================
echo("=== Pinclaw Clip v3 — Magnetic Lapel Design ===");
echo(str("Front plate: ~", (wide_r + narrow_r) + narrow_y, "mm long x ",
         wide_r * 2, "mm wide x ", front_h, "mm thick"));
echo(str("Back plate: ", back_dia, "mm dia x ", back_h, "mm thick"));
echo(str("Component stack: battery(", bat_h, ") + XIAO(",
         xiao_total_h, ") = ", bat_h + xiao_total_h, "mm"));
echo(str("Internal clearance: ", front_h - wall*2, "mm vs stack: ",
         bat_h + xiao_total_h, "mm"));
echo(str("Magnets: ", magnet_count, "x ", magnet_dia, "mm dia"));
