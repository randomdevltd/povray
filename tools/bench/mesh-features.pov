// Every mesh feature a triangle can carry: flat and smooth, per-triangle, interpolated and UV-mapped textures, inside tests.
#version 3.7;
global_settings { assumed_gamma 1 }
camera { location <0, 3, -7> look_at <0, 0.6, 0> angle 55 }
light_source { <-4, 9, -6> rgb 1 }
light_source { <5, 4, -3> rgb 0.5 }
plane { y, -1 pigment { checker rgb 0.3 rgb 0.6 } }
#declare TA = texture { pigment { rgb <1, 0.2, 0.2> } }
#declare TB = texture { pigment { rgb <0.2, 1, 0.2> } }
#declare TC = texture { pigment { rgb <0.2, 0.3, 1> } }
#declare Grid = 14;
#declare H = function(x, z) { 0.25 * sin(7 * x) * cos(5 * z) }
// mesh{}: flat and smooth triangles, per-triangle textures, uv coordinates.
#declare M1 = mesh {
  #for (J, 0, Grid - 1) #for (I, 0, Grid - 1)
    #local A = <I, 0, J> / Grid; #local B = <I + 1, 0, J> / Grid; #local C = <I, 0, J + 1> / Grid; #local D = <I + 1, 0, J + 1> / Grid;
    #local A = A + y * H(A.x, A.z); #local B = B + y * H(B.x, B.z); #local C = C + y * H(C.x, C.z); #local D = D + y * H(D.x, D.z);
    #if (mod(I + J, 3) = 0)
      triangle { A, B, D uv_vectors <A.x, A.z>, <B.x, B.z>, <D.x, D.z> texture { TA } }
    #else
      smooth_triangle { A, <0.2, 1, 0.1>, B, <-0.1, 1, 0.3>, D, <0, 1, -0.2> uv_vectors <A.x, A.z>, <B.x, B.z>, <D.x, D.z> }
    #end
    smooth_triangle { A, <0.1, 1, 0>, D, <0, 1, 0.2>, C, <-0.2, 1, 0> uv_vectors <A.x, A.z>, <D.x, D.z>, <C.x, C.z> texture { TB } }
  #end #end
  inside_vector y
}
object { M1 scale 2.2 translate <-2.6, 0, 0> texture { uv_mapping pigment { checker rgb 1 rgb 0.1 scale 0.1 } } }
// mesh2: vertex normals by vertex, explicit uv indices, textures including three-texture interpolation.
#declare N = 12;
#declare M2 = mesh2 {
  vertex_vectors { (N + 1) * (N + 1),
    #for (J, 0, N) #for (I, 0, N) <I / N, 0.3 * sin(6 * I / N) * sin(5 * J / N), J / N>, #end #end }
  normal_vectors { (N + 1) * (N + 1),
    #for (J, 0, N) #for (I, 0, N) <-1.8 * cos(6 * I / N) * sin(5 * J / N), 1, -1.5 * sin(6 * I / N) * cos(5 * J / N)>, #end #end }
  uv_vectors { 4, <0, 0>, <1, 0>, <0, 1>, <1, 1> }
  texture_list { 3, texture { TA } texture { TB } texture { TC } }
  face_indices { 2 * N * N,
    #for (J, 0, N - 1) #for (I, 0, N - 1)
      #local V = J * (N + 1) + I;
      <V, V + 1, V + N + 2>, mod(I, 3), <V, V + N + 2, V + N + 1>, mod(J, 3), mod(J + 1, 3), mod(I + 2, 3),
    #end #end }
  uv_indices { 2 * N * N, #for (K, 1, N * N) <0, 1, 3>, <0, 3, 2>, #end }
  inside_vector <0.1, 1, 0.2>
}
object { M2 scale 2.2 translate <0.4, 0, 0> }
// mesh2 without normals or textures, used in CSG so insideness matters, uv mapped by vertex.
#declare M3 = mesh2 {
  vertex_vectors { 8, <0,0,0>, <1,0,0>, <1,1,0>, <0,1,0>, <0,0,1>, <1,0,1>, <1,1,1>, <0,1,1> }
  uv_vectors { 8, <0,0>, <1,0>, <1,1>, <0,1>, <0.2,0>, <0.8,0>, <0.8,1>, <0.2,1> }
  face_indices { 12, <0,1,2>, <0,2,3>, <4,6,5>, <4,7,6>, <0,4,5>, <0,5,1>, <3,2,6>, <3,6,7>, <0,3,7>, <0,7,4>, <1,5,6>, <1,6,2> }
  inside_vector <0.3, 0.2, 1>
}
difference {
  object { M3 scale 1.6 translate <-0.3, -1, -2.8> }
  sphere { <0.5, -0.2, -2.8>, 0.75 }
  texture { uv_mapping pigment { checker rgb <1, 0.8, 0.3> rgb 0.2 scale 0.25 } finish { specular 0.4 } }
}
