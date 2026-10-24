// Mesh cameras bake panels, each into panel<k>, around one parse-heavy body, all from one parse; Cam=k renders one alone
// without its output, Thumb adds a 160x120 view of the whole body. Panels sets the camera count, Detail the parse and preparation cost.
#version 4.0;
#ifndef (Panels) #declare Panels = 6; #end
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Detail) #declare Detail = 1; #end
#ifndef (Thumb) #declare Thumb = 0; #end
global_settings { assumed_gamma 1 ambient_light 0 }
#include "functions.inc"

#macro Panel(K)
  mesh {
    triangle { <-0.5, 0, 0>, <0.5, 0, 0>, <0.5, 1.4, 0> uv_vectors <0, 0>, <1, 0>, <1, 1> }
    triangle { <-0.5, 0, 0>, <0.5, 1.4, 0>, <-0.5, 1.4, 0> uv_vectors <0, 0>, <1, 1>, <0, 1> }
    translate <0, -0.7, -1.8> rotate y * 360 * K / Panels
  }
#end

#for (K, 0, Panels - 1)
  #if ((Cam = -1) | (Cam = K))
    camera { mesh_camera { 1 3 Panel(K) } location <0, 0, 0.01> direction <0, 0, -1> #if (Cam = -1) output { file concat("panel", str(K, 0, 0)) } #end }
  #end
  object { Panel(K) pigment { gradient y color_map { [0 rgb <0.8, 0.3, 0.2>] [1 rgb <0.9, 0.8, 0.6>] } scale 0.2 } finish { diffuse 0.8 } }
#end

#if (Thumb & (Cam = -1))
  camera { location <0, 3, -7> look_at 0 angle 45 output { size <160, 120> file "thumbnail" } }
#end

#declare Body = isosurface_mesh {
  function { sqrt(x*x + 0.4*y*y + z*z) - 1 + 0.25 * (f_noise3d(5*x, 5*y, 5*z) - 0.5) }
  contained_by { sphere { 0, 2 } }
  max_gradient 3 min_size 0.03 / Detail
}
object { Body pigment { rgb <0.4, 0.5, 0.7> } }
#for (I, 1, 4000 * Detail)
  sphere { <mod(I * 7.31, 9) - 4.5, -2, mod(I * 3.17, 9) - 4.5>, 0.05 pigment { rgb <mod(I, 3) / 3, 0.5, 0.6> } }
#end
plane { y, -2.05 pigment { rgb 0.6 } }
light_source { <-4, 6, -5> rgb 1 }
light_source { <5, 4, 3> rgb 0.6 }
