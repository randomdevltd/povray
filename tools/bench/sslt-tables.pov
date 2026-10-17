// Method 2's tables under load: Case 0 a thin wedge whose colour varies along it and depth across it, so nearly every
// shading point needs layer tables of its own; Case 1 a sphere whose index varies across it by media refraction. See doc/PERF.md.
#version 4.0;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Refraction) #declare Refraction = 0.2; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}
camera { location <0, 3, -2.2> look_at <0, -0.1, 0> angle 45 right x * image_width / image_height }
light_source { <-2, 6, -3> rgb 1.2 }
background { rgb <0.2, 0.25, 0.3> }

#if (Case = 0)
intersection {
  box { <-1.2, -1, -1>, <1.2, 0, 1> }
  plane { -y, 0.02 rotate x * 21 translate z * -1 }
  pigment { gradient x color_map { [0 rgb <0.3, 0.15, 0.08>] [1 rgb <0.95, 0.9, 0.8>] } scale 2.4 translate x * -1.2 }
  finish { ambient 0 diffuse 0.8 subsurface { translucency <2, 1.6, 1.2> } }
  interior { ior 1.45 }
}
#else
sphere {
  0, 0.8
  pigment { rgb <0.9, 0.7, 0.5> }
  finish { ambient 0 diffuse 0.8 subsurface { translucency <1, 0.8, 0.6> } }
  interior { ior 1.4 media { refraction Refraction density { gradient x scale 1.6 translate x * -0.8 } } }
}
#end
