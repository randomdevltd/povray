// Two glass balls, each a portal onto the same island scaled up 20 times. Left: without exit the view runs on to the island's
// horizon. Right: with exit it ends at the glass and resumes in the room, so the island is a miniature inside the ball.
#version 3.8;
global_settings { assumed_gamma 1.0 max_trace_level 8 }
#include "portals.inc"

camera { location <0, 1.6, -3.4> look_at <0, 1.1, 0> angle 42 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 1.5>, <6, 4, 1.6> pigment { rgb <0.75, 0.25, 0.2> } }

#declare Island = <2000, 0, 0>;
#declare S = 20;
light_group
{
    light_source { Island + <-150, 300, -250> rgb 1.1 }
    disc { Island, y, 400 pigment { rgb <0.2, 0.45, 0.75> } }
    cylinder { Island - y, Island + 0.6 * y, 7 pigment { rgb <0.3, 0.6, 0.2> } }
    cone { Island + <-2, 0.6, 1>, 1.4, Island + <-2, 6.5, 1>, 0 pigment { rgb <0.1, 0.4, 0.15> } }
    cylinder { Island + <-2, 0, 1>, Island + <-2, 1.2, 1>, 0.3 pigment { rgb <0.35, 0.2, 0.1> } }
    box { <-1.5, 0, -1.5>, <1.5, 2.5, 1.5> rotate 30 * y translate Island + <2.5, 0.6, 0> pigment { rgb <0.95, 0.9, 0.8> } }
    intersection { box { <-1.25, -1.25, -1.6>, <1.25, 1.25, 1.6> rotate 45 * z } plane { -y, 0 } scale <1, 0.6, 1> translate <0, 2.5, 0> rotate 30 * y
        translate Island + <2.5, 0.6, 0> pigment { rgb <0.7, 0.2, 0.15> } }
    #for (I, 0, 11)
        cone { Island + vrotate(<30, 0, 0>, (30 * I + 10) * y), 2.5, Island + vrotate(<30, 7, 0>, (30 * I + 10) * y), 0 pigment { rgb <0.15, 0.45, 0.2> } }
    #end
    global_lights off
}

#macro Jar(X, Exit)
    cylinder { <X, 0, 0>, <X, 0.6, 0>, 0.3 pigment { rgb 0.9 } }
    difference
    {
        sphere { <X, 1.1, 0>, 0.5 }
        sphere { <X, 1.1, 0>, 0.49 }
        pigment { rgbt 1 }
        finish { specular 0.6 roughness 0.002 reflection { 0.02, 0.2 fresnel on } }
        interior { ior 1.3 }
    }
    portal { sphere { <X, 1.1, 0>, 0.485 } to { translate <-X, -1.1, 0> scale S / 0.485 * 0.45 translate Island + <0, 0.6, 0> } #if (Exit) exit #else far off #end }
#end
Jar(-0.75, false)
Jar(0.75, true)
