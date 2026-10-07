#version 4.0;
#ifndef (Variant) #declare Variant = 0; #end
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 5
  radiosity {
    pretrace_start 0.08 pretrace_end 0.02 count 50 error_bound 0.6
    recursion_limit 2 nearest_count 5 always_sample off
  }
}
camera { location <0, 1.5, -5> look_at <0, 0.8, 0> angle 50 filter_tags { "keep" } }
sky_sphere { pigment { rgb <0.3, 0.4, 0.6> } }
light_source { <-2, 5, -3> rgb 0.5 tags { "keep" } }
plane { y, 0 pigment { rgb 0.7 } finish { diffuse 0.8 } tags { "keep" } }
box { <-3, 0, 2>, <3, 3, 2.1> pigment { rgb 0.8 } tags { "keep" } }
box { <-3, 0, -1>, <-2.9, 3, 2> pigment { rgb <0.9, 0.1, 0.1> } tags { "keep" } }
sphere { <-0.7, 0.7, 0>, 0.7 pigment { rgb 0.7 } tags { "keep" } }
box { <0.5, 0, -0.3>, <1.5, 1.2, 0.7> pigment { rgb <0.1, 0.7, 0.2> } tags { "keep" } }
#if (Variant > 0)
  #declare SharedPicture = pigment {
    screen {
      camera {
        location <2, 0.6, -1> look_at <-0.6, 0.5, 0> angle 80
        filter_tags { "keep" } radiosity_size <96, 64>
      }
    }
  }
  #if (Variant = 1 | Variant = 3)
    box {
      <100, 0, 0>, <101, 1, 0.01> pigment { SharedPicture }
      finish { emission 1 diffuse 0 } no_radiosity tags { "discard" }
    }
  #end
  #if (Variant = 3 | Variant = 4)
    box {
      <1.2, 1.5, 0>, <2.2, 2.5, 0.01>
      pigment { SharedPicture translate <1.2, 1.5, 0> }
      finish { emission 1 diffuse 0 } no_radiosity tags { "keep" }
    }
  #end
#end
