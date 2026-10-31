// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A turntable from one render: a loop makes a camera every 30 degrees around the object, each with a bare output, so one parse writes twelve frames, turntable-01.png to turntable-12.png; turntable.pov4 is the same scene in 4.0. +w320 +h240
#version 3.7;
global_settings { assumed_gamma 1.0 }

#include "colors.inc"
#default { finish { ambient 0.1 } }

#for (Angle, 0, 330, 30)
  camera { location vrotate(<0, 2, -6>, y * Angle) right x * image_width / image_height look_at <0, 1, 0> angle 40 output }
#end

light_source { <-4, 8, -6> color White }
plane { y, 0 pigment { checker color Gray80 color Gray40 } }
lathe {
  cubic_spline 7, <0.3, -0.1>, <0.3, 0>, <0.9, 0.6>, <0.5, 1.6>, <0.4, 2.2>, <0.6, 2.4>, <0.6, 2.5>
  pigment { color rgb <0.9, 0.8, 0.5> } finish { phong 0.8 reflection 0.1 }
}
box { <0.9, 0, -0.3>, <1.5, 0.6, 0.3> pigment { color rgb <0.2, 0.5, 0.8> } }
