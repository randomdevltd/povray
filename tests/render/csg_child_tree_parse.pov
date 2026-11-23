#version 3.7;
// Parse-time trace() and inside() on large CSGs, written to csg_child_tree_parse.txt: the file must match the linear scan's
// (POV_CSG_CHILD_TREE=0) byte for byte. Unions, a merge, a difference with many cutters, an intersection of many lenses,
// nested unions, children inverted, clipped, bounded and meshes among them, a deferred mesh the first query builds,
// copies transformed between queries, and fresh copies each queried once.
#ifndef (N) #declare N = 6; #end
#declare R = seed(11);
#declare Cube = mesh {
  triangle { <-1,-1,-1>, <1,-1,-1>, <1,1,-1> } triangle { <-1,-1,-1>, <1,1,-1>, <-1,1,-1> }
  triangle { <-1,-1,1>, <1,1,1>, <1,-1,1> } triangle { <-1,-1,1>, <-1,1,1>, <1,1,1> }
  triangle { <-1,-1,-1>, <-1,1,1>, <-1,-1,1> } triangle { <-1,-1,-1>, <-1,1,-1>, <-1,1,1> }
  triangle { <1,-1,-1>, <1,-1,1>, <1,1,1> } triangle { <1,-1,-1>, <1,1,1>, <1,1,-1> }
  triangle { <-1,-1,-1>, <-1,-1,1>, <1,-1,1> } triangle { <-1,-1,-1>, <1,-1,1>, <1,-1,-1> }
  triangle { <-1,1,-1>, <1,1,1>, <-1,1,1> } triangle { <-1,1,-1>, <1,1,-1>, <1,1,1> }
  inside_vector <0.3, 0.7, 0.2>
}
// N*N*N small merges of a box and a sphere on a grid in a 6-unit cube, some inverted, clipped or bounded
#macro Bricks()
  #for (I, 0, N - 1) #for (J, 0, N - 1) #for (K, 0, N - 1)
    #local P = <I, J, K> * 6 / N + <rand(R), rand(R), rand(R)> * 0.3;
    #local C = mod(I + 2 * J + 3 * K, 7);
    #if (C = 0) merge { box { P - 0.25, P + 0.25 } sphere { P + <0.2, 0.2, 0>, 0.18 } }
    #elseif (C = 1) sphere { P, 0.3 clipped_by { box { P - 0.4, P + <0.4, 0.1, 0.4> } } }
    #elseif (C = 2) object { Cube scale 0.25 translate P }
    #elseif (C = 3) sphere { P, 0.28 bounded_by { box { P - 0.3, P + 0.3 } } }
    #elseif (C = 4) difference { box { P - 0.3, P + 0.3 } sphere { P + 0.3, 0.25 } }
    #else box { P - 0.22, P + 0.22 }
    #end
  #end #end #end
#end
#declare Shapes = array[8];
#declare Shapes[0] = union { Bricks() sphere { 3, 1.5 inverse clipped_by { box { 0, 6 } } } }
#declare Shapes[1] = merge { #for (I, 0, N * N * 3) sphere { <rand(R), rand(R), rand(R)> * 6, 0.5 + rand(R) * 0.4 } #end }
#declare Shapes[2] = difference {
  box { 0, 6 }
  #for (I, 0, N * N * 3) sphere { <rand(R) * 6, rand(R) * 6, rand(R) * 6>, 0.25 + rand(R) * 0.3 } #end
  #for (I, 0, N * 2) cylinder { <rand(R) * 6, -1, -1>, <rand(R) * 6, 7, 7>, 0.15 } #end
  object { Cube scale 0.6 translate <6, 2, 3> }
}
#declare Shapes[3] = intersection {
  #for (I, 0, N * 4) sphere { vrotate(x, <rand(R) * 360, rand(R) * 360, 0>) * 4.6 + 3, 7 } #end
}
#declare Shapes[4] = union {
  #for (I, 0, 3) #for (J, 0, 3)
    union { #for (K, 0, 7) sphere { <I * 1.6 + rand(R), K * 0.8, J * 1.6 + rand(R)>, 0.35 } #end }
  #end #end
}
#declare Shapes[5] = union { #for (I, 0, N * N * 2) sphere { <rand(R), rand(R), rand(R)> * 6, 0.3 } #end inverse }
// a copy moved after the original was queried: its tree must follow the move
#declare Shapes[6] = union { Bricks() }
// a deferred mesh among spheres: the first query builds it, the second the union's tree
#declare Blob = isosurface_mesh { function { sqrt(x * x + y * y + z * z) - 1 } contained_by { sphere { 0, 1.2 } } }
#declare Shapes[7] = union {
  object { Blob scale 1.5 translate 3 }
  #for (I, 0, N * N) sphere { <rand(R), rand(R), rand(R)> * 6, 0.4 } #end
}
#declare Out = "csg_child_tree_parse.txt";
#fopen F Out write
#macro Query(Index, Count)
  #for (Q, 1, Count)
    #local O = <rand(R), rand(R), rand(R)> * 10 - 2;
    #local D = <rand(R), rand(R), rand(R)> - 0.5;
    #local Normal = <0, 0, 0>;
    #local Hit = trace(Shapes[Index], O, D, Normal);
    #local In = inside(Shapes[Index], <rand(R), rand(R), rand(R)> * 7 - 0.5);
    #write (F, Index, " ", vstr(3, Hit, " ", 0, 17), " ", vstr(3, Normal, " ", 0, 17), " ", In, "\n")
  #end
#end
#for (S, 0, 7) Query(S, 300) #end
#declare Shapes[6] = object { Shapes[6] rotate <10, 20, 30> translate <0.5, -0.25, 1> }
Query(6, 300)
#declare Shapes[0] = object { Shapes[0] scale <1, 1.5, 1> }
Query(0, 300)
// fresh copies, each asked once and moved after
#for (C, 1, 100)
  #declare Shapes[1] = object { Shapes[1] translate <rand(R), rand(R), rand(R)> * 0.01 }
  #declare Normal = <0, 0, 0>;
  #declare Hit = trace(Shapes[1], <rand(R), rand(R), rand(R)> * 10 - 2, <rand(R), rand(R), rand(R)> - 0.5, Normal);
  #write (F, "1 ", vstr(3, Hit, " ", 0, 17), " ", vstr(3, Normal, " ", 0, 17), "\n")
#end
#fclose F
#debug "csg_child_tree_parse: queries written\n"
camera { location <3, 3, -10> look_at <3, 3, 3> }
