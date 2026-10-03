// skein_donut.pov: a torus about x, iced where its normal faces along that axis (norm.x) and stood up by the scene, with sprinkles from a cell pattern of the point (pos); Volume=N prints its volume, Sprinkle=0 drops them.
#version 3.8;
#ifndef (Volume) #declare Volume = 0; #end
#ifndef (Sprinkle) #declare Sprinkle = 1; #end
global_settings { assumed_gamma 1 }
#include "skein_volume.inc"

#declare Sprinkles = function { pattern { crackle form <1, 0, 0> } }
#declare Donut = skein {
  expressions {
    extrude { radius 0.45 }
    extrude { axis x  radius 1.2 }
    displace function(norm, pos) {
      min(1, max(0, (norm.x - 0.1)*5)) * (0.04 + Sprinkle*0.025*max(0, 1 - pow(Sprinkles(8*pos.x, 8*pos.y, 8*pos.z)/0.16, 2)))
    }
  }
  closed uv
}
#if (Volume > 0)
  #debug concat("donut volume from ", str(Volume*Volume, 0, 0), " rays: ", str(SolidVolume(Donut, Volume), 0, 6), "\n")
#end

background { rgb <0.93, 0.92, 0.88> }
light_source { <-3, 6, -5> rgb 1 }
light_source { <5, 2, -3> rgb 0.3 }
camera { location <0, 3.4, -3.4> look_at <0, -0.15, 0> angle 45 }
// why: a sprinkle is coloured where the surface stands further from the tube's centre circle than the icing does
#declare Lift = function { sqrt(pow(sqrt(y*y + z*z) - 1.2, 2) + x*x) }
object {
  Donut
  texture {
    pigment {
      function { Lift(x, y, z) }
      pigment_map {
        [0.4965 gradient x  color_map { [0.52 rgb <0.72, 0.45, 0.2>] [0.53 rgb <0.95, 0.55, 0.7>] }  translate -0.5*x]
        [0.4965 cells  color_map { [0.33 rgb <0.2, 0.5, 0.95>] [0.33 rgb <0.98, 0.85, 0.2>] [0.66 rgb <0.98, 0.85, 0.2>] [0.66 rgb 0.97] }  scale 0.03]
      }
    }
    finish { phong 0.4 }
  }
  rotate z*90
}
