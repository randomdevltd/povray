#version 3.8;
#include "functions.inc"

global_settings { assumed_gamma 1 }

camera {
  location <0, 1.0, -7>
  look_at <0, 0.5, 0>
  right x*16/9
  angle 48
}

light_source {
  <-8, 12, -4>, rgb 1.2
  parallel point_at <0, 0, 0>
  media_attenuation on
}

light_source {
  <5, 8, -3>, rgb <0.5, 0.7, 1>
  area_light <1, 0, 0>, <0, 0, 1>, 2, 2
  media_attenuation on
}

plane { y, -1 pigment { rgb <0.35, 0.4, 0.45> } }
sphere { <0, 0.4, 1>, 0.8 pigment { rgb <0.7, 0.6, 0.5> } }

#for (I, 0, 3)
  box {
    <-2.5, -0.7, -2>, <2.5, 2.2, 2>
    hollow
    pigment { rgbt 1 }
    interior {
      media {
        scattering { 1, <0.4, 0.55, 0.7> * 0.08 extinction 0.5 }
        absorption <0.01, 0.008, 0.006>
        emission <0.005, 0.004, 0.003>
        method 3 intervals 2 samples 20 jitter 0
        density {
          function {
            max(0, min(1, 0.4 + 0.2*f_noise3d(x*2,y*2,z*2)
              + 0.12*sin(x*y) + 0.1*cos(y*z)
              + 0.08*exp(-abs(x)) + 0.06*pow(abs(z)+1,0.3)))
          }
          color_map {
            [0 rgb 0.2]
            [0.5 rgb <0.5, 0.7, 0.9>]
            [1 rgb 1]
          }
          scale <1.2, 0.9, 1.1>
          translate <I*0.31, I*0.17, I*0.23>
        }
        density { bozo scale 0.35 color_map { [0 rgb 0.4] [1 rgb 1] } }
      }
    }
    translate <(I-1.5)*0.2, 0, (I-1.5)*0.16>
  }
#end
