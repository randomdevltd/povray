// Case 0: a slab's top; 1: a plane of the same material, which should match it; 2: a clipped shell over a floor.
#version 3.7;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Diffuse) #declare Diffuse = 200; #end
global_settings { assumed_gamma 1.0 mm_per_unit 40 subsurface { samples Diffuse, 12 } }
light_source { <-3, 6, -3>, rgb 1.2 }
#declare T = texture { pigment { rgb <0.9, 0.6, 0.3> } finish { diffuse 0.7 subsurface { translucency <3, 2, 1> } } }
#if (Case < 2)
  camera { location <0, 4, -0.01> look_at 0 angle 30 right x * 4 / 3 }
  #if (Case = 0) box { <-2, -1, -2>, <2, 0, 2> texture { T } interior { ior 1.4 } }
  #else plane { y, 0 texture { T } interior { ior 1.4 } }
  #end
#else
  camera { location <0, 1.5, -4> look_at <0, 0.6, 0> angle 40 right x * 4 / 3 }
  plane { y, 0 pigment { rgb 0.5 } }
  sphere { <0, 0.6, 0>, 0.6 clipped_by { box { <-1, 0, -1>, <1, 0.9, 1> } } texture { T } interior { ior 1.4 } }
#end
