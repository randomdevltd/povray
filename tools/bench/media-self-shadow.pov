// Scattering haze that shades itself (Shadow=1 drops no_shadow): a 64-layer crackle density_map times bozo.
// Turb ramps turbulence with height to that many cells at the top; TurbKeyword=0 drops the zero-amplitude turbulence lines.
#version 3.7;
#include "functions.inc"
#ifndef (Shadow) #declare Shadow = 1; #end
#ifndef (Layers) #declare Layers = 64; #end
#ifndef (Turb) #declare Turb = 0; #end
#ifndef (Octaves) #declare Octaves = 5; #end
#ifndef (TurbKeyword) #declare TurbKeyword = 1; #end
#ifndef (Bozo) #declare Bozo = 1; #end
#ifndef (Intervals) #declare Intervals = 6; #end
#ifndef (Samples) #declare Samples = 16; #end
#ifndef (Group) #declare Group = 1; #end
#ifndef (Glow) #declare Glow = 0.25; #end
#ifndef (Ext) #declare Ext = 0.4; #end
global_settings { assumed_gamma 1.0 max_trace_level 10 }
camera { perspective location <0, 2.1, -4.2> look_at <0, 0.6, 0> angle 60 right x * 16 / 9 up y }
light_source { <-300, 400, -200>, rgb 0.6 parallel point_at <0, 0, 0> }
plane { y, -0.2 pigment { rgb 0.4 } }
#declare Floor = height_field {
  function 128, 128 { 0.5 + 0.5 * f_noise3d(x * 6, 0, y * 6) }
  translate <-0.5, 0, -0.5> scale <7, 0.3, 7> translate y * -0.15
}
object { Floor pigment { rgb <0.35, 0.3, 0.25> } }
#macro Layer(K)
  #local T = K / (Layers - 1);
  #local Thr = 0.05 * (1 - T) + 0.6 * T;
  crackle form <-0.5, 0.5, 0> metric 2
  warp { planar }
  #if (TurbKeyword) turbulence Turb * T octaves Octaves omega 0.6 lambda 2.2 #end
  density_map { [0 rgb 0] [Thr rgb 0] [Thr + 0.06 rgb 0.08] [min(Thr + 0.15, 0.998) rgb 0.08] [min(Thr + 0.35, 0.999) rgb 3] [1 rgb 3] }
  scale 0.22
  rotate x * 90
#end
#declare Haze = difference {
  cylinder { <0, -0.1, 0>, <0, 1.5, 0>, 3 }
  object { Floor translate y * 0.002 }
  hollow
  pigment { rgbt 1 }
  interior {
    media {
      scattering { 1, <0.80, 0.86, 0.92> * Glow extinction Ext }
      density {
        gradient y
        #if (TurbKeyword) turbulence 0 octaves 4 omega 0.6 lambda 2.2 #end
        density_map { #for (K, 0, Layers - 1) [K / (Layers - 1) Layer(K)] #end }
        scale <1, 1.5, 1>
      }
      #if (Bozo) density { bozo density_map { [0 rgb 0.7] [1 rgb 1] } scale 0.1 } #end
      density { function { max(0, 1 - sqrt(x * x + z * z) / 3.9) } }
      method 3 intervals Intervals samples Samples
    }
  }
  #if (!Shadow) no_shadow #end
}
#if (Group)
light_group {
  light_source { <52, 142, 46> * 5 color rgb 0.9 parallel point_at <0, 0, 0> media_attenuation on }
  object { Haze }
  global_lights off
}
#else
light_source { <52, 142, 46> * 5 color rgb 0.9 parallel point_at <0, 0, 0> media_attenuation on }
object { Haze }
#end
