// A hard-edged closed mesh against the same solid built from boxes, lit only on its walls: Shape 0 an L block (a concave
// crease between a step and its riser), 1 a cube (convex edges only). Mesh 1 builds it as a mesh, 0 from boxes.
#version 3.7;
#ifndef (Shape) #declare Shape = 0; #end
#ifndef (Mesh) #declare Mesh = 1; #end
global_settings {
  assumed_gamma 1
  mm_per_unit 10
  subsurface { samples 12, 4 method 2 }
}

camera { location <1.7, 1.9, -2.3> look_at <0.25, 0.5, 0> right x * image_width / image_height angle 40 }
// Exactly along -x: the step and every top face are lit only through the material.
light_source { <50, 0.75, 0> rgb 2 parallel point_at <0, 0.75, 0> }
background { rgb 0 }

#declare Look = material {
  texture { pigment { rgb <0.9, 0.6, 0.4> } finish { ambient 0 diffuse 0.8 subsurface { translucency <3, 2, 1.5> } } }
  interior { ior 1.4 }
}

#macro Quad(A, B, C, D) triangle { A, B, C } triangle { A, C, D } #end
// A prism along z from -0.5 to 0.5 over the outline P[0..N-1], whose fan from P[0] covers it.
#macro Prism(P, N)
  #local F = <0, 0, -0.5>; #local B = <0, 0, 0.5>;
  #for (I, 1, N - 2)
    triangle { P[0] + F, P[I] + F, P[I + 1] + F }
    triangle { P[0] + B, P[I] + B, P[I + 1] + B }
  #end
  #for (I, 0, N - 1)
    Quad(P[I] + F, P[mod(I + 1, N)] + F, P[mod(I + 1, N)] + B, P[I] + B)
  #end
#end

#if (Shape = 0)
  #if (Mesh)
    #declare Outline = array[6] { <-1, 0, 0>, <1, 0, 0>, <1, 0.5, 0>, <0, 0.5, 0>, <0, 1, 0>, <-1, 1, 0> }
    mesh { Prism(Outline, 6) inside_vector <0.31, 0.83, 0.47> material { Look } }
  #else
    difference { box { <-1, 0, -0.5>, <1, 1, 0.5> } box { <0, 0.5, -1>, <2, 2, 1> } material { Look } }
  #end
#else
  #if (Mesh)
    #declare Outline = array[4] { <-0.5, 0, 0>, <0.5, 0, 0>, <0.5, 1, 0>, <-0.5, 1, 0> }
    mesh { Prism(Outline, 4) inside_vector <0.31, 0.83, 0.47> material { Look } }
  #else
    box { <-0.5, 0, -0.5>, <0.5, 1, 0.5> material { Look } }
  #end
#end
