#version 4.0;
#include "scene.inc"
#declare Red = rgb <0.8, 0.1, 0.1>;
#declare Hot = true;
#macro Scale(S) S * 0.5 #end
#declare Ball = sphere { 0, 1 pigment { color Red } }
Ball
object { Ball translate x * 2.5 scale Scale(1.5) }
sphere { <-2.5, 0, 0>, Scale(2) pigment { color rgb <1, #if (Hot) 0.2 #else 0.8 #end, 0> } finish { phong 1 } }
