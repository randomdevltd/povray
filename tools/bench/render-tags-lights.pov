// Sixteen shadowed point lights over a field of spheres, with no tags: the per-light cost of an ordinary scene.
#version 3.7;
global_settings { assumed_gamma 1 }

camera { location <0, 9, -16> look_at <0, 0, 0> angle 50 }

plane { y, 0 pigment { checker rgb 0.8 rgb 0.3 scale 2 } }

#declare R = seed(7);
#for (I, 0, 199)
  sphere {
    <rand(R) * 20 - 10, 0.5, rand(R) * 20 - 10>, 0.5
    pigment { rgb <rand(R), rand(R), rand(R)> }
    finish { specular 0.4 }
  }
#end

#for (I, 0, 15)
  light_source { <cos(I * pi / 8) * 12, 6 + mod(I, 3), sin(I * pi / 8) * 12>, rgb 0.09 }
#end
