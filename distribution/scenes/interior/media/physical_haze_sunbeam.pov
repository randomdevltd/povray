// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A sunbeam through a window into dusty air, in real units: 1 scene unit = 1 m, light colours in cd/pi (or lux/pi) so a white diffuse surface reads in cd/m2. +w640 +h427
// Room 5 x 6 m and 3 m high, window 1.6 x 1.5 m. Dust: sigma_s = 0.03 per m, non-absorbing (albedo 1), Henyey-Greenstein g = 0.6.
// Sun: parallel light of 100,000 lux normal to the beam, 35 degrees above the horizon. The sky seen through the window is 4000 cd/m2.
// Fill (stands in for sky and bounce light): a 1500 cd point light in the ceiling (about 180 lux on the floor). Albedos: floor 0.3, walls 0.6, ceiling 0.7.
// The dust is an object medium that fills the room: it dims the view but not the sun, which across the 6 m room would lose e^-0.18 = 16%.
// Declare=Look=0 physical method 4 (default), 1 classic method 3, 2 method 4 with 4x scattering and extinction 0.25 (the classic look),
// 3 absorption only. Declare=Exposure=E scales every colour: 0.0004 by default; 1 renders cd/m2 with +FH.
#version 3.8;
#ifndef (Look) #declare Look = 0; #end
#ifndef (Exposure) #declare Exposure = 0.0004; #end
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
#declare SIG = 0.03;
#declare G = 0.6;
#declare SunC = <1, 0.95, 0.88> / (0.2126 + 0.7152*0.95 + 0.0722*0.88);
#declare Warm = <1, 0.85, 0.65> / 0.868;
global_settings { assumed_gamma 1.0 ambient_light 0 max_trace_level 8 }
background { rgb <0.6, 0.75, 1.0> / (0.2126*0.6 + 0.7152*0.75 + 0.0722) * 4000 * Exposure }
camera { perspective location <4.6, 1.5, 5.6> look_at <1.0, 1.1, 2.6> right x*image_width/image_height angle 74 }
#declare SD = vnormalize(<1, -0.75, 0.35>);
light_source { <0.0, 1.65, 2.8> - 60*SD, rgb SunC*100000/pi*Exposure parallel point_at <0.0, 1.65, 2.8> media_attenuation on media_interaction on }
light_source { <2.5, 2.85, 3.0>, rgb Warm*1500/pi*Exposure fade_power 2 media_attenuation on media_interaction on }
#declare F = finish { ambient 0 diffuse 1 specular 0 }
// walls only, with the window cut out: the floor and ceiling are the boxes below, so each surface has one albedo
difference {
  box { <-0.25, -0.2, -0.2>, <5.2, 3.2, 6.2> } box { <0, -0.1, 0>, <5, 3.1, 6> }
  box { <-0.3, 0.9, 2.0>, <0.01, 2.4, 3.6> }
  pigment { rgb 0.6 } finish { F }
}
union {
  box { <-0.18, 0.9, 2.77>, <-0.12, 2.4, 2.83> }
  box { <-0.18, 1.62, 2.0>, <-0.12, 1.68, 3.6> }
  pigment { rgb 0.6 } finish { F }
}
box { <-0.1, -0.1, -0.1>, <5.1, 0, 6.1> pigment { rgb 0.3 } finish { F } }
box { <-0.1, 3.0, -0.1>, <5.1, 3.1, 6.1> pigment { rgb 0.7 } finish { F } }
box { <3.6, 0, 0.3>, <4.9, 0.8, 1.0> pigment { rgb 0.35 } finish { F } }
// the medium reaches into the walls, so every surface is inside it and light that reaches it is dimmed on the way
box { <-0.1, -0.1, -0.1>, <5.1, 3.1, 6.1> hollow pigment { rgbt 1 } finish { ambient 0 diffuse 0 } interior { Haze(SIG, G) } }
