// A camera miss integrates a constant scattering atmosphere over an unbounded ray.
#version 3.8;
#ifndef (Draws) #declare Draws=512; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
background { rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
media { method 4 absorption rgb <0.2,0.4,0.6>
 scattering { 1, rgb <0.04,0.03,0.02> extinction 1 }
}
box { <1,-0.1,0.35>,<1.2,0.1,0.65> pigment { rgbt 1 } hollow no_image
 interior { light { pigment { rgb <1,0.5,0.2> } brightness 100 lumens
  samples Draws fade_cutoff 0 shadows method 1 } }
}
