// ============================================================
// Pinclaw Clip Enclosure v1.0
// 42 x 25 x 14mm wearable AI voice assistant clip
// Board: XIAO nRF52840 Sense (21 x 17.5mm)
// ============================================================

/* [Main Dimensions] */
// Total outer length (mm)
body_length = 42;
// Total outer width (mm)
body_width = 25;
// Total outer height/thickness (mm)
body_height = 14;
// Wall thickness (mm)
wall = 1.5;
// Corner radius (mm)
corner_r = 3;

/* [Top/Bottom Split] */
// Height of bottom shell (mm)
bottom_height = 9;
// Height of top lid (mm)
top_height = body_height - bottom_height; // 5mm

/* [XIAO nRF52840 Sense] */
// Board dimensions
xiao_length = 21;
xiao_width = 17.5;
xiao_height = 3;
// Board position offset from center (towards USB end)
xiao_offset_x = 3;

/* [USB-C Port] */
usbc_width = 9.5;
usbc_height = 3.5;
// Height from inner floor to USB-C center
usbc_z_offset = 2.5;

/* [Microphone] */
// Mic hole diameter on top lid
mic_hole_dia = 2.0;
// Mic position from XIAO center
mic_offset_x = 5;

/* [Speaker Grille] */
// Speaker area diameter
speaker_dia = 12;
// Individual hole diameter
speaker_hole_dia = 1.2;
// Hole spacing
speaker_hole_spacing = 2.5;
// Speaker position offset from center
speaker_offset_x = -8;

/* [Buttons] */
// Button hole diameter
button_dia = 4;
// Button A (PTT) position - right side
button_a_x = 8;
// Button B position - right side
button_b_x = -5;
// Button height from bottom
button_z = 7;

/* [Battery Compartment] */
// LiPo 502030: 30x20x5mm
bat_length = 30;
bat_width = 20;
bat_height = 5;

/* [LED Window] */
led_width = 3;
led_height = 2;
led_x = 15; // near USB end

/* [Clip Mechanism] */
clip_length = 25;
clip_width = 10;
clip_thickness = 1.5;
clip_gap = 3; // opening gap
clip_spring_length = 8;

/* [Assembly] */
// Snap-fit tab dimensions
tab_width = 4;
tab_depth = 1;
tab_height = 2;
// Screw post diameter
post_dia = 4;
post_hole_dia = 1.8; // M2 self-tap

// Tolerance for printing
tol = 0.2;

// ============================================================
// Modules
// ============================================================

module rounded_box(l, w, h, r) {
    hull() {
        for (x = [-l/2+r, l/2-r])
            for (y = [-w/2+r, w/2-r])
                translate([x, y, 0])
                    cylinder(h=h, r=r, $fn=40);
    }
}

module rounded_box_shell(l, w, h, r, wall) {
    difference() {
        rounded_box(l, w, h, r);
        translate([0, 0, wall])
            rounded_box(l - wall*2, w - wall*2, h, r - wall/2);
    }
}

// Speaker grille pattern - grid of small holes
module speaker_grille(dia, hole_d, spacing) {
    r = dia / 2;
    for (x = [-r : spacing : r])
        for (y = [-r : spacing : r])
            if (x*x + y*y < r*r)
                translate([x, y, 0])
                    cylinder(d=hole_d, h=wall*3, center=true, $fn=16);
}

// Screw post
module screw_post(od, id, h) {
    difference() {
        cylinder(d=od, h=h, $fn=24);
        cylinder(d=id, h=h+1, $fn=24);
    }
}

// Snap-fit tab (on bottom shell wall)
module snap_tab() {
    // small bump that clicks into slot
    hull() {
        cube([tab_width, 0.1, tab_height], center=true);
        translate([0, tab_depth, 0])
            cube([tab_width - 1, 0.1, tab_height - 1], center=true);
    }
}

// ============================================================
// Bottom Shell
// ============================================================

module bottom_shell() {
    difference() {
        union() {
            // Main shell
            rounded_box_shell(body_length, body_width, bottom_height, corner_r, wall);

            // Screw posts (4 corners)
            for (x = [-body_length/2 + 5, body_length/2 - 5])
                for (y = [-body_width/2 + 5, body_width/2 + 5 - 10])
                    translate([x, y, wall])
                        screw_post(post_dia, post_hole_dia, bottom_height - wall - 1);

            // XIAO board standoffs (4 corners of board)
            for (x = [-xiao_length/2 + 1, xiao_length/2 - 1])
                for (y = [-xiao_width/2 + 1, xiao_width/2 - 1])
                    translate([x + xiao_offset_x, y, wall])
                        cylinder(d=2.5, h=2, $fn=16);

            // Battery retention walls
            translate([speaker_offset_x, 0, wall])
                difference() {
                    cube([bat_length + 2, bat_width + 2, bat_height], center=true);
                    cube([bat_length + tol, bat_width + tol, bat_height + 1], center=true);
                }
        }

        // USB-C cutout (front face)
        translate([body_length/2, 0, usbc_z_offset + wall])
            rotate([0, 90, 0])
                hull() {
                    for (x = [-usbc_width/2 + usbc_height/2, usbc_width/2 - usbc_height/2])
                        translate([0, x, 0])
                            cylinder(d=usbc_height, h=wall*3, center=true, $fn=24);
                }

        // Button A hole (right side wall)
        translate([button_a_x, body_width/2, button_z])
            rotate([90, 0, 0])
                cylinder(d=button_dia, h=wall*3, center=true, $fn=24);

        // Button B hole (right side wall)
        translate([button_b_x, body_width/2, button_z])
            rotate([90, 0, 0])
                cylinder(d=button_dia, h=wall*3, center=true, $fn=24);

        // LED window (front face, near USB)
        translate([led_x, 0, bottom_height - 2])
            cube([led_width, led_height, wall*3], center=true);
    }

    // Snap-fit tabs on long walls
    for (x = [-10, 10])
        for (side = [-1, 1])
            translate([x, side * (body_width/2 - wall/2), bottom_height - tab_height/2])
                rotate([0, 0, side > 0 ? 0 : 180])
                    snap_tab();
}

// ============================================================
// Top Lid
// ============================================================

module top_lid() {
    difference() {
        union() {
            // Main lid shell (inverted)
            rounded_box_shell(body_length, body_width, top_height, corner_r, wall);

            // Inner lip for alignment
            translate([0, 0, wall])
                difference() {
                    rounded_box(body_length - wall*2 - tol, body_width - wall*2 - tol, 2, corner_r - wall);
                    translate([0, 0, -0.1])
                        rounded_box(body_length - wall*4, body_width - wall*4, 2.2, corner_r - wall*1.5);
                }
        }

        // Microphone hole (centered on XIAO mic position)
        translate([mic_offset_x + xiao_offset_x, 0, 0])
            cylinder(d=mic_hole_dia, h=wall*3, center=true, $fn=24);

        // Speaker grille
        translate([speaker_offset_x, 0, 0])
            speaker_grille(speaker_dia, speaker_hole_dia, speaker_hole_spacing);

        // Snap-fit slots (matching tabs)
        for (x = [-10, 10])
            for (side = [-1, 1])
                translate([x, side * (body_width/2 - wall), top_height/2])
                    cube([tab_width + tol*2, tab_depth*2 + tol, tab_height + tol*2], center=true);

        // Screw holes through lid (matching posts)
        for (x = [-body_length/2 + 5, body_length/2 - 5])
            for (y = [-body_width/2 + 5, body_width/2 + 5 - 10])
                translate([x, y, 0])
                    cylinder(d=post_hole_dia + tol, h=wall*3, center=true, $fn=24);
    }
}

// ============================================================
// Clip
// ============================================================

module clip() {
    // Spring clip that attaches to bottom of enclosure
    translate([0, 0, -clip_gap - clip_thickness]) {
        difference() {
            union() {
                // Main clip plate
                hull() {
                    for (x = [-clip_length/2 + 2, clip_length/2 - 2])
                        for (y = [-clip_width/2 + 2, clip_width/2 - 2])
                            translate([x, y, 0])
                                cylinder(r=2, h=clip_thickness, $fn=20);
                }
                // Spring arm (connects clip to body)
                translate([-clip_length/2, -clip_width/2, 0])
                    cube([clip_spring_length, clip_width, clip_gap + clip_thickness]);
            }
            // Hollow out spring arm for flexibility
            translate([-clip_length/2 + 1.5, -clip_width/2 + 2, -0.1])
                cube([clip_spring_length - 3, clip_width - 4, clip_gap + clip_thickness + 0.2]);
        }
        // Grip ridges on clip surface
        for (x = [-5, 0, 5])
            translate([x, 0, -0.1])
                cube([1, clip_width - 2, 0.5], center=true);
    }
}

// ============================================================
// Render
// ============================================================

// Choose what to display:
// 0 = assembled view
// 1 = bottom shell only (for printing)
// 2 = top lid only (for printing, flip it)
// 3 = clip only (for printing)
// 4 = exploded view
display_mode = 4;

if (display_mode == 0) {
    // Assembled
    color("DimGray") bottom_shell();
    color("SlateGray") translate([0, 0, body_height - top_height])
        mirror([0, 0, 1]) top_lid();
    color("DarkSlateGray") clip();
}

if (display_mode == 1) {
    bottom_shell();
}

if (display_mode == 2) {
    // Flipped for printing (flat on build plate)
    mirror([0, 0, 1]) top_lid();
}

if (display_mode == 3) {
    clip();
}

if (display_mode == 4) {
    // Exploded view - parts separated for visibility
    color("DimGray", 0.9) bottom_shell();
    color("SlateGray", 0.9) translate([0, 0, body_height + 10])
        mirror([0, 0, 1]) top_lid();
    color("DarkSlateGray", 0.9) translate([0, 0, -15]) clip();
}
