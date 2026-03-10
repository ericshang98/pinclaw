// ============================================================
// Pinclaw Clip v4.0 — Matches pinclaw.ai 3D model exactly
// ============================================================
//
// Isosceles rounded triangle (wider top, pointed bottom)
// Based on ProductModel3D.tsx shape constants:
//   W=0.85, HT=0.55, HB=-1.0, RF=0.22, BULGE=0.04
//
// Features:
//   - Large parabolic dome button (40% of face, with border ring)
//   - Status display bar near top edge
//   - Volume side key on right slope
//   - LED indicator on right slope
//   - PINCLAW label near bottom point
//   - Front plate (electronics) + Back plate (magnets, 92% scale)
//   - FFC ribbon cable arc connecting plates at top
//   - All hardware components modeled to scale
//
// Display modes:
//   0 = assembled (transparent + components)
//   1 = front plate for printing
//   2 = back plate for printing
//   3 = exploded view (DEFAULT)
//   4 = components only + ghost shell
//   5 = cross-section
//   6 = wearable sim (with fabric)
// ============================================================

$fn = 60;

/* [Display] */
display_mode = 3;

// ============================================================
// SHAPE — Isosceles Triangle (from website)
// ============================================================
// Scale: 1 website unit ≈ 20mm real
// Website: W=0.85 HT=0.55 HB=-1.0 RF=0.22

/* [Triangle Shape] */
// Enlarged to fit 30x20mm battery + 21x17.5mm XIAO
tri_half_w = 19;       // mm — half-width at top vertices
tri_top_y = 13;        // mm — top vertices Y
tri_bot_y = -23;       // mm — bottom vertex Y
tri_rf = 5.5;          // mm — corner rounding radius

// Derived: overall dimensions
// Width:  2*(19+5.5) = 49mm
// Height: (13+5.5) - (-23-5.5) = 47mm

/* [Front Plate] */
front_h = 13;          // total thickness (mm) — extra room for stack
wall = 1.5;            // wall thickness
dome_h = 1.5;          // top surface dome

/* [Back Plate] */
back_scale = 0.92;     // 92% of front shape
back_h = 4;            // thinner plate
back_wall = 1.2;

/* [Spacing] */
fabric_gap = 2;        // gap for clothing fabric between plates

// ============================================================
// COMPONENTS — real dimensions
// ============================================================

/* [XIAO nRF52840 Sense — 21x17.5x4.5mm] */
xiao_l = 21;           // length (USB-C direction = +Y towards top)
xiao_w = 17.5;         // width (X direction)
xiao_pcb_h = 1.0;
xiao_total_h = 4.5;
xiao_x = 0;
xiao_y = 3;            // moved UP towards wide area
xiao_z_base = 0;       // set dynamically: on top of battery

usbc_w = 9.0;
usbc_h = 3.2;
usbc_protrude = 2;     // past PCB edge

/* [Button — 4x4x0.8mm SMD tact switch] */
btn_body = 4;
btn_h = 0.8;

/* [Dome Button Cap — matches website: 40% of face] */
// Website: dome radius 0.36, ring outer 0.41 at scale 20
dome_inner_r = 7.2;    // mm (0.36 * 20) — dome surface radius
dome_ring_outer = 8.2; // mm (0.41 * 20) — border ring outer
dome_peak = 0.7;       // mm (0.035 * 20) — dome peak height
dome_x = 0;
dome_y = -1;           // slightly below geometric center
dome_hole_r = 7.5;     // hole in shell for dome

/* [Status Display Bar — near top edge] */
bar_w = 12;            // mm (0.6 * 20)
bar_h_dim = 1.2;       // mm (0.06 * 20)
bar_thick = 0.5;
bar_y = 9;             // near top edge

/* [Volume Side Key — right slope] */
sidekey_l = 11;        // mm (0.55 * 20)
sidekey_w = 1.0;       // protrusion
sidekey_h = 3;         // height along Z

/* [LED Indicator] */
led_r = 0.7;           // mm (small dot)

/* [PINCLAW Label] */
label_w = 5.6;         // mm (0.28 * 20)
label_h_dim = 0.8;     // mm (0.04 * 20)
label_y = -17;         // near bottom point

/* [LiPo Battery 502030 — 30x20x5mm] */
bat_l = 30;
bat_w = 20;
bat_h = 5;
bat_x = 0;
bat_y = 3;             // moved UP to wide area of triangle

/* [Vibration Motor LCM-0827 — 8x2.7mm coin] */
vib_dia = 8;
vib_h = 2.7;

/* [WS2812B LED — 5x5x1.6mm] */
ws_led_size = 5;
ws_led_h = 1.6;

/* [Magnets — neodymium disc] */
mag_dia = 8;
mag_h = 2;
mag_count = 3;
mag_ring_r = 8;        // distance from center

/* [FFC Ribbon Cable] */
ffc_w = 8;             // cable width
ffc_thick = 1.2;       // cable thickness
ffc_arc_h = 14;        // arc height above plates

/* [Tolerances] */
tol = 0.3;

// ============================================================
// 2D SHAPE MODULE
// ============================================================

module pinclaw_triangle_2d(s = 1) {
    hull() {
        // Top-left vertex
        translate([-tri_half_w * s, tri_top_y * s])
            circle(r = tri_rf * s);
        // Top-right vertex
        translate([tri_half_w * s, tri_top_y * s])
            circle(r = tri_rf * s);
        // Bottom vertex (pointed)
        translate([0, tri_bot_y * s])
            circle(r = tri_rf * s);
    }
}

module pinclaw_triangle_3d(h, s = 1) {
    linear_extrude(height = h)
        pinclaw_triangle_2d(s);
}

// ============================================================
// RIGHT EDGE GEOMETRY (for side key + LED placement)
// ============================================================

// Right edge goes from (tri_half_w, tri_top_y) to (0, tri_bot_y)
// Direction vector and normal
re_dx = 0 - tri_half_w;        // -17
re_dy = tri_bot_y - tri_top_y; // -31
re_len = sqrt(re_dx*re_dx + re_dy*re_dy);
re_nx = -re_dy / re_len;       // outward normal X
re_ny = re_dx / re_len;        // outward normal Y
re_angle = atan2(re_dy, re_dx); // angle of edge

// Point on right edge at parametric t (0=top, 1=bottom)
function re_x(t) = tri_half_w + re_dx * t;
function re_y(t) = tri_top_y + re_dy * t;

// ============================================================
// FRONT PLATE (Main Electronics Housing)
// ============================================================

module front_plate() {
    bat_z = wall;                    // battery sits on floor
    xiao_z = wall + bat_h;          // XIAO on top of battery
    btn_z = xiao_z + xiao_total_h;  // button on top of XIAO

    difference() {
        union() {
            // Main body with slight dome
            hull() {
                pinclaw_triangle_3d(front_h - dome_h);
                translate([0, 0, front_h - dome_h])
                    pinclaw_triangle_3d(dome_h, 0.96);
            }

            // Side key bump (on right slope)
            sk_t = 0.35;  // 35% along right edge
            translate([re_x(sk_t) + re_nx * (wall + 0.5),
                      re_y(sk_t) + re_ny * (wall + 0.5),
                      front_h / 2])
                rotate([0, 0, re_angle * 180 / 3.14159])
                    cube([sidekey_l, sidekey_w + 1, sidekey_h],
                         center = true);
        }

        // --- Interior cavity (single clean offset) ---
        translate([0, 0, wall])
            linear_extrude(height = front_h) {
                offset(r = -wall)
                    pinclaw_triangle_2d();
            }

        // --- DOME BUTTON HOLE ---
        translate([dome_x, dome_y, -0.1])
            cylinder(r = dome_hole_r, h = front_h + dome_h + 1);

        // --- Microphone holes (array near XIAO mic) ---
        translate([xiao_x + 5, xiao_y, -0.1]) {
            cylinder(d = 1.8, h = wall + 0.2, $fn = 20);
            for (a = [0:60:300])
                translate([2.5 * cos(a), 2.5 * sin(a), 0])
                    cylinder(d = 0.8, h = wall + 0.2, $fn = 12);
        }

        // --- USB-C internal clearance (faces top/+Y) ---
        translate([-usbc_w/2 - tol,
                   xiao_y + xiao_l/2 - 1,
                   wall + bat_h + xiao_pcb_h])
            cube([usbc_w + tol*2, tri_top_y + tri_rf + 5, usbc_h + tol]);

        // --- Status bar recess (on top face, near top edge) ---
        translate([-bar_w/2, bar_y - bar_h_dim/2, front_h - bar_thick])
            cube([bar_w, bar_h_dim, bar_thick + 1]);

        // --- LED hole (right edge, above side key) ---
        sk_t_led = 0.20;  // higher than side key
        translate([re_x(sk_t_led) + re_nx * wall,
                  re_y(sk_t_led) + re_ny * wall,
                  front_h / 2])
            rotate([0, 90, re_angle * 180 / 3.14159])
                cylinder(r = led_r + 0.3, h = wall * 3,
                        center = true, $fn = 16);

        // --- PINCLAW label recess ---
        translate([-label_w/2, label_y - label_h_dim/2,
                   front_h - 0.3])
            cube([label_w, label_h_dim, 0.4]);

        // --- Magnet pockets (from bottom face) ---
        for (i = [0 : mag_count - 1]) {
            a = i * (360 / mag_count) - 90;
            translate([mag_ring_r * cos(a),
                      mag_ring_r * sin(a) + dome_y,
                      -0.1])
                cylinder(d = mag_dia + tol, h = mag_h + 0.2, $fn = 30);
        }

        // --- FFC cable slot (top edge, center) ---
        translate([-ffc_w/2 - tol, tri_top_y - 2,
                   front_h/2 - ffc_thick/2 - tol])
            cube([ffc_w + tol*2, tri_rf + 5, ffc_thick + tol*2]);

        // --- Speaker grille (bottom face, offset from magnets) ---
        translate([8, -10, -0.1])
            for (dx = [-3:2.5:3])
                for (dy = [-3:2.5:3])
                    if (dx*dx + dy*dy < 16)
                        translate([dx, dy, 0])
                            cylinder(d = 1.0, h = wall + 0.2, $fn = 12);
    }

    // (Internal mounting features removed for clean single-shell print.
    //  Battery and XIAO held by cavity walls + lid pressure.)
}

// ============================================================
// DOME BUTTON CAP (separate printable piece)
// ============================================================

module dome_button() {
    translate([dome_x, dome_y, front_h]) {
        // Border ring
        color("#8B1A1A")
        difference() {
            cylinder(r = dome_ring_outer, h = 0.5, $fn = 48);
            translate([0, 0, -0.1])
                cylinder(r = dome_inner_r, h = 0.7, $fn = 48);
        }

        // Parabolic dome surface
        // z = dome_peak * (1 - (r/dome_inner_r)^2)
        color("#C62828")
        translate([0, 0, 0.2]) {
            points_r = 20;
            points_s = 48;

            // Approximate with stacked cylinders
            for (i = [0 : points_r - 1]) {
                t = i / points_r;
                r1 = dome_inner_r * t;
                r2 = dome_inner_r * (t + 1/points_r);
                z1 = dome_peak * (1 - t * t);
                z2 = dome_peak * (1 - (t + 1/points_r) * (t + 1/points_r));
                translate([0, 0, min(z1, z2)])
                    difference() {
                        cylinder(r = r2, h = abs(z1 - z2) + 0.01,
                                $fn = points_s);
                        if (i > 0)
                            translate([0, 0, -0.01])
                                cylinder(r = r1, h = abs(z1-z2) + 0.03,
                                        $fn = points_s);
                    }
            }
        }

        // Plunger shaft (goes down to tact switch)
        color("DimGray")
        translate([0, 0, -3])
            cylinder(d = 3, h = 3, $fn = 16);
    }
}

// ============================================================
// BACK PLATE (Magnet Disc — 92% scale triangle)
// ============================================================

module back_plate() {
    difference() {
        // Outer shell
        hull() {
            pinclaw_triangle_3d(back_h - 0.5, back_scale);
            translate([0, 0, back_h - 0.5])
                pinclaw_triangle_3d(0.5, back_scale * 0.98);
        }

        // Interior (hollow if needed for weight, or solid for strength)
        // Keep it mostly solid, just magnet pockets
        // translate([0, 0, back_wall])
        //     pinclaw_triangle_3d(back_h, back_scale * 0.85);

        // Magnet pockets (from top face = fabric side)
        for (i = [0 : mag_count - 1]) {
            a = i * (360 / mag_count) - 90;
            translate([mag_ring_r * cos(a) * back_scale,
                      (mag_ring_r * sin(a) + dome_y) * back_scale,
                      back_h - mag_h])
                cylinder(d = mag_dia + tol, h = mag_h + 0.1, $fn = 30);
        }

        // Magnet ring marking on bottom (aesthetic)
        translate([0, dome_y * back_scale, -0.1])
            difference() {
                cylinder(r = mag_ring_r + 2, h = 0.3, $fn = 40);
                cylinder(r = mag_ring_r - 1, h = 0.4, $fn = 40);
            }

        // FFC cable slot (top edge)
        translate([-ffc_w/2 * back_scale - tol,
                   (tri_top_y - 2) * back_scale,
                   back_h/2 - ffc_thick/2 - tol])
            cube([ffc_w * back_scale + tol*2,
                  tri_rf * back_scale + 5,
                  ffc_thick + tol*2]);
    }
}

// ============================================================
// FFC RIBBON CABLE
// ============================================================

module ffc_cable() {
    // Simplified: rectangular strip bent in an arc
    // From front plate top edge → arc up → back plate top edge
    front_z = front_h / 2;
    back_z = -(fabric_gap + back_h) + back_h / 2;
    attach_y = tri_top_y + tri_rf - 2;

    color("#4a4a4a", 0.8)
    translate([0, attach_y, 0]) {
        // Connector on front plate
        translate([-ffc_w/2, 0, front_z - ffc_thick/2])
            cube([ffc_w, 2, ffc_thick]);

        // Arc section (approximated with segments)
        arc_steps = 20;
        for (i = [0 : arc_steps - 1]) {
            t1 = i / arc_steps;
            t2 = (i + 1) / arc_steps;
            // Parametric arc: Y goes outward then back, Z goes from front_z to back_z
            a1 = t1 * 180;
            a2 = t2 * 180;
            y1 = sin(a1) * ffc_arc_h / 3;
            y2 = sin(a2) * ffc_arc_h / 3;
            z1 = front_z + (back_z - front_z) * t1 +
                 sin(a1) * ffc_arc_h / 4;
            z2 = front_z + (back_z - front_z) * t2 +
                 sin(a2) * ffc_arc_h / 4;

            hull() {
                translate([-ffc_w/2, y1, z1])
                    cube([ffc_w, 0.1, ffc_thick]);
                translate([-ffc_w/2, y2, z2])
                    cube([ffc_w, 0.1, ffc_thick]);
            }
        }

        // Connector on back plate
        translate([-ffc_w/2, 0, back_z - ffc_thick/2])
            cube([ffc_w, 2, ffc_thick]);
    }

    // Wire groove lines (surface detail)
    color("#2a2a2a")
    for (wx = [-2.4, -0.8, 0.8, 2.4])
        translate([wx, attach_y, front_z + ffc_thick/2 + 0.05])
            cylinder(d = 0.6, h = 0.3, $fn = 8);
}

// ============================================================
// HARDWARE COMPONENT MODELS
// ============================================================

module hw_battery() {
    translate([bat_x, bat_y, wall]) {
        color("RoyalBlue", 0.85)
            translate([-bat_w/2, -bat_l/2, 0])
                cube([bat_w, bat_l, bat_h]);
        // JST connector
        color("White") translate([-3, bat_l/2, 0.5])
            cube([6, 2.5, 3.5]);
    }
}

module hw_xiao() {
    z = wall + bat_h;
    translate([xiao_x, xiao_y, z]) {
        // PCB
        color("DarkGreen", 0.9)
            translate([-xiao_w/2, -xiao_l/2, 0])
                cube([xiao_w, xiao_l, xiao_pcb_h]);
        // nRF52840 SoC shield
        color("DimGray", 0.9)
            translate([-4, -3, xiao_pcb_h])
                cube([8, 8, 2.2]);
        // PDM Mic
        color("DimGray", 0.8)
            translate([4, -1.5, xiao_pcb_h])
                cube([3, 3, 1.5]);
        // IMU
        color("DimGray", 0.8)
            translate([-7, 3, xiao_pcb_h])
                cube([3.5, 3, 1.2]);
        // USB-C (faces +Y = top edge)
        color("Silver", 0.9)
            translate([-usbc_w/2, xiao_l/2 - 1, xiao_pcb_h - 0.3])
                cube([usbc_w, usbc_protrude + 1, usbc_h]);
        // Castellated pads
        color("Gold", 0.8)
            for (i = [0:6])
                for (sx = [-1, 1])
                    translate([sx * (xiao_w/2 - 0.6),
                              -xiao_l/2 + 1.5 + i * 2.54, 0])
                        cube([1.2, 1.2, xiao_pcb_h]);
    }
}

module hw_tact_switch() {
    z = wall + bat_h + xiao_total_h;
    translate([dome_x, dome_y, z]) {
        color("Silver", 0.9)
            translate([-btn_body/2, -btn_body/2, 0])
                cube([btn_body, btn_body, btn_h]);
        color("DarkGray")
            cylinder(d = 2, h = btn_h + 0.2, $fn = 12);
    }
}

module hw_vib_motor() {
    // Tucked in bottom area below battery
    translate([0, -14, wall]) {
        color("OrangeRed", 0.85)
            cylinder(d = vib_dia, h = vib_h, $fn = 30);
    }
}

module hw_ws_led() {
    // Near the right edge, above side key area
    translate([12, 3, wall + bat_h]) {
        color("White", 0.9)
            translate([-ws_led_size/2, -ws_led_size/2, 0])
                cube([ws_led_size, ws_led_size, ws_led_h]);
    }
}

module hw_magnets_front() {
    for (i = [0 : mag_count - 1]) {
        a = i * (360 / mag_count) - 90;
        translate([mag_ring_r * cos(a),
                  mag_ring_r * sin(a) + dome_y,
                  0])
            color("DarkGray", 0.7)
                cylinder(d = mag_dia, h = mag_h, $fn = 24);
    }
}

module hw_magnets_back() {
    for (i = [0 : mag_count - 1]) {
        a = i * (360 / mag_count) - 90;
        translate([mag_ring_r * cos(a) * back_scale,
                  (mag_ring_r * sin(a) + dome_y) * back_scale,
                  back_h - mag_h])
            color("DarkGray", 0.7)
                cylinder(d = mag_dia, h = mag_h, $fn = 24);
    }
}

module all_components() {
    hw_battery();
    hw_xiao();
    hw_tact_switch();
    hw_vib_motor();
    hw_ws_led();
    hw_magnets_front();
}

// ============================================================
// STATUS BAR (separate piece, press-fit into recess)
// ============================================================

module status_bar() {
    translate([0, bar_y, front_h - bar_thick + 0.1]) {
        color("#111122")
            translate([-bar_w/2, -bar_h_dim/2, 0])
                cube([bar_w, bar_h_dim, bar_thick]);
    }
}

// ============================================================
// FABRIC (for wearable simulation)
// ============================================================

module fabric_layer() {
    color("Khaki", 0.3)
        translate([-30, -30, -(fabric_gap/2 + 0.75)])
            cube([60, 60, 1.5]);
}

// ============================================================
// DISPLAY MODES
// ============================================================

back_z_offset = -(fabric_gap + back_h);

if (display_mode == 0) {
    // Assembled — transparent shell + solid components
    color("DimGray", 0.2) front_plate();
    all_components();
    dome_button();
    status_bar();
    ffc_cable();
    translate([0, dome_y * (back_scale - 1), back_z_offset]) {
        color("DimGray", 0.4) back_plate();
        hw_magnets_back();
    }
}

if (display_mode == 1) {
    // Front plate for printing
    front_plate();
}

if (display_mode == 2) {
    // Back plate for printing
    back_plate();
}

if (display_mode == 3) {
    // Exploded view
    translate([0, 0, 26]) dome_button();
    translate([0, 0, 22]) status_bar();
    translate([0, 0, 14]) color("DimGray", 0.3) front_plate();
    translate([0, 0, 4]) all_components();
    translate([0, 0, 14]) ffc_cable();
    translate([0, 0, -10]) fabric_layer();
    translate([0, dome_y * (back_scale - 1), -20]) {
        color("DimGray", 0.5) back_plate();
        hw_magnets_back();
    }
}

if (display_mode == 4) {
    // Components only + ghost shell
    all_components();
    dome_button();
    status_bar();
    %front_plate();
    hw_magnets_front();
}

if (display_mode == 5) {
    // Cross-section
    difference() {
        union() {
            color("DimGray", 0.3) front_plate();
            all_components();
            dome_button();
        }
        translate([-60, -60, -1]) cube([60, 120, front_h + 10]);
    }
}

if (display_mode == 6) {
    // Wearable simulation
    translate([0, 0, fabric_gap/2 + 0.75]) {
        color("DimGray", 0.3) front_plate();
        all_components();
        dome_button();
        status_bar();
        ffc_cable();
    }
    fabric_layer();
    translate([0, dome_y * (back_scale-1), -(fabric_gap/2 + 0.75 + back_h)]) {
        color("DimGray", 0.5) back_plate();
        hw_magnets_back();
    }
}

// ============================================================
// CONSOLE INFO
// ============================================================
echo("=== Pinclaw Clip v4 — Website-Matched Design ===");
echo("=== Pinclaw Clip v4.1 — Fixed Layout ===");
echo(str("Overall: ~", (tri_half_w + tri_rf) * 2, "mm wide x ",
         tri_top_y + tri_rf - (tri_bot_y - tri_rf), "mm tall x ",
         front_h, "mm thick"));
echo(str("Back plate: 92% scale, ", back_h, "mm thick"));
echo(str("Dome button: ", dome_ring_outer * 2, "mm outer dia"));
echo(str("Component stack: bat(", bat_h, ") + XIAO(",
         xiao_total_h, ") + btn(", btn_h, ") = ",
         bat_h + xiao_total_h + btn_h, "mm"));
echo(str("Internal height: ", front_h - wall, "mm, stack: ",
         bat_h + xiao_total_h + btn_h, "mm, clearance: ",
         front_h - wall - (bat_h + xiao_total_h + btn_h), "mm"));
echo(str("Battery at Y=", bat_y, ", spans Y=[",
         bat_y - bat_l/2, " to ", bat_y + bat_l/2, "]"));
