// A homogeneous slab lit from behind, made whole or from two boxes meeting halfway along the viewing ray.
#version 3.7;
#ifndef (Split) #declare Split=1; #end
#ifndef (Single) #declare Single=32; #end
#ifndef (Round) #declare Round=0; #end
global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { method 2 samples 8,Single } }
camera { orthographic location <0,0,-4> look_at 0 right x*0.3 up y*0.3 }
background { rgb 0 }
light_source { <0,0,20> rgb 2 parallel point_at <0,0,0> }
#declare Wax=texture { pigment { rgb 0.4 } finish { ambient 0 diffuse 1 subsurface { translucency 0.7 } } }
#if (Split) union {
  #if (Round)
  cylinder { <0,0,-0.5>, <0,0,0>, 1 }
  cylinder { <0,0,0>, <0,0,0.5>, 1 }
  #else
  box { <-1,-1,-0.5>, <1,1,0> }
  box { <-1,-1,0>, <1,1,0.5> }
  #end
#elseif (Round) cylinder { <0,0,-0.5>, <0,0,0.5>, 1
#else box { <-1,-1,-0.5>, <1,1,0.5>
#end
texture { Wax } interior { ior 1.3 } subsurface { domain_cells 128 } }
