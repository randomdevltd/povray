// Shadow order test: a floor meets a steeper slope in a crease the light grazes, seen from very close above.
#version 3.7;
global_settings { assumed_gamma 1 }
#declare P = 3e-5;
camera { orthographic location <0, 1, 0> direction -y right z * image_width * P up -x * image_height * P }
light_source { <cos(radians(15)), sin(radians(15)), 0> * 1000 color rgb 1 parallel point_at 0 }
background { color rgb <0, 0, 0.2> }
plane { y, 0 pigment { color rgb 1 } finish { ambient 0.1 diffuse 0.9 } }
plane { <-sin(radians(30)), cos(radians(30)), 0>, 0 pigment { color rgb 0.5 } finish { ambient 0.1 diffuse 0.9 } }
