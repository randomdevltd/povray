// A wall of security monitors, each a screen on its own camera around a courtyard that the render's camera never sees.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"
#include "projections.inc"

camera { location <0, 1.7, -4.3> look_at <0, 1.45, 0> angle 55 }
light_source { <-3, 4, -5> rgb 0.6 }
light_source { <3, 3.5, -4> rgb 0.25 shadowless }
Floor()
box { <-6, 0, 0.1>, <6, 4, 0.3> pigment { rgb <0.3, 0.32, 0.36> } }
box { <-2.4, 0, -1.6>, <2.4, 0.75, -0.9> pigment { rgb <0.35, 0.25, 0.18> } }
box { <-0.5, 0.75, -1.4>, <0.5, 0.78, -1.05> rotate -8 * x pigment { rgb 0.15 } }

#declare P = <500, 0, 0>;
light_group
{
    light_source { P + <-40, 60, -30> rgb 1.1 }
    box { P + <-12, -0.1, -12>, P + <12, 0, 12> pigment { checker rgb 0.7 rgb 0.55 scale 1.5 } }
    box { P + <-12, 0, 10>, P + <12, 3, 10.4> pigment { rgb <0.8, 0.7, 0.55> } }
    box { P + <-12.4, 0, -12>, P + <-12, 3, 10.4> pigment { rgb <0.8, 0.7, 0.55> } }
    difference { cylinder { P, P + 0.5 * y, 1.5 } cylinder { P + 0.1 * y, P + y, 1.35 } pigment { rgb 0.85 } }
    disc { P + 0.4 * y, y, 1.35 pigment { rgb <0.2, 0.45, 0.8> } }
    cylinder { P, P + 1.4 * y, 0.15 pigment { rgb 0.85 } }
    sphere { P + 1.5 * y, 0.3 pigment { rgb 0.85 } }
    cylinder { P + <-5, 0, 4>, P + <-5, 1.5, 4>, 0.25 pigment { rgb <0.35, 0.22, 0.12> } }
    cone { P + <-5, 1.2, 4>, 1.6, P + <-5, 5, 4>, 0 pigment { rgb <0.12, 0.45, 0.15> } }
    union
    {
        box { <-1.1, 0.3, -0.6>, <1.1, 0.9, 0.6> }
        box { <-0.6, 0.9, -0.55>, <0.5, 1.35, 0.55> }
        #for (I, 0, 3)
            cylinder { <-0.7 + 1.4 * mod(I, 2), 0.3, -0.62 + 1.24 * floor(I / 2)>, <-0.7 + 1.4 * mod(I, 2), 0.3, -0.5 + 1.0 * floor(I / 2)>, 0.3 pigment { rgb 0.1 } }
        #end
        pigment { rgb <0.8, 0.12, 0.1> }
        rotate 25 * y translate P + <4, 0, -3>
    }
    union
    {
        cylinder { <0, 0, 0>, <0, 1.4, 0>, 0.22 pigment { rgb <0.2, 0.3, 0.6> } }
        sphere { <0, 1.6, 0>, 0.2 pigment { rgb <0.85, 0.65, 0.5> } }
        translate P + <1.6, 0, 3>
    }
    global_lights off
}

#declare MercatorForward = vnormalize(<2.5, -0.8, 3>);
#declare MercatorRight = vnormalize(vcross(y, MercatorForward));
#declare MercatorUp = vcross(MercatorForward, MercatorRight);
#declare MercatorView = MercatorCamera(P + <-2.5, 1.6, -3>, MercatorForward, MercatorRight, MercatorUp, -55, 65);
#declare Cams = array[6]
{
    camera { location P + <0, 3, -12> look_at P + <0, 0.5, 0> right x * 4 / 3 angle 55 },
    camera { fisheye location P + <0, 8, -0.5> look_at P right x * 4 / 3 angle 180 },
    camera { ultra_wide_angle location P + <9, 4, 9> look_at P + <0, 0.5, 0> right x * 4 / 3 angle 150 },
    camera { panoramic location P + <7, 1.5, -7> look_at P + <4, 0.7, -3> right x * 4 / 3 },
    camera { MercatorView },
    camera { orthographic location P + <-10, 5, 8> look_at P + <-2, 1, 0> right 12 * x up 9 * y }
};
#for (I, 0, 5)
    #local Place = <-1.35 + 1.35 * mod(I, 3), 0.95 + 1.05 * (1 - floor(I / 3)), 0>;
    box { <-0.66, -0.06, -0.02>, <0.66, 0.96, 0.08> translate Place pigment { rgb 0.08 } }
    box
    {
        <0, 0, -0.03>, <1.2, 0.9, -0.02>
        pigment { screen { camera { Cams[I] } } scale <1.2, 0.9, 1> }
        finish { Glow }
        translate Place - 0.6 * x
    }
#end
