// Camera appearance is independent of gathered illumination; real scattering remains gatherable.
#version 3.8;
#ifndef (Glow) #declare Glow = 0.000001; #end
#ifndef (ExplicitLight) #declare ExplicitLight = 0; #end
#ifndef (Sigma) #declare Sigma = 0; #end
#ifndef (Spot) #declare Spot = 0; #end
#ifndef (Radio) #declare Radio = 1; #end
#ifndef (ViewGlow) #declare ViewGlow = 0; #end
#ifndef (Modified) #declare Modified = 0; #end
global_settings {
  assumed_gamma 1 ambient_light rgb 0
  #if (Radio)
    radiosity { count 128 nearest_count 1 error_bound 0.2 recursion_limit 1
      media on brightness 1 pretrace_start 0.08 pretrace_end 0.04 }
  #end
}
background { rgb 0 }
#if (ViewGlow)
  camera { orthographic location <0.95, 0.7, -3> look_at <0.95, 0.7, 0> right x * 0.4 up y * 0.4 }
#else
  camera { orthographic location <0, 4, 0> sky z look_at 0 right x * 0.8 up z * 0.8 }
#end
box { <-0.5, -0.05, -0.5>, <0.5, 0, 0.5> pigment { rgb 1 }
  finish { ambient 0 emission 0 diffuse 1 } }
box {
  <0.55, 0.2, -0.7>, <1.35, 1.2, 0.7> pigment { rgbt 1 } hollow
  interior {
    media { method 3 emission rgb Glow
      #if (Sigma > 0) scattering { 1, rgb Sigma } #end }
    #if (Modified) media { mix multiply method 4 resolution 0.1 density { rgb 1 } } #end
    #if (ExplicitLight) light { brightness 10 lumens samples 32 } #end
  }
}
#if (Spot)
  light_source {
    <0.95, 0.7, -2> color rgb 50 spotlight point_at <0.95, 0.7, 0>
    radius 4 falloff 8 tightness 1 media_interaction on media_attenuation on
  }
#end
