#version 4.0;
// Mode 0 merges a pending .povm mesh with a sphere, mode 1 unions them, mode 2 places them apart.
#ifndef (Mode) #declare Mode = 0; #end
global_settings { assumed_gamma 1 }
camera { orthographic location <1.1, 1.1, -5> look_at <1.1, 1.1, 0> right 4.4*x up 3.3*y }
background { rgb 0 }
#declare M = mesh2 { povm "deferred_mesh_bounds.povm" inside_vector <0.123, 0.937, 0.271> }
#switch (Mode)
  #case (0) merge { object { M translate y*1.1 } sphere { <2.2, 0.3, 0>, 0.3 } pigment { rgb 1 } finish { emission 1 diffuse 0 } } #break
  #case (1) union { object { M translate y*1.1 } sphere { <2.2, 0.3, 0>, 0.3 } pigment { rgb 1 } finish { emission 1 diffuse 0 } split_union off } #break
  #case (2)
    object { M translate y*1.1 pigment { rgb 1 } finish { emission 1 diffuse 0 } }
    sphere { <2.2, 0.3, 0>, 0.3 pigment { rgb 1 } finish { emission 1 diffuse 0 } }
  #break
#end
