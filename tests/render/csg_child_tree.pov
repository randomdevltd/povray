#version 3.7;
// Large CSGs whose children a box tree indexes: every image must match the linear scan's (POV_CSG_CHILD_TREE=0).
// Rows: union kept whole by a light, intersection-wrapped union, nested unions, merge, difference with many cutters,
// intersection of many lenses, plane clipped_by a union, children each clipped_by, union bounded_by a union.
// Children the tree must keep asking sit among them: a mesh, an open mesh, a bounded_by child, inverted and clipped cutters.
#ifndef (N) #declare N = 6; #end
global_settings { assumed_gamma 1 max_trace_level 8 }
camera { location <13, 20, -34> look_at <13, 0, 4> angle 46 }
light_source { <-30, 60, -40> rgb 1 }
background { rgb 0.15 }
#declare R = seed(7);
#declare Cube = mesh {
  triangle { <-1,-1,-1>, <1,-1,-1>, <1,1,-1> } triangle { <-1,-1,-1>, <1,1,-1>, <-1,1,-1> }
  triangle { <-1,-1,1>, <1,1,1>, <1,-1,1> } triangle { <-1,-1,1>, <-1,1,1>, <1,1,1> }
  triangle { <-1,-1,-1>, <-1,1,1>, <-1,-1,1> } triangle { <-1,-1,-1>, <-1,1,-1>, <-1,1,1> }
  triangle { <1,-1,-1>, <1,-1,1>, <1,1,1> } triangle { <1,-1,-1>, <1,1,1>, <1,1,-1> }
  triangle { <-1,-1,-1>, <-1,-1,1>, <1,-1,1> } triangle { <-1,-1,-1>, <1,-1,1>, <1,-1,-1> }
  triangle { <-1,1,-1>, <1,1,1>, <-1,1,1> } triangle { <-1,1,-1>, <1,1,-1>, <1,1,1> }
  inside_vector <0.3, 0.7, 0.2>
}
// torus as a quartic: unbounded, so it keeps its bounded_by box
#declare Ring = quartic {
  < 1, 0, 0, 0, 2, 0, 0, 2, 0, -104, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 56, 0, 0, 0, 0, 1, 0, -104, 0, 784 >
  sturm bounded_by { box { <-10, -3.6, -10>, <10, 3.6, 10> } } scale 0.08
}
#declare Hollow = mesh {
  triangle { <-1,-1,-1>, <1,-1,-1>, <1,1,-1> } triangle { <-1,-1,-1>, <1,1,-1>, <-1,1,-1> }
  triangle { <-1,-1,1>, <1,1,1>, <1,-1,1> } triangle { <-1,-1,1>, <-1,1,1>, <1,1,1> }
  triangle { <-1,-1,-1>, <-1,1,1>, <-1,-1,1> } triangle { <-1,-1,-1>, <-1,1,-1>, <-1,1,1> }
  triangle { <1,-1,-1>, <1,-1,1>, <1,1,1> } triangle { <1,-1,-1>, <1,1,1>, <1,1,-1> }
  triangle { <-1,-1,-1>, <-1,-1,1>, <1,-1,1> } triangle { <-1,-1,-1>, <1,-1,1>, <1,-1,-1> }
  triangle { <-1,1,-1>, <1,1,1>, <-1,1,1> } triangle { <-1,1,-1>, <1,1,-1>, <1,1,1> }
}
#declare OpenSheet = mesh { triangle { <-1,0,-1>, <1,0,-1>, <0,0,1> } }
// N*N*N small shapes on a grid in a 6-unit cube, every third a box
#macro Grid(Size)
  #for (I, 0, N - 1) #for (J, 0, N - 1) #for (K, 0, N - 1)
    #local P = <I, J, K> * 6 / N + <rand(R), rand(R), rand(R)> * 0.3;
    #if (mod(I + J + K, 3) = 0) box { P - Size, P + Size } #else sphere { P, Size * 1.2 } #end
  #end #end #end
#end
#declare Paint = texture { pigment { rgb <0.85, 0.7, 0.5> } finish { specular 0.3 } }
// row 1: union kept whole by its light, intersection-wrapped union, nested unions
union {
  Grid(0.22)
  object { Cube scale 0.3 translate <3, 3, -0.6> }
  object { OpenSheet scale 0.5 translate <1, 6.4, 1> }
  object { Ring translate <5, 6.3, 5> }
  light_source { <3, 7, 3> rgb 0.3 }
  texture { Paint }
  translate <0, 0, 8>
}
intersection {
  union { Grid(0.25) }
  box { <-1, -1, -1>, <7, 4.1, 7> }
  texture { pigment { rgb <0.5, 0.75, 0.9> } }
  translate <9, 0, 8>
}
union {
  #for (I, 0, 3) #for (J, 0, 3)
    union {
      #for (K, 0, 7) sphere { <I * 1.6 + rand(R), K * 0.8, J * 1.6 + rand(R)>, 0.35 } #end
      pigment { rgb <0.4 + I * 0.15, 0.6, 0.9 - J * 0.15> }
    }
  #end #end
  translate <18, 0, 8>
}
// row 2: merge of overlapping glass spheres, difference with many cutters, intersection of many lenses
merge {
  #for (I, 0, N * N * 3) sphere { <rand(R), rand(R), rand(R)> * 5, 0.5 + rand(R) * 0.4 } #end
  texture { pigment { rgbf <0.8, 0.95, 0.85, 0.7> } finish { reflection 0.05 } }
  interior { ior 1.3 }
  translate <0, 0, -2>
}
difference {
  box { <0, 0, 0>, <6, 4, 6> }
  #for (I, 0, N * N * 3) sphere { <rand(R) * 6, 4 + (rand(R) - 0.5) * 1.2, rand(R) * 6>, 0.25 + rand(R) * 0.3 } #end
  #for (I, 0, N * 2) cylinder { <rand(R) * 6, -1, -1>, <rand(R) * 6, 5, 7>, 0.15 } #end
  sphere { <0, 4, 0>, 1.5 clipped_by { box { <-2, 3, -2>, <0.5, 5, 0.5> } } }
  object { Cube scale 0.6 translate <6, 2, 3> }
  texture { pigment { rgb <0.9, 0.55, 0.4> } }
  translate <9, 0, -2>
}
intersection {
  #for (I, 0, N * 4)
    #local D = vrotate(x, <rand(R) * 360, rand(R) * 360, 0>);
    sphere { D * 4.6, 7 }
  #end
  texture { pigment { rgb <0.7, 0.85, 0.5> } }
  translate <21, 2.5, 1>
}
// row 3: plane clipped_by a union, children each clipped_by, union bounded_by a union
plane { <0.2, 1, -0.3>, 2
  clipped_by { union { Grid(0.3) } }
  texture { pigment { rgb <0.95, 0.9, 0.3> } }
  translate <0, 0, -12>
}
union {
  #for (I, 0, N * N * 2)
    #local P = <rand(R) * 6, rand(R) * 4, rand(R) * 6>;
    sphere { P, 0.5 clipped_by { box { P - <0.6, 0.6, 0.6>, P + <0.6, 0.15, 0.6> } } }
  #end
  texture { pigment { rgb <0.6, 0.5, 0.9> } }
  translate <9, 0, -12>
}
union {
  #for (I, 0, N * N * 2) sphere { <rand(R) * 6, rand(R) * 4, rand(R) * 6>, 0.3 } #end
  bounded_by { union { #for (I, 0, 11) box { <I * 0.5, 0, 0>, <I * 0.5 + 0.55, 4.5, 6.5> } #end } }
  texture { pigment { rgb <0.9, 0.4, 0.7> } }
  translate <18, 0, -12>
}
// row 0, behind: an inverted CSG holding a mesh without inside_vector, whose inside ignores `inverse`: it contains no point,
// so the plane clipped_by a union holding it shows only the union's spheres
#declare Shell = union {
  #for (I, 0, 9) sphere { <mod(I, 5), div(I, 5), 0> * 1.2, 0.5 } #end
  object { Hollow scale 0.4 translate <2.4, 0.6, -1> }
  inverse
}
object { Shell texture { pigment { rgb <0.3, 0.9, 0.6> } } translate <8, 8, 18> }
plane { z, 0
  clipped_by { union { object { Shell } #for (I, 0, 9) sphere { <I * 2.4, 3 + mod(I, 3), 0>, 1 } #end } }
  texture { pigment { rgb <0.9, 0.3, 0.3> } }
  translate <2, 0, 20>
}
plane { y, -0.01 pigment { rgb 0.5 } }
