// The transparent medium occupies the right half so its blocks should dominate a timing heat map.
#version 3.7;
global_settings { assumed_gamma 1.0 }
camera { location <0, 0, -6> look_at 0 angle 55 right x * 2 }
light_source { <-4, 7, -5> rgb 1.5 }
plane { z, 2 pigment { checker rgb 0.2 rgb 0.8 scale 0.25 } }
box {
  <0, -3, -1>, <6, 3, 2>
  hollow
  pigment { rgbt 1 }
  interior {
    media {
      emission <0.15, 0.22, 0.4>
      scattering { 1, 0.12 }
      density { bozo turbulence 0.7 colour_map { [0 rgb 0.1] [1 rgb 1] } scale 0.35 }
      method 3 intervals 1 samples 80 jitter 0
    }
  }
}
