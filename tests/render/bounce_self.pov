// bounce_self.pov: a lit cloud with Bounce passes, Self 0 keeping its bounce off its own media, Emit a light of its
// own; Scene 1 adds a second cloud and a floor, Mask 1 shows only the floor, flat.
#version 3.8;
#ifndef (Bounce) #declare Bounce = 0; #end
#ifndef (Self) #declare Self = 1; #end
#ifndef (Emit) #declare Emit = 0; #end
#ifndef (Scene) #declare Scene = 0; #end
#ifndef (Mask) #declare Mask = 0; #end
#ifndef (Samples) #declare Samples = 4; #end
global_settings { assumed_gamma 1 }
camera { location <0, 1.5, -6> look_at <0, 1.5, 0> angle 60 }
background { rgb 0 }
#if (!Mask) light_source { <-0.5, 5, -2>, rgb 6 fade_distance 1 fade_power 2 } #end
#macro Cloud(Centre, Light)
  box { -0.7, 0.7 pigment { rgbt 1 } hollow
    interior {
      media { method 4
        scattering { 1, rgb 2 } absorption rgb 0.2
        density { spherical turbulence 0.2 color_map { [0 rgb 0] [0.4 rgb 1] [1 rgb 1.5] } scale 0.7 }
      }
      #if (Light)
        light {
          #if (Emit) pigment { rgb <1, 0.6, 0.3> } brightness 0.5 #end
          samples Samples
          #if (Bounce > 0) bounce { passes Bounce self Self } #end
        }
      #end
    }
    translate Centre
  }
#end
#declare Lit = (Bounce > 0 | Emit);
#if (!Mask)
  Cloud(<-1.4, 1.7, 0>, Lit)
  #if (Scene = 1) Cloud(<1.4, 1.7, 0>, 0) #end
#end
#if (Scene = 1)
  box { <-4, -0.1, -4>, <4, 0, 3>
    #if (Mask) pigment { rgb 1 } finish { ambient 0 emission 1 diffuse 0 }
    #else pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 } #end
  }
#end
