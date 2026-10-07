#version 4.0;
#ifndef (Mode) #declare Mode = 0; #end
global_settings { assumed_gamma 1 }
#if (Mode = 1) global_settings { filter_tags { "screen" } } #end

#declare ScreenView = camera {
  orthographic
  location <100, 0, -5>
  look_at <100, 0, 0>
  right 2*x
  up 2*y
  #if (Mode = 0) filter_tags { "screen" } #end
}

camera {
  orthographic
  location <0, 0, -5>
  look_at 0
  right 2*x
  up 2*y
  filter_tags { none }
}

background { color rgb 0 }

sphere {
  <100, 0, 0>, 0.8
  pigment { color rgb <0, 0, 1> }
  finish { emission 1 diffuse 0 }
  tags { "screen" }
}

box {
  <-1, -1, 0>, <1, 1, 0.01>
  pigment { screen { camera { ScreenView } } scale 2 translate <-1, -1, 0> }
  finish { emission 1 diffuse 0 }
}
