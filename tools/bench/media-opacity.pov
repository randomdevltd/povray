#version 3.8;
#ifndef (Method) #declare Method=2; #end
#ifndef (Intervals) #declare Intervals=1; #end
#ifndef (Samples) #declare Samples=40; #end
#ifndef (Absorption) #declare Absorption=2; #end
#ifndef (Asymmetric) #declare Asymmetric=0; #end
#ifndef (StartDensity) #declare StartDensity=1; #end
global_settings { assumed_gamma 1.0 }
camera { location <0,1,-5> look_at <0,0,0> right x*4/3 angle 30 }
light_source { <0,10,0> rgb 1000 }
plane { y,0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
box { <-1,2,-1>, <1,8,1> hollow pigment { rgbt 1 }
 interior { media { absorption #if (Asymmetric) <Absorption,Absorption,0.2> #else Absorption #end method Method intervals Intervals samples Samples jitter 0
  density { planar color_map { [0 rgb StartDensity] [1 rgb 1] } }
 } }
}
