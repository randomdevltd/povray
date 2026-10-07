#version 4.0;
#ifndef (Lamp) #declare Lamp = 1; #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 10
  photons { count 8000 gather 10, 30 jitter 0 }
}
camera {
  location <0, 2, -5> look_at <0, 0.3, 2> angle 45
  filter_tags { "near" }
}
#if (Lamp)
  light_source {
    <0, 3, -3> rgb 3 photon_only on
    spotlight point_at <0, 0.7, 2> radius 15 falloff 20
    photons { refraction on reflection off } tags { "near" }
  }
#end
portal {
  polygon { 5, <-5, -5, 0>, <5, -5, 0>, <5, 5, 0>, <-5, 5, 0>, <-5, -5, 0> }
  to { translate 0*x }
  far off
  near { front on back on filter_tags { "far" } }
  tags { "near" }
}
sphere {
  <0, 0.75, 2>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1.5 } photons { target refraction on reflection off collect off }
  tags { "near", "far" }
}
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } tags { "far" } }
