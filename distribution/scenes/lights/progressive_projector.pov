// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// Progressive (method 2) photons from an oriented circular area light behind the scene, shining through a projected_through slot that admits only part of its beam; the glass sphere focuses that part into a caustic in front of it. +w640 +h480 +a0.3
#version 3.8;
global_settings { assumed_gamma 1 max_trace_level 10 photons { method 2 quality 4 } }
camera { location <0, 5, -8> look_at <0, 0.5, 0> right x*image_width/image_height angle 42 }
background { rgb <0.03, 0.04, 0.06> }
light_source { <-4, 5, -4> rgb 0.2 photons { reflection off refraction off } }
plane { y, 0 pigment { rgb <0.8, 0.85, 0.9> } finish { ambient 0 diffuse 1 } }
light_source {
    <-3, 6, 3> rgb <8, 5, 2> photon_only fade_power 2 fade_distance 0
    area_light 0.6*x, 0.6*z, 5, 5 circular orient
    projected_through { box { <-2.5, 3.5, 0.5>, <-1.3, 3.6, 2.5> } }
    photons { reflection on refraction on }
}
sphere {
    y, 0.9 pigment { rgbt 1 }
    finish { ambient 0 diffuse 0 reflection { 0, 1 fresnel } conserve_energy }
    interior { ior 1.5 }
    photons { target reflection on refraction on }
}
