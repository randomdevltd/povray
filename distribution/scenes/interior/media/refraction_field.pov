// Independent index contributions add raw signed function values.
#version 3.7;
global_settings { assumed_gamma 1 atmospheric_ior 1 }
camera { orthographic location <0, 0, -6> look_at 0 right x*4 up y*3 }
plane { z, 3 pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 0.2 } finish { emission 1 diffuse 0 } }
box {
  <-1.5,-1,-1>, <1.5,1,1>
  pigment { rgbt 1 }
  interior { ior 1 refraction { function { -0.03*(2+x/4) } detail 0.06 } }
}
