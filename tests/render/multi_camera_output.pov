// Output cameras: a detail camera chosen by filter_tags, a windowed thumbnail into its own file, a windowed camera at the render's
// size; Cam renders one alone without its block, Mixed adds a plain camera, Clash and BadWindow fail.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Clash) #declare Clash = 0; #end
#ifndef (BadWindow) #declare BadWindow = 0; #end
#ifndef (Mixed) #declare Mixed = 0; #end
#declare Own = (Cam = -1);
global_settings { assumed_gamma 1 ambient_light 0 }

#if ((Cam = -1) | (Cam = 0))
  camera {
    location <0.5, 1.1, -1.4> look_at <0, 0.6, 0> angle 35 filter_tags { "detail" } tags { "view-detail" }
    #if (Own) output { size <48, 64> #if (Clash = 1) file "multi_camera_thumb.ppm" #elseif (Clash = 2) file "multi_camera_clash-3.ppm" #end } #end
  }
#end
#if ((Cam = -1) | (Cam = 1))
  camera {
    location <0, 4, -6> look_at <0, 0.5, 0> angle 50 tags { "view-thumb" }
    #if (Own) output { size <32, 16> window <0.25, 0, 1, 1> file "multi_camera_thumb" #switch (BadWindow) #case (1) window <1, 1, 99, 99> #break #case (2) window <20, 5, 9, 12> #break #case (3) window <0, 0, 0, 0> #break #end } #end
  }
#end
#if ((Cam = -1) | (Cam = 2))
  camera { location <3, 3, -3> look_at <0, 0.5, 0> angle 45 tags { "view-side" } #if (Own) output { window <9, 5, 40, 30> } #end }
#end
#if (Mixed) camera { location <-3, 3, -3> look_at 0 tags { "view-side" } } #end

plane { y, 0 pigment { checker rgb 0.9 rgb 0.5 } tags { "context" } }
sphere { <0, 0.6, 0>, 0.5 pigment { rgb <0.9, 0.4, 0.2> } finish { phong 0.6 } tags { "detail" } }
box { <-2, 0, 1>, <-1, 1.5, 2> pigment { rgb <0.2, 0.6, 0.3> } tags { "context" } }
light_source { <-3, 5, -4> rgb 1 tags { "detail", "context" } }
