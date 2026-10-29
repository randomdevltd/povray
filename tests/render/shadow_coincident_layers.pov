#version 3.8;
// A point light straight above two translucent boxes with absorbing media, stacked face to face (Gap=0) or apart.
// Mesh=1 makes the upper box a mesh, whose face rounds apart from the box's by an ulp or so.
#ifndef (Gap) #declare Gap = 0; #end
#ifndef (Mesh) #declare Mesh = 0; #end
global_settings { assumed_gamma 1 ambient_light rgb 0 }
camera { orthographic location <0, 10, 0> look_at 0 right x * 2 up z * 2 }
light_source { <0, 20, 0> rgb 1 }
plane { y, 0 pigment { rgb 1 } finish { ambient 0 diffuse 1 } }
#declare Layer = material { texture { pigment { rgbt <1, 1, 1, 0.7> } finish { ambient 0 diffuse 0 } }
                            interior { media { absorption 0.4 } } }
box { <-0.7, 1, -0.7>, <0.7, 1.5, 0.7> hollow no_image material { Layer } }
#declare Upper = <0, 1.5 + Gap, 0>;
#if (Mesh)
  mesh {
    #declare A = Upper + <-0.6, 0, -0.6>; #declare B = Upper + <0.6, 0.5, 0.6>;
    triangle { A, <B.x, A.y, A.z>, <B.x, A.y, B.z> } triangle { A, <B.x, A.y, B.z>, <A.x, A.y, B.z> }
    triangle { <A.x, B.y, A.z>, <B.x, B.y, B.z>, <B.x, B.y, A.z> } triangle { <A.x, B.y, A.z>, <A.x, B.y, B.z>, <B.x, B.y, B.z> }
    triangle { A, <A.x, B.y, A.z>, <B.x, B.y, A.z> } triangle { A, <B.x, B.y, A.z>, <B.x, A.y, A.z> }
    triangle { <A.x, A.y, B.z>, <B.x, B.y, B.z>, <A.x, B.y, B.z> } triangle { <A.x, A.y, B.z>, <B.x, A.y, B.z>, <B.x, B.y, B.z> }
    triangle { A, <A.x, A.y, B.z>, <A.x, B.y, B.z> } triangle { A, <A.x, B.y, B.z>, <A.x, B.y, A.z> }
    triangle { <B.x, A.y, A.z>, <B.x, B.y, B.z>, <B.x, A.y, B.z> } triangle { <B.x, A.y, A.z>, <B.x, B.y, A.z>, <B.x, B.y, B.z> }
    inside_vector y hollow no_image material { Layer }
  }
#else
  box { Upper + <-0.6, 0, -0.6>, Upper + <0.6, 0.5, 0.6> hollow no_image material { Layer } }
#end
