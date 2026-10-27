// Contextual units, repeated brightness settings, defaults and attenuation ordering.
#version 3.8;
#ifndef (Case) #declare Case = 0; #end
#switch (Case)
  #case (1) #default { interior { light { brightness 1 } } } #break
  #case (2) #default { interior { light { samples 16 } } } #break
  #case (3) #default { interior { light { shadowless off } } } #break
  #case (4) #default { interior { light { attenuation 2, 1 } } } #break
  #case (5) #default { interior { light { far_radii 8 } } } #break
  #case (6) #default { interior { light { media_attenuation on } } } #break
  #case (7) #default { interior { light { fade_cutoff 0 } } } #break
#end
global_settings { assumed_gamma 1 }
#declare ConvertedFlux = scale_lumens(8, 0.5);
#declare FluxFunction = function(a, b) { scale_lumens(a, b) };
#if (abs(ConvertedFlux - 32) > 1e-10 | abs(FluxFunction(8, 0.5) - 32) > 1e-10)
  #error "scale_lumens immediate and compiled evaluations disagree"
#end
camera { orthographic location <0, 8, 0> sky z look_at 0 right x * 2 up z * 2 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
box {
  <-0.5, 1, -0.4>, <0.5, 2, 0.4> pigment { rgbt 1 } hollow no_image
  interior {
    #if (Case = 21) media { light_source { } } #end
    light {
      samples 64 fade_cutoff 0
      #switch (Case)
        #case (11) brightness 10 lumens brightness 0.1 nits #break
        #case (12) brightness 0.1 nits #break
        #case (13) brightness 10 lumens brightness 0.1 #break
        #case (14) brightness 0.1 #break
        #case (15) brightness 0.1 attenuation 2, 1 #break
        #case (16) brightness 0.1 attenuation 1 #break
        #case (20) fade_power 2 #break
        #case (22) cluster_radii 2 cluster_radii 4 #break
        #case (23) far_radii 2 far_radii 4 #break
        #case (24) fade_cutoff 0.001 fade_cutoff 0.002 #break
        #else brightness 0.1
      #end
    }
  }
}
