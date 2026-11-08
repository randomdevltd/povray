// This scene is licensed under CC BY 3.0, http://creativecommons.org/licenses/by/3.0/
// A stack of clear filtering slabs over a white base under a surface light. From #version 4.0 each lit hit takes the
// light's samples in proportion to its contribution to the pixel (light_samples by_contribution), so the hits seen through
// reflections and through the slabs take fewer samples while every surface seen directly, the clear slabs included,
// takes them all. Render with Declare=Full=1 for the classic full sampling. +w640 +h480 +a0.3
#version 4.0;
#ifndef (Full) #declare Full = 0; #end
#ifndef (Samples) #declare Samples = 32; #end
global_settings { assumed_gamma 1 max_trace_level 12 }
#if (Full) #default { finish { light_samples 1 full } } #end
camera { location <0, 5, -7> look_at <0, 0.7, 0> angle 38 right x * image_width / image_height }
background { rgb 0.02 }
box { <-4, -0.1, -4>, <4, 0, 4> pigment { rgb 0.8 } finish { ambient 0 diffuse 0.9 } }
#declare Tints = array[5] { <1, 0.55, 0.55>, <0.6, 1, 0.6>, <0.6, 0.7, 1>, <1, 1, 0.5>, <0.9, 0.6, 1> };
#for (I, 0, 4)
  box { <-1.3, 0, -1.3>, <1.3, 0.14, 1.3>
    pigment { rgbf <Tints[I].x, Tints[I].y, Tints[I].z, 0.93> }
    finish { ambient 0 diffuse 0.25 reflection { 0.05 } }
    interior { ior 1.4 }
    rotate y * I * 14
    translate <(I - 2) * 0.22, 0.2 + I * 0.32, (I - 2) * 0.1>
  }
#end
// the surface light: a panel above the stack, lighting downwards
box { <-1.2, 4.18, -0.2>, <1.2, 4.2, 2.2>
  pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
  light { brightness 6 nits samples Samples }
}
