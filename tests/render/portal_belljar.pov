// A bell jar on a marble plinth whose inside shows the same room in miniature: a person looking at a bell jar
// on a plinth, inside which is a person looking at a bell jar, and so on. The miniature ends at the glass.
#version 3.8;
#include "portal.inc"
#ifndef (S) #declare S = 4; #end                  // how much larger the portal's target volume is than the jar
#ifndef (Depth) #declare Depth = 12; #end         // how many portal views may nest
#ifndef (Rad) #declare Rad = 0; #end              // 1: radiosity on
#ifndef (Ior) #declare Ior = 1.1; #end            // refractive index of the glass
#ifndef (Gloss) #declare Gloss = 0.25; #end       // strongest reflection of the glass, at grazing angles
global_settings
{
    assumed_gamma 1.0 max_trace_level 16 adc_bailout 0.002   // each nested jar costs two glass crossings
    #if (Rad) radiosity { pretrace_start 0.08 pretrace_end 0.02 count 150 error_bound 0.6 recursion_limit 2 } #end
}

camera { location <-0.67, 2.0, -1.66> look_at <0, 1.35, 0> angle 62 }

// The room: 12 by 12, 6.5 high, so the jar's target volume (4 high above the jar's base) lies inside it.
#declare Paint = pigment { rgb <0.88, 0.84, 0.76> };
box { <-6, -0.2, -6>, <6, 0, 6>
    pigment { wood color_map { [0 rgb <0.55, 0.36, 0.2>] [1 rgb <0.38, 0.22, 0.1>] } turbulence 0.04 scale <0.25, 1, 0.25> rotate <0, 20, 0> }
    finish { diffuse 0.8 specular 0.15 roughness 0.04 }
}
box { <-6.2, 0, -6.2>, <-6, 6.5, 6.2> pigment { Paint } }
box { < 6, 0, -6.2>, < 6.2, 6.5, 6.2> pigment { Paint } }
box { <-6.2, 0, 6>, < 6.2, 6.5, 6.2> pigment { Paint } }
box { <-6.2, 0, -6.2>, < 6.2, 6.5, -6> pigment { Paint } }
box { <-6.2, 6.5, -6.2>, < 6.2, 6.7, 6.2> pigment { rgb 0.95 } }
light_source { <0, 6.0, -1> rgb 1.1 }
light_source { <-4, 4.5, -4> rgb 0.35 shadowless }

// The plinth: a turned white marble pedestal with a footed base, beads, a vase shaft, neck rings and a flared top at 1.1.
#declare Marble = texture
{
    pigment
    {
        marble turbulence 0.6 omega 0.55 lambda 2.4
        color_map { [0 rgb <0.94, 0.93, 0.91>] [0.5 rgb <0.91, 0.90, 0.88>] [0.62 rgb <0.55, 0.56, 0.60>] [0.67 rgb <0.90, 0.89, 0.88>] [1 rgb <0.95, 0.94, 0.92>] }
        scale 0.6 rotate <15, 30, 70>
    }
    finish { diffuse 0.75 specular 0.5 roughness 0.004 reflection { 0.03, 0.15 fresnel on } }
}
#declare Top = 1.1;
#declare PlinthR = function(y)
{
    0.17 + 0.32 * exp(-pow(y / 0.10, 6)) + 0.07 * exp(-pow((y - 0.13) / 0.035, 2)) + 0.04 * exp(-pow((y - 0.19) / 0.02, 2))
    + 0.07 * exp(-pow((y - 0.52) / 0.28, 2)) + 0.035 * exp(-pow((y - 0.88) / 0.012, 2)) + 0.03 * exp(-pow((y - 0.93) / 0.012, 2))
    + 0.40 * exp(-pow((y - Top) / 0.11, 4))
}
lathe
{
    linear_spline 113, <0, 0>
    #local I = 0;
    #while (I <= 110)
        , <PlinthR(I / 100), I / 100>
        #local I = I + 1;
    #end
    , <0, Top>
    texture { Marble }
    interior { ior 1.5 }
}

// The jar: a thin glass shell open at the bottom, and a portal a hair inside it onto the room scaled S times about the base.
#declare B = <0, 1.102, 0>;
#declare Thick = 0.02;
#declare Radius = 0.5;
#declare Outer = merge { cylinder { B, B + 0.5 * y, Radius } sphere { B + 0.5 * y, Radius } };
#declare Cavity = merge { cylinder { B - 0.01 * y, B + 0.5 * y, Radius - Thick } sphere { B + 0.5 * y, Radius - Thick } };
#declare Mouth = Inset(Cavity, 0.002);
#declare Base = <B.x, min_extent(Mouth).y, B.z>;
difference
{
    object { Outer }
    object { Cavity }
    texture { pigment { rgbt 1 } finish { diffuse 0 specular 1 roughness 0.001 reflection { 0.02, Gloss fresnel on } } }
    interior { ior Ior fade_distance 0.5 fade_power 1 fade_color <0.7, 0.95, 0.85> }
}
portal { Mouth to { translate -Base scale S translate <B.x, -0.01, B.z> } exit max_trace_level Depth }  // the floor a hair above the base

// A person looking down at the jar: simple shapes, close to the plinth.
#declare Skin = pigment { rgb <0.85, 0.62, 0.48> };
union
{
    cylinder { <-0.12, 0.05, 0>, <-0.12, 0.85, 0>, 0.07 pigment { rgb <0.15, 0.17, 0.3> } }
    cylinder { < 0.12, 0.05, 0>, < 0.12, 0.85, 0>, 0.07 pigment { rgb <0.15, 0.17, 0.3> } }
    sphere { <0, 0.9, 0>, 0.17 scale <1.1, 0.8, 0.8> pigment { rgb <0.15, 0.17, 0.3> } }
    cylinder { <0, 0.95, 0>, <0, 1.45, 0>, 0.19 pigment { rgb <0.7, 0.25, 0.2> } }
    sphere { <0, 1.45, 0>, 0.19 scale <1.05, 0.5, 0.7> pigment { rgb <0.7, 0.25, 0.2> } }
    cylinder { <-0.26, 1.35, 0>, <-0.2, 0.95, 0.15>, 0.045 pigment { Skin } }
    cylinder { < 0.26, 1.35, 0>, < 0.2, 0.95, 0.15>, 0.045 pigment { Skin } }
    cylinder { <0, 1.5, 0>, <0, 1.6, 0>, 0.06 pigment { Skin } }
    sphere { <0, 1.72, 0>, 0.13 pigment { Skin } }
    sphere { <0, 1.74, -0.03>, 0.135 pigment { rgb <0.12, 0.08, 0.05> } }
    finish { diffuse 0.8 }
    rotate <0, 45, 0>
    translate <-0.6, 0, -0.6>
}
