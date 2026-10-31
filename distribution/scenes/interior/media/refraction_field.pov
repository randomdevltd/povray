// An interior refraction field with a signed function: inside the box the index is 1 - 0.03 * (2 + x/4), below 1 and falling along x, so the rays bend and the ramps of the backdrop seen through the box shift against those seen around it.
#version 3.7;
global_settings { assumed_gamma 1 atmospheric_ior 1 }
camera { orthographic location <0, 0, -6> look_at 0 right x*4 up y*3 }
plane { z, 3 pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 0.2 } finish { emission 1 diffuse 0 } }
box {
  <-1.5,-1,-1>, <1.5,1,1>
  pigment { rgbt 1 }
  interior { ior 1 refraction { function { -0.03*(2+x/4) } detail 0.06 } }
}
