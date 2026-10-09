// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// media_blend subtract: a turbulent cloud carved out of turbulent fog, so the hollow frays instead of cutting clean. +w480 +h270; Declare=Blend=0 leaves the fog whole.
#version 4.0;
#ifndef (Blend) #declare Blend = 1; #end

global_settings { assumed_gamma 1 }
camera { location <0, 2.2, -10> look_at <0, 2.2, 0> angle 50 }
background { rgb <0.01, 0.015, 0.03> }
light_source { <-8, 14, -6> rgb <1.3, 1.2, 1.0> }
light_source { <6, 4, 12> rgb <0.3, 0.45, 0.9> }
plane { y, 0 pigment { checker rgb 0.12 rgb 0.2 scale 1.5 } finish { diffuse 0.8 } }

#declare Fog = media {
  scattering { 1, rgb 0.14 }
  absorption rgb <0.01, 0.015, 0.02>
  density {
    bozo turbulence 0.8 octaves 6 lambda 2.6 omega 0.55 scale 1.4
    color_map { [0 rgb 0] [0.4 rgb 0.1] [0.7 rgb 0.8] [1 rgb 1.4] }
  }
}
box { <-7, 0, -1.5>, <7, 5, 3.5> pigment { rgbt 1 } interior { media { Fog } } }

#if (Blend)
  sphere {
    0, 1
    pigment { rgbt 1 }
    interior {
      media_blend subtract
      media {
        scattering { 1, rgb 0.14 }
        absorption rgb <0.01, 0.015, 0.02>
        density {
          spherical turbulence 0.55 octaves 5 lambda 2.4 omega 0.6
          color_map { [0 rgb 0] [0.25 rgb 0.6] [0.5 rgb 1.6] [1 rgb 2.5] }
        }
        density { spherical color_map { [0 rgb 0] [0.35 rgb 1] [1 rgb 1] } }
      }
    }
    scale <2.8, 1.9, 2.6> translate <0.2, 2.4, 1>
  }
#end
