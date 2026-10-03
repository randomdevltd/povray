// A portal sphere and its far mouth, 3 units apart: each shows what is behind the other, so the cone and the box trade places.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 1.6, -6.2> look_at <0, 0.95, 0> angle 50 }
light_source { <-4, 9, -7> rgb 1 }
light_source { <5, 6, -8> rgb 0.3 shadowless }
Sky()
Floor()
box { <-6, 0, 3>, <6, 4, 3.1> pigment { rgb <0.85, 0.82, 0.75> } }

cone { <-1.5, 0, 2>, 0.6, <-1.5, 1.8, 2>, 0 pigment { rgb <0.2, 0.35, 0.95> } }
box { <-0.5, 0, -0.5>, <0.5, 1, 0.5> rotate 30 * y translate <1.5, 0, 2> pigment { rgb <0.9, 0.2, 0.15> } }
cylinder { <-1.5, 0, 0>, <-1.5, 0.3, 0>, 0.4 pigment { rgb 0.3 } }
cylinder { <1.5, 0, 0>, <1.5, 0.3, 0>, 0.4 pigment { rgb 0.3 } }
torus { 0.78, 0.025 rotate 90 * x translate <-1.5, 1.1, 0> pigment { rgb 0.3 } }
torus { 0.78, 0.025 rotate 90 * x translate <1.5, 1.1, 0> pigment { rgb 0.3 } }
portal { sphere { <-1.5, 1.1, 0>, 0.75 } to { translate 3 * x } no_lights }
