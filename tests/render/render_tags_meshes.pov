#version 4.0;
// Deferred meshes are built only for the objects some view keeps. Mode 2 filters in the camera, Mode 3 in
// global_settings, Mode 4 queries B's extent while parsing, which builds it then.
#ifndef (Mode) #declare Mode = 0; #end
global_settings {
  assumed_gamma 1
  #if (Mode = 3) filter_tags { !"b" } #end
}
background { rgb 0 }
camera {
  orthographic location <0, 0, -5> look_at 0 right 6*x up 3*y
  #if (Mode = 2) filter_tags { !"b" } #end
}
light_source { <0, 3, -6> rgb 1 }
#declare Ball = function(x, y, z) { sqrt(x*x + y*y + z*z) - 0.6 }
#declare A = isosurface_mesh { function { Ball(x, y, z) } contained_by { box { -0.7, 0.7 } } max_angle 30 }
#declare B = isosurface_mesh { function { Ball(x, y, z) } contained_by { box { -0.7, 0.7 } } max_angle 30 }
#if (Mode = 4)
  #if (max_extent(B).x < 0.55)
    #error "the queried mesh was not built"
  #end
#end
object { A translate -2*x pigment { rgb 1 } }
object { A pigment { rgb <1, 0.5, 0.2> } }
// B is used only inside a declared union, placed by tagged copies, as instanced assets usually are.
#declare Bunch = union { object { B } sphere { <0, 0.75, 0>, 0.15 } pigment { rgb <0.2, 0.5, 1> } }
object { Bunch translate 2*x tags { "b" } }
object { Bunch translate <2, -0.9, 0> scale 0.5 tags { "b" } }
