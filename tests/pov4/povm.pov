#version 4.0;
#include "scene.inc"
// pov4_octahedron.povm is written by identical.sh into the directory the test runs in
mesh2 { povm "pov4_octahedron.povm" pigment { rgb <0.2, 0.5, 0.9> } finish { phong 0.5 } scale 1.5 translate y * 0.5 }
