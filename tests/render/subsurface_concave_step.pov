// A shallow concave dish, radius 1000 mm, cut into a method 2 block: a far point on the dish lies a little above the exit
// point's tangent plane and behind its sample base, so the surface map's crease test steps up to it and asks the escape runs.
#version 3.7;
global_settings {
  assumed_gamma 1
  mm_per_unit 1
  subsurface { samples 12, 4 method 2 }
}
camera { location <0, 0, -150> look_at <0, 0, 0> angle 40 }
light_source { <40, 60, -200> rgb 1 }
background { rgb 0 }
difference {
  box { <-60, -60, 0>, <60, 60, 30> }
  sphere { <0, 0, -995>, 1000 }
  texture { pigment { rgb 0.995 } finish { ambient 0 diffuse 0.8 subsurface { translucency 0.3 } } }
  interior { ior 1.4 }
}
