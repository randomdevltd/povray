// Cameras with output blocks against a render of one alone (Cam, without a block); Plain drops the blocks. Feature 0 plain, 1 radiosity,
// 2/3 photon methods 1/2, 4 subsurface, 5 media and fog, 6 photon radius and multiplier, 7/8 media photons, 9 subsurface spacing.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Feature) #declare Feature = 0; #end
#ifndef (Shared) #declare Shared = 0; #end
#ifndef (PhotonFile) #declare PhotonFile = 0; #end
#ifndef (Plain) #declare Plain = 0; #end
#declare Own = ((Cam = -1) & !Plain);
global_settings {
  assumed_gamma 1 ambient_light 0 max_trace_level 8
  #if (Feature = 1) radiosity { count 30 nearest_count 4 recursion_limit 1 pretrace_start 0.08 pretrace_end 0.04 } #end
  #if (Feature = 2) photons { count 6000 gather 10, 40 jitter 0.4 #if (PhotonFile) save_file "multi_camera_save.ph" #end } #end
  #if (Feature = 3) photons { method 2 quality 0.3 } #end
  #if (Feature = 4) mm_per_unit 10 subsurface { samples 8, 2 } #end
  #if (Feature = 9) mm_per_unit 10 subsurface { samples 8, 2 spacing 0.5 } photons { method 2 quality 0.3 } #end
  #if (Feature = 6) photons { count 6000 gather 10, 40 radius 0.3, 1.5 } #end
  #if (Feature = 7) photons { count 6000 gather 10, 40 media 60 } #end
  #if (Feature = 8) photons { method 2 quality 0.3 } #end
}

#if ((Cam = -1) | (Cam = 0))
  camera { location <0, 2, -5> look_at <0, 0.5, 0> angle 50 #if (!Shared) filter_tags { "shared" | "red" } #end #if (Own) output #end }
#end
#if ((Cam = -1) | (Cam = 1))
  camera { location <3, 3, -3> look_at <0, 0.5, 0> angle 45 #if (!Shared) filter_tags { "shared" | "blue" } #end #if (Own) output #end }
#end
#if ((Cam = -1) | (Cam = 2))
  camera { orthographic location <0, 6, 0.01> look_at 0 right 5*x up 2.5*y #if (Own) output #end }
#end

plane { y, 0 pigment { checker rgb 0.9 rgb 0.5 } finish { diffuse 0.8 } tags { "shared" } }
sphere {
  <0, 0.8, 0>, 0.7
  #if ((Feature = 4) | (Feature = 9))
    texture { pigment { rgb <0.8, 0.5, 0.4> } finish { diffuse 0.3 subsurface { translucency <2, 1, 0.6> } } } interior { ior 1.4 }
  #else
    pigment { rgbf <1, 1, 1, 0.95> } finish { reflection 0.05 } interior { ior 1.5 }
  #end
  photons { target refraction on reflection on collect off }
  tags { "shared" }
}
box { <-2.2, 0, 0.6>, <-1.4, 1.2, 1.4> pigment { rgb <0.2, 0.7, 0.3> } tags { "red" } }
cone { <1.8, 0, 0.8>, 0.6, <1.8, 1.5, 0.8>, 0 pigment { rgb <0.8, 0.7, 0.2> } tags { "blue" } }
#if ((Feature = 7) | (Feature = 8))
  box {
    <-2, 0.01, -1.5>, <2, 2.5, 1.5> hollow pigment { rgbt 1 }
    interior { media { scattering { 1, 0.25 } } } photons { pass_through } tags { "shared" }
  }
#end
#if (Feature = 5)
  fog { distance 12 rgb <0.6, 0.7, 0.8> }
  box {
    <-1, 0, -2>, <1, 2, -1> hollow pigment { rgbt 1 }
    interior { media { scattering { 1, 0.4 } } } tags { "shared" }
  }
#end

light_source { <-3, 5, -4> rgb <1.4, 0.6, 0.5> photons { refraction on reflection on } tags { "red" } }
light_source { <4, 6, -2> rgb <0.5, 0.7, 1.4> photons { refraction on reflection on } tags { "blue" } }
light_source { <0, 8, 0> rgb 0.3 tags { "shared" } }
