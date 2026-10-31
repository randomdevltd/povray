// framing: classic twin of framing.pov4; Reference=1 is the explicit camera it frames to
#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
global_settings { assumed_gamma 1.0 }
light_source { <4, 6, -3> rgb 1 }
plane { y, -1.5 pigment { checker color rgb 0.8 color rgb <0.25, 0.3, 0.4> } }
#declare Subject = union {
  box { -1, <0, 1, 1> pigment { color rgb <0.2, 0.5, 0.9> } }
  sphere { x * 0.5, 1 - 0.5 pigment { color rgb <0.9, 0.3, 0.2> } }
}
object { Subject }
#declare Yaw = (atan2(-1, 9) + atan2(1, 9.5)) / 2;
#if (Reference) camera { location <0, 0, -10> direction 3.6 * cos(Yaw) * z
                        right x * 4 / 3 up y look_at <sin(Yaw), 0, -10 + cos(Yaw)> }
#else camera { location <0, 0, -10> right x * 4 / 3 up y look_at Subject, 0.8 } #end
