// Thread identity: shadows from blockers a thread's shadow cache may hold must not depend on what that thread traced before.
// Left: a union bounded_by a union of boxes that also holds an infinite plane, so the union stays whole and its parts
// are reached only through the bound. Middle: an unsplit union holding a bounded union, a bounded mesh and nested bounds.
// Right: opaque, see-through, merged, clipped and mesh blockers under an area light. Floors far from every blocker must stay lit.
#version 3.7;
global_settings { assumed_gamma 1 max_trace_level 6 }
camera { location <9, 15, -14> look_at <9, 0, 4> angle 62 }
light_source { <-30, 60, -40> rgb 0.8 }
light_source { <40, 50, -30> rgb 0.6 area_light <6, 0, 0>, <0, 0, 6>, 4, 4 }
background { rgb 0.15 }
plane { y, 0 pigment { rgb 0.6 } }
#declare R = seed(3);
#declare Tet = mesh {
  triangle { <0, 0, 0>, <1, 0, 0>, <0.5, 0, 0.9> } triangle { <0, 0, 0>, <0.5, 0, 0.9>, <0.5, 0.8, 0.3> }
  triangle { <0, 0, 0>, <0.5, 0.8, 0.3>, <1, 0, 0> } triangle { <1, 0, 0>, <0.5, 0.8, 0.3>, <0.5, 0, 0.9> }
  inside_vector <0.2, 0.7, 0.1>
}
// a plane keeps the union's box open, so the union stays whole with its bounding union
union {
  #for (I, 0, 39) sphere { <rand(R) * 6, rand(R) * 4, rand(R) * 6>, 0.35 } #end
  plane { y, 4.2 }
  bounded_by { union { #for (I, 0, 5) box { <I, 0, 0>, <I + 0.6, 4.5, 6> } #end } }
  pigment { rgb <0.9, 0.4, 0.7> }
  translate <-3, 0, 2>
}
// kept whole by split_union off: its parts sit inside a bounded union, a bounded mesh and nested bounded unions
union {
  union {
    #for (I, 0, 19) sphere { <rand(R) * 4, rand(R) * 3, rand(R) * 4>, 0.4 } #end
    plane { y, 3.4 }
    object { Tet scale 2 translate <1, 0, 2.5> }
    bounded_by { box { <0, 0, 0>, <4, 3.6, 2> } }
  }
  object { Tet scale 3 translate <0.5, 0, 4.5> bounded_by { sphere { <2, 1, 6>, 1.2 } } }
  box { <5, 0, 1>, <5.5, 2, 5> }
  // nested bounds: the outer one cuts off the spheres past x = 2, so nothing may see them or be shadowed by them
  union {
    union {
      #for (I, 0, 7) sphere { <I * 0.5, 1.5, -2>, 0.3 } #end
      bounded_by { box { <-0.5, 1, -2.5>, <4, 2, -1.5> } }
    }
    sphere { <0.5, 1.2, -1.2>, 0.2 }
    bounded_by { box { <-0.5, 0.9, -2.6>, <2, 2.1, -1.4> } }
  }
  split_union off
  pigment { rgb <0.4, 0.8, 0.5> }
  translate <6, 0, 2>
}
// mixed blockers under the area light
union {
  sphere { <1, 1.5, 1>, 0.8 pigment { rgb <0.9, 0.7, 0.3> } }
  box { <2.5, 0.2, 0>, <3.5, 2.5, 2> pigment { rgbf <0.6, 0.8, 1, 0.7> } }
  merge {
    box { <0, 2.5, 3>, <1.5, 2.8, 4.5> }
    box { <1.8, 2.5, 3>, <3.3, 2.8, 4.5> pigment { rgbt <1, 1, 1, 0.6> } }
    pigment { rgb 0.8 }
  }
  difference { box { <4, 0, 3>, <5.5, 2, 4.5> } cylinder { <4.75, -1, 3.75>, <4.75, 3, 3.75>, 0.5 } pigment { rgb <0.5, 0.6, 0.9> } }
  sphere { <4.5, 1.4, 0.8>, 0.9 clipped_by { plane { -y, -1.2 } } pigment { rgb <0.8, 0.5, 0.4> } }
  object { Tet scale 2 translate <0.5, 0, 5> pigment { rgb <0.7, 0.7, 0.9> } }
  translate <15, 0, 2>
}
// crowns cut from a tube, as in advanced/chess2.pov: the inverted inner cylinder narrows their box inside the outer wall
#declare Tube = quadric { <1, 0, 1>, <0, 0, 0>, <0, 0, 0>, -1 }
#for (I, 0, 2)
  intersection {
    union { plane { x, -0.25 } plane { -x, -0.25 } plane { y, 1.6 } }
    union { plane { z, -0.25 } plane { -z, -0.25 } plane { y, 1.6 } }
    plane { y, 2 }
    object { Tube }
    object { Tube scale <0.6, 1, 0.6> inverse }
    plane { -y, -1 }
    pigment { rgb <0.9, 0.8, 0.6> }
    translate <14 + I * 2.5, 0, -1>
  }
#end
