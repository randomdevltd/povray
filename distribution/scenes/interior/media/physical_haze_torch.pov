// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A torch beam in a smoky room, in real units: 1 scene unit = 1 m, light colours in cd/pi so a white diffuse surface reads in cd/m2. +w640 +h427
// Room 4 x 5 m and 2.6 m high. Smoke: sigma_s = 0.2 per m, non-absorbing (albedo 1), Henyey-Greenstein g = 0.5.
// Torch: LED, 3000 cd peak, spot 4/9 degrees, 3.3 m to the wall (about 230 lux in the spot). Fill: a 15 cd point light in the ceiling (about 2.4 lux on the floor).
// Albedos: walls 0.5, floor 0.3, ceiling 0.6, cabinet 0.25. Only atmospheric media dim a light source, so the smoke is the atmosphere. Declare=Look=0 physical method 4 (default), 1 classic method 3, 2 method 4 with 4x
// scattering and extinction 0.25 (the classic look), 3 absorption only. Declare=Exposure=E scales every colour: 0.05 by default; 1 renders cd/m2 with +FH.
#version 3.8;
#ifndef (Look) #declare Look = 0; #end
#ifndef (Exposure) #declare Exposure = 0.05; #end
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
#declare SIG = 0.2;
#declare G = 0.5;
#declare Cool = <0.92, 0.97, 1.08> / (0.2126*0.92 + 0.7152*0.97 + 0.0722*1.08);
#declare Warm = <1, 0.85, 0.65> / 0.868;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb 0 }
camera { perspective location <3.0, 1.6, 0.3> look_at <0.4, 1.2, 4.2> right x*image_width/image_height angle 72 }
#declare TP = <2.8, 1.12, 4.3>;
#declare TA = <0, 1.4, 2.6>;
#declare TD = vnormalize(TA - TP);
light_source { TP + 0.02*TD, rgb Cool*3000/pi*Exposure spotlight point_at TA radius 4 falloff 9 tightness 0
  fade_power 2 media_attenuation on media_interaction on }
light_source { <2.0, 2.5, 2.5>, rgb Warm*15/pi*Exposure fade_power 2 media_attenuation on media_interaction on }
#declare F = finish { ambient 0 diffuse 1 specular 0 }
// walls only: the floor and ceiling are the boxes below, so each surface has one albedo
difference { box { <-0.2, -0.2, -0.2>, <4.2, 2.8, 5.2> } box { <0, -0.1, 0>, <4, 2.7, 5> } pigment { rgb 0.5 } finish { F } }
box { <-0.1, -0.1, -0.1>, <4.1, 0, 5.1> pigment { rgb 0.3 } finish { F } }
box { <-0.1, 2.6, -0.1>, <4.1, 2.7, 5.1> pigment { rgb 0.6 } finish { F } }
box { <2.45, 0, 3.95>, <3.2, 1.0, 4.7> pigment { rgb 0.25 } finish { F } }
box { <0, 0.75, 3.2>, <0.6, 0.79, 4.6> pigment { rgb 0.25 } finish { F } }
box { <0.5, 0, 3.25>, <0.55, 0.75, 3.3> pigment { rgb 0.25 } finish { F } }
box { <0.5, 0, 4.5>, <0.55, 0.75, 4.55> pigment { rgb 0.25 } finish { F } }
union {
  cylinder { TP - 0.20*TD, TP - 0.01*TD, 0.022 }
  cylinder { TP - 0.04*TD, TP - 0.005*TD, 0.03 }
  pigment { rgb 0.05 } finish { F }
}
// the torch lens, 20000 cd/m2
disc { TP - 0.004*TD, -TD, 0.026 no_shadow pigment { rgb Cool*Exposure } finish { ambient 0 diffuse 0 emission 20000 } }
// the smoke is the atmosphere (the room is closed), so it dims the torch on its way as well as the view of it
Haze(SIG, G)
