// A short stone tunnel on a hillside whose far end is a portal onto a sea at sunset: the light at the end of the tunnel.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0.7, 1.4, -5> look_at <0, 1.1, 3> angle 48 }
light_source { <-6, 12, -8> rgb 1 }
light_source { <6, 6, -10> rgb 0.3 shadowless }
Sky()
box { <-60, -0.1, -60>, <60, 0, 60> pigment { rgb <0.3, 0.55, 0.2> } }
difference
{
    box { <-1.6, 0, 0>, <1.6, 2.8, 6> }
    box { <-1, -0.1, -0.1>, <1, 2.2, 6.1> }
    pigment { brick rgb 0.55, rgb 0.45 scale 0.1 }
}
difference { sphere { <0, -6, 7>, 9.5 } box { <-1.6, -0.1, -10>, <1.6, 2.8, 6> } pigment { rgb <0.3, 0.55, 0.2> } }
#declare Far = <1000, 0, 0>;
Elsewhere(Far)
portal { object { Door(2.0, 2.2) translate 5.9 * z } to { translate Far - 5.9 * z } }
