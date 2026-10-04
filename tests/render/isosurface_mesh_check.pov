// isosurface_mesh_check.pov: isosurface_mesh against its isosurface in hits, inside() and a kept sharp edge; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "functions.inc"
#ifndef (CheckRays) #declare CheckRays = 2000; #end
#declare Failures = 0;
// the chord error of a surface of radius R whose normal turns max_angle (10 degrees) across a cell
#declare Sagitta = function(R) { R * (1 - cos(radians(10))) }

// MeshCheck: rays hit the isosurface A and its mesh M alike, M's hits lie within Chord of the surface, and for a solid inside() agrees off it.
#macro MeshCheck(Name, A, M, Distance, Chord, Solid)
  #local Lo = min_extent(M) - 0.1;
  #local Hi = max_extent(M) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Hits = 0;
  #local Odd = 0;
  #local Off = 0;
  #for (I, 1, CheckRays)
    #local Origin = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local Target = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
    #local NA = <0, 0, 0>;
    #local NM = <0, 0, 0>;
    #local PA = trace(A, Origin, Target - Origin, NA);
    #local PM = trace(M, Origin, Target - Origin, NM);
    #if ((vlength(NA) > 0) != (vlength(NM) > 0))
      #local Odd = Odd + 1;
    #end
    #if (vlength(NM) > 0)
      #local Hits = Hits + 1;
      #local Off = max(Off, abs(Distance(PM.x, PM.y, PM.z)));
    #end
  #end
  #local Cells = 16;
  #local Agree = 0;
  #local Counted = 0;
  #if (Solid)
    #for (I, 0, Cells - 1)
      #for (J, 0, Cells - 1)
        #for (K, 0, Cells - 1)
          #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
          #if (abs(Distance(Q.x, Q.y, Q.z)) > Chord)
            #local Counted = Counted + 1;
            #local Agree = Agree + (inside(A, Q) = inside(M, Q));
          #end
        #end
      #end
    #end
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(CheckRays, 0, 0), " rays hit the mesh, ", str(Odd, 0, 0), " hit only one\n")
  #debug concat("  max chord error ", str(Off, 0, 6), " (limit ", str(Chord, 0, 6), "); inside agrees at ", str(Agree, 0, 0), " of ", str(Counted, 0, 0), "\n")
  #if ((Hits = 0) | (Odd > 0.01 * CheckRays) | (Off > Chord) | (Agree < Counted))
    #declare Failures = Failures + 1;
  #end
#end

#declare SphereDistance = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 }
#declare Sphere = isosurface { function { SphereDistance(x, y, z) } contained_by { sphere { 0, 1.2 } } }
#declare SphereMesh = isosurface_mesh { function { SphereDistance(x, y, z) } contained_by { sphere { 0, 1.2 } } max_angle 10 }
MeshCheck("sphere, sampled", Sphere, SphereMesh, SphereDistance, Sagitta(1), 1)
#declare Uses = union { object { SphereMesh } object { SphereMesh translate 3*x } object { SphereMesh scale 0.5 translate -3*x } }
#if (!inside(Uses, <3, 0.9, 0>) | !inside(Uses, <-3, 0.45, 0>) | inside(Uses, <-3, 0.55, 0>)) #declare Failures = Failures + 1; #end

// a cube turned off the grid: range arithmetic culls its cells, and rays aimed along each edge's bisector must meet the edge itself
#declare U = vrotate(x, <20, 30, 10>);
#declare V = vrotate(y, <20, 30, 10>);
#declare W = vrotate(z, <20, 30, 10>);
#declare U1 = U.x; #declare U2 = U.y; #declare U3 = U.z;
#declare V1 = V.x; #declare V2 = V.y; #declare V3 = V.z;
#declare W1 = W.x; #declare W2 = W.y; #declare W3 = W.z;
#declare CubeDistance = function(x, y, z) {
  max(abs(U1*x + U2*y + U3*z), abs(V1*x + V2*y + V3*z), abs(W1*x + W2*y + W3*z)) - 0.8
}
#declare Cube = isosurface { function { CubeDistance(x, y, z) } contained_by { box { -1.5, 1.5 } } }
#declare CubeMesh = isosurface_mesh { function { CubeDistance(x, y, z) } contained_by { box { -1.5, 1.5 } } min_size 0.01 }
MeshCheck("turned cube, range-checked", Cube, CubeMesh, CubeDistance, 0.002, 1)
#declare Stream = seed(35);
#declare EdgeOff = 0;
#for (I, 1, 400)
  #declare A = int(3 * rand(Stream));
  #declare S1 = 2 * int(2 * rand(Stream)) - 1;
  #declare S2 = 2 * int(2 * rand(Stream)) - 1;
  #declare T = 1.4 * rand(Stream) - 0.7;
  #declare L = (A = 0 ? <T, 0.8*S1, 0.8*S2> : (A = 1 ? <0.8*S1, T, 0.8*S2> : <0.8*S1, 0.8*S2, T>));
  #declare D = (A = 0 ? <0, S1, S2> : (A = 1 ? <S1, 0, S2> : <S1, S2, 0>)) / sqrt(2);
  #declare P = U * L.x + V * L.y + W * L.z;
  #declare Out = U * D.x + V * D.y + W * D.z;
  #declare N = <0, 0, 0>;
  #declare H = trace(CubeMesh, P + 0.5 * Out, -Out, N);
  #declare EdgeOff = max(EdgeOff, (vlength(N) > 0 ? vlength(H - P) : 1));
#end
#debug concat("  edges: rays along the bisectors miss the edge by at most ", str(EdgeOff, 0, 6), " (limit 0.001)\n")
#if (EdgeOff > 0.001) #declare Failures = Failures + 1; #end

// a sphere wider than its box: closed, the box's faces cap it; open, the mesh stops at the box as the isosurface does
#declare CutDistance = function(x, y, z) { max(SphereDistance(x/1.3, y/1.3, z/1.3)*1.3, abs(x) - 1, abs(y) - 1, abs(z) - 1) }
#declare Cut = isosurface { function { SphereDistance(x/1.3, y/1.3, z/1.3)*1.3 } contained_by { box { -1, 1 } } }
#declare CutMesh = isosurface_mesh { function { SphereDistance(x/1.3, y/1.3, z/1.3)*1.3 } contained_by { box { -1, 1 } } }
MeshCheck("sphere closed by its box", Cut, CutMesh, CutDistance, Sagitta(1.3), 1)
#declare OpenDistance = function(x, y, z) { SphereDistance(x/1.3, y/1.3, z/1.3)*1.3 }
#declare Open = isosurface { function { OpenDistance(x, y, z) } contained_by { box { -1, 1 } } open }
#declare OpenMesh = isosurface_mesh { function { OpenDistance(x, y, z) } contained_by { box { -1, 1 } } open }
MeshCheck("sphere left open by its box", Open, OpenMesh, OpenDistance, Sagitta(1.3), 0)

// polarity and threshold: inside where 1 - r^2 is above 0.19, so a sphere of radius 0.9
#declare Ball = isosurface { function { 1 - (x*x + y*y + z*z) } threshold 0.19 polarity 1 max_gradient 2 }
#declare BallMesh = isosurface_mesh { function { 1 - (x*x + y*y + z*z) } threshold 0.19 polarity 1 max_gradient 2 }
#declare BallDistance = function(x, y, z) { sqrt(x*x + y*y + z*z) - 0.9 }
MeshCheck("polarity and threshold", Ball, BallMesh, BallDistance, Sagitta(0.9), 1)

// a realistic shape: a sphere displaced by noise, no range arithmetic for f_noise3d
#declare Rough = function(x, y, z) { SphereDistance(x, y, z) + 0.3 * (f_noise3d(3*x, 3*y, 3*z) - 0.5) }
#declare RoughIso = isosurface { function { Rough(x, y, z) } contained_by { sphere { 0, 1.3 } } max_gradient 2.5 }
#declare RoughMesh = isosurface_mesh { function { Rough(x, y, z) } contained_by { sphere { 0, 1.3 } } max_gradient 2.5 }
MeshCheck("noisy sphere", RoughIso, RoughMesh, Rough, Sagitta(1), 1)

#if (Failures > 0)
  #error concat("isosurface_mesh_check: ", str(Failures, 0, 0), " meshes outside tolerance\n")
#end
#debug "isosurface_mesh_check: every mesh within tolerance\n"
