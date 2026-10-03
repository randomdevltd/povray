// A poster printed live in halftone: a screen used as a pigment_pattern picks, for its brightness, a grid of dots of one size.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.4, -3.2> look_at <0, 1.3, 0> angle 50 }
light_source { <-3, 6, -6> rgb 1 }
light_source { <4, 4, -6> rgb 0.3 shadowless }
Sky()
Floor()
light_source { <2, 5, -3> rgb 0.6 }
box { <-6, 0, 0.1>, <6, 4, 0.2> pigment { rgb <0.55, 0.62, 0.7> } }

// The subject, behind the camera: white shapes before a dark wall.
box { <-3, 0, -9.2>, <3, 3, -9> pigment { rgb 0.08 } }
box { <-3, 0, -9>, <3, 0.01, -6> pigment { rgb 0.12 } }
sphere { <-0.5, 0.5, -7>, 0.5 pigment { rgb 0.95 } }
cone { <0.5, 0, -7.2>, 0.45, <0.5, 1.4, -7.2>, 0 pigment { rgb 0.95 } }
box { <-0.3, 0, -0.3>, <0.3, 0.6, 0.3> rotate 30 * y translate <1.3, 0, -7.5> pigment { rgb 0.95 } }

#macro Dots(R)
    function { sqrt(pow(x - floor(x) - 0.5, 2) + pow(y - floor(y) - 0.5, 2)) } color_map { [R rgb 0.04] [R rgb 0.92] } scale <1 / 40, 1 / 30, 1>
#end
#declare Print = pigment
{
    pigment_pattern { screen { camera { location <0.3, 1.0, -4.5> look_at <0.3, 0.55, -7.2> right x * 4 / 3 angle 45 } } }
    pigment_map
    {
        #for (K, 0, 7)
            [K / 8 Dots(sqrt((1 - (K + 0.5) / 8) / pi))]
            [(K + 1) / 8 Dots(sqrt((1 - (K + 0.5) / 8) / pi))]
        #end
    }
};
box { <-1.3, 0.3, 0.06>, <1.3, 2.3, 0.1> pigment { rgb 0.15 } }
box { <0, 0, 0>, <2.4, 1.8, 0.01> pigment { Print scale <2.4, 1.8, 1> } finish { diffuse 0.9 } translate <-1.2, 0.4, 0.04> }
