// Heat-set insert hole test coupon (M3 threaded inserts).
// Print it, press an insert into each hole, keep the size that goes in
// straight with a small ring of melted plastic and doesn't spin or pull out.
// Then set INSERT_HOLE in the enclosure designs to that size.
// Top row: short insert (5.65 OD x 3.85 L). Bottom row: long insert (5.8 OD x 6.66 L).

holes      = [3.8, 4.0, 4.2, 4.4];  // hole diameters to try, mm
labels     = ["3.8", "4.0", "4.2", "4.4"];  // printed labels (str(4.0) would print "4")
short_len  = 3.85;
long_len   = 6.66;
extra      = 1.0;    // depth past the insert, room for displaced plastic
pitch      = 12;     // center spacing, leaves >= 6 mm of plastic between holes
height     = 10;
text_size  = 3;
$fn = 64;

w = pitch * len(holes) + 4;
d = 32;

difference() {
  cube([w, d, height]);
  for (i = [0 : len(holes) - 1]) {
    x = 8 + i * pitch;
    // short-insert row
    translate([x, d - 8, height - (short_len + extra)]) cylinder(d = holes[i], h = short_len + extra + 1);
    // long-insert row
    translate([x, 12, height - (long_len + extra)]) cylinder(d = holes[i], h = long_len + extra + 1);
    // size label, engraved 0.6 mm on the front face
    translate([x, 0.59, 2]) rotate([90, 0, 0])
      linear_extrude(1) text(labels[i], size = text_size, halign = "center");
  }
  // row labels on the left end: S = short, L = long
  translate([0.59, d - 8, height - 4]) rotate([90, 0, -90]) linear_extrude(1)
    text("S", size = text_size, halign = "center");
  translate([0.59, 12, height - 4]) rotate([90, 0, -90]) linear_extrude(1)
    text("L", size = text_size, halign = "center");
}
