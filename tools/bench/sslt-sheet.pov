// Subsurface on objects with no inside, lit from behind: a leaf as a mesh2 without inside_vector, the same leaf as a
// union of triangles, and a solid sphere sharing their texture; see doc/PERF.md.
#version 3.7;
#ifndef (Method) #declare Method = 1; #end
global_settings {
  assumed_gamma 1.0
  mm_per_unit 80
  subsurface { samples 74, 12 method Method }
}

camera { location <0, 0.9, -3.2> look_at <0, 0.55, 0> angle 55 right x * 4 / 3 }

light_source { <-3, 5, 12>, rgb <1.0, 0.95, 0.85> * 1.6 }
light_source { <4, 3, -6>, rgb <0.6, 0.7, 1.0> * 0.2 }
sky_sphere { pigment { gradient y color_map { [0 rgb <0.55, 0.6, 0.65>] [1 rgb <0.15, 0.25, 0.5>] } } }
plane { y, 0 pigment { rgb <0.35, 0.3, 0.25> } }

#declare Leaf = texture {
  pigment { rgb <0.3, 0.62, 0.18> }
  finish { diffuse 0.6, 0.3 subsurface { translucency <0.45, 1, 0.55> } }
}

// A point of an upright, curved, pointed leaf of height 1.2, at S along it and T across it, both in [0, 1] and [-1, 1].
#macro LeafPoint(S, T)
  #local Half = 0.01 + 0.35 * sin(pi * S) * (1 - 0.3 * S);
  <T * Half, 1.2 * S, 0.25 * S * S - 0.12 * T * T>
#end
#declare NL = 16;
#declare NW = 8;

mesh2 {
  vertex_vectors { (NL + 1) * (NW + 1),
    #for (J, 0, NL) #for (K, 0, NW) LeafPoint(J / NL, 2 * K / NW - 1), #end #end
  }
  face_indices { 2 * NL * NW,
    #for (J, 0, NL - 1) #for (K, 0, NW - 1)
      #local A = J * (NW + 1) + K;
      <A, A + 1, A + NW + 2>, <A, A + NW + 2, A + NW + 1>,
    #end #end
  }
  texture { Leaf }
  interior { ior 1.4 }
  translate x * -0.95
}

union {
  #for (J, 0, NL - 1) #for (K, 0, NW - 1)
    #local P00 = LeafPoint(J / NL, 2 * K / NW - 1);
    #local P01 = LeafPoint(J / NL, 2 * (K + 1) / NW - 1);
    #local P10 = LeafPoint((J + 1) / NL, 2 * K / NW - 1);
    #local P11 = LeafPoint((J + 1) / NL, 2 * (K + 1) / NW - 1);
    triangle { P00, P01, P11 }
    triangle { P00, P11, P10 }
  #end #end
  texture { Leaf }
  interior { ior 1.4 }
}

sphere { <0.95, 0.4, 0>, 0.4 texture { Leaf } interior { ior 1.4 } }
