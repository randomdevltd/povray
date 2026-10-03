// skein_knot.pov: the (2, 3) torus knot as a straight tube bent along a curve; Volume=N prints its volume from N x N rays.
#version 3.8;
#ifndef (Volume) #declare Volume = 0; #end
global_settings { assumed_gamma 1 }
#include "skein_volume.inc"

#declare A = function(s) { 2*pi*s }
#declare K = function(s) { 2 + cos(3*A(s)) }
// a bend keeps the material's length, so the tube is as long as the knot: its speed is 2 pi sqrt(9 + 4 K squared)
#declare Speed = function(s) { 2*pi*sqrt(9 + 4*pow(K(s), 2)) }
#declare Length = 0;
#declare Panel = 0;
#while (Panel < 32)
  #declare Length = Length + (Speed(Panel/32) + 4*Speed((Panel + 0.5)/32) + Speed((Panel + 1)/32))/192;
  #declare Panel = Panel + 1;
#end
#declare Knot = skein {
  expressions {
    scale <1, Length, 1>
    extrude { radius 0.05 }
    bend {
      axis y
      along function(t) { K(t)*cos(2*A(t)) }, function(t) { K(t)*sin(2*A(t)) }, function(t) { -sin(3*A(t)) }
    }
  }
  closed uv
}

#if (Volume > 0)
  #debug concat("knot volume from ", str(Volume*Volume, 0, 0), " rays: ", str(SolidVolume(Knot, Volume), 0, 6), "\n")
#end

background { rgb <0.93, 0.92, 0.88> }
light_source { <-4, 8, -10> rgb 1 }
light_source { <6, 3, -6> rgb 0.35 }
camera { location <0, 2, -10.5> look_at <0, 0, 0> angle 45 }
object {
  Knot
  pigment { rgb <0.75, 0.2, 0.15> }
  finish { phong 0.5 phong_size 40 }
}
