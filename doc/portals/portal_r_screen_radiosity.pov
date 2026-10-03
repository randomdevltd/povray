// Three sets show three identical corners behind the camera, lit by one lamp each and by radiosity that only the screens'
// cameras see: left at the default pretrace, middle with radiosity_size <8, 6>, right with no_radiosity.
#version 3.8;
global_settings
{
    assumed_gamma 1.0 max_trace_level 5
    radiosity { pretrace_start 0.08 pretrace_end 0.01 count 100 error_bound 0.5 recursion_limit 2 }
}
#include "portals.inc"

camera { location <0, 0.8, -4.6> look_at <0, 0.78, 0> angle 56 }
light_source { <-6, 10, -8> rgb 0.9 }
Sky()
Floor()
box { <-2.6, 0, -0.1>, <2.6, 0.15, 0.8> pigment { rgb <0.4, 0.28, 0.2> } no_radiosity }

#macro Corner(X)
    light_group
    {
        light_source { <2.4, 2.6, -11> rgb 1.2 }
        box { <-3.1, 0, -16.1>, <3.1, 0.01, -9> pigment { rgb 0.8 } }
        box { <-3, 0, -16.1>, <3, 3, -16> pigment { rgb 0.9 } }
        box { <-3.1, 0, -16>, <-3, 3, -9> pigment { rgb <0.9, 0.12, 0.08> } }
        box { <3, 0, -16>, <3.1, 3, -9> pigment { rgb <0.1, 0.6, 0.2> } }
        box { <-1.6, 0, -15.2>, <-0.4, 1.2, -14> pigment { rgb 0.9 } }
        sphere { <1.2, 0.7, -14.2>, 0.7 pigment { rgb <0.9, 0.85, 0.3> } }
        translate X * x
        global_lights off
    }
#end
Corner(-40)
Corner(0)
Corner(40)
#declare Sets = array[3]
{
    pigment { screen { camera { location <-40, 1.3, -9> look_at <-40, 1.0, -16> right x * 4 / 3 angle 55 } } },
    pigment { screen { camera { location <0, 1.3, -9> look_at <0, 1.0, -16> right x * 4 / 3 angle 55 radiosity_size <8, 6> } } },
    pigment { screen { camera { location <40, 1.3, -9> look_at <40, 1.0, -16> right x * 4 / 3 angle 55 no_radiosity } } }
};
#for (I, 0, 2)
    object { Panel(1.4, 1.05, Sets[I], 0.12) no_radiosity translate <-1.6 + 1.6 * I, 0.25, 0> }
#end
