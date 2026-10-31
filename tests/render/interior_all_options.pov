#version Version;

global_settings { assumed_gamma 1 }
camera { location <0, 2, -4> look_at <0, 1, 0> right x*4/3 }
plane { y, 0 pigment { rgb 0.6 } finish { ambient 0 diffuse 0.8 } }

sphere {
  0, 1
  hollow
  pigment { rgbt 1 }
  finish { ambient 0 diffuse 0 }
  interior {
    ior 1.0003
    light {
      pigment { rgb <1, 0.8, 0.5> }
      brightness 806 lumens // Alternatives: brightness 100 nits; brightness 100 (also nits).
      samples 256
      attenuation 2, 1
      shadowless off
      media_interaction on
      media_attenuation on
      photons { reflection on refraction on } // Requires separate global photon configuration.
    }
    refraction {
      function { -0.0003*max(0, 1-x*x-y*y-z*z) }
      detail 0.02
    }
    media {
      method 4
      resolution 0.025
      scattering { 1, rgb 0.02 }
      absorption rgb 0.005
    }
  }
  scale 0.25
  translate <0, 1, 0>
}
