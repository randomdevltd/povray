// skein_torus.pov: one torus (major radius 2, minor 0.5, axis x) three ways for a cost comparison: Object=1 skein, 2 torus, 3 isosurface.
#version 3.8;
#ifndef (Object) #declare Object = 1; #end
#ifndef (MaxGradient) #declare MaxGradient = 4.2; #end
#ifndef (Accuracy) #declare Accuracy = 1e-4; #end
global_settings { assumed_gamma 1 }
#include "skein_shapes.inc"

// why: max |grad| in the box is 2*sqrt(2^2 + 0.5^2) = 4.12, on the axis; doc/skein.md#torus-cost-comparison
#declare IsoTorus = isosurface {
  function { pow(sqrt(y*y + z*z) - 2, 2) + x*x - 0.25 }
  contained_by { box { <-0.5, -2.5, -2.5>, <0.5, 2.5, 2.5> } }
  max_gradient MaxGradient
  accuracy Accuracy
}

background { rgb <0.2, 0.3, 0.4> }
light_source { <6, 8, -10> rgb 1 }
light_source { <-8, 3, -4> rgb 0.4 }
camera { location <-7, -4, 0> look_at 0 angle 45 }

object {
  #switch (Object)
    #case (1) SkeinTorus #break
    #case (2) PrimTorus #break
    #case (3) IsoTorus #break
  #end
  pigment { rgb <0.9, 0.7, 0.4> }
  finish { phong 0.6 }
}
