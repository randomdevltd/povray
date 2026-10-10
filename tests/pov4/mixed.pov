#version 4.0;
#include "colors.inc"
#include "scene.inc"
#include "pillar.inc"
#declare S = seed(7);
#for (I, 0, 4)
  sphere { <rand(S) * 4 - 2, rand(S), rand(S) * 2>, 0.2 pigment { color Orange } }
#end
Pillar(1.5, Orange)
#declare K = 3;
#declare Amp = 0.3;
isosurface {
  function { sqrt(x * x + y * y + z * z) - 1 + Amp * sin(K * x) * sin(K * z) }
  contained_by { box { -1.5, 1.5 } }
  max_gradient 4
  pigment { rgb <0.5, 0.8, 0.5> }
  translate <2, 1, 3>
}
