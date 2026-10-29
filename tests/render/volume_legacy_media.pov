#version 3.8;
// A volume light seen through a classic absorbing medium beside a classic glow: both stay legal with light blocks.
#ifndef (Interaction) #declare Interaction = 1; #end
#ifndef (Scatter) #declare Scatter = 0; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 0, -5> look_at 0 right x * 4 up y * 4 }
background { rgb 0 }
sphere {
  <-1, 0, 0>, 0.6 hollow pigment { rgbt 1 }
  interior { media { method 4 absorption 0.01 } light { brightness 4 samples 16 media_interaction Interaction } }
}
box {
  <-2, -2, -1.5>, <2, 2, -1> hollow pigment { rgbt 1 }
  interior { media { method 3 absorption 0.4 scattering { 1, Scatter } } }
}
sphere {
  <1, 0, 0>, 0.6 hollow pigment { rgbt 1 }
  interior { media { method 2 emission 0.5 } }
}
plane { z, 1 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
