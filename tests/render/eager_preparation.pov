#version 4.0;
// A, B, D and Unused are recorded before a later global_settings changes what their functions read (Mode 0 noise, 1 gamma, 2 waves);
// A, B queried unless Queries is 0. Mode 3 filters B and D, 4 errs, 5 is povm; 6, 8-10 nest functions, 7 tests an object.
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Late) #declare Late = 1; #end
#ifndef (Tail) #declare Tail = 0; #end
#ifndef (Queries) #declare Queries = 1; #end
#ifndef (Fine) #declare Fine = 0; #end
#if (Mode != 1) global_settings { assumed_gamma 1 } #end
#if (Mode = 3) global_settings { filter_tags { !"b" } } #end
#include "functions.inc"
background { rgb 0 }
camera { orthographic location <0, 0, -10> look_at 0 right 13*x up 4*y }
light_source { <-3, 5, -8> rgb 1 }
#switch (Mode)
  #case (1)
    #declare Tone = function { pigment { gradient y scale 3 translate -1.5*y color_map { blend_mode 1 [0 rgb 0] [1 rgb 1] } } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.6 * (Tone(x, y, z).gray - 0.5) }
  #break
  #case (6)
    #declare Inner = function(x, y, z) { 0.5 + 0.5 * sin(4 * x) * cos(3 * z) }
    #declare Mix = function { pigment { function { Inner(x, y, z) } color_map { [0 rgb 0] [1 rgb 1] } } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.4 * (Mix(x, y, z).gray - 0.5) }
  #break
  #case (7)
    #declare Probe = function { pigment { object { sphere { 0, 0.9 } rgb 0, rgb 1 } } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.3 * (Probe(x, y, z).gray - 0.5) }
  #break
  #case (8)
    #declare Inner = function(x, y, z) { 0.5 + 0.5 * sin(4 * x) * cos(3 * z) }
    #declare Ripple = pigment { function { Inner(x, y, z) } color_map { [0 rgb 0] [1 rgb 1] } }
    #declare Mix = function { pigment { gradient y pigment_map { [0 Ripple] [1 rgb 0.5] } scale 2 translate -y } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.4 * (Mix(x, y, z).gray - 0.5) }
  #break
  #case (9)
    #declare Leaf = function(x, y, z) { 0.5 + 0.5 * sin(4 * x) * cos(3 * z) }
    #declare Inner = function { pigment { function { Leaf(x, y, z) } color_map { [0 rgb 0] [1 rgb 1] } } }
    #declare Middle = function(x, y, z) { Inner(x, y, z).gray }
    #declare Outer = function { pigment { function { Middle(x, y, z) } color_map { [0 rgb 0.2] [1 rgb 1] } } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.4 * (Outer(x, y, z).gray - 0.5) }
  #break
  #case (10)
    #declare Shared = function(x, y, z) { 0.5 + 0.5 * sin(4 * x) * cos(3 * z) }
    #declare Across = pigment { function { Shared(x, y, z) } color_map { [0 rgb 0] [1 rgb 1] } }
    #declare Along = pigment { function { Shared(z, y, x) } color_map { [0 rgb 1] [1 rgb 0] } }
    #declare Mix = function { pigment { average pigment_map { [1 Across] [1 Along] } } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.4 * (Mix(x, y, z).gray - 0.5) }
  #break
  #case (2)
    #declare Swell = function { pattern { waves scale 0.4 } }
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.4 * (Swell(x, y, z) - 0.5) }
  #break
  #else
    #declare Rough = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 + 0.6 * (f_noise3d(3*x, 3*y, 3*z) - 0.5) }
#end
#macro Lump() isosurface_mesh { function { Rough(x, y, z) } contained_by { sphere { 0, 1.5 } } max_gradient 6
  #if (Fine) max_angle 6 min_size 0.007 #else max_angle 30 min_size 0.04 #end } #end
#if (Mode = 5)
#declare P = mesh2 { povm "deferred_mesh_bounds.povm" inside_vector <0.123, 0.937, 0.271> }
#declare Acc = 0;
#for (I, 1, Tail) #declare Acc = mod(Acc * 3 + I, 1000); #end
object { P pigment { rgb 1 } }
#else
#declare A = Lump();
#declare B = Lump();
#declare D = Lump();
#declare Unused = Lump();
#if (Fine) #debug "eager: recorded\n" #end
#if (Queries) #declare QA = max_extent(A); #end
#if (Late)
  #switch (Mode)
    #case (1) global_settings { assumed_gamma 2.2 } #break
    #case (2) global_settings { number_of_waves 3 } #break
    #else global_settings { noise_generator 1 }
  #end
#end
#declare C = Lump();
#if (Queries) #declare QB = max_extent(B); #end
#declare Acc = 0;
#for (I, 1, Tail) #declare Acc = mod(Acc * 3 + I, 1000); #end
#declare Paint = pigment { rgb <0.9, 0.8, 0.6> }
object { A translate -4.5*x pigment { Paint } }
object { B translate -1.5*x pigment { Paint } #if (Mode = 3) tags { "b" } #end }
object { C translate 1.5*x pigment { Paint } }
object { D translate 4.5*x pigment { Paint } #if (Mode = 3) tags { "b" } #end }
#if (Mode = 4) #error "stop after the meshes are recorded" #end
#end
#if (Fine) #debug "eager: parsed\n" #end
