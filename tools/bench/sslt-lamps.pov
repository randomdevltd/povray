// Subsurface wax and marble under two soft area suns, a spotlight and two lamps; see doc/PERF.md.
#version 3.7;
#ifndef (Diffuse) #declare Diffuse = 74; #end
#ifndef (Single) #declare Single = 21; #end
#ifndef (Subsurface) #declare Subsurface = 1; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 40
  #if (Subsurface) subsurface { samples Diffuse, Single } #end
}

camera { perspective location <0, 3.2, -6.5> look_at <0, 1.1, 0> angle 48 right x * 4 / 3 }

light_source {
  <-300, 380, -150>, rgb <1.0, 0.85, 0.7> * 1.6
  parallel point_at <0, 0, 0>
  area_light <8, 0, 0>, <0, 0, 8>, 9, 9
  adaptive 1 jitter circular orient
}
light_source {
  <260, 300, 240>, rgb <0.6, 0.7, 1.0> * 0.7
  parallel point_at <0, 0, 0>
  area_light <8, 0, 0>, <0, 0, 8>, 9, 9
  adaptive 1 jitter circular orient
}
light_source { <1.8, 3.5, -1.5>, rgb <1, 0.6, 0.3> * 0.8 spotlight point_at <0, 1, 0> radius 20 falloff 35 }
light_source { <-2.5, 0.6, -1.2>, rgb <1, 0.8, 0.5> * 0.5 fade_distance 1.5 fade_power 2 }
light_source { <2.2, 0.5, 1.8>, rgb <0.5, 0.8, 1> * 0.5 fade_distance 1.5 fade_power 2 }

sky_sphere { pigment { gradient y color_map { [0 rgb <0.6, 0.7, 1.0>] [0.7 rgb <0.1, 0.2, 0.7>] } } }

plane { y, 0 pigment { checker rgb 0.5, rgb 0.35 scale 0.7 } }

#for (I, -8, 8)
  box { <I * 0.5 - 0.1, 4.2, -6>, <I * 0.5 + 0.1, 4.3, 6> pigment { rgb 0.3 } }
#end

#declare Wax = texture {
  pigment { rgb <0.85, 0.55, 0.12> }
  normal { wrinkles 0.4 scale 0.15 }
  finish { diffuse 0.6 specular 0.5 roughness 0.08 subsurface { translucency <5, 3, 1> * 0.5 } }
}
#declare Marble = texture {
  pigment { agate color_map { [0.5 rgb <0.83, 0.79, 0.75>] [0.9 rgb <0.66, 0.63, 0.6>] [1.0 rgb <0.5, 0.38, 0.35>] } scale 0.4 }
  normal { granite 0.15 scale 0.3 }
  finish { diffuse 0.8 specular 0.5 subsurface { translucency <0.4562, 0.3811, 0.3325> } }
}

blob {
  threshold 0.5
  cylinder { <0, 0, 0>, <0, 2, 0>, 0.7, 1 }
  sphere { <0, 2.4, 0>, 0.6, -2 }
  sphere { <0, 1.9, -0.36>, 0.08, -0.2 }
  cylinder { <0, 1.8, -0.38>, <0, 1.2, -0.38>, 0.05, 0.2 }
  texture { Wax }
  interior { ior 1.45 }
  translate <-0.9, 0, 0.3>
}
box { <-0.9, -0.35, -0.7>, <0.9, 0.35, 0.7> translate <1.2, 0.35, 0.2> texture { Marble } interior { ior 1.5 } }
sphere { <1.1, 1.1, 0.4>, 0.4 texture { Wax } interior { ior 1.45 } }
torus { 0.5, 0.18 rotate x * 70 translate <0.1, 0.55, -1.2> texture { Marble } interior { ior 1.5 } }
