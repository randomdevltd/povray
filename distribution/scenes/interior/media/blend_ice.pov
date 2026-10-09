// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Ice crystals in haze: each crystal's cloudy core replaces the haze, so none leaks in; the clear one has no media at all. +w480 +h320; Declare=Blend=0 makes the cores add instead, letting the golden haze in.
#version 4.0;
#ifndef (Blend) #declare Blend = 1; #end
#declare R = seed(23);

global_settings { assumed_gamma 1 max_trace_level 16 }
camera { location <0, 2.4, -9> look_at <0, 1.6, 0> angle 45 }
background { rgb <0.01, 0.015, 0.03> }
light_source { <-9, 9, 6> rgb <1.4, 1.5, 1.8> spotlight point_at <0.5, 1.6, 0.5> radius 5 falloff 9 }
light_source { <6, 5, -8> rgb <0.5, 0.45, 0.4> media_interaction off }
plane { y, 0 pigment { rgb <0.15, 0.18, 0.24> } finish { reflection 0.15 } }
box { <-5, 0, -1.5>, <5, 4.5, 3.5> pigment { rgbt 1 } interior { media { scattering { 1, rgb <0.5, 0.33, 0.12> } absorption rgb <0.02, 0.05, 0.12> } } }

#macro Prism(Length, Width)
  intersection {
    #for (A, 0, 300, 60) plane { x, Width rotate y * A } #end
    plane { y, Length } plane { -y, Length }
  }
#end
#declare Ice = texture {
  pigment { rgbf <0.92, 0.97, 1, 0.97> }
  finish { specular 0.8 roughness 0.001 reflection { 0.02, 1 fresnel } conserve_energy }
}
#macro Core(Strength, Length, Width)
  interior {
    ior 1.31
    media {
      mix #if (Blend) replace #else add #end
      scattering { 1, rgb <0.6, 0.8, 1.2> * Strength }
      density {
        spherical turbulence 0.5 octaves 5 lambda 2.5 scale <Width, Length, Width> * 0.85
        color_map { [0 rgb 0] [0.3 rgb 0] [0.6 rgb 0.6] [1 rgb 1] }
      }
    }
  }
#end
object { Prism(1.9, 0.6) texture { Ice } Core(9, 1.9, 0.6) rotate <10, 20, -25> translate <-2.4, 1.9, 0.8> }
object { Prism(1.3, 0.8) texture { Ice } Core(6, 1.3, 0.8) rotate <70, 40, 10> translate <0.2, 1.2, 1.4> }
object { Prism(1.0, 0.4) texture { Ice } Core(12, 1.0, 0.4) rotate <-30, 0, 50> translate <1.3, 3.0, 0.3> }
object { Prism(1.6, 0.5) texture { Ice } interior { ior 1.31 } rotate <0, 30, 15> translate <3.1, 1.7, 1.2> }
