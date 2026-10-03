// Two monitors showing the render's own view nest two deep; at the limit the left shows black, the right its fallback.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

#declare View = camera { location <0, 0.95, -4.4> look_at <0, 0.8, 0> right x * 4 / 3 angle 50 };
camera { View }
light_source { <-4, 6, -6> rgb 1 }
Sky()
Floor()
box { <-6, 0, 1.2>, <6, 4, 1.3> pigment { rgb <0.85, 0.82, 0.75> } }
sphere { <0, 0.25, -0.5>, 0.25 pigment { rgb <0.9, 0.15, 0.1> } finish { phong 0.5 } }

#declare Static = pigment { cells scale <1 / 96, 1 / 72, 1> color_map { [0 rgb 0.05] [1 rgb 0.9] } };
object { Panel(1.6, 1.2, pigment { screen { camera { View } max_trace_level 2 } }, <0.25, 0.35, 0.6>) translate <-0.95, 0.2, 0> }
object { Panel(1.6, 1.2, pigment { screen { camera { View } max_trace_level 2 fallback { Static } } }, <0.6, 0.3, 0.2>) translate <0.95, 0.2, 0> }
