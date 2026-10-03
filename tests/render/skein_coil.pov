// skein_coil.pov: a coil spring, a tube extruded six times around x with a bend's shift that grows with v; Volume=N prints its volume.
#version 3.8;
#ifndef (Volume) #declare Volume = 0; #end
global_settings { assumed_gamma 1 }
#include "skein_volume.inc"

#declare Coil = skein {
  expressions {
    extrude { radius 0.08 }
    extrude { axis x  radius 0.6  arc 6*360 }
    bend { axis x  translate map { uv.v  linear { scale 0.6*pi } } }
  }
  closed u  ends flat
}
#if (Volume > 0)
  #debug concat("coil volume from ", str(Volume*Volume, 0, 0), " rays: ", str(SolidVolume(Coil, Volume), 0, 6), "\n")
#end

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 7, -6> rgb 1 }
light_source { <6, 2, -4> rgb 0.3 }
camera { location <0, 1.9, -6.2> look_at <0, 0.95, 0> angle 45 }
object {
  Coil
  pigment { rgb <0.7, 0.72, 0.75> }
  finish { phong 0.8 phong_size 80 reflection 0.15 metallic }
  rotate z*90
}
