// Two rooms far apart joined by a doorway pair: the door in the back wall of the blue room opens into the orange one, and
// its far mouth leads back. Night=1 puts out both lamps and lights a candle in the orange room, whose light comes through.
#version 3.8;
#ifndef (Night) #declare Night = 0; #end
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <-1.4, 1.4, -2.7> look_at <0.6, 1.1, 3> angle 58 }

#macro Room(C, Wall, Floor1, Floor2)
    difference
    {
        box { C + <-3.2, -0.1, -3.2>, C + <3.2, 3.2, 3.2> }
        box { C + <-3, 0, -3>, C + <3, 3, 3> }
        box { C + <0.3, 0, 2.9>, C + <1.3, 2.1, 3.3> }
        pigment { rgb Wall }
    }
    box { C + <-3, -0.1, -3>, C + <3, 0.001, 3> pigment { checker rgb Floor1 rgb Floor2 scale 0.6 } }
    #if (!Night) light_source { C + <-0.5, 2.3, -0.5> rgb 0.9 fade_distance 3 fade_power 1 } #end
#end
#declare Cool = <0, 0, 0>;
#declare Warm = <1000, 0, 0>;
Room(Cool, <0.55, 0.65, 0.82>, <0.85, 0.85, 0.88>, <0.6, 0.62, 0.7>)
Room(Warm, <0.9, 0.6, 0.35>, <0.7, 0.48, 0.3>, <0.55, 0.35, 0.2>)
#if (Night) light_source { <-3, 2.8, -2> rgb 0.06 shadowless } #end

// The blue room: a plant and a stool.
cylinder { <-2, 0, 2>, <-2, 0.5, 2>, 0.3 pigment { rgb <0.7, 0.35, 0.2> } }
sphere { <-2, 1.0, 2>, 0.55 pigment { rgb <0.15, 0.5, 0.2> } }
cylinder { <2, 0, 1>, <2, 0.6, 1>, 0.35 pigment { rgb 0.3 } }

// The orange room: a sofa against the far wall, a picture, a table, a candle near the door.
union
{
    box { <-1.4, 0, -3>, <1.4, 0.45, -2.2> }
    box { <-1.4, 0, -3>, <1.4, 1.0, -2.8> }
    box { <-1.4, 0, -3>, <-1.2, 0.7, -2.2> }
    box { <1.2, 0, -3>, <1.4, 0.7, -2.2> }
    pigment { rgb <0.7, 0.12, 0.15> }
    translate Warm
}
box { Warm + <-0.8, 1.4, -2.99>, Warm + <0.8, 2.3, -2.95> pigment { gradient y color_map { [0 rgb <0.2, 0.5, 0.3>] [1 rgb <0.5, 0.75, 0.95>] } scale 0.9 translate 1.4 * y } }
cylinder { Warm + <-0.6, 0, 0.6>, Warm + <-0.6, 0.7, 0.6>, 0.5 pigment { rgb <0.4, 0.25, 0.15> } }
#if (Night)
    cylinder { Warm + <-0.6, 0.7, 0.6>, Warm + <-0.6, 0.9, 0.6>, 0.04 pigment { rgb 0.95 } }
    light_source
    {
        Warm + <-0.6, 0.98, 0.6> rgb <1, 0.6, 0.25> * 6 fade_distance 0.8 fade_power 2
        looks_like { sphere { 0, 0.035 pigment { rgb <1, 0.8, 0.4> } finish { Glow } } }
    }
#end

// The doorway pair: in through the blue room's door, out of the orange room's, and back.
portal { object { Door(1.0, 2.1) translate <0.8, 0, 3.1> } to { translate <-0.8, 0, -3.1> rotate 180 * y translate Warm + <0.8, 0, 3.1> } }
