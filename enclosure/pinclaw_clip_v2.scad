// ============================================================
// Pinclaw Clip Enclosure v2.0
// Rounded-triangle (guitar pick) shape
// Reference: pinclaw.ai product renders
// Large center button, clip on back
// ============================================================
//
// All hardware components modeled to scale for
// interference checking. Transparent shell + colored
// components in assembled/exploded views.
//
// Display modes:
//   0 = assembled (transparent shell + components)
//   1 = bottom shell only (for print)
//   2 = top lid only (for print, flipped)
//   3 = clip only (for print)
//   4 = exploded view (default)
//   5 = components only (no shell, check layout)
//   6 = cross-section view
// ============================================================

// ---- Global settings ----
$fn = 60;

/* [Display] */
display_mode = 4; // [0:Assembled, 1:Bottom, 2:Top, 3:Clip, 4:Exploded, 5:Components, 6:Cross-section]

/* [Shell Dimensions] */
// The three corner radii of the rounded triangle
corner_r = 8;          // radius at each vertex
// Triangle "size" - distance from center to each vertex
tri_radius = 19;       // controls overall size
// How much to rotate the triangle shape (degrees)
tri_rotation = -90;    // point faces towards USB-C end
// Overall height
shell_height = 14;
// Wall thickness
wall = 1.5;
// Top dome extra height
dome_h = 2;
// Fillet radius for top/bottom edges
edge_fillet = 1.5;

/* [Split] */
// Bottom shell height (from base)
bottom_h = 9;
// Top lid height
top_h = shell_height - bottom_h;

/* [XIAO nRF52840 Sense - 21x17.5x4.5mm] */
xiao_pcb_l = 21;       // length (USB-C direction)
xiao_pcb_w = 17.5;     // width
xiao_pcb_h = 1.0;      // bare PCB thickness
xiao_comp_h = 3.5;     // component height above PCB
xiao_total_h = 4.5;    // total height
// Position: centered, shifted towards the pointed end
xiao_x = 2;            // offset towards USB end
xiao_y = 0;
xiao_z = 1.5;          // standoff height from floor

// USB-C connector dimensions (on XIAO)
usbc_w = 9.0;
usbc_h = 3.2;
usbc_depth = 7.5;      // how far it sticks from PCB edge

/* [Tactile Button - 4x4x0.8mm SMD] */
// User's purchased switch
btn_body_l = 4;
btn_body_w = 4;
btn_body_h = 0.8;
// Center button position (on top of shell)
btn_x = 0;
btn_y = 0;
// Button cap diameter (the big press area on top)
btn_cap_dia = 12;      // large center button cap
btn_cap_h = 1.0;
// Button hole in shell
btn_hole_dia = 5;      // hole for plunger/cap mechanism

/* [LiPo Battery 502030 - 30x20x5mm] */
bat_l = 30;
bat_w = 20;
bat_h = 5;
// Battery position (under the XIAO, shifted to wide end)
bat_x = -3;
bat_y = 0;
bat_z = 1.5;           // wall thickness = floor

/* [MAX98357A Breakout - 19.4x17.8x3mm] */
amp_l = 19.4;
amp_w = 17.8;
amp_h = 3.0;
// Position: stacked near battery or beside XIAO
amp_x = -3;
amp_y = 0;
amp_z = 1.5;           // on floor

/* [Speaker - 15mm dia x 2.5mm] */
spk_dia = 15;
spk_h = 2.5;
spk_x = -5;
spk_y = 0;

/* [Vibration Motor LCM-0827 - 8mm dia x 2.7mm] */
vib_dia = 8;
vib_h = 2.7;
vib_x = 10;
vib_y = -8;

/* [WS2812B LED - 5x5x1.6mm] */
led_l = 5;
led_w = 5;
led_h = 1.6;
led_x = 12;
led_y = 5;

/* [Clip] */
clip_l = 22;
clip_w = 10;
clip_thick = 1.2;
clip_gap = 3;

/* [Tolerances] */
tol = 0.3;             // printing tolerance

// ============================================================
// Utility: Rounded Triangle 2D profile
// ============================================================
// Creates a 2D rounded triangle (3 circles hulled)
// centered at origin

module rounded_triangle_2d(r_outer, r_corner) {
    hull() {
        for (i = [0:2]) {
            angle = i * 120 + tri_rotation;
            translate([r_outer * cos(angle), r_outer * sin(angle)])
                circle(r=r_corner);
        }
    }
}

// 3D rounded triangle extrusion
module rounded_triangle_3d(r_outer, r_corner, h) {
    linear_extrude(height=h)
        rounded_triangle_2d(r_outer, r_corner);
}

// Rounded triangle with slightly domed top
module shell_outer() {
    // Main body
    rounded_triangle_3d(tri_radius, corner_r, shell_height - dome_h);
    // Dome on top (slight curve)
    translate([0, 0, shell_height - dome_h])
        scale([1, 1, dome_h / (tri_radius + corner_r) * 2])
            resize([0, 0, dome_h])
                rounded_triangle_3d(tri_radius, corner_r, dome_h);
}

// Inner cavity
module shell_inner() {
    translate([0, 0, wall])
        rounded_triangle_3d(tri_radius - wall, corner_r - wall/2,
                           shell_height - wall + 1);
}

// ============================================================
// Hardware Component Models (for visualization)
// ============================================================

// XIAO nRF52840 Sense
module xiao_board() {
    translate([xiao_x, xiao_y, xiao_z]) {
        // PCB (green)
        color("DarkGreen", 0.9)
            translate([-xiao_pcb_l/2, -xiao_pcb_w/2, 0])
                cube([xiao_pcb_l, xiao_pcb_w, xiao_pcb_h]);

        // Components on top (dark gray)
        color("DimGray", 0.9) {
            // nRF52840 SoC (center, metal shield)
            translate([-3, -4, xiao_pcb_h])
                cube([8, 8, 2]);
            // PDM Microphone (small, near center-top)
            translate([3, -2, xiao_pcb_h])
                cube([3, 4, 1.5]);
            // IMU (small, offset)
            translate([-7, 2, xiao_pcb_h])
                cube([4, 3, 1.2]);
        }

        // USB-C connector (silver, one end)
        color("Silver", 0.9)
            translate([xiao_pcb_l/2 - 1, -usbc_w/2, xiao_pcb_h - 0.5])
                cube([usbc_depth, usbc_w, usbc_h]);

        // Pin pads (gold, both sides) - castellated
        color("Gold", 0.8) {
            for (i = [0:6]) {
                // Left side pads
                translate([-xiao_pcb_l/2 + 1.5 + i * 2.54, -xiao_pcb_w/2 - 0.2, 0])
                    cube([1.2, 1.5, xiao_pcb_h]);
                // Right side pads
                translate([-xiao_pcb_l/2 + 1.5 + i * 2.54, xiao_pcb_w/2 - 1.3, 0])
                    cube([1.2, 1.5, xiao_pcb_h]);
            }
        }

        // Reset button (tiny, left side)
        color("White", 0.9)
            translate([-2, -xiao_pcb_w/2 + 1, xiao_pcb_h])
                cube([2.5, 2, 0.8]);

        // User button (tiny, right side)
        color("White", 0.9)
            translate([-2, xiao_pcb_w/2 - 3, xiao_pcb_h])
                cube([2.5, 2, 0.8]);

        // Antenna area (PCB trace, far end from USB)
        color("DarkGreen", 0.5)
            translate([-xiao_pcb_l/2, -6, xiao_pcb_h])
                cube([3, 12, 0.1]);
    }
}

// Tactile button switch 4x4x0.8mm
module tact_switch() {
    // The tiny SMD switch (sits on a small carrier PCB or directly on wire)
    translate([btn_x, btn_y, bottom_h - btn_body_h - 0.5]) {
        // Switch body
        color("Silver", 0.9)
            translate([-btn_body_l/2, -btn_body_w/2, 0])
                cube([btn_body_l, btn_body_w, btn_body_h]);
        // Actuator nub
        color("DarkGray")
            cylinder(d=2.5, h=btn_body_h + 0.3);
    }
}

// Big button cap (user presses this, transfers to tact switch)
module button_cap() {
    translate([btn_x, btn_y, bottom_h - 0.5]) {
        color("DarkSlateGray", 0.8) {
            // Flat cap with slight dome
            cylinder(d=btn_cap_dia, h=btn_cap_h);
            // Dome on top
            translate([0, 0, btn_cap_h])
                scale([1, 1, 0.3])
                    sphere(d=btn_cap_dia);
            // Plunger going down to tact switch
            cylinder(d=3, h=-1);
        }
    }
}

// LiPo Battery 502030
module battery() {
    translate([bat_x, bat_y, bat_z]) {
        color("RoyalBlue", 0.8)
            translate([-bat_l/2, -bat_w/2, 0])
                cube([bat_l, bat_w, bat_h]);
        // JST connector + wires
        color("White", 0.9)
            translate([bat_l/2, -3, 0.5])
                cube([3, 6, 4]);
        // Red/black wires
        color("Red", 0.7)
            translate([bat_l/2 + 3, -1, 2])
                cube([5, 1, 0.5]);
        color("Black", 0.7)
            translate([bat_l/2 + 3, 0.5, 2])
                cube([5, 1, 0.5]);
    }
}

// MAX98357A amplifier breakout
module amplifier() {
    translate([amp_x, amp_y, amp_z]) {
        // PCB
        color("Purple", 0.8)
            translate([-amp_l/2, -amp_w/2, 0])
                cube([amp_l, amp_w, amp_h]);
        // Terminal block
        color("DarkGreen", 0.9)
            translate([-amp_l/2, -5, amp_h])
                cube([5, 10, 2.5]);
    }
}

// Speaker
module speaker() {
    translate([spk_x, spk_y, wall]) {
        color("DarkGray", 0.7) {
            cylinder(d=spk_dia, h=spk_h);
            // Cone
            translate([0, 0, spk_h])
                cylinder(d1=spk_dia - 2, d2=spk_dia - 6, h=0.5);
        }
    }
}

// Vibration motor (coin type)
module vib_motor() {
    translate([vib_x, vib_y, wall]) {
        color("OrangeRed", 0.8) {
            cylinder(d=vib_dia, h=vib_h);
            // Wires
            translate([0, vib_dia/2, vib_h/2])
                rotate([0, 90, 90])
                    cylinder(d=0.5, h=5);
        }
    }
}

// WS2812B LED
module ws_led() {
    translate([led_x, led_y, wall]) {
        color("White", 0.9)
            translate([-led_l/2, -led_w/2, 0])
                cube([led_l, led_w, led_h]);
        // Light dome
        color("Yellow", 0.3)
            translate([0, 0, led_h])
                sphere(d=3);
    }
}

// All components
module all_components() {
    xiao_board();
    tact_switch();
    battery();
    // Note: amp and speaker won't both fit in this prototype
    // The amp + speaker are P2 priority, show them for reference
    // amplifier();
    // speaker();
    vib_motor();
    ws_led();
}

// ============================================================
// Enclosure: Bottom Shell
// ============================================================

module bottom_shell() {
    difference() {
        // Outer shape
        rounded_triangle_3d(tri_radius, corner_r, bottom_h);

        // Inner cavity
        translate([0, 0, wall])
            rounded_triangle_3d(tri_radius - wall, corner_r - wall/2,
                               bottom_h);

        // USB-C port cutout
        translate([xiao_x + xiao_pcb_l/2 - 1, -usbc_w/2 - tol,
                   xiao_z + xiao_pcb_h - 0.5 - tol])
            cube([wall * 4 + corner_r, usbc_w + tol*2, usbc_h + tol*2]);

        // LED window (small rectangular cutout on shell wall)
        translate([led_x, led_y, wall + led_h/2])
            rotate([0, 90, 0])
                cylinder(d=3, h=corner_r + wall + 1);

        // Speaker grille holes on bottom
        translate([spk_x, spk_y, -0.1])
            for (x = [-5:2.5:5])
                for (y = [-5:2.5:5])
                    if (x*x + y*y < 36)
                        translate([x, y, 0])
                            cylinder(d=1.2, h=wall + 0.2, $fn=16);
    }

    // XIAO mounting posts (4 corners)
    for (dx = [-xiao_pcb_l/2 + 1.5, xiao_pcb_l/2 - 1.5])
        for (dy = [-xiao_pcb_w/2 + 1.5, xiao_pcb_w/2 - 1.5])
            translate([xiao_x + dx, xiao_y + dy, wall])
                cylinder(d=2.5, h=xiao_z - wall, $fn=16);

    // Battery retaining ridge
    translate([bat_x, bat_y, wall]) {
        // Two thin walls on long edges of battery
        for (side = [-1, 1])
            translate([-bat_l/2 - 0.5, side * (bat_w/2 + 0.5), 0])
                cube([bat_l + 1, wall, bat_h * 0.6]);
    }

    // Alignment lip (rim for top lid)
    difference() {
        translate([0, 0, bottom_h - 2])
            rounded_triangle_3d(tri_radius - wall + 0.5,
                               corner_r - wall/2, 2);
        translate([0, 0, bottom_h - 2 - 0.1])
            rounded_triangle_3d(tri_radius - wall - 0.5,
                               corner_r - wall, 2.2);
        // Don't block the cavity
        translate([0, 0, bottom_h - 2 - 0.1])
            rounded_triangle_3d(tri_radius - wall * 2,
                               corner_r - wall, 2.2);
    }
}

// ============================================================
// Enclosure: Top Lid
// ============================================================

module top_lid() {
    difference() {
        union() {
            // Outer shape with dome
            hull() {
                rounded_triangle_3d(tri_radius, corner_r, top_h - dome_h);
                translate([0, 0, top_h - dome_h])
                    rounded_triangle_3d(tri_radius - dome_h,
                                       corner_r, dome_h);
            }
        }

        // Inner cavity
        translate([0, 0, -0.1])
            rounded_triangle_3d(tri_radius - wall, corner_r - wall/2,
                               top_h - wall + 0.1);

        // Big center button hole
        translate([btn_x, btn_y, -0.1])
            cylinder(d=btn_hole_dia, h=top_h + dome_h + 1, $fn=40);

        // Microphone hole (aligned with XIAO mic)
        translate([xiao_x + 3, xiao_y, -0.1])
            cylinder(d=2.0, h=top_h + dome_h + 1, $fn=24);

        // Extra mic holes (array for better pickup)
        for (a = [0:60:300])
            translate([xiao_x + 3 + 3*cos(a), xiao_y + 3*sin(a), -0.1])
                cylinder(d=1.0, h=top_h + dome_h + 1, $fn=16);

        // Alignment slot (matches bottom lip)
        translate([0, 0, -0.1])
            difference() {
                rounded_triangle_3d(tri_radius - wall + 0.5 + tol,
                                   corner_r - wall/2, 2.2);
                rounded_triangle_3d(tri_radius - wall - 0.5 - tol,
                                   corner_r - wall, 2.4);
            }
    }

    // Button guide tube (centers the button cap)
    translate([btn_x, btn_y, wall]) {
        difference() {
            cylinder(d=btn_hole_dia + 2, h=2, $fn=32);
            cylinder(d=btn_hole_dia + tol, h=2.1, $fn=32);
        }
    }
}

// ============================================================
// Clip Mechanism
// ============================================================

module clip() {
    // Spring clip that friction-fits or screws to bottom
    color("DarkSlateGray", 0.9)
    translate([0, 0, -clip_gap - clip_thick]) {
        difference() {
            union() {
                // Clip plate (matches triangle shape, smaller)
                scale([0.6, 0.5, 1])
                    rounded_triangle_3d(tri_radius, corner_r, clip_thick);

                // Spring arm (connects clip to body at one vertex)
                translate([tri_radius * cos(tri_rotation) * 0.5,
                          tri_radius * sin(tri_rotation) * 0.5, 0])
                    translate([-4, -clip_w/2, 0])
                        cube([8, clip_w, clip_gap + clip_thick]);
            }

            // Hollow out spring arm for flexibility
            translate([tri_radius * cos(tri_rotation) * 0.5,
                      tri_radius * sin(tri_rotation) * 0.5, 0])
                translate([-2.5, -clip_w/2 + 2, -0.1])
                    cube([5, clip_w - 4, clip_gap + clip_thick + 0.2]);
        }

        // Grip texture (small ridges)
        for (i = [-2:1:2])
            translate([i * 4, 0, -0.1])
                cube([1.5, 8, 0.5], center=true);
    }
}

// ============================================================
// Assembly & Display
// ============================================================

module assembled_view() {
    // Bottom shell (semi-transparent)
    color("DimGray", 0.3) bottom_shell();

    // Top lid (semi-transparent)
    translate([0, 0, bottom_h])
        color("SlateGray", 0.3)
            mirror([0, 0, 1]) top_lid();

    // Button cap (visible on top)
    button_cap();

    // Clip
    color("DarkSlateGray", 0.6) clip();

    // Internal components (solid colors)
    all_components();
}

module exploded_view() {
    // Bottom shell
    color("DimGray", 0.4) bottom_shell();

    // Top lid (lifted up)
    translate([0, 0, bottom_h + 18])
        color("SlateGray", 0.4)
            mirror([0, 0, 1]) top_lid();

    // Button cap (floating above lid)
    translate([0, 0, 25])
        button_cap();

    // Clip (dropped down)
    translate([0, 0, -18]) clip();

    // Components (in place, visible through gap)
    translate([0, 0, 2]) all_components();
}

module cross_section() {
    difference() {
        assembled_view();
        // Cut away front half
        translate([0, -50, -20])
            cube([100, 50, 60]);
    }
}

// ---- Main ----

if (display_mode == 0) {
    assembled_view();
} else if (display_mode == 1) {
    bottom_shell();
} else if (display_mode == 2) {
    // Flipped for printing
    translate([0, 0, top_h])
        mirror([0, 0, 1]) top_lid();
} else if (display_mode == 3) {
    clip();
} else if (display_mode == 4) {
    exploded_view();
} else if (display_mode == 5) {
    // Components only - check layout without shell
    all_components();
    // Show shell outline as wireframe
    %rounded_triangle_3d(tri_radius, corner_r, shell_height);
} else if (display_mode == 6) {
    cross_section();
}

// ============================================================
// Dimension Reference (echo to console)
// ============================================================
// Run F5 to see these in the console:

echo("=== Pinclaw Clip v2 Dimensions ===");
echo(str("Shell footprint: ~",
    (tri_radius + corner_r) * 2, "mm across"));
echo(str("Shell height: ", shell_height, "mm"));
echo(str("XIAO board: ", xiao_pcb_l, "x", xiao_pcb_w, "x",
    xiao_total_h, "mm"));
echo(str("Battery: ", bat_l, "x", bat_w, "x", bat_h, "mm"));
echo(str("Button switch: ", btn_body_l, "x", btn_body_w, "x",
    btn_body_h, "mm"));
echo(str("Button cap diameter: ", btn_cap_dia, "mm"));
echo(str("Vib motor: ", vib_dia, "dia x", vib_h, "mm"));
echo(str("Wall thickness: ", wall, "mm"));
