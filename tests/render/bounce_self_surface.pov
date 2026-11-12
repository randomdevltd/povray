// bounce_self_surface.pov: an open box lit inside, a surface bounce light with Bounce passes and Self its self share;
// Emit 1 gives it a light of its own, Subsurface 1 a method 2 finish, Photons 1 a mirror returning its light by
// progressive photons, Buried 1 a lamp inside its back wall whose light leaves through the wall as the box's emission;
// Scene 1 adds a floor before it, Mask 1 shows only the box, flat.
#version 3.7;
#ifndef (Bounce) #declare Bounce = 0; #end
#ifndef (Self) #declare Self = 1; #end
#ifdef (Tint) #declare Self = <0, 0.5, 1>; #end
#ifndef (Emit) #declare Emit = 0; #end
#ifndef (Subsurface) #declare Subsurface = 0; #end
#ifndef (Buried) #declare Buried = 0; #end
#ifndef (Photons) #declare Photons = 0; #end
#ifndef (Scene) #declare Scene = 0; #end
#ifndef (Mask) #declare Mask = 0; #end
#ifndef (Samples) #declare Samples = 4; #end
global_settings { assumed_gamma 1 max_trace_level 8
  #if (Subsurface) subsurface { method 2 samples 16, 4 spacing 0.5 } #end
  #if (Photons) photons { method 2 quality 0.25 } #end
}
camera { location <0.6, 1.6, -5> look_at <0, 0.2, 0> angle 50 }
background { rgb 0 }
#if (!Mask) light_source { <0.3, 0.6, -0.6>, rgb 0.4 fade_distance 1 fade_power 2 } #end
difference {
  box { <-1, -1, -1>, <1, 1, 1> }
  box { <-0.9, -0.9, -1.1>, <0.9, 0.9, 0.9> }
  #if (Mask) pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
  #else
    pigment { rgb <0.9, 0.85, 0.8> }
    finish { ambient 0 emission 0 diffuse 0.8 #if (Subsurface) subsurface { translucency 0.4 } #end }
    #if (Bounce > 0 | Emit | Buried)
      light {
        #if (Buried) emission #end
        #if (Emit) pigment { rgb <1, 0.6, 0.3> } brightness 0.1 #end
        samples Samples
        #if (Buried) domain_cells 32768 #end
        #if (Photons) photons { reflection on } #end
        #if (Bounce > 0) bounce { passes Bounce self Self } #end
      }
    #end
  #end
}
#if (Buried & !Mask)
  box { <-0.3, -0.3, 0.93>, <0.3, 0.3, 0.97> pigment { rgbt 1 } hollow
    interior { ior off light { pigment { rgb <1, 0.6, 0.3> } brightness 2 samples 4 } } }
#end
#if (Scene = 1)
  box { <-3, -1.1, -4>, <3, -1, -1.05>
    #if (Mask) pigment { rgb 0 } finish { ambient 0 emission 0 diffuse 0 }
    #else pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 } #end
  }
#end
#if (Photons & !Mask)
  box { <-1.2, -0.95, -2.2>, <1.2, 0.95, -2.18>
    pigment { rgb 0 } finish { ambient 0 diffuse 0 reflection 1 }
    photons { target reflection on collect off }
    no_image
  }
#end
