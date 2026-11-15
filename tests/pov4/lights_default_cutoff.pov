// lights_default_cutoff: classic twin of lights_default_cutoff.pov4
#version 4.0;
#default { light_source { fade_cutoff 0.01 } light { attenuation { cutoff 0 } } interior { light { attenuation { cutoff 0.001 } } } }
global_settings { assumed_gamma 1.0 }
camera { location <0, 2, -4> look_at <0, 0, 4> }
plane { y, 0 pigment { color rgb 0.8 } finish { ambient 0 diffuse 1 } }
light_source { <0, 3, -2>, rgb 1 fade_distance 1 fade_power 2 }
light_source { <40, 3, 40>, rgb 1 fade_distance 1 fade_power 2 }
light_source { <2, 3, 0>, rgb 0.5 fade_distance 1 fade_power 2 fade_cutoff 0 }
sphere { <0, 1, 1>, 0.25 pigment { rgbt 1 } no_image interior { light { pigment { rgb 50 } samples 4 } } }
box { <-1, 2, 2>, <-0.5, 2.1, 2.5> pigment { rgb 1 } finish { ambient 0 diffuse 0 } no_image light { brightness 20 samples 4 } }
