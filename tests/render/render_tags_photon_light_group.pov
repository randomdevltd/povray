#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 10
  photons { count 12000 gather 10, 30 jitter 0 }
}
camera {
  orthographic location <1.5, 3, -6> look_at <1.5, 0.5, 0> right 5*x up 2.5*y
  filter_tags { "keep" }
}
#declare Lens = sphere {
  <0, 0.75, 0>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1.5 } photons { target refraction on reflection off collect off }
}
#declare Receiver = box {
  <-1, -0.1, -1>, <1, 0, 1> pigment { rgb 1 } finish { ambient 0 diffuse 1 }
}
light_group {
  light_source { <1.5, 4, 0> rgb 3 photon_only on photons { refraction on reflection off } }
  object { Lens }
  object { Receiver }
  global_lights off
  tags { "keep" }
}
#if (!Reference)
  object { Lens translate 3*x tags { "keep" } }
  object { Receiver translate 3*x tags { "keep" } }
#end
