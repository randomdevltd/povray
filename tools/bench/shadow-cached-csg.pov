// A merge with an opaque texture and one see-through part, seen from under both: that part's shadow must match `Declare=WithOpaque=0`.
#version 3.7;
#ifndef (WithOpaque) #declare WithOpaque = 1; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 2, 0> look_at <0, 0, 0> sky z right x * 6 up z * 3 }
light_source { <0, 100, 0> rgb 1 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
merge {
  #if (WithOpaque) box { <-2.5, 5, -1>, <-0.5, 5.2, 1> } #end
  box { <0.5, 5, -1>, <2.5, 5.2, 1> texture { pigment { rgbf <1, 1, 1, 0.5> } } }
  pigment { rgb 1 }
}
