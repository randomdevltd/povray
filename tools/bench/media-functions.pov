#version 3.8;
#include "functions.inc"
#ifndef (Case) #declare Case=0; #end
#ifndef (Method) #declare Method=3; #end
#ifndef (Samples) #declare Samples=89; #end
#ifndef (Intervals) #declare Intervals=1; #end
#ifndef (AbsorptionScale) #declare AbsorptionScale=1; #end
#ifndef (LightPower) #declare LightPower=1; #end
#declare Height=function { max(0,min(1,(12-y)/16)) }
#declare Nested=function(a,b,c) { max(0,min(1,0.8-a/40+b/60-c/80)) }
global_settings { assumed_gamma 1.0 }
camera { location <0,0.7,-8> look_at <0,0,0> right x*4/3 angle 45 }
light_source { <-20,40,10> rgb 1.2*LightPower }
light_source { <20,30,-10> rgb <0.5,0.6,0.8>*LightPower }
plane { y,0 pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
box { <-100,1,-100>,<100,10,100> hollow pigment { rgbt 1 }
 interior { media { absorption <0.002,0.0017,0.0011>*AbsorptionScale method Method intervals Intervals samples Samples jitter 0
  density {
   #switch (Case)
    #case (0) function { max(0,min(1,(12-y)/16)) } #break
    #case (1) function { Height(x,y,z) } #break
    #case (2) function { max(0,min(1,0.8-x/40+y/60-z/80)) } #break
    #case (3) function { min(1,max(0,(y-2)/7)) } #break
    #case (4) function { max(0,min(1,2-y/4)) } #break
    #case (5) function { 1.25+y/16 } #break
    #case (6) function { -0.25+y/16 } #break
    #case (7) function { Nested(z,x,y) } #break
    #case (8) function { max(0,min(1,y*y/100)) } #break
    #case (9) function { f_noise3d(x,y,z) } #break
    #case (10) function { max(0,min(1,(12-y)/16)) } cubic_wave #break
    #case (11) function { max(0,min(1,(12-y)/16)) } frequency 2.3 phase 0.2 #break
    #case (12) function { max(0,min(1,(12-y)/16)) } poly_wave 2 #break
    #case (13) function { max(0,min(1,(12-y)/16)) } triangle_wave #break
    #case (14) function { sin(y) } #break
    #case (15) function { select(y-5,0.1,0.8) } #break
   #end
   scale <1.2,0.9,1.1> rotate <5,0,3> translate <0,0.4,0>
   color_map { [0 rgb 1] [0.5 rgb <0.75,0.8,0.9>] [1 rgb 0.3] }
  }
  density { spherical scale 60 color_map { [0 rgb 1] [1 rgb 0.4] } }
 } }
}
