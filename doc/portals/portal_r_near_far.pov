// One doorway pair: the portal's pigment tints both mouths orange, and far { } overrides it with blue for the far mouth.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.6, -5> look_at <0, 1.0, 0> angle 50 }
light_source { <-4, 9, -3> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 1.5>, <6, 4, 1.6> pigment { brick rgb 0.75, rgb <0.65, 0.3, 0.2> scale 0.08 } }
box { <-8, 0, -9.1>, <8, 4, -9> pigment { rgb 0.92 } }
#for (I, -3, 3)
    cylinder { <2 * I, 0, -8.6>, <2 * I, 4, -8.6>, 0.25 pigment { rgb 0.3 } }
#end

object { Frame(1.0, 1.9, <0.2, 0.3, 0.28>) translate -1.1 * x }
object { Frame(1.0, 1.9, <0.2, 0.25, 0.3>) translate 1.1 * x }
portal
{
    object { Door(1.0, 1.9) translate -1.1 * x }
    to { translate 1.1 * x rotate 180 * y translate 1.1 * x }
    pigment { rgb <1, 0.6, 0.3> }
    far { pigment { rgb <0.45, 0.7, 1> } }
}
