// A television showing a broadcast from a studio elsewhere in the scene through a convex tube.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0.9, 1.3, -3.9> look_at <0.1, 0.95, 0> angle 48 }
light_source { <-3, 5, -4> rgb 0.9 }
light_source { <4, 3, -5> rgb 0.25 shadowless }
Floor()
box { <-6, 0, 1>, <6, 4, 1.1> pigment { rgb <0.7, 0.62, 0.5> } }
box { <-1.2, 0, -0.3>, <1.2, 0.5, 0.9> pigment { rgb <0.35, 0.22, 0.14> } }
cylinder { <-0.9, 0.5, -0.1>, <-0.9, 0.62, -0.1>, 0.12 pigment { rgb <0.2, 0.4, 0.3> } }

// The studio: a presenter at a desk before a backdrop, with lights of its own.
#declare S = <0, 0, 500>;
light_group
{
    light_source { S + <-3, 4, -5> rgb 1 }
    light_source { S + <3, 3, -4> rgb 0.5 }
    box { S + <-4, 0, 1>, S + <4, 3, 1.1> pigment { gradient y color_map { [0 rgb <0.1, 0.2, 0.5>] [1 rgb <0.3, 0.55, 0.9>] } scale 3 } }
    box { S + <-1.2, 0, -0.4>, S + <1.2, 0.8, 0.1> pigment { rgb <0.8, 0.8, 0.85> } }
    cylinder { S + <0, 0.6, 0.4>, S + <0, 1.3, 0.4>, 0.3 pigment { rgb <0.15, 0.15, 0.2> } }
    sphere { S + <0, 1.55, 0.4>, 0.22 pigment { rgb <0.85, 0.65, 0.5> } }
    global_lights off
}
#declare Broadcast = pigment
{
    screen { camera { location S + <0, 1.2, -3> look_at S + <0, 1.15, 0> right x * 4 / 3 angle 40 } }
};
object { TV(1.2, 0.9, Broadcast) translate <0.2, 0.62, -0.1> }
