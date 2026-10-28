// A coordinate screen measures a graded-index ray independently of surface brightness.
#version 3.8;
#ifndef (Bend) #declare Bend=1; #end
#ifndef (Split) #declare Split=0; #end
#ifndef (Sigma) #declare Sigma=0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 max_trace_level 8 }
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
  refraction { detail 0.06
   #if (Split)
    function { 0.05+0.05*x }
    function { 0.05+0.05*x }
   #else
    function { #if (Bend) 0.1+0.1*x #else 0.1 #end }
   #end
  }
  media { method 3 mix add absorption rgb Sigma }
 }
}
