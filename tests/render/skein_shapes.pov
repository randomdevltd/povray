// skein_shapes.pov: one skein shape (Shape=1..29) or, with Prim=1, the primitive it equals; Checker=1 shows uv, Cut=1 a CSG cutaway.
#version 3.8;
#ifndef (Shape) #declare Shape = 1; #end
#ifndef (Prim) #declare Prim = 0; #end
#ifndef (Checker) #declare Checker = 0; #end
#ifndef (Cut) #declare Cut = 0; #end
global_settings { assumed_gamma 1 }
#include "skein_shapes.inc"

background { rgb <0.2, 0.3, 0.4> }
light_source { <6, 8, -10> rgb 1 }
light_source { <-8, 3, -4> rgb 0.4 }

#switch (Shape)
  #case (1)
    #declare S = SkeinTorus;
    #declare P = PrimTorus;
    camera { location <-7, -4, 0> look_at 0 angle 45 }
  #break
  #case (2)
    #declare S = SkeinTube;
    #declare P = PrimTube;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (3)
    #declare S = SkeinFrustum;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (4)
    #declare S = SkeinHexColumn;
    #declare P = PrimHexColumn;
    camera { location <1.5, 4, -5> look_at <0, 1.4, 0> angle 45 }
  #break
  #case (5)
    #declare S = SkeinTwistedTube;
    #declare P = PrimTube;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (6)
    #declare S = SkeinTwistedColumn;
    #declare P = SkeinTwistedColumn;
    camera { location <1.5, 4, -5> look_at <0, 1.4, 0> angle 45 }
  #break
  #case (7)
    #declare S = SkeinFrustumUV;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (8)
    #declare S = SkeinFrustumPos;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (9)
    #declare S = SkeinFrustumMixed;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (10)
    #declare S = SkeinTubeNormal;
    #declare P = PrimTube;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (11)
    #declare S = SkeinTorusDisplaced;
    #declare P = PrimTorusDisplaced;
    camera { location <-7, -4, 0> look_at 0 angle 45 }
  #break
  #case (12)
    #declare S = SkeinTubeDisplaced;
    #declare P = SkeinTubeDisplaced;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (13)
    #declare S = SkeinFrustumLinear;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (14)
    #declare S = SkeinFrustumRange;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (15)
    #declare S = SkeinFrustumScaled;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (16)
    #declare S = SkeinTubeLength;
    #declare P = PrimTubeThick;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (17)
    #declare S = SkeinTubeSway;
    #declare P = SkeinTubeSway;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (18)
    #declare S = SkeinTubeRidged;
    #declare P = SkeinTubeRidged;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (19)
    #declare S = SkeinTubeLobes;
    #declare P = SkeinTubeLobes;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (20)
    #declare S = SkeinBicone;
    #declare P = SkeinBicone;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (21)
    #declare S = SkeinFrustumPath;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (22)
    #declare S = SkeinTubeHandles;
    #declare P = PrimTube;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (23)
    #declare S = SkeinFrustumArc;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (24)
    #declare S = SkeinVase;
    #declare P = SkeinVase;
    camera { location <0, 3.2, -4.5> look_at <0, 1, 0> angle 45 }
  #break
  #case (26)
    #declare S = SkeinTubeGroup;
    #declare P = PrimTube;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (27)
    #declare S = SkeinThickGroup;
    #declare P = PrimTubeThick;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (28)
    #declare S = SkeinFrustumGroup;
    #declare P = PrimFrustum;
    camera { location <0, 2.8, -4.5> look_at <0, 0.9, 0> angle 45 }
  #break
  #case (29)
    #declare S = SkeinDeclared;
    #declare P = SkeinInline;
    camera { location <0, 2.6, -4> look_at <0, 1, 0> angle 45 }
  #break
  #case (25)
    #declare S = SkeinTrefoil;
    #declare P = SkeinTrefoil;
    camera { location <0, -5, -8> look_at 0 angle 45 }
  #break
#end

object {
  #if (Cut)
    difference {
      object { #if (Prim) P #else S #end }
      box { <0, 0.6, -3>, <3, 3, 0> }
      sphere { <-0.6, 1.4, -0.6>, 0.55 }
    }
  #else
    #if (Prim) P #else S #end
  #end
  #if (Checker)
    texture {
      uv_mapping
      pigment { checker rgb <0.9, 0.7, 0.4>, rgb <0.25, 0.15, 0.1> scale <1/16, 1/8, 1> }
      finish { phong 0.6 }
    }
  #else
    pigment { rgb <0.9, 0.7, 0.4> }
    finish { phong 0.6 }
  #end
}
