// Cameras of their own size built from declared cameras (one with a size of its own already), with an angle, an orthographic
// angle, an explicit right and focal blur at the default focal distance; Cam renders one alone, without its block, at that size.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#declare Own = (Cam = -1);
global_settings { assumed_gamma 1 ambient_light 0 }
#declare Plain = camera { location <0, 2, -5> look_at <0, 0.5, 0> }
#declare Sized = camera { location <3, 3, -3> look_at <0, 0.5, 0> angle 45 #if (Own) output { size <48, 24> } #end }
#if ((Cam = -1) | (Cam = 0)) camera { Plain #if (Own) output { size <48, 24> } #end } #end
#if ((Cam = -1) | (Cam = 1)) camera { Sized #if (Own) output { size <32, 32> } #end } #end
#if ((Cam = -1) | (Cam = 2)) camera { orthographic location <0, 5, -5> look_at 0 angle 40 #if (Own) output { size <24, 48> } #end } #end
#if ((Cam = -1) | (Cam = 3)) camera { location <0, 4, -6> look_at 0 right x up y #if (Own) output { size <40, 20> } #end } #end
#declare Blurred = camera { location <0, 1.5, -3> look_at <0, 0.6, 0> angle 40 aperture 0.4 blur_samples 4 }
#if ((Cam = -1) | (Cam = 4)) camera { Blurred #if (Own) output { size <48, 24> } #end } #end
#if ((Cam = -1) | (Cam = 5)) camera { location <1, 1, -3> look_at <0, 0.6, 0> angle 50 aperture 0.3 blur_samples 4 #if (Own) output { size <24, 32> } #end } #end
plane { y, 0 pigment { checker rgb 0.9 rgb 0.5 } }
sphere { <0, 0.6, 0>, 0.5 pigment { rgb <0.9, 0.4, 0.2> } }
light_source { <-3, 5, -4> rgb 1 }
