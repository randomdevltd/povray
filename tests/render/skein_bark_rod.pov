// skein_bark_rod.pov: a rod of bulges and necks (radius 0.05 to 0.55), cracked by Method (skein_bark.inc); Bent=1 extrudes it round a 45 degree arc instead of twisting.
#version 3.8;
#ifndef (Bent) #declare Bent = 0; #end
#ifndef (Seam) #declare Seam = 0; #end
#declare Shape = (Bent ? 3 : 2);
#if (Seam) #declare Twist = 0; #end
global_settings { assumed_gamma 1 }
#include "skein_bark.inc"

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
#if (Seam)
  camera { location <2, 0, -4.6> look_at <2, 0, 0> angle 40 }
  object { BarkSkein() texture { BarkTexture } rotate y*90 rotate z*-90 }
#else
  camera { location <0, 3.4, -7.4> look_at <0, 1.1, 0> angle 40 }
  object { BarkSkein() texture { BarkTexture } rotate z*-62 translate <-1.5, 0.3, 0> }
#end
