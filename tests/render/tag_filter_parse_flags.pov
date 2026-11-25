#version 4.0;
// Scene-wide state drawn from objects comes from those Filter_Tags keeps: the opaque sphere far off is filtered out, so the
// half-transparent boxes with coincident faces render as in a scene that never had it. Reference=1 leaves the sphere out.
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1 }
background { rgb <0.2, 0.3, 0.4> }
camera { orthographic location <0, 0, -10> look_at <0, 0, 0> right 4*x up 2*y }
light_source { <-5, 8, -10> rgb 1 }
box { <-1, -1, 0>, <0.5, 1, 1> pigment { rgbt <1, 0.4, 0.2, 0.5> } }
box { <-0.5, -1, 0>, <1, 1, 1> pigment { rgbt <0.2, 0.4, 1, 0.5> } }
#if (!Reference)
  sphere { <50, 50, 50>, 1 pigment { rgb 1 } tags { "hidden" } }
#end
