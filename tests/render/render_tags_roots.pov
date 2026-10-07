#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Split) #declare Split = 0; #end
global_settings { assumed_gamma 1 }
camera {
  orthographic location <0, 0, -5> look_at 0 right 6*x up 2*y
  #if (!Reference) filter_tags { "keep" } #end
}
union {
  sphere { <-2.4, 0, 0>, 0.45 tags { "discard" } }
  sphere { <-1.4, 0, 0>, 0.45 }
  pigment { rgb <1, 0, 0> } finish { emission 1 diffuse 0 }
  split_union Split
  tags { "keep" }
}
difference {
  sphere { 0, 0.7 tags { "discard" } }
  box { <-0.25, -1, -1>, <0.25, 1, 1> tags { "discard" } }
  pigment { rgb <0, 1, 0> } finish { emission 1 diffuse 0 }
  tags { "keep" }
}
light_group {
  light_source { <0, 5, -4> rgb <0, 0, 1> }
  sphere { <2, 0, 0>, 0.7 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
  tags { "keep" }
}
#if (!Reference)
  union {
    sphere { <0, 0, -2>, 1.5 tags { "keep" } }
    pigment { rgb 1 } finish { emission 1 diffuse 0 }
    tags { "discard" }
  }
#end
