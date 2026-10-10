#version 4.0;
#include "scene.inc"
#macro Tree(H)
  cylinder { 0, y * H, 0.1 pigment { rgb <0.4, 0.3, 0.2> } }
  sphere { y * H, H / 3 pigment { rgb <0.1, 0.5, 0.1> } }
#end
#macro Perp(V) vcross(V, y) #end
#macro Clamp(V) #if (V < 0) 0 #elseif (V > 1) 1 #else V #end #end
Tree(2)
union { Tree(1.5) translate x * 2 }
union { Tree(1) translate -x * 2 }
difference {
  box { <-1, -1, -1>, <1, 0.5, 1> }
  Tree(0.8)
  translate <0, 0, 3>
  pigment { rgb Clamp(1.5) }
}
#declare V = Perp(x);
sphere { V * 2 + y * 2, 0.3 pigment { rgb 0.5 } }
