// skein_csg_render.pov: CSG construction Case (1..14, skein_csg.inc) from skeins or, with Prim=1, primitives; Look=1 see-through glass, Look=2 a uv checker on the skeins; Case=14 is an open skein in an intersection.
#version 3.8;
#ifndef (Case) #declare Case = 1; #end
#ifndef (Prim) #declare Prim = 0; #end
#ifndef (Look) #declare Look = 0; #end
global_settings { assumed_gamma 1 max_trace_level 12 }
#include "skein_csg.inc"
#declare Glass = material { texture { pigment { rgbf <0.95, 0.85, 0.7, 0.85> } finish { phong 0.6 reflection 0.04 } } interior { ior 1.3 } }
#declare Checker = texture { uv_mapping pigment { checker rgb <0.9, 0.7, 0.4>, rgb <0.25, 0.15, 0.1> scale <1/16, 1/8, 1> } finish { phong 0.5 } }
#if (Look = 2) #declare CsgCutter = texture { pigment { rgb <0.3, 0.55, 0.85> } finish { phong 0.3 } } #end
background { rgb <0.93, 0.92, 0.88> }
light_source { <6, 8, -10> rgb 1 }
light_source { <-8, 3, -4> rgb 0.4 }
#if (Case < 10)
  camera { location <1, 3.5, -7.5> look_at <0, 0.4, 0> angle 45 }
#else
  camera { location <0.4, 1.4, -2.2> look_at <0, 0.45, 0.1> angle 45 }
#end
#if (Case = 14)
  #declare Sheet = skein { expressions { translate <-0.5, 0, 0>  scale <2, 2, 1>  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius 0.3  angle 270 } } }
  #declare Csg14 = intersection { object { Sheet }  box { <-0.6, 0.3, -0.5>, <0.6, 1.6, 0.8> } }
#end
object {
  #if (Case = 14) Csg14 #else Csg(Case, Prim) #end
  #switch (Look)
    #case (1) material { Glass } #break
    #case (2) texture { Checker } #break
    #else pigment { rgb <0.9, 0.7, 0.4> } finish { phong 0.6 }
  #end
}
