// Finite-source single scattering with independently integrable piecewise media.
#version 3.8;
#ifndef (Case) #declare Case=0; #end
#ifndef (Resolution) #declare Resolution=0.05; #end
#ifndef (Draws) #declare Draws=256; #end
#ifndef (Phase) #declare Phase=0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-1> direction z right x*0.02 up y*0.02 }
#macro Slab(Z0,Z1,Density)
box { <-0.1,-0.1,Z0>,<0.1,0.1,Z1> pigment { rgbt 1 } hollow
  interior { media { mix add method 4 resolution Resolution
    density { #if (Case=1) function { select(z-0.5,0.25,0.75) } color_map { [0 rgb 0] [1 rgb 2] } #else rgb 1 #end }
    absorption rgb <0.2,0.4,0.6>*Density
    scattering { #if (Phase=0) 1 #else 5 #end , rgb <0.04,0.03,0.02>*Density
      extinction 1 #if (Phase!=0) eccentricity Phase #end }
  } }
}
#end
#if (Case=4)
Slab(0,0.5,0.5) Slab(0.5,1,1.5)
#else
Slab(0,1,1)
#end
#if (Case=2)
box { <-0.1,-0.1,0.25>,<0.1,0.1,0.75> pigment { rgbt 1 } hollow
  interior { media { mix multiply method 4 resolution Resolution density { rgb 0.5 } } }
}
#end
#if (Case=3)
box { <0.49,-1,-1>,<0.51,1,0.5> pigment { rgb 0 } no_image }
#end
box { <1,-0.1,0.35>,<1.2,0.1,0.65> pigment { rgbt 1 } hollow no_image
 interior { light { pigment { rgb <1,0.5,0.2> } brightness 100 lumens samples Draws
 } }
}
