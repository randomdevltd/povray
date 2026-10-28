// A coordinate screen measures a graded-index ray independently of surface brightness.
#version 3.8;
#ifndef (Bend) #declare Bend=1; #end
#ifndef (Native) #declare Native=0; #end
#ifndef (Sigma) #declare Sigma=0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 refraction_detail auto max_trace_level 8 }
camera { orthographic location <0,0,-1> direction z right x*0.1 up y*0.1 }
plane { z,2 hollow
 pigment { function { 0.5+0.2*x }
  color_map { [0 rgb <0,0.6,0.3>] [1 rgb <0.6,0.6,0.3>] }
 }
 finish { ambient 0 emission 1 diffuse 0 }
}
box { <-0.5,-0.5,0>,<0.5,0.5,1> pigment { rgbt 1 } hollow
 finish { ambient 0 emission 0 diffuse 0 }
 interior { ior 1
  #if (Native)
   refraction { strength 0.2 field { function { 0.5+0.5*x } } detail 0.06 }
  #else
   media { method 3 mix add refraction 0.2
    density { #if (Bend) function { 0.5+0.5*x } #else rgb 0.5 #end }
   }
  #end
  media { method 3 mix add absorption rgb Sigma }
 }
}
