#version 3.7;
#ifndef (SourceDepth) #declare SourceDepth = 0.35; #end
global_settings {
  assumed_gamma 1 mm_per_unit 1 max_trace_level 8
  subsurface { method 2 samples 4, 32 spacing 0.5 }
}
camera { orthographic location <0, 3, 0> look_at 0 right x * 0.01 up z * 0.01 }
background { rgb 0 }
// The method 2 albedo floor is 1e-4: sigma_s=1e-4/mm, sigma_t=1/mm.
box {
  <-1, -1, -1>, <1, 0, 1>
  pigment { rgb 0 }
  finish { ambient 0 diffuse 1 subsurface { translucency 10000 } }
  interior { ior 1 }
}
sphere {
  <0, -SourceDepth, 0>, 0.12
  hollow no_image pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
  interior {
    media { method 4 domain_cells 512 emission 0.5 }
    light { emission brightness 1 samples 16 attenuation { cutoff 0 } }
  }
}
