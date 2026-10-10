#version 4.0;
#include "scene.inc"
#declare Lamp = light_source { <4, 6, -3> rgb 0.4 }
light_source { Lamp translate y }
union { #include "parts.inc" pigment { rgb <1, 0.6, 0.2> } translate <-2.5, 0, 0> }
#declare Grid = array[3][3] { {0, 0.2, 0.4}, {0.6, 0.8, 1}, {1.2, 1.4, 1.6} }
#include "grid.inc"
sphere { <dimensions(Grid), 0, -1>, dimension_size(Grid, 2) * 0.1 pigment { rgb 1 } }
#declare Blend = 1;
box { -1, 1 pigment { rgbt 1 }
  interior { media { mix add emission rgb <0.4, 0.1, 0> } media { mix #if (Blend) replace #else add #end emission rgb <0, 0.3, 0.6> } }
  translate <3, 0.5, 0> }
