#version 3.7;
// A volume light touching the inner face of a subsurface membrane, seen through that face.
#ifndef (Samples) #declare Samples = 64; #end
#ifndef (SingleSamples) #declare SingleSamples = 32; #end
global_settings {
  assumed_gamma 1 max_trace_level 8 mm_per_unit 10
  subsurface { method 2 samples Samples, SingleSamples }
}
camera { location <0, 0, -3.2> look_at 0 angle 40 }
background { rgb 0 }
difference {
  sphere { 0, 1 }
  sphere { 0, 0.9 }
  pigment { rgb <0.9, 0.7, 0.6> }
  finish { ambient 0 diffuse 1 subsurface { translucency <0.8, 0.5, 0.4> } }
  interior { ior 1.33 }
}
sphere {
  <0, 0, -0.6>, 0.3
  hollow no_image pigment { rgbt 1 }
  interior { light { brightness 2000 lumens samples 16 } }
}
