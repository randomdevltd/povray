#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Fallback) #declare Fallback = 0; #end
global_settings { assumed_gamma 1 }
#if (!Fallback)
  #if (!Reference) camera { orthographic location <10, 0, -5> look_at <10, 0, 0> right 2*x up 2*y tags { "keep" } } #end
  camera { orthographic location <0, 0, -5> look_at 0 right 2*x up 2*y tags { "keep" } }
#end
#if (!Reference) camera { location <100, 0, -5> look_at <100, 0, 0> tags { "discard" } } #end
sphere { <0, 0, 3>, 0.8 pigment { rgb <0, 0.5, 1> } finish { emission 1 diffuse 0 } }
