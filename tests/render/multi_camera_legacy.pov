// Output cameras in an older scene (Version 3.1 or 3.7); before 3.5 a camera starts from the one before it, so each sets
// what it uses. Cam renders one alone without its block.
#ifndef (Version) #declare Version = 3.1; #end
#version Version;
#ifndef (Cam) #declare Cam = -1; #end
#if ((Cam = -1) | (Cam = 0)) camera { location <0, 2, -5> look_at <0, 0.5, 0> angle 30 #if (Cam = -1) output { size <40, 30> } #end } #end
#if ((Cam = -1) | (Cam = 1)) camera { #if (Cam = -1) output { size <30, 40> } #end location <3, 3, -3> look_at <0, 0.5, 0> angle 45 } #end
plane { y, 0 pigment { checker rgb 0.9 rgb 0.4 } }
sphere { <0, 0.8, 0>, 0.7 pigment { rgb <0.8, 0.3, 0.2> } }
light_source { <-3, 5, -4> rgb 1 }
