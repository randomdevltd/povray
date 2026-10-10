#version 4.0;
#include "scene.inc"
#declare N = 12;
mesh2 {
  vertex_vectors { N + 1,
    #for (I, 0, N - 1) <cos(I * tau / N), 0, sin(I * tau / N)>, #end
    <0, 1, 0> }
  face_indices { N, #for (I, 0, N - 1) <I, mod(I + 1, N), N> #if (I < N - 1) , #end #end }
  pigment { rgb <0.2, 0.6, 0.9> }
  translate <0, 0, 2>
}
#declare Path = array[7];
#for (I, 0, 6) #declare Path[I] = <I - 3, 0.5 + 0.3 * sin(I), -1.5>; #end
sphere_sweep {
  linear_spline 7,
  #for (I, 0, 6) Path[I], 0.15 #if (I < 6) , #end #end
  pigment { rgb <0.9, 0.5, 0.1> }
}
