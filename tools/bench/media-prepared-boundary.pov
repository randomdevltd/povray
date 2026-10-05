// Moving an invisible transparent shadow receiver must not extend bounded media.
#version 4.0;
#ifndef (Distance) #declare Distance = 200; #end
#ifndef (Wide) #declare Wide = 0; #end
#ifndef (Method) #declare Method = 4; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0,0,-4> look_at 0 right x*1.5 up y*1.5 }
background { rgb 0.1 }
light_source { <0,0,500> rgb 1 media_attenuation on }
box { -1, 1 hollow no_shadow
  pigment { rgbt 1 }
  interior { media {
    method Method
    #if (Method = 4) resolution 0.05 #end
    absorption rgb 0.4
    scattering { 1, rgb 0.2 }
    density { function { 1 } }
    intervals 1 samples 16
  } }
}
plane { z, Distance hollow no_image pigment { rgbt 1 } }

#if (Wide)
box { <-2,-2,-5>, <2,2,150> hollow no_shadow
  pigment { rgbt 1 }
  interior { media {
    method Method
    #if (Method = 4) resolution 4 #end
    absorption rgb 0.001
    scattering { 1, rgb 0.001 }
    density { function { 1 } }
    intervals 1 samples 16
  } }
}
#end
