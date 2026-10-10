#version 4.0;
#include "scene.inc"
#declare Sizes = dictionary { .small: 0.2, ["big one"]: 0.45 };
#declare I = 0;
#while (I < 5)
  #declare R = (I < 2 ? Sizes.small : Sizes["big one"]);
  #if (I != 3)
    sphere { <I - 2, R, 0>, R pigment { rgb <I / 4, 0.5, 1 - I / 4> } }
  #end
  #declare I = I + 1;
#end
#for (A, 0, 1, 0.25) box { <A * 4 - 2, -1, 1>, <A * 4 - 1.8, A, 1.2> pigment { rgb 1 } } #end
#declare Total = 0 + 0.2 + 0.45;
sphere { <0, 2, 0>, Total / 4 pigment { rgb x } }
sphere { <-2, 2, 0>, A / 5 scale #if (Total > 0.5) 0.3 * 2 #else 0.5 #end pigment { rgb y } }
