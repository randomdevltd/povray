#version 4.0;
#declare quality = 0.125;
global_settings { assumed_gamma 1 max_trace_level 8 photons { method 2 quality (quality * 1) } }
camera { location <0, 5, -8> look_at 0 right x*image_width/image_height angle 40 }
light_source { <-2, 6, -1> rgb 40 photon_only fade_power 2 fade_distance 0
    photons { refraction on reflection on } }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere { <0, 1, 0>, 1 pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { ior 1.5 } photons { target refraction on reflection off collect off } }
