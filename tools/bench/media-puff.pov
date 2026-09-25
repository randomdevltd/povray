// A small dense puff whose floor shadow comes only from shadow-ray media; a coarse transmittance rule steps over it.
#version 3.7;
#ifndef (Samples) #declare Samples = 40; #end
#ifndef (Method) #declare Method = 2; #end
#ifndef (Height) #declare Height = 2.2; #end
global_settings { assumed_gamma 1.0 }
camera { perspective location <0, 3, -8> look_at <0, 1.5, 0> angle 40 right x * 4 / 3 }
light_source { <-300, 400, 200>, rgb 1.5 parallel point_at <0, 0, 0> }
plane { y, 0 pigment { rgb 0.8 } }
box {
  <-24, -1, -24>, <24, 12, 24>
  hollow
  pigment { rgbt 1 }
  interior {
    media {
      absorption 30
      density { spherical color_map { [0 rgb 0] [0.35 rgb 0] [0.36 rgb 1] [1 rgb 1] } scale 0.9 translate <0, Height, 0> }
      method Method
      intervals 1
      samples Samples
      aa_level 3
      aa_threshold 0.1
    }
  }
}
