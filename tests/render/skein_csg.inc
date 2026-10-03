// skein_csg.inc: Csg(N, Prim) is CSG construction N (1..13) built from skeins, or with Prim=1 (cases 1..9) from the primitives they equal; cutters wear CsgCutter if declared.
#include "skein_shapes.inc"

#declare CurlSteps = expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1>  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius 0.12  angle 300 } }
#declare ThickCurl = skein { expressions { CurlSteps  envelope { thickness function(uv) { 0.08*(1 - pow(2*uv.v - 1, 8)) }  edge round } } closed u  ends sealed }
#declare OverlapCurl = skein { expressions { CurlSteps  envelope { thickness function(uv) { 0.3*(1 - pow(2*uv.v - 1, 8)) }  edge round } } closed u  ends sealed }
#declare CsgNames = array[14] {
  "", "union of two tori", "merge of two tori", "frustum and sphere intersected", "torus minus a tube", "box minus a tube",
  "box with an inverse torus", "torus clipped by a plane", "torus bounded by a box", "three levels nested",
  "thick curl minus a sphere", "thick curl minus a tube", "thick curl and a box intersected", "self-overlapping curl merged with a sphere"
}

#macro Csg(N, Prim)
  #local Torus = object { #if (Prim) PrimTorus #else SkeinTorus #end }
  #local Tube = object { #if (Prim) PrimTube #else SkeinTube #end }
  #local Frustum = object { #if (Prim) PrimFrustum #else SkeinFrustum #end }
  #switch (N)
    #case (1) union { object { Torus }  object { Torus rotate x*90 translate <1.6, 0, 0> } } #break
    #case (2) merge { object { Torus }  object { Torus rotate x*90 translate <1.6, 0, 0> } } #break
    #case (3) intersection { object { Frustum }  sphere { <0, 1, 0>, 0.8 } } #break
    #case (4) difference { object { Torus }  object { Tube scale <0.6, 2.2, 0.6> translate <0, -2.2, 0> rotate x*90 translate <2, 0, 0> #ifdef (CsgCutter) texture { CsgCutter } #end } } #break
    #case (5) difference { box { <-0.9, 0.2, -0.9>, <0.9, 1.8, 0.9> }  object { Tube scale <1.2, 1.4, 1.2> translate <0, -0.4, 0> #ifdef (CsgCutter) texture { CsgCutter } #end } } #break
    #case (6) intersection { box { <-3, -3, -0.8>, <3, 3, 0.8> }  object { Torus inverse } } #break
    #case (7) object { Torus clipped_by { plane { x, 1 } } } #break
    #case (8) object { Torus bounded_by { box { <-2.6, -2.6, -0.6>, <2.6, 2.6, 0.6> } } } #break
    #case (9)
      difference {
        merge { object { Torus }  object { Tube scale <0.5, 2.5, 0.5> rotate z*90 translate <2.5, 0, 0> } }
        intersection { object { Frustum scale 1.5 rotate x*90 translate <-2, 0, -1.5> }  sphere { <-2, 0, 0>, 1 } #ifdef (CsgCutter) texture { CsgCutter } #end }
      }
    #break
    #case (10) difference { object { ThickCurl }  sphere { <0.35, 0.5, 0.1>, 0.3 #ifdef (CsgCutter) texture { CsgCutter } #end } } #break
    #case (11) difference { object { ThickCurl }  object { SkeinTube scale <0.3, 0.8, 0.3> rotate z*90 translate <0.8, 0.5, 0.15> #ifdef (CsgCutter) texture { CsgCutter } #end } } #break
    #case (12) intersection { object { ThickCurl }  box { <-0.85, 0.2, -0.4>, <0.6, 0.8, 0.6> } } #break
    #case (13) merge { object { OverlapCurl }  sphere { <-0.3, 0.5, 0>, 0.25 } } #break
  #end
#end
