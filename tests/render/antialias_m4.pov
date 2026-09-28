// Anti-aliasing method 4 tests: Mode 0 a checkerboard of one-pixel squares, 1 grain alone, 2 an emissive disc.
#version 3.7;
#ifndef (Mode) #declare Mode = 0; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 0, -1> direction z right x * image_width up y * image_height }
#switch (Mode)
#case (0)
  plane { z, 0.5 pigment { checker color rgb 0 color rgb 1 } finish { ambient 0 diffuse 0 emission 1 } }
#break
#case (1)
  light_source { <0, 0, -100> color 1 parallel point_at 0 }
  plane { z, 0.5 pigment { color rgb 0.6 } finish { ambient 0 diffuse 1 crand 0.3 } }
#break
#else
  background { color rgb <0, 0, 0.2> }
  disc { 0, -z, image_height * 0.37 pigment { color rgb <1, 0.8, 0.3> } finish { ambient 0 diffuse 0 emission 1 } }
#end
