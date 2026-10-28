// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Reuse the media's RGB emission field to distribute 12 lumens. +w400 +h300
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <0, 3, -6> look_at <0, 0.7, 0> right x * image_width/image_height angle 40 }
plane { y, 0 pigment { rgb 0.7 } finish { diffuse 0.8 } }
sphere {
  <0, 1, 0>, 0.5
  pigment { rgbt 1 } hollow
  interior {
    media { emission rgb <100, 40, 10> density { spherical } }
    light { emission brightness 12 lumens samples 64 }
  }
}
