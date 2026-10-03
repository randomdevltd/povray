// Doorways leading three units in front of themselves nest to max_trace_level 3: past it, left absent, right its fallback.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.2, -6.5> look_at <0, 1.05, 0> angle 45 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 0.1>, <6, 4, 0.3> pigment { rgb <0.85, 0.82, 0.75> } }
box { <-2.0, 0.2, 0.05>, <-0.4, 1.9, 0.1> pigment { rgb <0.3, 0.55, 0.8> } }
box { <0.4, 0.2, 0.05>, <2.0, 1.9, 0.1> pigment { rgb <0.3, 0.55, 0.8> } }

object { Frame(1.2, 1.8, <0.8, 0.3, 0.15>) translate -1.2 * x }
portal { object { Door(1.2, 1.8) translate -1.2 * x } to { translate -3 * z } far off max_trace_level 3 }
object { Frame(1.2, 1.8, <0.8, 0.3, 0.15>) translate 1.2 * x }
portal { object { Door(1.2, 1.8) translate 1.2 * x } to { translate -3 * z } far off max_trace_level 3 fallback { rgb <1, 0.85, 0.2> } }
