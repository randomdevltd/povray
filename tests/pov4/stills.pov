// stills: classic twin of stills.pov4
#version 4.0;
global_settings { assumed_gamma 1.0 }
camera { location <0, 1.6, -4.5> look_at <0, 0.9, 0> }
light_source { <4, 6, -3> rgb 1 }
plane { y, -1
  pigment { checker color rgb <0.8, 0.8, 0.8> color rgb <0.25, 0.3, 0.4> }
  finish { phong 0.2 }
}
sphere { <-1.1, 1, 0>, 1
  pigment { color rgb <0.9, 0.3, 0.2> }
  finish { phong 0.7 phong_size 40 }
}
sphere { <1.1, 1, 0>, 1
  pigment { color rgb <0.2, 0.5, 0.9> }
  finish { phong 0.7 phong_size 40 }
}
sphere { <-0.9, 0.25, 1.4>, 0.25
  pigment { color rgb <0.9, 0.9, 0.9> }
  finish { phong 0.5 }
}
sphere { <0, 0.25, 1.4>, 0.25
  pigment { color rgb <0.9, 0.9, 0.9> }
  finish { phong 0.5 }
}
sphere { <0.9, 0.25, 1.4>, 0.25
  pigment { color rgb <0.9, 0.9, 0.9> }
  finish { phong 0.5 }
}
torus { 0.55, 0.18
  translate <0, 1.8, 0>
  pigment { color rgb <0.95, 0.8, 0.2> }
  finish { phong 0.9 phong_size 60 }
}
