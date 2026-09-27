// Isosurface stress: the camera in tunnels through rock, where a noise field is negative; rays start inside the
// container and cross many cells and walls. Declare Open=1, MaxGradient=g, Accuracy=a, Probe=1 (a red sky).
#version 3.7;
#include "functions.inc"
#ifndef (Open) #declare Open = 0; #end
#ifndef (MaxGradient) #declare MaxGradient = 1.5; #end
#ifndef (Accuracy) #declare Accuracy = 0.001; #end
global_settings { assumed_gamma 1.0 }

#declare Cave = function(x, y, z) {
  0.46 - f_noise3d(x * 0.45, y * 0.45, z * 0.45) - 0.35 * (f_noise3d(x * 1.3, y * 1.3, z * 1.3) - 0.5)
  - 0.12 * (f_noise3d(x * 3.7, y * 3.7, z * 3.7) - 0.5)
}

// The camera walks from the centre to the first point well inside the open space.
#declare Eye = <0, 0, 0>;
#declare Step = 0;
#while ((Cave(Eye.x, Eye.y, Eye.z) < 0.06) & (Step < 500))
  #declare Eye = Eye + <0.11, 0.023, 0.07>;
  #declare Step = Step + 1;
#end
#declare Ahead = vnormalize(<1, -0.05, 0.6>);

isosurface {
  function { Cave(x, y, z) }
  contained_by { box { -12, 12 } }
  #if (Open) open #end
  max_gradient MaxGradient
  accuracy Accuracy
  pigment { granite color_map { [0 rgb <0.42, 0.36, 0.3>] [1 rgb <0.62, 0.55, 0.46>] } scale 0.7 }
  finish { ambient 0.02 diffuse 0.85 }
}

light_source { Eye + <0, 0.25, 0> color rgb <1.0, 0.85, 0.65> fade_distance 3 fade_power 2 }
light_source { Eye + Ahead * 5 color rgb <0.5, 0.6, 0.9> fade_distance 2 fade_power 2 }
#ifdef (Probe) background { color rgb <1, 0, 0> } #end
camera { perspective angle 80 location Eye look_at Eye + Ahead right x * 4/3 up y }
