// Anti-aliasing method 4 test: an emissive disc on a dark background.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { orthographic location <0, 0, -1> direction z right x * image_width up y * image_height }
background { color rgb <0, 0, 0.2> }
disc { 0, -z, image_height * 0.37 pigment { color rgb <1, 0.8, 0.3> } finish { ambient 0 diffuse 0 emission 1 } }
