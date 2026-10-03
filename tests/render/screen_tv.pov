// Television showing a second view of the same scene through a `screen` pigment on a convex glass screen.
#version 3.8;
#ifndef (Crt) #declare Crt = 1; #end
#ifndef (Bulge) #declare Bulge = 1.2; #end
#ifndef (Rad) #declare Rad = 0; #end              // 1: radiosity on
global_settings
{
    assumed_gamma 1.0 max_trace_level 5
    #if (Rad) radiosity { pretrace_start 0.08 pretrace_end 0.02 count 150 error_bound 0.6 recursion_limit 2 } #end
}

#include "colors.inc"
#include "tv.inc"

camera { location <0, 1.6, -7> look_at <0, 1.0, 0> angle 45 }

light_source { <-6, 10, -8> rgb 1.2 }
light_source { <6, 8, -20> rgb 0.6 }

sky_sphere { pigment { gradient y color_map { [0 rgb <0.8, 0.85, 1>] [1 rgb <0.2, 0.35, 0.8>] } } }

plane { y, 0 pigment { checker rgb 0.9, rgb 0.25 } }

// Behind the main camera, out of its sight, but in view of the screen's camera.
sphere { <-1.5, 1, -14>, 1 pigment { rgb <0.9, 0.15, 0.1> } finish { phong 0.6 } }
sphere { < 1.5, 1, -14>, 1 pigment { rgb <0.1, 0.7, 0.2> } finish { phong 0.6 } }
box { <-0.7, 0, -12>, <0.7, 1.8, -11.5> pigment { rgb <0.9, 0.8, 0.1> } }

// The set.
#declare Picture = pigment
{
    screen
    {
        camera
        {
            location <0, 1, -9>
            look_at <0, 1, -14>
            right x * 4 / 3
            up y
            angle 50
        }
        #if (Crt)
            // Barrel distortion: sample further out toward the edges, like a curved tube.
            perturb
            {
                user_defined
                {
                    function { Bulge * (x - 0.5) * (pow(x - 0.5, 2) + pow(y - 0.5, 2)) },
                    function { Bulge * (y - 0.5) * (pow(x - 0.5, 2) + pow(y - 0.5, 2)) },
                    function { 0 }
                }
            }
        #end
    }
}
TV(2, 1.5, Picture)
