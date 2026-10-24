// Generated meshes declared first, then a parse-heavy placement loop: the shape eager preparation overlaps.
// Meshes isosurface_mesh assets, then Steps passes of a macro loop, then Keep of the assets placed.
#version 4.0;
global_settings { assumed_gamma 1 }
#ifndef (Meshes) #declare Meshes = 8; #end
#ifndef (Keep) #declare Keep = Meshes; #end
#ifndef (Steps) #declare Steps = 100000; #end
#include "functions.inc"
#declare Rough = function(x, y, z, k) { sqrt(x*x + y*y + z*z) - 1 + 0.3 * (f_noise3d(3*x + k, 3*y, 3*z) - 0.5) }
#declare Asset = array[Meshes];
#for (I, 0, Meshes - 1)
  #declare Asset[I] = isosurface_mesh {
    function { Rough(x, y, z, I) }
    contained_by { sphere { 0, 1.3 } }
    max_gradient 2.5
  }
#end
#macro Step(A, B)
  #local C = A * 0.5 + B;
  #if (C > 1000)
    #local C = C - 1000;
  #end
  C
#end
#declare Acc = 0;
#for (I, 1, Steps)
  #declare Acc = Step(Acc, I);
#end
#for (I, 0, Keep - 1)
  object { Asset[I] translate <3 * mod(I, 4), 3 * div(I, 4), 0> pigment { rgb <0.5, 0.7, 1> } }
#end
camera { location <4.5, 3, -14> look_at <4.5, 3, 0> }
light_source { <-3, 5, -10> rgb 1 }
sphere { <4.5, -3, 0>, 0.5 pigment { rgb mod(Acc, 1000) / 1000 } }
