#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1 }
#default { camera { orthographic location <0, 0, -5> look_at 0 right 2*x up 2*y } }
camera { location <100, 0, -5> look_at <100, 0, 0> filter_tags { none } }
sphere { 0, 0.8 pigment { rgb <0, 0, 1> } finish { emission 1 diffuse 0 } tags { "screen" } }
box {
  <99, -1, 0>, <101, 1, 0.01>
  pigment {
    screen { #if (Reference) camera { orthographic location <0, 0, -5> look_at 0 right 2*x up 2*y } #end }
    scale 2 translate <99, -1, 0>
  }
  finish { emission 1 diffuse 0 }
}
