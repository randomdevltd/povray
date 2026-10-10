#version 4.0;
#include "scene.inc"
#declare S = 0;
#for (I, 1, 100000) #declare S = S + sin(I) * cos(I / 3); #end
#debug concat("loop sum ", str(S, 0, 9), "\n")
sphere { <0, 0.5, 0>, 0.5 + mod(abs(S), 1) * 0.5 pigment { rgb <0.3, 0.7, 0.4> } }
