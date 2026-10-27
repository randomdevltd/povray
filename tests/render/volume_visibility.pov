// Every opaque crossing before a sampled source point blocks it, including its own enclosing shell.
#version 3.8;
#ifndef (Kind) #declare Kind = 0; #end
#ifndef (Opaque) #declare Opaque = 0; #end
#ifndef (Blocker) #declare Blocker = 0; #end
#ifndef (SceneScale) #declare SceneScale = 1; #end
#ifndef (Cache) #declare Cache = 2; #end
global_settings { assumed_gamma 1 }
camera {
  orthographic location <0, 1, 0> sky z look_at 0
  right x * 0.16 * SceneScale up z * 0.16 * SceneScale
}
plane { y, 0 pigment { rgb 1 } finish { ambient 0 emission 0 diffuse 1 } }
union {
  #if (Kind = 0)
    sphere { <0, 0.12, 0>, 0.03 }
  #else
    box { <-0.025, 0.09, -0.025>, <0.025, 0.0902, 0.025> }
  #end
  pigment { rgbt <1, 1, 1, 1 - Opaque> } finish { ambient 0 emission 0 diffuse 0 }
  hollow no_image
  interior { light { brightness 0.05 lumens samples 1024 fade_cutoff 0 shadows method Cache } }
  scale SceneScale
}
#if (Blocker)
  box { <-1, 0.08969, -1>, <1, 0.08970, 1> pigment { rgb 0 } no_image scale SceneScale }
#end
