// Radiosity through a screen: a TV shows a lit corner behind the render's camera. Mode=0 the screen; 1 the screen's camera
// alone; 2 mode 1's picture (Shot) as an image_map; 3 and 4 a mask pair; 5 the screen filling the frame, pixel for pixel mode 1.
#version 3.8;
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Size) #declare Size = 0; #end            // > 0: the screen camera's radiosity_size is Size * 4 / 3 by Size
#ifndef (Sets) #declare Sets = 1; #end            // 2: a second set on the same camera; 3: on another camera; 4: on the render's
#ifndef (Shot) #declare Shot = "shot.png"; #end
#ifndef (Rad) #declare Rad = 1; #end              // 0: radiosity off
#ifndef (NoRad) #declare NoRad = 0; #end          // 1: the screen cameras have no_radiosity, and no ambient anywhere
global_settings
{
    assumed_gamma 1.0 max_trace_level 5
    #if (NoRad) ambient_light 0 #end
    #if (Rad & (Mode != 3) & (Mode != 4)) radiosity { pretrace_start 0.08 pretrace_end 0.01 count 100 error_bound 0.5 recursion_limit 2 } #end
}

#include "tv.inc"

#declare Main = camera { location <0, 1.6, -6> look_at <0, 1.0, 0> angle 45 }
#declare Corner = camera { location <0, 1.3, -9> look_at <0, 1.0, -16> right x * 4 / 3 angle 55 }
#declare Wide = camera { location <0, 1.6, -8> look_at <0, 1.0, -16> right x * 4 / 3 angle 70 }
#switch (Mode)
    #case (1) camera { Corner } #break
    #case (5) camera { orthographic location <0, 0.75, -5> direction z right x * 2 up y * 1.5 } #break
    #else camera { Main }
#end

light_source { <-6, 10, -8> rgb 0.9 }
light_source { <2.4, 2.6, -11> rgb 0.6 }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.6, 0.65, 0.8>] [1 rgb <0.15, 0.25, 0.6>] } } }
plane { y, 0 pigment { checker rgb 0.85, rgb 0.35 } }

box { <-3, 0, -16.1>, <3, 3, -16> pigment { rgb 0.9 } }
box { <-3.1, 0, -16>, <-3, 3, -10> pigment { rgb <0.9, 0.12, 0.08> } }
box { <3, 0, -16>, <3.1, 3, -10> pigment { rgb <0.1, 0.6, 0.2> } }
box { <-1.6, 0, -15.2>, <-0.4, 1.2, -14> pigment { rgb 0.9 } }
sphere { <1.2, 0.7, -14.2>, 0.7 pigment { rgb <0.9, 0.85, 0.3> } }

#macro Set(View, Place)
    #switch (Mode)
        #case (2) #local Picture = pigment { image_map { png Shot interpolate 2 } } #break
        #case (3) #local Picture = pigment { rgb 0 } #break
        #case (4) #local Picture = pigment { rgb 1 } #break
        #else
            #local Picture = pigment
            {
                screen { camera { View #if (Size > 0) radiosity_size <Size * 4 / 3, Size> #end #if (NoRad) no_radiosity #end } }
            }
    #end
    // The set's glow is kept out of the radiosity, so the photographed corner is lit the same in every mode.
    object { TV(2, 1.5, Picture) no_radiosity translate Place }
#end

#if (Mode = 5)
    box { <-1, 0, -0.01>, <1, 1.5, 0>
        pigment { screen { camera { Corner #if (NoRad) no_radiosity #end } } scale <2, 1.5, 1> translate <-1, 0, 0> }
        finish { emission 1 diffuse 0 }
        no_radiosity
    }
#else
    Set(Corner, <0, 0, 0>)
#end
#switch (Sets)
    #case (2) Set(Corner, <2.8, 0, 1>) #break
    #case (3) Set(Wide, <2.8, 0, 1>) #break
    #case (4) Set(Main, <2.8, 0, 1>) #break
#end
