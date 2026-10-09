// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A portal ringed by distorted space: a torus of refracting media around its rim, densest at the rim and fading to nothing at the torus surface, lenses the courtyard and the view through the portal into each other. +w640 +h400
#version 4.0;
#ifndef (Ring) #declare Ring = 1; #end

global_settings { assumed_gamma 1 max_trace_level 12 }
camera { location <1.2, 1.7, -7> look_at <0, 1.6, 0> angle 44 right x * image_width / image_height }
light_source { <-8, 12, -10> rgb 1.1 }
light_source { <6, 6, -6> rgb 0.3 shadowless }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.8, 0.85, 0.95>] [0.4 rgb <0.35, 0.5, 0.85>] } } }

plane { y, 0 pigment { checker rgb 0.75 rgb 0.55 } }
#for (I, -3, 3)
  cylinder { <I * 2.2, 0, 4>, <I * 2.2, 3.5, 4>, 0.25 pigment { rgb <0.85, 0.8, 0.7> } }
#end
box { <-8, 3.5, 3.7>, <8, 4, 4.3> pigment { rgb <0.85, 0.8, 0.7> } }

// Far side: a red sandstone court 40 units away.
plane { y, 0.002 pigment { checker rgb <0.75, 0.35, 0.2> rgb <0.55, 0.25, 0.15> } translate z * 40 clipped_by { box { <-30, -1, 30>, <30, 1, 90> } } }
#for (I, 0, 5) cone { <-6 + I * 2.4, 0, 52>, 0.5, <-6 + I * 2.4, 2.5 + mod(I, 2), 52>, 0 pigment { rgb <0.9, 0.6, 0.2> } } #end

#declare Rim = 1.5;
torus { Rim, 0.06 rotate x * 90 translate y * 1.7 pigment { rgb <0.25, 0.25, 0.3> } finish { specular 0.6 metallic } }
portal { disc { y * 1.7, -z, Rim - 0.02 } to { translate z * 40 } }

#if (Ring)
  torus {
    Rim, 0.9
    pigment { rgbt 1 }
    interior {
      media {
        method 3
        refraction 0.15
        density {
          function { pow(max(0, 1 - sqrt(pow(sqrt(x * x + z * z) - Rim, 2) + y * y) / 0.9), 2) }
        }
      }
    }
    rotate x * 90 translate y * 1.7
  }
#end
