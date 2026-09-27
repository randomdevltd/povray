// Isosurface stress: a sphere displaced by five octaves of strong noise: steep, with thin spikes and small fragments
// broken off. Declare MaxGradient=g, Under=1 to halve it, Pad=k to widen the container k times.
#version 3.7;
#include "functions.inc"
#ifndef (MaxGradient) #declare MaxGradient = 14; #end
#ifndef (Under) #declare Under = 0; #end
#ifndef (Pad) #declare Pad = 1; #end
global_settings { assumed_gamma 1.0 }

#declare Octaves = function(x, y, z) {
    (2 * f_noise3d(x, y, z) - 1)
  + (2 * f_noise3d(x * 2.1, y * 2.1, z * 2.1) - 1) * 0.5
  + (2 * f_noise3d(x * 4.3, y * 4.3, z * 4.3) - 1) * 0.25
  + (2 * f_noise3d(x * 8.7, y * 8.7, z * 8.7) - 1) * 0.125
  + (2 * f_noise3d(x * 17.9, y * 17.9, z * 17.9) - 1) * 0.0625
}

isosurface {
  function { f_sphere(x, y, z, 1) - 0.95 * Octaves(x * 1.7, y * 1.7, z * 1.7) }
  contained_by { box { -2.2 * Pad, 2.2 * Pad } }
  max_gradient MaxGradient * (Under ? 0.5 : 1)
  accuracy 0.0005
  pigment { color rgb <0.85, 0.75, 0.6> }
  finish { ambient 0.08 diffuse 0.8 specular 0.3 roughness 0.02 }
}

light_source { <-6, 9, -8> color rgb 1.0 }
light_source { <7, 2, -5> color rgb 0.35 }
background { color rgbt <0.1, 0.1, 0.14, 1> }
camera { perspective angle 38 location <0, 0.8, -7.5> look_at <0, 0, 0> right x * 4/3 up y }
