#version 4.0;
global_settings { assumed_gamma 1 }

camera {
  location <0, 0, -5>
  look_at 0
  right 4/3*x
  filter_tags { "lit" }
}

background { color rgb 0 }

light_source {
  <-3, 4, -4>
  color rgb <1, 0, 0>
  tags { "lit" }
}

light_source {
  <3, 4, -4>
  color rgb <0, 1, 0>
  tags { "hidden" }
}

sphere {
  0, 1
  pigment { color rgb 1 }
  finish { ambient 0 diffuse 1 }
  tags { "lit" }
}
