#version 4.0;
#ifndef (LoadMap) #declare LoadMap = 0; #end
#ifndef (Mismatch) #declare Mismatch = 0; #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 10
  photons {
    count 8000 gather 10, 30 jitter 0
    #if (LoadMap) load_file "render_tags_photon_maps.ph"
    #else save_file "render_tags_photon_maps.ph" #end
  }
}
camera {
  orthographic location <0, 0, -5> look_at 0 right 4*x up 2*y
  filter_tags { "screen" }
}
#declare Red = camera {
  location <100, 2, -4> look_at <100, 0.2, 0> angle 55
  filter_tags { "shared" | "red" }
}
#declare Blue = camera {
  Red
  #if (Mismatch) filter_tags { "shared" | "red" }
  #else filter_tags { "shared" | "blue" } #end
}
box {
  <-2, -1, 0>, <0, 1, 0.01>
  pigment { screen { camera { Red } } scale 2 translate <-2, -1, 0> }
  finish { emission 1 diffuse 0 } tags { "screen" }
}
box {
  <0, -1, 0>, <2, 1, 0.01>
  pigment { screen { camera { Blue } } scale 2 translate <0, -1, 0> }
  finish { emission 1 diffuse 0 } tags { "screen" }
}
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } tags { "shared" } }
sphere {
  <100, 0.75, 0>, 0.5 pigment { rgbf 1 } finish { ambient 0 diffuse 0 }
  interior { ior 1.5 } photons { target refraction on reflection off collect off }
  tags { "shared" }
}
light_source {
  <100, 4, 0> rgb <3, 0, 0> photon_only on
  spotlight point_at <100, 0, 0> radius 20 falloff 25
  photons { refraction on reflection off } tags { "red" }
}
light_source {
  <100, 4, 0> rgb <0, 0, 3> photon_only on
  spotlight point_at <100, 0, 0> radius 20 falloff 25
  photons { refraction on reflection off } tags { "blue" }
}
