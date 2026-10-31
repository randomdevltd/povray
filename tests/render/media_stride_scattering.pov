#version 3.8;
// A constant fog sharing a finely stepped medium: its single scattering is linear in its density.
// LightSamples, when declared, is the fog's light_samples.
#ifndef (Fog) #declare Fog = 0.005; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0,0,-5> look_at 0 right x*2 up y*1.5 }
box { -1, 1 hollow pigment { rgbt 1 }
  interior {
    media { method 4 absorption rgb 0.02 density { function { 0.5 + 0.5 * sin(40 * x) * sin(40 * y) } } }
    #if (Fog > 0) media { method 4 scattering { 1, rgb Fog } #ifdef (LightSamples) light_samples LightSamples #end } #end
  }
}
sphere { <0, 2, 0>, 0.3 hollow no_image pigment { rgbt 1 } interior { light { brightness 0.001 samples 1 } } }
light_source { <0, 0, -3> rgb 30 }
