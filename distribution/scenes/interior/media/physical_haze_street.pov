// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A street lamp in night fog, in real units: 1 scene unit = 1 m, light colours in cd/pi so a white diffuse surface reads in cd/m2. +w640 +h427
// Fog: visibility V = 150 m, sigma_s = 3.9/V = 0.026 per m, non-absorbing (albedo 1), Henyey-Greenstein g = 0.7.
// Lamp: 6 m mounting height, LED of about 3000 K, 900 cd straight down (about 25 lux on the road below), cut-off optic (spot 55/75 degrees).
// Albedos: road 0.12, kerb and pavement 0.25, building 0.35. Only atmospheric media dim a light source, so the fog is the atmosphere. Declare=Look=0 physical method 4 (default), 1 classic method 3, 2 method 4 with 4x
// scattering and extinction 0.25 (the classic look), 3 absorption only. Declare=Exposure=E scales every colour: 0.5 by default; 1 renders cd/m2 with +FH.
#version 3.8;
#ifndef (Look) #declare Look = 0; #end
#ifndef (Exposure) #declare Exposure = 0.5; #end
#ifndef (HazeSamples) #declare HazeSamples = 200; #end
#macro Haze(Sig, G)
  media {
    #switch (Look)
      #case (0) method 4 samples HazeSamples scattering { 5, rgb Sig eccentricity G } #break
      #case (1) method 3 intervals 40 samples 3 jitter 0 aa_level 4 aa_threshold 0.002 scattering { 5, rgb Sig eccentricity G } #break
      #case (2) method 4 samples HazeSamples scattering { 5, rgb 4*Sig eccentricity G extinction 0.25 } #break
      #case (3) method 4 absorption rgb Sig #break
    #end
  }
#end
#declare SIG = 0.026;
#declare G = 0.7;
#declare Warm = <1, 0.85, 0.65> / 0.868;
#declare Icd = 900;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb 0 }
camera { perspective location <-1.0, 1.6, -6> look_at <2.0, 3.0, 25> right x*image_width/image_height angle 62 }
light_source { <2.6, 5.9, 8>, rgb Warm*Icd/pi*Exposure spotlight point_at <2.6, 0, 8> radius 55 falloff 75 tightness 0
  fade_power 2 media_attenuation on media_interaction on }
#declare Asph = texture { pigment { rgb 0.12 } finish { ambient 0 diffuse 1 specular 0 } }
#declare Stone = texture { pigment { rgb 0.25 } finish { ambient 0 diffuse 1 } }
#declare Paint = texture { pigment { rgb 0.5 } finish { ambient 0 diffuse 1 } }
#declare Metal = texture { pigment { rgb 0.08 } finish { ambient 0 diffuse 1 } }
box { <-4, -1, -20>, <4, 0, 120> texture { Asph } }
#for (I, -3, 23) box { <-0.07, 0, I*5>, <0.07, 0.002, I*5+2.5> texture { Paint } } #end
box { <4, -1, -20>, <7.5, 0.12, 120> texture { Stone } }
box { <-7, -1, -20>, <-4, 0.12, 120> texture { Stone } }
box { <7.5, -1, -20>, <9, 11, 120> pigment { rgb 0.35 } finish { ambient 0 diffuse 1 } }
#for (I, 0, 14) #for (J, 0, 2)
  box { <7.48, 1.0+J*3.3, -4+I*8>, <7.6, 2.6+J*3.3, -2+I*8> pigment { rgb 0.04 } finish { ambient 0 diffuse 1 } }
#end #end
#macro Post(Z)
  union {
    cylinder { <4.6, 0, Z>, <4.6, 6.1, Z>, 0.08 }
    cylinder { <4.6, 6.1, Z>, <2.9, 6.1, Z>, 0.04 }
    box { <2.0, 6.0, Z-0.3>, <3.4, 6.18, Z+0.3> }
    texture { Metal }
  }
#end
Post(8) Post(38) Post(68) Post(98)
// the lit lens of the lamp seen from below, 5000 cd/m2
box { <2.1, 5.94, 7.8>, <3.1, 5.99, 8.2> no_shadow pigment { rgb Warm*Exposure } finish { ambient 0 diffuse 0 emission 5000 } }
// the fog is the atmosphere, so it dims the light of the lamp on its way as well as the view of it
Haze(SIG, G)
