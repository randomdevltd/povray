// A sunlit bounce floor among many small lights, lighting a wall the sun does not reach.
#version 3.8;
#ifndef (Lights) #declare Lights = 40; #end
#ifndef (Samples) #declare Samples = 16; #end
// Cells=N sets the floor's node budget; without it the floor takes the renderer's default.
#ifdef (Cells) #default { light { domain_cells Cells } } #end
global_settings { assumed_gamma 1 ambient_light 0 }
background { rgb 0 }
camera { location <0, 1.0, -2.5> look_at <0, 0.6, 1> angle 64 right x * image_width / image_height }
light_source { <-1, 2, 0.3> * 1000, rgb 1 parallel point_at 0 }
#declare R = seed(11);
#for (K, 1, Lights)
  sphere { <5 * rand(R) - 2.5, 0.1 + 0.6 * rand(R), 2.2 * rand(R) - 2>, 0.015 hollow no_image
    pigment { rgbt 1 } finish { ambient 0 diffuse 0 }
    interior { light { brightness 40 samples 4 attenuation { cutoff 0 } } } }
#end
box { <-3, -0.05, -3>, <3, 0, 1> pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } light { bounce samples Samples } }
box { <-3, 0, 1>, <3, 2, 1.05> pigment { rgb 0.8 } finish { ambient 0 diffuse 1 } }
