// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// look_at framing, rendered from one parse into three images: look_at_framing_ball.png turns to one object and fills the frame with it, look_at_framing_cubes.png frames the objects tagged "cubes", and look_at_framing_spherical.png frames that selection with a spherical camera; a selection holds the objects placed before it, so the cameras come last. +w320 +h240
#version 4.0;
global_settings {assumed_gamma 1.0}

#include "colors.inc"
#include "camera-context.inc" //common file containing object definitions for the camera demos

#declare Ball = sphere {<-2,3,14>,3 pigment {SteelBlue} finish {phong .2 reflection {.3}}}

box {<-15,0,-15>,<15,-1,15>
     pigment {checker White,Gray90 scale 5}
     finish {reflection {.1}}
     }

object {Cubes1 translate <5,0,-5> tags {"cubes"}}
object {Cubes2 translate <9,8,-6> tags {"cubes"}}
object {Cubes3 translate <-15,0,5>}
object {Cubes4 translate <-10,0,-5>}
object {Ball}

sky_sphere {pigment {rgb <0.859,0.910,0.831>}}

light_source {<-5,10,10> White*2}
light_source {<20,40,-60> White*0.8}

camera {location <4,9,-45> right x*image_width/image_height look_at Ball output {file "look_at_framing_ball"}}
camera {
  location <4,9,-45> right x*image_width/image_height look_at tagged {"cubes"}, 0.8
  output {file "look_at_framing_cubes"}
}
camera {
  spherical location <0,10,0> look_at tagged {"cubes"}, 0.8
  output {size <320,160> file "look_at_framing_spherical"}
}
