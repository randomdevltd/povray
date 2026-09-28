#version 3.8;
#include "functions.inc"
#ifndef (Case) #declare Case=0; #end
#ifndef (Generator) #declare Generator=3; #end
#ifndef (Samples) #declare Samples=89; #end
#ifndef (Method) #declare Method=3; #end
#ifndef (Fallback) #declare Fallback=0; #end
#declare Octaves=function {
 0.5*f_noise3d(x,y,z)+0.25*f_noise3d(2*x,2*y,2*z)+0.125*f_noise3d(4*x,4*y,4*z)+0.0625*f_noise3d(8*x,8*y,8*z)
}
#declare Cloud=function { max(0,min(1,Octaves(x,y,z)+0.1*sin(x*2)+0.1*cos(y*3))) }
global_settings { assumed_gamma 1.0 noise_generator Generator }
camera { location <0,0.7,-8> look_at <0,0,0> right x*4/3 angle 45 }
light_source { <-20,40,10> rgb 1.2 }
light_source { <20,30,-10> rgb <0.5,0.6,0.8> }
plane { y,0 pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
box { <-100,1,-100>,<100,10,100> hollow pigment { rgbt 1 }
 interior { media { absorption <0.007,0.006,0.004> method Method intervals 1 samples Samples jitter 0
  density {
   #switch (Case)
    #case (0) function { f_noise3d(x,y,z) } #break
    #case (1) function { max(0,min(1,f_noise3d(x,y,z)+0.2*sin(x*3)+0.15*cos(z*2))) } #break
    #case (2) function { Octaves(x,y,z) } #break
    #case (3) function { Cloud(x,y,z) } #break
    #case (4) function { select(x,0.3+0.4*f_noise3d(x,y,z),0.6+0.2*f_noise3d(2*x,y,z)) } #break
    #case (5) function { 1.25+0.75*f_noise3d(x,y,z) } #break
    #case (6) function { f_noise_generator(x,y,z,Generator) } #break
    #case (7) function { max(0,min(1,0.3+0.1*sin(x*y)+0.1*cos(y*z)+0.1*exp(-abs(x)/8)+0.1*pow(abs(z)+1,0.3))) } #break
    #case (8) function { f_ridged_mf(x,y,z,0.9,2,5,1,2,0) } #break
   #end
   #if (Fallback=1) turbulence 0.3 octaves 4 #end
   scale <1.2,0.9,1.1> rotate <5,0,3> translate <0,0.4,0>
   color_map {
    #if (Fallback=2) blend_mode 2 blend_gamma 2.2 #end
    [0 rgb 0.1] [0.4 rgb <0.4,0.6,0.9>] [0.7 rgb <0.8,0.7,0.5>] [1 rgb 1]
   }
  }
  density { spherical scale 60 color_map { [0 rgb 1] [1 rgb 0.4] } }
 } }
}
