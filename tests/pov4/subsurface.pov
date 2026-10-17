#version 4.0;
#include "scene.inc"
global_settings { mm_per_unit 10 subsurface { samples 8, 2 method 2 spacing 2 memory 256 } }
sphere { 0, 1 pigment { rgb <0.8, 0.4, 0.3> } finish { diffuse 0.2 subsurface { translucency <0.6, 0.4, 0.3> } } }
