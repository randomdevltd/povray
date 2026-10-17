#version 4.0;
// Containers are never judged: each child answers to its effective tags, so its own list replaces its container's.
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Split) #declare Split = 0; #end
global_settings { assumed_gamma 1 }
camera {
  orthographic location <0, 0, -5> look_at 0 right 6*x up 2*y
  #if (!Reference) filter_tags { "keep" } #end
}
union {
  #if (!Reference) sphere { <-2.4, 0, 0>, 0.45 tags { "discard" } } #end
  sphere { <-1.4, 0, 0>, 0.45 }
  pigment { rgb <1, 0, 0> } finish { emission 1 diffuse 0 }
  split_union Split
  #if (!Reference) tags { "keep" } #end
}
#if (!Reference)
  difference {
    sphere { 0, 0.7 tags { "discard" } }
    box { <-0.25, -1, -1>, <0.25, 1, 1> tags { "discard" } }
    pigment { rgb <0, 1, 0> } finish { emission 1 diffuse 0 }
    tags { "keep" }
  }
#end
light_group {
  light_source { <0, 5, -4> rgb <0, 0, 1> }
  sphere { <2, 0, 0>, 0.7 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
  #if (!Reference) tags { "keep" } #end
}
union {
  sphere { <0, 0, -2>, 1.5 #if (!Reference) tags { "keep" } #end }
  pigment { rgb 1 } finish { emission 1 diffuse 0 }
  #if (!Reference) tags { "discard" } #end
}
