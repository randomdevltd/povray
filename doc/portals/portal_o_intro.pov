// A screen and a portal: the set shows what is behind the camera, and the doorway leads to a sea far away.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.45, -5.6> look_at <-0.15, 1.0, 0> angle 56 }
light_source { <-5, 9, -7> rgb 1 }
light_source { <6, 6, -9> rgb 0.35 shadowless }
Sky()
Floor()
box { <-6, 0, 0.9>, <6, 4, 1> pigment { rgb <0.85, 0.82, 0.75> } }

// Behind the camera, seen only by the screen's camera.
sphere { <-1.2, 0.8, -12>, 0.8 pigment { rgb <0.9, 0.15, 0.1> } finish { phong 0.5 } }
sphere { <1.2, 0.8, -12>, 0.8 pigment { rgb <0.1, 0.65, 0.2> } finish { phong 0.5 } }

box { <-2.5, 0, -0.4>, <-0.7, 0.6, 0.8> pigment { rgb <0.4, 0.28, 0.2> } }
object
{
    TV(1.5, 1.125, pigment { screen { camera { location <0, 1.3, -7.6> look_at <0, 0.75, -11.5> right x * 4 / 3 angle 55 } } })
    translate <-1.6, 0.72, -0.2>
}

object { Frame(1.2, 2.1, <0.2, 0.3, 0.28>) translate <1.5, 0, 0> }
portal { object { Door(1.2, 2.1) translate <1.5, 0, 0> } to { translate <998.5, 0, 0> } far off }
Elsewhere(<1000, 0, 0>)
