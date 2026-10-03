// Four doorways onto the sea set into a brick wall: open; rgbt 0.5, half open; tinted blue; and a gradient, shut at the
// foot and open at the head.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.3, -6.6> look_at <0, 1.0, 0> angle 56 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 0.1>, <6, 4, 0.3> pigment { brick rgb 0.75, rgb <0.65, 0.3, 0.2> scale 0.08 } }

#declare Far = <1000, 0, 0>;
Elsewhere(Far)
#declare Opening = array[4]
{
    pigment { rgb 1 },
    pigment { rgbt <1, 1, 1, 0.5> },
    pigment { rgb <0.4, 0.6, 1> },
    pigment { gradient y color_map { [0 rgbt <1, 1, 1, 1>] [1 rgb 1] } scale 1.8 }
};
#for (I, 0, 3)
    #local X = -2.55 + 1.7 * I;
    object { Frame(1.1, 1.8, <0.2, 0.3, 0.28>) translate X * x }
    portal { object { Door(1.1, 1.8) translate X * x } to { translate Far - X * x } far off pigment { Opening[I] } }
#end
