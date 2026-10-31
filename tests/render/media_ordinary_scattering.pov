#version 3.8;
// A constant method 4 fog lit only by a point light, in a container (Global=0) or filling the scene (Global=1);
// Scatter=0 drops its scattering. media_ordinary_scattering.sh requires the scattered light.
#ifndef (Global) #declare Global = 0; #end
#ifndef (Scatter) #declare Scatter = 1; #end
global_settings { assumed_gamma 1 }
camera { location <0, 0, -6> look_at 0 angle 40 }
background { rgb 0 }
light_source { <3, 4, -5> rgb 1 }
#declare Fog = media { method 4 absorption 0.2 #if (Scatter) scattering { 1, 0.2 } #end }
#if (Global)
  media { Fog }
  sphere { 0, 1 pigment { rgb 0.5 } }
#else
  box { -1, 1 pigment { rgbt 1 } hollow interior { media { Fog } } }
#end
