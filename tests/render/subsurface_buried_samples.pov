// A volume light buried in a method 2 box, seen from above. The buried light's internal paths are the receiver's
// draws (the light's samples times the finish's light_samples), so the second subsurface count, which sets single
// scattering only, cannot switch the glow off: Single=0 must light the box exactly as a nonzero count does.
#version 3.7;
#ifndef (Single) #declare Single = 0; #end
#ifndef (LightSamples) #declare LightSamples = 1; #end
global_settings { assumed_gamma 1 mm_per_unit 1 subsurface { method 2 samples 16, Single } }
camera { orthographic location <0, 3, -0.001> look_at 0 right x * 2.4 up z * 1.8 }
background { rgb 0 }
box { <-1, -1, -1>, <1, 0, 1>
  pigment { rgb 0.8 }
  finish { ambient 0 diffuse 1 light_samples LightSamples subsurface { translucency 0.5 } }
  interior { ior 1.4 }
}
sphere { <0, -0.35, 0>, 0.12
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior { light { brightness 0.05 samples 8 attenuation { cutoff 0 } } }
}
