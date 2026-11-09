// A method 2 cap lying exactly on a face of the point cloud's cells (y = 0), lit straight down its axis and seen from
// above: its light must be the same all round the axis. R turns the cylinder about its own axis, which turns the
// rounding of its surface points about the face; S scales the whole scene with mm_per_unit.
#version 4.0;
#ifndef (R) #declare R=0; #end
#ifndef (S) #declare S=1; #end
global_settings { assumed_gamma 1 mm_per_unit 1000/S subsurface { method 2 samples 8,8 } }
background { rgb 0 }
camera { location <0,0.06,0>*S sky z look_at <0,0,0> angle 20 }
light_source { <0,0.05,0>*S rgb 0.5 }
cylinder { <0,-0.03,0>,<0,0,0>,0.008
 texture { pigment { rgb <0.95,0.85,0.7> } finish { diffuse 0.8 subsurface { method 2 translucency <3,2,1> colour rgb 0.95 } } }
 interior { ior 1.45 } rotate y*R scale S }
