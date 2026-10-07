#version 4.0;
#ifndef (Reference) #declare Reference = 0; #end
#ifndef (Effect) #declare Effect = 0; #end
#ifndef (MapMode) #declare MapMode = 0; #end
global_settings {
  assumed_gamma 1 max_trace_level 8 ambient_light 0
  #if (Effect = 1)
    radiosity { pretrace_start 0.08 pretrace_end 0.04 count 30 error_bound 0.5 recursion_limit 1 always_sample off }
  #end
  #if (Effect = 2)
    photons {
      count 4000 gather 10, 30 jitter 0
      #if (MapMode = 1) save_file "render_tags_legacy.ph" #end
      #if (MapMode = 2) load_file "render_tags_legacy.ph" #end
    }
  #end
}
camera {
  location <0, 2, -5> look_at <0, 0.7, 0> angle 50
  #if (!Reference) filter_tags { "keep" } #end
}
light_source { <-3, 5, -2> rgb 1 tags { "keep" } }
plane { y, 0 pigment { rgb 0.8 } finish { ambient 0 diffuse 0.8 reflection 0.1 } tags { "keep" } }
sphere {
  <0, 0.8, 0>, 0.7
  pigment { rgbf 1 } finish { ambient 0 diffuse 0 reflection 0.05 }
  interior { ior 1.5 }
  #if (Effect = 2) photons { target refraction on reflection on collect off } #end
  tags { "keep" }
}
#if (Effect = 0)
  box {
    <-1.5, 0.1, 0.7>, <1.5, 1.8, 1.2> hollow pigment { rgbt 1 }
    interior { media { scattering { 1, 0.3 } absorption 0.1 samples 5 jitter 0 method 3 } }
    tags { "keep" }
  }
#end
#if (!Reference)
  light_source { <2, 3, -1> rgb <5, 0, 0> tags { "discard" } }
  box { <-3, 0, 2>, <3, 4, 2.1> pigment { rgb <0, 1, 0> } finish { emission 4 diffuse 1 } tags { "discard" } }
#end
