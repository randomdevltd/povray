// A screen's camera with an output item renders as its own view, seeing its own screen; Cam renders the main camera (0) or
// the screen's camera (1) alone. Case 1 inherits output through a declared camera, 2 repeats a screen, 3 tags its camera.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Case) #declare Case = 0; #end
#ifndef (Radiosity) #declare Radiosity = 0; #end
#declare Own = (Cam = -1);
global_settings {
  assumed_gamma 1 ambient_light 0
  #if (Radiosity) radiosity { count 20 nearest_count 3 recursion_limit 1 pretrace_start 0.08 pretrace_end 0.04 } #end
}
#declare Monitor = camera { location <1.5, 1.2, -2.5> look_at <0, 0.5, 0> angle 50 }
#macro Screen(Lens, X)
  box { <-1, 0.6, 1>, <1, 1.8, 1.01> pigment { screen { camera { Lens } } scale <2, 1.2, 1> translate <-1, 0.6, 0> }
        finish { emission 1 diffuse 0 } translate x * X }
#end
#switch (Case)
  #case (0)
    #if ((Cam = -1) | (Cam = 0)) camera { location <0, 1.5, -5> look_at <0, 0.8, 0> angle 45 #if (Own) output #end } #end
    #if (Cam = 1) camera { Monitor } #end
    box { <-1, 0.6, 1>, <1, 1.8, 1.01> pigment { screen { camera { Monitor #if (Own) output #end } } scale <2, 1.2, 1> translate <-1, 0.6, 0> }
          finish { emission 1 diffuse 0 } }
    #break
  #case (1)
    #declare View = camera { location <0, 1.5, -5> look_at <0, 0.8, 0> angle 45 output { file "multi_camera_view" } }
    camera { View }
    Screen(View, 0)
    #break
  #case (2)
    camera { location <0, 1.5, -5> look_at <0, 0.8, 0> angle 45 output }
    box { <-1, 0.6, 1>, <1, 1.8, 1.01> pigment { screen { camera { Monitor output } } scale <2, 1.2, 1> translate <-1, 0.6, 0> } }
    box { <-1, 0.6, 1>, <1, 1.8, 1.01> pigment { screen { camera { Monitor output } } scale <2, 1.2, 1> translate <-1, 0.6, 0> } translate x * 2.5 }
    #break
  #case (3)
    camera { location <0, 1.5, -5> look_at <0, 0.8, 0> angle 45 tags { "main" } output }
    box { <-1, 0.6, 1>, <1, 1.8, 1.01> pigment { screen { camera { Monitor tags { "screen-view" } output } } scale <2, 1.2, 1> translate <-1, 0.6, 0> } }
    #break
#end
plane { y, 0 pigment { checker rgb 0.9 rgb 0.5 } }
sphere { <0, 0.5, 0>, 0.5 pigment { rgb <0.9, 0.4, 0.2> } }
light_source { <-3, 5, -4> rgb 1 }
