#version 4.0;
#include "scene.inc"
#declare Tint = rgb <0.75, 0.6, 0.4>;
#macro Tinted(K) Tint * K #end
#for (i, 0, 3)
sphere { <i * 2.2 - 2.2, 0, 0>, 0.9 pigment { color Tinted(0.5 + i * 0.25) } finish { phong 1 diffuse 0.7 } }
#end
sphere { <0, -2.2, 0>, 0.9 pigment { color rgb <1, 0.4, 0.2> } finish { ambient 0.1 phong 0.9 phong_size 60 } }
