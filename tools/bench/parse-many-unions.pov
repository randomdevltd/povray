// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Parse and preparation cost of many CSGs: Count small nested unions (a sphere, a box and a union of a cylinder and a
// merge), one union of Count bricks, and Count intersections; split_union off keeps the unions whole. Declares Count.
// Render with +w64 +h48 -a.
#version 3.7;
#ifndef (Count) #declare Count = 4000; #end
global_settings { assumed_gamma 1 }
camera { location <0, 60, -160> look_at <0, 0, 0> angle 60 }
light_source { <100, 200, -100> rgb 1 }
#declare R = seed(3);
#declare Side = ceil(sqrt(Count));
#for (I, 0, Count - 1)
  #declare P = <mod(I, Side) - Side / 2, 0, div(I, Side) - Side / 2> * 2;
  union {
    sphere { 0, 0.4 }
    box { <-0.3, 0.4, -0.3>, <0.3, 0.8, 0.3> }
    union {
      cylinder { <0, 0.8, 0>, <0, 1.2, 0>, 0.1 }
      merge { sphere { <0, 1.3, 0>, 0.15 } sphere { <0.1, 1.3, 0>, 0.15 } }
    }
    pigment { rgb <rand(R), 0.6, 0.4> }
    split_union off
    translate P
  }
  intersection {
    sphere { <0, -1, 0>, 1.2 }
    sphere { <0, 1, 0>, 1.2 }
    pigment { rgb 0.8 }
    translate P + <1, 0.5, 0>
  }
#end
union {
  #for (I, 0, Count - 1)
    box { <0, 0, 0>, <0.8, 0.3, 0.8> translate <mod(I, Side) - Side / 2, -2, div(I, Side) - Side / 2> * <2, 1, 2> }
  #end
  pigment { rgb 0.5 }
  split_union off
}
