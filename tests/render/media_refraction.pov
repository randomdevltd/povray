#version Version;
// Media refraction checks for media_refraction.sh: Declare=Version=<v> Declare=Case=<n>, Declare=Ramp=1 for a vertical ramp backdrop.
// Mix and MixAll pick an ior_mix (0 surface, 1 mean, 2 replace) for the inner object or for every clear one.
#ifndef (Case) #declare Case = 0; #end
#ifndef (Ramp) #declare Ramp = 0; #end
#ifndef (Far) #declare Far = 0; #end
#ifndef (Inner) #declare Inner = 1; #end
#ifndef (BallIor) #declare BallIor = 1.5; #end
#ifndef (Gap) #declare Gap = 0; #end
#ifndef (Mix) #declare Mix = -1; #end
#ifndef (MixAll) #declare MixAll = -1; #end
#ifndef (Priority) #declare Priority = 0; #end
#ifndef (Fog) #declare Fog = 0; #end
#ifndef (Atmosphere) #declare Atmosphere = 1; #end
#macro IorMix(M) #switch (M) #case (0) ior_mix surface #break #case (1) ior_mix mean #break #case (2) ior_mix replace #break #end #end
global_settings { assumed_gamma 1 atmospheric_ior Atmosphere }
camera { orthographic location <Far, 0, -10> look_at <Far, 0, 0> right x * 8 up y * 6 }
#declare Backdrop = plane {
  z, 4
  #if (Ramp = 2) pigment { gradient x color_map { [0 rgb 0] [1 rgb 1] } scale 8 translate -4 * x }
  #elseif (Ramp = 3) pigment { rgb 1 }
  #elseif (Ramp) pigment { gradient y color_map { [0 rgb 0] [1 rgb 1] } scale 8 translate -4 * y }
  #else pigment { checker rgb 0.1 rgb 0.9 scale 0.5 }
  #end
  finish { ambient 0 emission 1 diffuse 0 }
}
#if ((Case != 16) & ((Case < 42) | (Case > 49) | (Case = 44))) object { Backdrop } #end

#declare Clear = texture { pigment { rgbt 1 } finish { diffuse 0 } };
#declare Lens = media { method 3 refraction 0.6 density { spherical scale 2.5 } }
#declare Rise = media { method 3 refraction 0.2 density { gradient y scale 4 translate -2 * y } }
#declare Riser = box { <-5, -2, -2>, <5, 2, 2> texture { Clear } interior { ior 1 media { Rise } } }
#declare Tube = box { <-1, -1, -3>, <1, 1, 3> texture { Clear } };
#declare Ball = sphere { 0, 1.5 texture { Clear } interior { ior BallIor IorMix(MixAll) } };
#declare Water = box { -3, 3 texture { Clear } interior { ior 1.33 IorMix(MixAll) } };
#declare A = sphere { -0.7 * x, 1.6 };
#declare B = sphere { 0.7 * x, 1.6 };
#declare Wall = <cos(radians(70)), 0, -sin(radians(70))>;
#declare WaterFront = intersection { box { <-3, -2, -1>, <3, 2, 1> } plane { -Wall, 0 } pigment { rgbf <1, 0.8, 0.8, 1> } finish { diffuse 0 } interior { ior 1.33 } };
#declare GlassBack = intersection { box { <-3, -2, -1>, <3, 2, 1> } plane { Wall, 0 } texture { Clear } interior { ior 1.5 } };

#switch (Case)
#case (1)
  sphere { 0, 2 texture { Clear } interior { ior 1 media { refraction 0.5 } media { absorption 0.2 emission 0.1 } } }
#break
#case (2)
  sphere { 0, 2 texture { Clear } interior { ior 1.5 media { absorption 0.2 emission 0.1 / 2.25 } } }
#break
#case (3) // the lens's density fades to zero inside its box, which has a Fresnel mirror finish
  box { -3, 3 texture { pigment { rgbt 1 } finish { diffuse 0 reflection { 0, 1 fresnel } } } interior { ior 1 media { Lens } } }
#break
#case (5)
  object { Riser }
#break
#case (6)
  box { <-5, -2, -2>, <5, 2, 2> texture { Clear }
        interior { ior 1 media { refraction 0.2 density { gradient y scale 4 translate -2 * y } } } }
#break
#case (7)
  object { Riser }
  object { Tube interior { media { mix subtract refraction 0.2 } } }
#break
#case (8)
  object { Riser }
  object { Tube interior { media { mix multiply refraction 1 density { rgb 0 } } } }
#break
#case (9)
  object { Riser }
  object { Tube interior { media { mix replace refraction 0.1 } } }
#break
#case (10)
  object { Tube interior { media { mix subtract refraction 0.2 } } }
  object { Riser }
#break
#case (11) object { Ball } object { Water } #break
#case (12) object { Water } object { Ball } #break
#case (13) object { Ball } #break
#case (14)
  box { <-3, -2, -1>, <3, 2, 1> texture { Clear } interior { media { refraction 0.3 } } }
#break
#case (15) object { Water } #break
#case (16) object { Ball } object { Backdrop } #break
#case (17) object { Ball } box { -3, 3 texture { Clear } interior { ior 1 #if (Fog) media { absorption 0.1 } #end } } #break
#case (50) object { Ball } box { -3, 3 texture { Clear } #if (Fog) interior { media { absorption 0.1 } } #end } #break
#case (20) // glass and water sharing the wall z = x
  intersection { box { <-3, -2, -1>, <3, 2, 1> } plane { <-1, 0, 1>, 0 } texture { Clear } interior { ior 1.5 } }
  intersection { box { <-3, -2, -1>, <3, 2, 1> } plane { <1, 0, -1>, 0 } texture { Clear } interior { ior 1.33 } }
#break
#case (21)
  intersection { box { <-3, -2, -12>, <3, 2, 1> } plane { <-1, 0, 1>, 0 } texture { Clear } interior { ior 1.5 } }
  intersection { box { <-3, -2, -1>, <3, 2, 1> } plane { <1, 0, -1>, 0 } texture { Clear } interior { ior 1.33 } }
#break
#case (22)
  #for (K, 0, 2)
    #local A = radians(120 * K + 10); #local B = radians(120 * K + 130);
    intersection {
      box { <-2, -2, -2>, <2, 2, 2> } plane { <sin(A), 0, -cos(A)>, 0 } plane { <-sin(B), 0, cos(B)>, 0 }
      texture { Clear } interior { ior 1.5 }
    }
  #end
#break
#case (23) box { <-2, -2, -2>, <2, 2, 2> texture { Clear } interior { ior 1.5 } } #break
#case (24)
#case (25)
  light_source { <-4, 5, -10> rgb 1 }
  box { <-3, -2, 2>, <3, 2, 2.5> pigment { rgb 0.8 } finish { diffuse 0.9 } }
  box { <-3, -2, -1>, <3, 2, (Case = 24 ? 2 : 1.99)> texture { Clear } interior { media { absorption 0.05 } } }
#break
#case (26) // pixel 48, 36 clips this box's top edge 5e-7 deep at 0.002 ahead, then bends over it
  box { <-5, -2, -2>, <5, 2, 2> texture { Clear } interior { ior 1 media { method 3 refraction 10 density { gradient y scale 4 translate -2 * y } } } }
  box { <-3, -3, -1.998>, <3, 3 - 36.5 * 6 / 72 + 5e-7, 1> texture { Clear } interior { ior 1 } }
#break
#case (27) box { <-3, -3, -0.75e-4>, <3, 3, 0.75e-4> rotate y * 30 texture { Clear } interior { ior 1.5 } } #break
#case (28) object { Ball translate x * Far } #break
#case (29)
  box { <-2, -2, -1>, <2, 2, 1> texture { Clear } interior { ior 1.5 } }
  box { <-1.5, -1.5, -1 + 1e-4>, <1.5, 1.5, 0.5> pigment { uv_mapping checker rgbf <1, 1, 1, 0.5>, rgbf <0.5, 0.5, 1, 0.5> } finish { diffuse 0 } }
#break
#case (30)
#case (31)
#case (32)
#case (33)
  light_source { <-4, 5, -10> rgb 1 }
  #if (Case < 32) box { <-3, -2, 2>, <3, 2, 2.5> pigment { rgb 0.8 } finish { diffuse 0.9 } }
  #else polygon { 5, <-3, -2, 2>, <3, -2, 2>, <3, 2, 2>, <-3, 2, 2>, <-3, -2, 2> pigment { rgb 0.8 } finish { diffuse 0.9 } }
  #end
  box { <-3, -2, -1>, <3, 2, (mod(Case, 2) = 0 ? 2 : 1.99)> texture { Clear } interior { ior (Case < 32 ? 1 : 1.5) } }
#break
#case (34) // a smooth field bends rays onto an opaque block whose front face alone is white
  box { <-4, -3, -3>, <4, 3, 3> texture { Clear }
        interior { media { method 3 refraction 0.05 density { function { pow(max(0, 1 - (x * x + y * y + z * z) / 9), 2) } } } } }
  box { <-1.5, -1.5, 0.5>, <1.5, 1.5, 1.5> pigment { function { z < 0.51 } color_map { [0 rgb 0] [1 rgb 1] } }
        finish { ambient 0 emission 1 diffuse 0 } }
#break
#case (35) // the same field around a clear box of glowing media
  box { <-4, -3, -3>, <4, 3, 3> texture { Clear }
        interior { media { method 3 refraction 0.05 density { function { pow(max(0, 1 - (x * x + y * y + z * z) / 9), 2) } } } } }
  box { <-1.5, -1.5, 0.5>, <1.5, 1.5, 1.5> texture { Clear } interior { media { emission 1.5 } } }
#break
#case (18)
  sphere { 0, 2.5 texture { Clear } interior { ior 1.5 IorMix(MixAll) } }
  sphere { 0, 1 texture { Clear } interior { ior Inner IorMix(Mix) IorMix(MixAll) } }
#break
#case (53) sphere { 0, 2.5 texture { Clear } interior { ior 1.5 } } #break
#case (54) // a replace bubble placed before its glass, outranking it only by priority
  sphere { 0, 1 texture { Clear } interior { ior 1 ior_mix replace priority Priority } }
  sphere { 0, 2.5 texture { Clear } interior { ior 1.5 } }
#break
#case (55) // a replace bubble inside the overlap of two glasses that mean, and the same bubble cut out of both
#case (56)
  #local C = sphere { 0, 0.5 };
  #if (Case = 55)
    object { A texture { Clear } interior { ior 1.5 } } object { B texture { Clear } interior { ior 1.3 } }
    object { C texture { Clear } interior { ior 1 ior_mix replace } }
  #else
    difference { object { A } object { C } texture { Clear } interior { ior 1.5 } }
    difference { object { B } object { C } texture { Clear } interior { ior 1.3 } }
  #end
#break
#case (57) object { Ball } #break // under atmospheric_ior 1.33 the ball refracts by 1.5 / 1.33
#case (58) difference { box { -3, <3, 3, 5> } sphere { 0, 1.5 } texture { Clear } interior { ior 1.33 } } object { Ball } #break
#case (59) object { Ball } box { -3, 3 texture { Clear } interior { ior 1.33 } } #break
#case (19) box { <-5, -4, -12>, <5, 4, 3.5> texture { Clear } interior { ior 1.5 } } sphere { 0, 1.5 texture { Clear } interior { ior Inner } } #break
#case (51) // a bubble cut with difference, and the same hole filled by a separate sphere
#case (52)
  difference { sphere { 0, 2.5 } sphere { 0, 1 } texture { Clear } interior { ior 1.5 } }
  #if (Case = 52) sphere { 0, 1 texture { Clear } interior { ior 1.25 } } #end
#break
#case (37) object { A texture { Clear } interior { ior 1.5 } } object { B texture { Clear } interior { ior 1.3 } } #break
#case (38) object { B texture { Clear } interior { ior 1.3 } } object { A texture { Clear } interior { ior 1.5 } } #break
#case (39) // the overlap cut out as a solid of the mean ior
  difference { object { A } object { B } texture { Clear } interior { ior 1.5 } }
  difference { object { B } object { A } texture { Clear } interior { ior 1.3 } }
  intersection { object { A } object { B } texture { Clear } interior { ior 1.4 } }
#break
#case (40) object { WaterFront } object { GlassBack } #break // rays meet the shared wall 70 degrees from its normal
#case (41) object { GlassBack } object { WaterFront } #break
#case (44) // seen at 30 degrees: a small water slab Gap behind a big glass slab's face; parallel faces make the gap invisible
  camera { orthographic location <-5, 0, -8.66> look_at 0 right x * 8 up y * 6 }
  box { <-4, -3, -1>, <4, 3, 0> texture { Clear } interior { ior 1.5 } }
  box { <-3, -2, Gap>, <3, 2, Gap + 0.1> texture { Clear } interior { ior 1.33 } }
#break
#case (45) // from above: a floor lit through a red filter box, then a blue one Gap behind it, or two absorbing media boxes
#case (46)
  camera { orthographic location <0, 10, 0> look_at 0 right x * 8 up z * 6 }
  light_source { <0, 3, 20> rgb 1 media_attenuation on }
  plane { y, -0.5 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
  #if (Case = 45)
    box { <-5, 0, -1>, <5, 4, 1> pigment { rgbf <1, 0.3, 0.3, 1> } finish { diffuse 0 } }
    box { <-5, 0, 1 + Gap>, <5, 4, 3> pigment { rgbf <0.3, 0.3, 1, 1> } finish { diffuse 0 } }
  #else
    box { <-5, 0, -1>, <5, 4, 1> texture { Clear } interior { media { absorption rgb <0, 0.5, 0.5> } } }
    box { <-5, 0, 1 + Gap>, <5, 4, 3> texture { Clear } interior { media { absorption rgb <0.5, 0.5, 0> } } }
  #end
#break
#case (47) // a lit floor under a clear union: split (47), bounded_by (48) and split_union off (49) must match
#case (48)
#case (49)
  camera { location <0, 4, -8> look_at <0, 0.5, 0> }
  light_source { <-3, 8, -4> rgb 1 }
  plane { y, 0 pigment { rgb 0.8 } finish { diffuse 1 } }
  union {
    box { <-1, 0.01, -1>, <1, 1, 1> }
    sphere { <0, 1, 0>, 0.8 }
    #if (Case = 48) bounded_by { box { <-2, 0, -2>, <2, 2, 2> } } #end
    #if (Case = 49) split_union off #end
    pigment { rgbt 0.9 } finish { diffuse 0 } interior { ior 1.4 }
  }
#break
#case (42) // from above: a polygon wall within the tolerance of a clear face must shade the floor behind both
#case (43)
  camera { orthographic location <0, 10, 0> look_at 0 right x * 8 up z * 6 }
  light_source { <0, 3, 20> rgb 1 }
  plane { y, -0.5 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
  polygon { 5, <-5, -0.5, 0>, <5, -0.5, 0>, <5, 4, 0>, <-5, 4, 0>, <-5, -0.5, 0> pigment { rgb 0.5 } translate z * (2 + 1e-5) }
  box { <-5, 0, -1>, <5, 4, (Case = 42 ? 2 : 1.99)> texture { Clear } }
#break
#end
