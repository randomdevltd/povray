// skein_bark_taper.pov: a log tapering from radius 0.6 to a 0.04 needle, cracked by Method (skein_bark.inc); Seam=1 turns u = 0 to the camera, untwisted; Tip=1 views the thin half.
#version 3.8;
#ifndef (Seam) #declare Seam = 0; #end
#ifndef (Tip) #declare Tip = 0; #end
#declare Shape = 1;
#if (Seam) #declare Twist = 0; #end
global_settings { assumed_gamma 1 }
#include "skein_bark.inc"

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
#if (Seam)
  camera { location <1, 0, -3.4> look_at <1, 0, 0> angle 38 }
  object { BarkSkein() texture { BarkTexture } rotate y*90 rotate z*-90 }
#elseif (Tip)
  camera { location <3.3, 0.3, -2.7> look_at <3.3, 0, 0> angle 30 }
  object { BarkSkein() texture { BarkTexture } rotate z*-90 }
#else
  camera { location <0, 3.4, -7.4> look_at <0, 1.1, 0> angle 40 }
  object { BarkSkein() texture { BarkTexture } rotate z*-62 translate <-1.5, 0.3, 0> }
#end
