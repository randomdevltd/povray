// Each medium keeps its selected density representation within overlapping transport; the classic one only absorbs.
#version 3.8;
#ifndef (Draws) #declare Draws=256; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
box { <-0.1,-0.1,0>,<0.1,0.1,1> pigment { rgbt 1 } hollow
 interior { media { method 4 resolution 0.05
  absorption rgb <0.2,0.4,0.6>
  scattering { 1, rgb <0.04,0.03,0.02> extinction 1 }
 } }
}
box { <-0.09,-0.09,0.25>,<0.09,0.09,0.75> pigment { rgbt 1 } hollow
 interior {
  media { method 3 density { rgb 1 } absorption rgb (<0.2,0.4,0.6> + <0.04,0.03,0.02>)*0.5 }
  media { method 4 scattering { 1, rgb <0.04,0.03,0.02>*0.5 extinction 0 } }
 }
}
box { <1,-0.1,0.35>,<1.2,0.1,0.65> pigment { rgbt 1 } hollow no_image
 interior { light { pigment { rgb <1,0.5,0.2> } brightness 100 lumens
  samples Draws } }
}
