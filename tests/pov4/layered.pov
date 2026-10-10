#version 4.0;
#include "scene.inc"
#declare Tint = color red 0.9 green 0.6 blue 0.2;
#declare Layered = texture { pigment { color Tint } } texture { pigment { rgbt <0, 0, 0, 0.7> } finish { phong 1 } }
#declare ReadHeight = 1.5;
#declare ReadTexture = texture { pigment { color Tint } }
#include "reads.inc"
ReadBack
box { <-3, -1, -1>, <-2, 0, 0> texture { Layered } }
sphere { 0, 0.5 pigment { rgb 1 } matrix <1, 0, 0, 0, 1, 0, 0, 0, 1, 2, 0, 0> }
