// Renders the mesh that mesh-cylinder.py writes to mesh-cylinder.inc, for mesh memory and trace cost.
#version 3.7;
#include "mesh-cylinder.inc"
global_settings { assumed_gamma 1 max_trace_level 5 }
camera { location <0, 2.2, -3.2> look_at <0, 2, 0> angle 60 }
light_source { <-5, 8, -6> rgb 1 }
light_source { <6, 3, -4> rgb 0.4 }
plane { y, 0 pigment { rgb 0.6 } }
object { M pigment { rgb <0.7, 0.5, 0.35> } finish { specular 0.3 } }
