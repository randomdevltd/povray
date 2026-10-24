// A turntable: Frames cameras made in a loop, each with a bare output, around one object; Photons 1 or 2 adds a caustic, Radiosity indirect light.
// Cam renders frame Cam alone without its output.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Frames) #declare Frames = 12; #end
#ifndef (Photons) #declare Photons = 0; #end
#ifndef (Radiosity) #declare Radiosity = 0; #end
global_settings {
  assumed_gamma 1 ambient_light 0
  #if (Photons = 1) photons { count 20000 gather 10, 60 } #end
  #if (Photons = 2) photons { method 2 quality 0.5 } #end
  #if (Radiosity) radiosity { count 40 nearest_count 5 recursion_limit 2 pretrace_start 0.08 pretrace_end 0.02 } #end
}
#declare Frame = 0;
#while (Frame < Frames)
  #if ((Cam = -1) | (Cam = Frame))
    camera { location vrotate(<0, 1.6, -4>, y * 360 * Frame / Frames) look_at <0, 0.6, 0> angle 40 #if (Cam = -1) output #end }
  #end
  #declare Frame = Frame + 1;
#end
plane { y, 0 pigment { checker rgb 0.9 rgb 0.5 } }
sphere { <0, 0.7, 0>, 0.6 pigment { rgbf <1, 1, 1, 0.95> } interior { ior 1.5 } photons { target refraction on reflection off } }
box { <-1.2, 0, -0.2>, <-0.8, 0.4, 0.2> pigment { rgb <0.2, 0.5, 0.8> } }
light_source { <-3, 5, -2> rgb 1.2 photons { refraction on } }
