// Video feedback: a rolled camera sees a television showing what it sees, so the nested frames spiral inward.
// Declare=Depth=N nests N views before static (128 proves deep recursion); Declare=Step=K rebuilds it from loop_(K-1).png.
#version 3.8;
#ifndef (Depth) #declare Depth = 2; #end
#ifndef (Step) #declare Step = 0; #end
#ifndef (Rad) #declare Rad = 0; #end              // 1: radiosity on
global_settings
{
    assumed_gamma 1.0 max_trace_level 5
    #if (Rad) radiosity { pretrace_start 0.08 pretrace_end 0.02 count 150 error_bound 0.6 recursion_limit 2 } #end
}

#include "tv.inc"

#declare Aspect = image_width / image_height;

#declare View = camera
{
    location <0.3, 1.5, -6>
    look_at <0, 0.8, 0>
    right x * Aspect
    sky <-0.15, 1, 0>
    angle 42
}
camera { View }

light_source { <-6, 10, -8> rgb 1.2 }
light_source { <6, 8, -4> rgb 0.5 }

sky_sphere { pigment { gradient y color_map { [0 rgb <0.8, 0.85, 1>] [1 rgb <0.2, 0.35, 0.8>] } } }
plane { y, 0 pigment { checker rgb 0.9, rgb 0.25 } }

sphere { <-1.7, 0.5, -3.5>, 0.5 pigment { rgb <0.9, 0.15, 0.1> } finish { phong 0.6 } }
sphere { < 1.7, 0.4, -3.0>, 0.4 pigment { rgb <0.1, 0.7, 0.2> } finish { phong 0.6 } }

#declare Picture = pigment
{
    #if (Step = 0)
        screen
        {
            camera { View }
            max_trace_level Depth
            fallback { cells scale <1 / 96, 1 / 72, 1> color_map { [0 rgb 0.05] [1 rgb 0.9] } }
        }
    #else
        #if (Step = 1)
            rgb 0
        #else
            image_map { png concat("loop_", str(Step - 1, 0, 0), ".png") interpolate 2 once }
        #end
    #end
}
TV(1.5 * Aspect, 1.5, Picture)
