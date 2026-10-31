// Final publication prepares scalar and scene-dependent fields before tracing.
#version 3.8;
#ifndef (Case) #declare Case=0; #end
#ifndef (Draws) #declare Draws=256; #end
#ifndef (Resolution) #declare Resolution=0.05; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
#declare ProbeObject=box { <-2,-2,0.5>,<2,2,2> }
#declare ObjectValue=function { pigment { object { ProbeObject rgb 1 rgb 1 } } }
#declare ProbeView=camera { location <0,0,-1> look_at 0 }
#declare ScreenValue=function { pattern { pigment_pattern {
 screen { camera { ProbeView } fallback { rgb 1 } }
} } }
box { <-0.1,-0.1,0>,<0.1,0.1,1> pigment { rgbt 1 } hollow
 interior { media { method 4 resolution Resolution
  absorption rgb <0.2,0.4,0.6>
  scattering { 1, rgb <0.04,0.03,0.02> extinction 1 }
  #switch(Case)
  #case(1) density { function { select(z-0.5,0.25,0.75) }
   color_map { [0 rgb 0] [1 rgb 2] } } #break
  #case(2) density { function { ObjectValue(x,y,z).red } } #break
  #case(3) density { function { ScreenValue(x,y,z) } } #break
  #end
 } }
}
box { <1,-0.1,0.35>,<1.2,0.1,0.65> pigment { rgbt 1 } hollow no_image
 interior { light { pigment { rgb <1,0.5,0.2> } brightness 100 lumens
  samples Draws fade_cutoff 0 shadows method 1 } }
}
