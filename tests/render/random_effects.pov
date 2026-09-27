// Every random effect at once; tests/render/same_image.sh renders it several ways and expects identical files.
#version 3.7;
global_settings { assumed_gamma 1 max_trace_level 6
  photons { spacing 0.05 jitter 0.4 }
  subsurface { samples 12, 6 } mm_per_unit 20 }
camera { location <0, 3, -7> look_at <0, 0.8, 0> angle 55 }
light_source { <-4, 7, -3> color 0.9 area_light <2, 0, 0>, <0, 0, 2>, 6, 6 adaptive 1 jitter circular orient
               photons { refraction on reflection on } }
light_source { <5, 6, -6> color 0.4 area_light <1, 0, 0>, <0, 1, 0>, 3, 3 adaptive 0 jitter area_illumination on }
rainbow { angle 42.5 width 5 distance 1e3 direction <-0.2, -0.2, 1> jitter 0.05 arc_angle 120 falloff_angle 30
  color_map { [0 color rgbt <1,0,0,1>] [0.3 color rgbt <1,0.6,0,0.6>] [0.6 color rgbt <0,1,0,0.6>] [1 color rgbt <0,0,1,1>] } }
background { color rgb <0.3, 0.4, 0.6> }
plane { y, 0 pigment { checker color rgb 0.8 color rgb 0.6 scale 0.7 } finish { ambient 0 diffuse 0.8 crand 0.15 } }
sphere { <-1.6, 1, 0>, 1 pigment { color rgbf <1, 1, 1, 0.95> } interior { ior 1.5 }
         finish { ambient 0 reflection 0.1 specular 0.6 } photons { target reflection on refraction on } }
box { <0.4, 0, -0.6>, <1.6, 1.6, 0.6> hollow pigment { color rgbt 1 }
      interior { media { scattering { 1, 0.6 } absorption 0.2 samples 3 jitter 1 method 3 } } }
box { <0, 0, 2.5>, <5, 2.2, 2.7> rotate y * -20 pigment { color rgb 0.7 } finish { ambient 0 } }
sphere { <2.6, 0.7, -1.5>, 0.7 pigment { color rgb <0.9, 0.6, 0.5> }
         finish { ambient 0 diffuse 0.7 subsurface { translucency <1.5, 0.8, 0.5> } } }
