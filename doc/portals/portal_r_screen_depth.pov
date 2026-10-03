// Three monitors, each showing a camera aimed at itself, with max_trace_level 1, 2 and 4 from left to right.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.0, -4.6> look_at <0, 0.95, 0> angle 52 }
light_source { <-4, 6, -6> rgb 1 }
Sky()
Floor()
box { <-6, 0, 0.3>, <6, 4, 0.4> pigment { rgb <0.85, 0.82, 0.75> } }

#declare Levels = array[3] { 1, 2, 4 };
#declare Bezels = array[3] { <0.8, 0.3, 0.15>, <0.2, 0.55, 0.3>, <0.25, 0.35, 0.75> };
#for (I, 0, 2)
    #local X = -1.5 + 1.5 * I;
    #local Self = camera { location <X, 0.95, -1.3> look_at <X, 0.95, 0> right x * 4 / 3 angle 58 };
    object { Panel(1.2, 0.9, pigment { screen { camera { Self } max_trace_level Levels[I] } }, Bezels[I]) translate <X, 0.5, 0> }
    cylinder { <X, 0, 0.05>, <X, 0.42, 0.05>, 0.05 pigment { rgb 0.2 } }
#end
