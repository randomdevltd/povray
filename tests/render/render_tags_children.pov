#version 4.0;
// Tags inside groups and CSG: Mode 0 has every object, Mode 1 filters in the camera, Mode 2 is the reference without
// the "a" and "lamp" objects. An excluded cutter leaves its difference uncut; an excluded operand empties an intersection.
#ifndef (Mode) #declare Mode = 0; #end
#ifndef (Split) #declare Split = 1; #end
#declare Reference = (Mode = 2);
global_settings { assumed_gamma 1 }
background { rgb 0 }
camera {
  orthographic location <0, 0, -5> look_at 0 right 10*x up 5*y
  #if (Mode = 1) filter_tags { !"a" & !"lamp" } #end
}
light_source { <0, 2, -8> rgb 0.4 }
#declare Lit = texture { pigment { rgb 1 } finish { diffuse 1 ambient 0 } }
light_group {
  #if (!Reference) sphere { <-4.2, 0, 0>, 0.5 texture { Lit } tags { "a" } } #end
  sphere { <-3.1, 0, 0>, 0.5 texture { Lit } }
  #if (!Reference) light_source { <-3.5, 3, -3> rgb <1, 0.5, 0.2> tags { "lamp" } } #end
  light_source { <-3.5, -3, -3> rgb <0.2, 0.4, 1> }
  global_lights on
}
union {
  #if (!Reference) sphere { <-1.9, 0, 0>, 0.5 tags { "a" } } #end
  box { <-1.3, -0.5, -0.5>, <-0.5, 0.5, 0.5> }
  pigment { rgb <0.3, 0.6, 1> } finish { emission 1 diffuse 0 }
  split_union Split
}
merge {
  #if (!Reference) sphere { <0.2, 0, 0>, 0.55 tags { "a" } } #end
  sphere { <0.7, 0, 0>, 0.55 }
  pigment { rgb <1, 0.4, 0.4> transmit 0.5 } finish { emission 1 diffuse 0 }
}
difference {
  box { <1.6, -0.6, -0.6>, <2.8, 0.6, 0.6> }
  #if (!Reference) cylinder { <2.2, 0, -1>, <2.2, 0, 1>, 0.35 tags { "a" } } #end
  pigment { rgb <0.4, 1, 0.4> } finish { emission 1 diffuse 0 }
}
#if (!Reference)
  intersection {
    box { <3.2, -0.6, -0.6>, <4.4, 0.6, 0.6> }
    sphere { <3.8, 0, 0>, 0.75 tags { "a" } }
    pigment { rgb <1, 1, 0.3> } finish { emission 1 diffuse 0 }
  }
#end
