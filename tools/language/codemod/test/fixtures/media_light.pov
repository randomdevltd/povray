// A classic scene whose media carry light_source blocks: the conversion moves each to its interior.
#version 3.8;
global_settings { assumed_gamma 1 }
camera { location <0, 3, -6> look_at <0, 1, 0> }
plane { y, 0 pigment { rgb 0.8 } }
sphere {
  0, 0.5 pigment { rgbt 1 } hollow
  interior {
    media {
      method 4
      emission rgb <14, 6, 1.6> * 200
      density { spherical color_map { [0 rgb 0] [1 rgb 1] } }
      light_source { samples 32 brightness 2 fade_power 2 fade_distance 1 shadowless }
    }
  }
  translate y
}
