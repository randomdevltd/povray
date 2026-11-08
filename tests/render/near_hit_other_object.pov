// Reflected and refracted rays reach an opaque object nearer than the minimum intersection depth when they leave another one.
// Case 0: a mirror wall on a glowing floor, seen 6e-5 above the floor, reflects down onto it from 2e-5 to 1e-4 away.
// Case 1: a glass block 5e-5 above the floor, seen from straight above; its refracted rays leave it 5e-5 from the floor.
// Every pixel shows the green floor; a ray that passes through it reads the black background.
// Case 2: liquid overlapping its glass's inner wall by Overlap; the two are one interface, so 1e-8 and 1e-10 render alike.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Overlap) #declare Overlap = 1e-8; #end
global_settings { assumed_gamma 1 max_trace_level 5 }
background { rgb 0 }
plane { y, 0 pigment { rgb <0, 1, 0> } finish { ambient 0 diffuse 0 emission 1 } }
#switch (Case)
#case (0)
  camera { orthographic location <0, 6e-5, 0> + <1, 1, 0> * 1e-2 look_at <0, 6e-5, 0> right x * 6e-5 up y * 6e-5 }
  box { <-1, 0, -1>, <0, 1, 1> pigment { rgb 0 } finish { ambient 0 diffuse 0 reflection 1 } }
#break
#case (1)
  camera { orthographic location <0, 2, 0> look_at <0, 0, 0> sky z right x * 0.5 up z * 0.5 }
  box { <-1, 5e-5, -1>, <1, 1, 1> pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1.5 } }
#break
#case (2)
  camera { orthographic location <0.95, 0.5, -3> look_at <0.95, 0.5, 0> right x * 0.1 up y * 0.1 }
  plane { z, 3 pigment { gradient x scale 4 } finish { ambient 0 diffuse 0 emission 1 } }
  difference { cylinder { -2 * y, 3 * y, 1 } cylinder { -3 * y, 4 * y, 0.9 } pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1.5 } }
  cylinder { -2 * y, 3 * y, 0.9 + Overlap pigment { rgbf 1 } finish { ambient 0 diffuse 0 } interior { ior 1.2 } }
#break
#end
