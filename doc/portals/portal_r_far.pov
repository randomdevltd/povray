// Two doorway pairs, A then B, each mouth showing the room behind the camera; the right pair is far off, so its B is empty.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.6, -6.4> look_at <0, 1.0, 0> angle 60 }
light_source { <-4, 9, -3> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 1.5>, <6, 4, 1.6> pigment { brick rgb 0.75, rgb <0.65, 0.3, 0.2> scale 0.08 } }

// The other side of the room, behind the camera.
box { <-8, 0, -9.1>, <8, 4, -9> pigment { rgb <0.95, 0.6, 0.2> } }
#for (I, -3, 3)
    cylinder { <2 * I, 0, -8.6>, <2 * I, 4, -8.6>, 0.25 pigment { rgb <0.15, 0.3, 0.8> } }
#end

#macro Pair(XA, XB, Far)
    object { Frame(1.0, 1.9, <0.2, 0.3, 0.28>) translate XA * x }
    object { Frame(1.0, 1.9, <0.2, 0.25, 0.3>) translate XB * x }
    portal { object { Door(1.0, 1.9) translate XA * x } to { translate -XA * x rotate 180 * y translate XB * x } #if (!Far) far off #end }
#end
Pair(-2.6, -1.0, true)
Pair(1.0, 2.6, false)
