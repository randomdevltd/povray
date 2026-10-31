// Texture filter test: minified bumps with iridescence and an angle-dependent reflection of a checker wall, on a plane to the horizon.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { perspective location <0, 1, -4> look_at <0, 0.6, 10> right x * image_width / image_height }
background { color rgb <0.2, 0.3, 0.6> }
light_source { <-3, 4, 30> color rgb 1.5 }
light_source { <6, 8, -6> color rgb 0.6 }
plane { y, 0
  pigment { color rgb 0.4 }
  normal { bumps 0.8 scale 0.06 }
  finish { ambient 0 diffuse 0.5 irid { 0.6 thickness 0.4 } reflection { 0.05, 0.8 falloff 2 } }
}
plane { -z, -40
  pigment { checker color rgb <0.9, 0.8, 0.2> color rgb <0.1, 0.2, 0.3> scale 2 }
  finish { ambient 1 diffuse 0 }
}
