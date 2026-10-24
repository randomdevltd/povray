// This work is licensed under the Creative Commons Attribution 3.0 Unported License.
// To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/
// or send a letter to Creative Commons, 444 Castro Street, Suite 900, Mountain View,
// California, 94041, USA.

// Persistence Of Vision Raytracer sample file.
// This file demonstrates cameras with output items, rendered from one parse into their own images: the main view at the
// command line's size, a small thumbnail of it, and a close-up that filter_tags limits to the tagged vase.
//
// +w640 +h480

#version 4.0;
global_settings { assumed_gamma 1.0 }

#include "colors.inc"

// writes camera_outputs-1.png at the command line's size
camera { location <0, 3, -7> look_at <0, 1, 0> angle 50 output }
// a thumbnail of the same view, into its own file
camera { location <0, 3, -7> look_at <0, 1, 0> angle 50 output { size <160, 120> file "camera_outputs_thumbnail" } }
// the vase alone, square and in detail
camera {
  location <1.2, 1.6, -2.2> look_at <0.8, 1, 0> angle 30 filter_tags { "vase" | "room" }
  output { size <400, 400> file "camera_outputs_vase" }
}

light_source { <-4, 8, -6> color White * 0.9 tags { "room" } }
light_source { <5, 6, -3> color White * 0.4 tags { "room" } }
plane { y, 0 pigment { checker color Gray80 color Gray40 } tags { "room" } }
plane { z, 3 pigment { color rgb <0.6, 0.7, 0.8> } tags { "room" } }
box { <-2.5, 0, -0.5>, <-1.5, 1.2, 0.5> pigment { color rgb <0.8, 0.3, 0.2> } tags { "furniture" } }
sphere { <-0.6, 0.5, -0.4>, 0.5 pigment { color rgb <0.2, 0.5, 0.8> } finish { phong 0.6 } tags { "furniture" } }
lathe {
  cubic_spline 7, <0.3, -0.1>, <0.3, 0>, <0.45, 0.4>, <0.25, 1.1>, <0.2, 1.5>, <0.3, 1.7>, <0.3, 1.8>
  translate <0.8, 0, 0> pigment { color rgb <0.9, 0.8, 0.5> } finish { phong 0.8 reflection 0.1 } tags { "vase" }
}
