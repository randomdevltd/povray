// Cameras: two mesh cameras bake two panels from one parse; Cam selects one for a render of it alone, Sized gives each
// its own image size.
#version 4.0;
#ifndef (Cam) #declare Cam = -1; #end
#ifndef (Sized) #declare Sized = 0; #end
#ifndef (Radiosity) #declare Radiosity = 0; #end
global_settings {
  assumed_gamma 1 ambient_light 0
  #if (Radiosity) radiosity { count 20 nearest_count 3 recursion_limit 1 pretrace_start 0.08 pretrace_end 0.04 } #end
}

#macro Panel(X0)
  mesh {
    triangle { <X0, 0, 0>, <X0 + 1, 0, 0>, <X0 + 1, 1, 0> uv_vectors <0, 0>, <1, 0>, <1, 1> }
    triangle { <X0, 0, 0>, <X0 + 1, 1, 0>, <X0, 1, 0> uv_vectors <0, 0>, <1, 1>, <0, 1> }
  }
#end

#if ((Cam = -1) | (Cam = 0))
  camera { mesh_camera { 1 3 Panel(-1.2) } location <0, 0, 0.01> direction <0, 0, -1> filter_tags { "room" | "left" } #if (Cam = -1) output { #if (Sized) size <40, 40> #end } #end }
#end
#if ((Cam = -1) | (Cam = 1))
  camera { mesh_camera { 1 3 Panel(0.2) } location <0, 0, 0.01> direction <0, 0, -1> filter_tags { "room" | "right" } #if (Cam = -1) output { #if (Sized) size <24, 48> #end } #end }
#end

object { Panel(-1.2) pigment { gradient x scale 0.25 } finish { diffuse 0.8 } tags { "room" } }
object { Panel(0.2) pigment { radial frequency 6 translate <0.7, 0.5, 0> } finish { diffuse 0.8 } tags { "room" } }
plane { y, -0.2 pigment { rgb <0.7, 0.6, 0.5> } tags { "room" } }
sphere { <-0.7, 0.5, -0.6>, 0.3 pigment { rgb <1, 0.2, 0.1> } tags { "left" } }
sphere { <0.7, 0.5, -0.6>, 0.3 pigment { rgb <0.1, 0.3, 1> } tags { "right" } }
light_source { <-2, 3, -4> rgb 1.2 tags { "room" } }
