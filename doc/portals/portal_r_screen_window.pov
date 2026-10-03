// Render with +UA: the screen's picture is opaque, sky and all, while outside its unit-square window the panel is clear.
#version 3.8;
global_settings { assumed_gamma 1.0 }
#include "portals.inc"

camera { location <0, 0.75, -4> look_at <0, 0.75, 0> angle 45 }
light_source { <-4, 6, -6> rgb 1 }
background { rgbt <0.3, 0.45, 0.75, 1> }
light_source { <96, 6, -6> rgb 1 }

// The panel, 2.4 by 1.5, outlined; the window is the middle 1.2 by 0.9 of it.
union
{
    box { <-1.25, -0.05, 0>, <-1.2, 1.55, 0.05> }
    box { <1.2, -0.05, 0>, <1.25, 1.55, 0.05> }
    box { <-1.2, -0.05, 0>, <1.2, 0, 0.05> }
    box { <-1.2, 1.5, 0>, <1.2, 1.55, 0.05> }
    pigment { rgb 0.25 }
}
box
{
    <-1.2, 0, 0>, <1.2, 1.5, 0.01>
    pigment { screen { camera { location <100, 1.2, -5> look_at <100, 0.8, 0> right x * 4 / 3 angle 40 } } scale <1.2, 0.9, 1> translate <-0.6, 0.3, 0> }
    finish { Glow }
}
torus { 0.5, 0.12 rotate 90 * x translate <0.9, 1.0, 1.2> pigment { rgb <0.95, 0.75, 0.1> } }

sphere { <99.3, 0.7, 0>, 0.6 pigment { rgb <0.9, 0.15, 0.1> } }
cone { <100.8, 0, 0.3>, 0.5, <100.8, 1.4, 0.3>, 0 pigment { rgb <0.1, 0.6, 0.25> } }
