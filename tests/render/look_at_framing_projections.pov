// look_at_framing_projections: user_defined cylindrical projections framing a red box; Case picks the setup.
// Frame 0 unframed, 1 the declared box, 2 a tagged selection, 3 an area light, 4 it turned and moved, 5 a point light.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Frame) #declare Frame = 1; #end
global_settings { assumed_gamma 1 }
#include "projections.inc"
#declare Glow = finish { emission 1 diffuse 0 }
#declare Target = box { <9, 1, -1>, <11, 3, 1> }
#declare Profile = ProjectionMercator;
#declare Lower = -60; #declare Upper = 60;
#declare Origin = ProjectionSingleOrigin;
#declare Multiplier = 0.9;
#switch (Case)
  #case (1) #declare Profile = ProjectionMiller; #break
  #case (2) #declare Target = box { <-1, 1, -11>, <1, 3, -9> }; #break
  #case (3) #declare Target = box { <9, -10, -1>, <11, 10, 1> }; #declare Lower = -20; #declare Upper = 20; #break
  #case (4) #declare Multiplier = 0.6; #break
  #case (5)
    #declare Lower = -64; #declare Upper = 77;
    #declare Spot = 60 * <sin(radians(150)) * cos(radians(15)), sin(radians(15)), cos(radians(150)) * cos(radians(15))>;
    #declare Target = box { Spot - 0.3, Spot + 0.3 };
  #break
  #case (6) #declare Origin = ProjectionCylinderOrigin; #break
#end
#declare Projection = CylindricalProjectionCamera(Profile, Lower, Upper, Origin, 4, 0, <0, 0, 0>, z, x, y);
object { Target pigment { rgb <1, 0, 0> } finish { Glow } tags { "target" } }
plane { y, -2 pigment { checker rgb 0.1 rgb 0.25 } finish { Glow } }
#declare Low = min_extent(Target); #declare High = max_extent(Target);
#declare Span = High - Low; #declare Middle = (Low + High) / 2;
#switch (Frame)
  #case (0) camera { Projection } #break
  #case (1) camera { Projection look_at Target, Multiplier } #break
  #case (2) camera { Projection look_at tagged { "target" }, Multiplier } #break
  #case (3)
    #declare Lamp = light_source { Middle rgb 1 area_light <Span.x, Span.y, 0>, <0, 0, Span.z>, 2, 2 }
    camera { Projection look_at Lamp, Multiplier }
  #break
  #case (4)
    #declare Lamp = light_source { 0 rgb 1 area_light <0, Span.y, Span.x>, <Span.z, 0, 0>, 2, 2 rotate y * 90 translate Middle }
    camera { Projection look_at Lamp, Multiplier }
  #break
  #case (5) #declare Lamp = light_source { Middle rgb 1 } camera { Projection look_at Lamp } #break
#end
