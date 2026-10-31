// Texture filter test: a mirror with bumps finer than a pixel to the horizon, reflecting a banded sky_sphere, optionally under a filtering layer.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { perspective location <0, 1, -4> look_at <0, 0.8, 10> right x * image_width / image_height }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.9, 0.9, 1>] [0.08 rgb <0.9, 0.5, 0.2>] [0.16 rgb <0.2, 0.3, 0.8>] [1 rgb <0.05, 0.1, 0.4>] } }
  // with RGBF, an upper layer of fine bands alternately filtering red and opaque green
  #ifdef (RGBF) pigment { gradient y frequency 24 color_map { [0.5 rgbf <1, 0, 0, 1>] [0.5 rgbf <0, 0.6, 0, 0>] } } #end
}
plane { y, 0
  pigment { color rgb 1 }
  normal { bumps 0.4 scale 0.05 }
  finish { ambient 0 diffuse 0 reflection 0.9 }
}
