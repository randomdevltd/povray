// bounce_no_surface.pov: a bounce light on an object with no surface to find. Case=0: a difference that removes all of
// it; Case=1: a sphere clipped away entirely. Each renders with a warning naming the object, and no bounce.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
camera { location <0, 2, -6> look_at 0 }
light_source { <3, 5, -4>, rgb 1 }
plane { y, -1 pigment { rgb 0.8 } }
#declare M = material { texture { pigment { rgb 0.8 } finish { diffuse 0.9 } } }
#if (Case = 0)
  difference { sphere { 0, 1 } sphere { 0, 2 } material { M } light { bounce domain_cells 64 } }
#else
  sphere { 0, 1 clipped_by { plane { y, -2 } } material { M } light { bounce domain_cells 64 } }
#end
