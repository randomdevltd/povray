// Texture filter test: a fine checker plane running to the horizon, or with LAYERED half filtering over a yellow layer.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { perspective location <0, 1, -4> look_at <0, 0.6, 10> right x * image_width / image_height }
background { color rgb <0.4, 0.6, 1> }
#ifdef (LAYERED)
plane { y, 0
  texture { pigment { color rgb <1, 0.8, 0.2> } finish { ambient 0 diffuse 0 emission 1 } }
  texture { pigment { checker color rgb <0.2, 0.4, 1> color rgbf 1 scale 0.25 } finish { ambient 0 diffuse 0 emission 1 } }
}
#else
plane { y, 0 pigment { checker color rgb 1 color rgb 0 scale 0.25 } finish { ambient 0 diffuse 0 emission 1 } }
#end
