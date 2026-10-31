// Scattering modifiers and an independent index field leave source sampling unchanged.
#version 3.8;
#ifndef (Case) #declare Case=0; #end
#ifndef (Resolution) #declare Resolution=0.05; #end
#ifndef (Control) #declare Control=0; #end
#ifndef (Draws) #declare Draws=256; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
box { <-0.1,-0.1,0>,<0.1,0.1,1> pigment { rgbt 1 } hollow
 interior { media { mix add method 4 resolution Resolution
  #if (Control) density { rgb 1 } #end
  absorption rgb <0.2,0.4,0.6>
  scattering { 1, rgb <0.04,0.03,0.02> extinction 1 }
 } }
}
#if (Case>0)
box { <-0.1,-0.1,0.25>,<0.1,0.1,0.75> pigment { rgbt 1 } hollow
 interior {
 #if (Case = 3)
  refraction { function { 0.01 } detail auto }
 #else
 media { method 4 resolution Resolution
 #switch(Case)
 #case(1) mix multiply density { rgb 0.5 } #break
 #case(2) mix subtract density { rgb 1 }
  absorption rgb <0.2,0.4,0.6>*0.25
  scattering { 1, rgb <0.04,0.03,0.02>*0.25 extinction 1 }
 #break
 #end
 }
 #end
 }
}
#end
box { <1,-0.1,0.35>,<1.2,0.1,0.65> pigment { rgbt 1 } hollow no_image
 interior { light { pigment { rgb <1,0.5,0.2> } brightness 100 lumens
  samples Draws } }
}
