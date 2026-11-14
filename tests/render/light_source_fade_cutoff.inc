// light_source_fade_cutoff.inc: faded light_sources near a floor and far from it, seen from above; the .sh names the cases.
#ifndef (Far) #declare Far = 1; #end
#ifndef (Fade) #declare Fade = 1; #end
#ifndef (FadeDistance) #declare FadeDistance = 1; #end
#ifndef (Area) #declare Area = 0; #end
#ifndef (Surface) #declare Surface = 0; #end

#ifdef (DefaultCutoff) #default { light_source { fade_cutoff DefaultCutoff } } #end
#ifdef (DefaultLightCutoff) #default { light { attenuation { cutoff DefaultLightCutoff } } } #end
#ifdef (DefaultInteriorCutoff) #default { interior { light { attenuation { cutoff DefaultInteriorCutoff } } } } #end

global_settings { assumed_gamma 1 }
camera { orthographic location <0, 10, 0> sky z look_at 0 right x * 4 up z * 4 }
background { rgb 0 }
box { <-2, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
sphere { <0.3, 0.4, -0.2>, 0.4 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }

#macro Lamp(Where, Colour, Size)
  light_source { Where, Colour
    #if (Fade) fade_distance FadeDistance fade_power 2 #end
    #if (Area) area_light x * Size, z * Size, 3, 3 #end
    #ifdef (Cutoff) fade_cutoff Cutoff #end
  }
#end

// near lamps: within their reach at every floor point for every cutoff the script uses
Lamp(<-1, 1.5, -1>, rgb <0.4, 0.3, 0.2>, 0.5)
Lamp(< 1, 1.5,  1>, rgb <0.2, 0.3, 0.4>, 0.5)
#if (Far)
  // far lamps, about 40 away: near 0.001 each with fade, beyond the reach of a 0.01 cutoff
  #declare FarColour = (Fade ? 1 : 0.001);
  #for (I, 0, 3)
    Lamp(<30 + 2 * I, 5, 26 - 3 * I>, rgb FarColour, 2)
  #end
#end
#if (Surface)
  box { <29, 5, 25>, <30, 5.2, 26> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 0 } no_image
        light { brightness 400 samples 4 #ifdef (SurfaceCutoff) attenuation { cutoff SurfaceCutoff } #end } }
#end
