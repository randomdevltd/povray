// Independent interior axes preserve surrounding optical media.
#version 4.0;
#ifndef (Case) #declare Case=0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 atmospheric_ior 1 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
box { <-2,-2,-2>,<2,2,3> pigment { rgbt 1 } hollow
 interior {
  #if (Case=4)
   media { method 3 absorption rgb 0.1 emission rgb 0.02 scattering { 1,rgb 0.1 extinction 1 } }
  #else
   media { method 3 absorption rgb 0.2 }
  #end
 }
}
plane { z,2 hollow pigment { rgb <0.4,0.6,0.2> } finish { ambient 0 emission 1 diffuse 0 } }
box { <-0.5,-0.5,0>,<0.5,0.5,1> pigment { rgbt 1 }
 finish { ambient 0 emission 0 diffuse 0 }
 interior { ior 1
  #if (Case=0 | Case=4) refraction { function { 0.1 } } #end
  #if (Case=1) light { brightness 10 lumens samples 16 pigment { rgb 1 } } #end
  #if (Case=3) media { mix replace absorption 0 } #end
 }
}
