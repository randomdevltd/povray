// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// mix multiply: a density field scales the fog beneath it, thinning it in streaks and thickening it between. +w480 +h270; Declare=Blend=0 leaves the fog even.
#version 4.0;
#ifndef (Blend) #declare Blend = 1; #end

global_settings { assumed_gamma 1 }
camera { location <0, 1.6, -10> look_at <0, 2.2, 6> angle 60 }
background { rgb <0.08, 0.1, 0.16> }
light_source { <-25, 30, 60> rgb <2, 1.7, 1.3> parallel point_at <0, 0, 9> }
plane { y, 0 pigment { rgb <0.25, 0.22, 0.18> } }
#for (I, 0, 7)
  cylinder { <-6 + I * 1.7, 0, 9>, <-6 + I * 1.7, 9, 9>, 0.35 pigment { rgb 0.4 } }
#end
box { <-12, 0, -10>, <12, 9, 14> pigment { rgbt 1 } interior { media { scattering { 1, rgb 0.07 } absorption 0.005 } } }

#if (Blend)
  box {
    <-12, 0, -10>, <12, 9, 14>
    pigment { rgbt 1 }
    interior {
      media {
        mix multiply
        density {
          wood turbulence 0.35 octaves 4 rotate x * 90 rotate z * 20 scale 2.5
          color_map { [0 rgb 0.02] [0.4 rgb 0.15] [0.6 rgb 1.6] [1 rgb 3] }
        }
      }
    }
  }
#end
