// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Progressive (method 2) photons: a glass sphere focuses a photon-only point light into a caustic on the floor, beside a dim fill light that shoots none; the photon block sets its quality with an expression, and quality stays usable as an ordinary name. progressive_photons4.pov4 is the same scene in 4.0. +w640 +h480 +a0.3
#version 4.0;
#declare quality = 4;
global_settings { assumed_gamma 1 max_trace_level 8 photons { method 2 quality (quality * 2) } }
camera { location <0, 5, -8> look_at 0 right x*image_width/image_height angle 40 }
light_source { <-2, 6, 2> rgb 60 photon_only fade_power 2 fade_distance 0
    photons { refraction on reflection on } }
light_source { <3, 5, -4> rgb 0.06 photons { reflection off refraction off } }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
sphere { <0, 1, 0>, 1 pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { ior 1.5 } photons { target refraction on reflection off collect off } }
