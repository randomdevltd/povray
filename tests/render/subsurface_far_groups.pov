// A thick method 2 slab lit evenly, seen face on: its long red channel reaches far enough to read the point cloud's far
// groups, whose light must be per area as a point's, so the face is as red at any cloud density. C sets domain_cells.
#version 4.0;
#ifndef (C) #declare C=4096; #end
global_settings { assumed_gamma 1 mm_per_unit 80 subsurface { method 2 samples 8, 4 } }
background { rgb 0 }
camera { orthographic location <0,0,-5> look_at 0 right x*0.6 up y*0.6 }
light_source { <0,0,-50> rgb 1 parallel point_at 0 }
box { <-1,-1,-0.6>,<1,1,0.6>
 texture { pigment { rgb <0.78,0.55,0.35> }
  finish { diffuse 0.72 subsurface { method 2 translucency 4*<1,0.46,0.27> pigment { rgb <0.78,0.55,0.35> } } } }
 subsurface { domain_cells C } }
