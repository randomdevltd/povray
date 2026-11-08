// Layers of clear mesh bricks, a few of them opaque, between a surface light and a floor; shadow rays cross many mesh boundaries.
#version 3.8;
#ifndef (Nx) #declare Nx = 10; #end
#ifndef (Nz) #declare Nz = 6; #end
#ifndef (Ny) #declare Ny = 6; #end
#ifndef (OpaqueEvery) #declare OpaqueEvery = 11; #end
#ifndef (Samples) #declare Samples = 16; #end
// Boxes=1 builds the bricks from box primitives instead, a reference for the mesh crossings.
#ifndef (Boxes) #declare Boxes = 0; #end
global_settings { assumed_gamma 1 max_trace_level 8 }
camera { location <-7, 9, -13> look_at <0, 1.5, 0> angle 48 }

plane { y, 0 pigment { checker rgb 0.75, rgb 0.55 } finish { ambient 0 diffuse 0.85 } }

// The lamp: a thin slab above the bricks, lit by its own surface light.
box { <-4, 8, -3>, <4, 8.1, 3>
  pigment { rgb 1 } finish { ambient 0 diffuse 0 emission 1 }
  light { pigment { rgb <1, 0.95, 0.9> } brightness 3 nits samples Samples }
}

// A closed unit box as twelve triangles, so it tests inside like a solid.
#if (Boxes)
#declare Brick = box { 0, 1 }
#else
#declare Brick = mesh {
  triangle { <0, 0, 0>, <1, 0, 0>, <1, 1, 0> } triangle { <0, 0, 0>, <1, 1, 0>, <0, 1, 0> }
  triangle { <0, 0, 1>, <1, 1, 1>, <1, 0, 1> } triangle { <0, 0, 1>, <0, 1, 1>, <1, 1, 1> }
  triangle { <0, 0, 0>, <0, 1, 0>, <0, 1, 1> } triangle { <0, 0, 0>, <0, 1, 1>, <0, 0, 1> }
  triangle { <1, 0, 0>, <1, 1, 1>, <1, 1, 0> } triangle { <1, 0, 0>, <1, 0, 1>, <1, 1, 1> }
  triangle { <0, 0, 0>, <1, 0, 1>, <1, 0, 0> } triangle { <0, 0, 0>, <0, 0, 1>, <1, 0, 1> }
  triangle { <0, 1, 0>, <1, 1, 0>, <1, 1, 1> } triangle { <0, 1, 0>, <1, 1, 1>, <0, 1, 1> }
  inside_vector y
}
#end
#declare Clear = texture { pigment { rgbf <0.85, 0.95, 1, 0.85> } finish { ambient 0 diffuse 0.05 specular 0.3 } }
#declare Solid = texture { pigment { rgb <0.7, 0.35, 0.25> } finish { ambient 0 diffuse 0.8 } }

#for (K, 0, Ny - 1)
  #for (I, 0, Nx - 1)
    #for (J, 0, Nz - 1)
      #local N = I + J * Nx + K * Nx * Nz;
      object { Brick scale <0.7, 0.3, 0.45>
        translate <(I - Nx / 2) * 0.75 + mod(K, 2) * 0.37, 1.0 + K * 0.6, (J - Nz / 2) * 0.5>
        texture { #if (mod(N, OpaqueEvery) = 0) Solid #else Clear #end }
      }
    #end
  #end
#end
