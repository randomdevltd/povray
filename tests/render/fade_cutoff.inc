// fade_cutoff.inc: a volume light near a floor and one far from it, seen from above; fade_cutoff.sh names the cases.
#ifndef (Far) #declare Far = 1; #end
#ifndef (Fade) #declare Fade = 0; #end
#declare Radius = 0.25;
#declare Brightness = 16;

global_settings { assumed_gamma 1 }
camera { orthographic location <0, 10, 0> sky z look_at 0 right x * 4 up z * 4 }
background { rgb 0 }
box { <-2, -1, -2>, <2, 0, 2> pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }

#macro EmittingBall(Where)
  sphere {
    0, Radius pigment { rgbt 1 } #if (version < 4.0) hollow #end no_image
    interior { light {
      brightness Brightness samples 8
      #if (Fade) attenuation 4 #end
      #ifdef (Cutoff) fade_cutoff Cutoff #end
    } }
    translate Where
  }
#end

#ifndef (DefaultLast) #declare DefaultLast = 0; #end
#macro SetDefault() #default { interior { light { fade_cutoff DefaultCutoff } } } #end
#ifdef (DefaultCutoff) #if (!DefaultLast) SetDefault() #end #end
#ifdef (Switch) #include "fade_cutoff_switch.inc" #end
EmittingBall(<-0.5, 1, 0.5>)
#switch (Far)
  #case (1) EmittingBall(<30, 1, 26>) #break
  #case (2) EmittingBall(<1500, 1, 1500>) #break
#end
#ifdef (DefaultCutoff) #if (DefaultLast) SetDefault() #end #end
