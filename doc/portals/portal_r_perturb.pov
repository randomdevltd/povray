// The same doorway onto the sea twice; the right one bends its view with perturb { normal { ripples } }.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.3, -5.4> look_at <0, 1.0, 0> angle 45 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 0.1>, <6, 4, 0.3> pigment { brick rgb 0.75, rgb <0.65, 0.3, 0.2> scale 0.08 } }

#declare Far = <1000, 0, 0>;
Elsewhere(Far)
object { Frame(1.4, 2.0, <0.2, 0.3, 0.28>) translate -1.0 * x }
portal { object { Door(1.4, 2.0) translate -1.0 * x } to { translate Far + 1.0 * x } far off }
object { Frame(1.4, 2.0, <0.2, 0.3, 0.28>) translate 1.0 * x }
portal { object { Door(1.4, 2.0) translate 1.0 * x } to { translate Far - 1.0 * x } far off perturb { normal { ripples 0.35 scale 0.25 translate <1, 1, 0> } } }
