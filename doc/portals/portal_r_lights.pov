// A doorway onto a lamp in a closed room far away. The lamp's light comes out of the doorway onto the floor and the ball in
// front of it; NoLights=1 adds no_lights, and only the view goes through.
#version 3.8;
#ifndef (NoLights) #declare NoLights = 0; #end
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <1.4, 2.4, -5.2> look_at <0, 0.8, 0> angle 48 }
light_source { <-3, 10, -8> rgb 0.15 shadowless }
Floor()
box { <-6, 0, 2.5>, <6, 4, 2.6> pigment { rgb <0.85, 0.82, 0.75> } }
sphere { <-1.1, 0.35, -1.0>, 0.35 pigment { rgb <0.9, 0.9, 0.85> } }

#declare Far = <1000, 0, 0>;
difference
{
    box { Far + <-2.1, -0.1, -0.1>, Far + <2.1, 3.1, 4.1> }
    box { Far + <-2, 0, 0>, Far + <2, 3, 4> }
    pigment { rgb <0.8, 0.7, 0.55> }
}
light_source
{
    Far + <0.3, 1.2, 1.8> rgb <1, 0.6, 0.25> * 4 fade_distance 1 fade_power 2
    looks_like { sphere { 0, 0.08 pigment { rgb <1, 0.8, 0.5> } finish { Glow } } }
}
object { Frame(1.0, 2.0, <0.2, 0.3, 0.28>) }
portal { Door(1.0, 2.0) to { translate Far } far off #if (NoLights) no_lights #end }
