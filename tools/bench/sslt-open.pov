// Case 0: a slab's top; 1: a plane of the same material, which should match it; 2: a clipped shell over a floor;
// 3: a block with a hollow cut by difference; 4: a block as a mesh wound inward, with an inside vector.
#version 3.7;
#ifndef (Case) #declare Case = 0; #end
#ifndef (Diffuse) #declare Diffuse = 200; #end
#ifndef (Method) #declare Method = 1; #end
global_settings { assumed_gamma 1.0 mm_per_unit 40 subsurface { samples Diffuse, 12 method Method } }
light_source { <-3, 6, -3>, rgb 1.2 }
#declare T = texture { pigment { rgb <0.9, 0.6, 0.3> } finish { diffuse 0.7 subsurface { translucency <3, 2, 1> } } }
#if (Case < 2)
  camera { location <0, 4, -0.01> look_at 0 angle 30 right x * 4 / 3 }
  #if (Case = 0) box { <-2, -1, -2>, <2, 0, 2> texture { T } interior { ior 1.4 } }
  #else plane { y, 0 texture { T } interior { ior 1.4 } }
  #end
#elseif (Case = 2)
  camera { location <0, 1.5, -4> look_at <0, 0.6, 0> angle 40 right x * 4 / 3 }
  plane { y, 0 pigment { rgb 0.5 } }
  sphere { <0, 0.6, 0>, 0.6 clipped_by { box { <-1, 0, -1>, <1, 0.9, 1> } } texture { T } interior { ior 1.4 } }
#else
  camera { location <0, 2.6, -3.2> look_at <0, 0.4, 0> angle 45 right x * 4 / 3 }
  plane { y, -0.01 pigment { rgb 0.5 } }
  #if (Case = 3)
    difference { box { <-1, 0, -1>, <1, 1, 1> } sphere { <0, 1, 0>, 0.5 } texture { T } interior { ior 1.4 } }
  #else
    #declare A = <-1, 0, -1>; #declare B = <1, 0, -1>; #declare C = <1, 0, 1>; #declare D = <-1, 0, 1>;
    #declare E = <-1, 1, -1>; #declare F = <1, 1, -1>; #declare G = <1, 1, 1>; #declare H = <-1, 1, 1>;
    mesh {
      triangle { A, C, B } triangle { A, D, C } triangle { E, F, G } triangle { E, G, H }
      triangle { A, B, F } triangle { A, F, E } triangle { B, C, G } triangle { B, G, F }
      triangle { C, D, H } triangle { C, H, G } triangle { D, A, E } triangle { D, E, H }
      inside_vector y texture { T } interior { ior 1.4 }
    }
  #end
#end
