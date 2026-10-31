// Texture filter test: a checker of bumpy and flat tiles running to the horizon, with a sharp specular highlight.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { perspective location <0, 1, -4> look_at <0, 0.6, 10> right x * image_width / image_height }
background { color rgb 0 }
light_source { <-3, 4, 30> color rgb 1.5 }
light_source { <6, 8, -6> color rgb 0.6 }
plane { y, 0
  pigment { color rgb 0.5 }
  normal { checker normal { bumps 1.2 scale 0.06 } normal { bumps 0 } scale 0.25 }
  finish { ambient 0 diffuse 0.6 specular 0.8 roughness 0.01 }
}
