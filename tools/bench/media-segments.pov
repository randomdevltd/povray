#version 3.8;
#ifndef (Area) #declare Area=0; #end
#ifndef (Atmosphere) #declare Atmosphere=0; #end
#ifndef (Segments) #declare Segments=128; #end
global_settings { assumed_gamma 1.0 }
camera { location <0,0.7,-8> look_at <0,0,0> right x*4/3 angle 45 }
light_source { <0,Segments*0.03+7,0> rgb 1
#if (Area) area_light x,z,3,3 adaptive 1 #end
}
#if (Atmosphere)
media { absorption 0.00005 method 2 intervals 1 samples 4 jitter 0
density { planar scale 0.001 translate y*7 color_map { [0 rgb 0] [0.999 rgb 0] [1 rgb 1] } }
}
#end
plane { y,0 pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
#for (K,0,Segments-1)
 #declare Centre=1+K*0.03;
 box { <-100,Centre-0.005,-100>,<100,Centre+0.005,100> hollow pigment { rgbt 1 }
  interior { media { absorption 0.1 method 2 intervals 1 samples 4 jitter 0
   density { planar scale 0.0001 translate y*Centre
    color_map { [0 rgb 0] [0.999 rgb 0] [1 rgb 1] } }
  } }
 }
#end
