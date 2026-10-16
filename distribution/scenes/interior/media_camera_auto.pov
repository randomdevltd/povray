#version 4.0;
global_settings { assumed_gamma 1 prepared_media_cells 1000000 }
background { rgb 0.025 }
#declare Cloud = box {
  <-1, -1, -1>, <1, 1, 1>
  pigment { rgbt 1 }
  hollow
  interior { media {
    resolution auto 1.5
    emission rgb <0.4, 0.65, 1>
    density { spherical turbulence 0.18 colour_map { [0 rgb 0] [1 rgb 1] } }
  } }
  scale <1.8, 1, 1>
  rotate y * 25
  tags { "cloud" }
}
camera { location <4, 2, -8> angle 40 look_at filter_tags { "cloud" } }
object { Cloud }
