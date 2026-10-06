// skein_mesh_check.pov: skein_mesh against its skein in hits, normals and inside(); stops with an error past tolerance. skein.sh checks the counts.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 2000;
#include "skein_shapes.inc"
#include "skein_check.inc"

// MeshCheck: rays hit the skein A and its mesh M alike, M's hits lie within Chord of the surface, normals turn past Angle degrees at no more
// than a fraction Wide of hits (the strips along creases), and inside() agrees off the surface.
#macro MeshCheck(Name, A, M, Distance, Chord, Angle, Wide)
  #local Lo = min_extent(A) - 0.1;
  #local Hi = max_extent(A) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Hits = 0;
  #local Odd = 0;
  #local Off = 0;
  #local Turn = 0;
  #local Wider = 0;
  #local H = 1e-5;
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
      #local G = <Distance(PM.x + H, PM.y, PM.z) - Distance(PM.x - H, PM.y, PM.z), Distance(PM.x, PM.y + H, PM.z) - Distance(PM.x, PM.y - H, PM.z),
                  Distance(PM.x, PM.y, PM.z + H) - Distance(PM.x, PM.y, PM.z - H)>;
      #local T = degrees(acos(min(1, vdot(vnormalize(NM), vnormalize(G)))));
      #local Turn = max(Turn, T);
      #local Wider = Wider + (T > Angle);
    #end
  #end
  #local Cells = 16;
  #local Agree = 0;
  #local Counted = 0;
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
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(CheckRays, 0, 0), " rays hit the mesh, ", str(Odd, 0, 0), " hit only one\n")
  #debug concat("  max chord error ", str(Off, 0, 6), " (limit ", str(Chord, 0, 6), "), max normal turn ", str(Turn, 0, 3), " degrees\n")
  #debug concat("  normal past ", str(Angle, 0, 0), " degrees at ", str(Wider, 0, 0), " hits; inside agrees at ", str(Agree, 0, 0), " of ", str(Counted, 0, 0), "\n")
  #if ((Hits = 0) | (Odd > 0.01 * CheckRays) | (Off > Chord) | (Wider > Wide * Hits) | (Agree < Counted))
    #declare Failures = Failures + 1;
  #end
#end

#declare TorusDistance = function(x, y, z) { sqrt(pow(sqrt(y*y + z*z) - 2, 2) + x*x) - 0.5 }
#declare HexDistance = function(x, y, z) {
  max(max(abs(cos(pi/6)*x - 0.5*z), abs(z), abs(cos(pi/6)*x + 0.5*z)) - 0.5*cos(pi/6), -y, y - 3)
}
#declare SphereDistance = function(x, y, z) { sqrt(x*x + y*y + z*z) - 1 }
#declare SlabDistance = function(x, y, z) { max(-x, x - 2, -y, y - 1, abs(z) - 0.05) }

// 64 cells of 5.625 degrees each way: the sagitta of both circles bounds the chord error
#declare Step = 2*pi/64;
#declare TorusMesh = skein_mesh { expressions { extrude { radius 0.5 }  extrude { axis x  radius 2 } } closed uv  max_angle 10 }
MeshCheck("torus mesh", SkeinTorus, TorusMesh, TorusDistance, 2.5*(1 - cos(Step/2)) + 0.5*(1 - cos(Step/2)) + 1e-5, 10, 0)
#declare Uses = union { object { TorusMesh } object { TorusMesh translate 6*x } object { TorusMesh scale 0.5 translate -6*x } }
#if (!inside(Uses, <6, 2, 0>) | !inside(Uses, <-6, 1, 0>)) #declare Failures = Failures + 1; #end

#declare HexMesh = skein_mesh { expressions { scale <1, 3, 1>  extrude { radius function(u) { Hexagon(u) } } } closed u  ends flat }
MeshCheck("hexagonal column mesh, creases and flat caps", SkeinHexColumn, HexMesh, HexDistance, 0.004, 10, 0.02)

#declare Sphere = skein {
  expressions {
    extrude { radius map { uv.v  sin { frequency 0.5 } } }
    bend { axis y  translate sum { map { uv.v  cos { frequency 0.5  amplitude -1 } }  map { uv.v  linear { scale -1 } } } }
  }
  closed u  ends pole
}
#declare SphereMesh = skein_mesh {
  expressions {
    extrude { radius map { uv.v  sin { frequency 0.5 } } }
    bend { axis y  translate sum { map { uv.v  cos { frequency 0.5  amplitude -1 } }  map { uv.v  linear { scale -1 } } } }
  }
  closed u  ends pole  max_angle 10
}
MeshCheck("sphere mesh, poles", Sphere, SphereMesh, SphereDistance, 2*(1 - cos(Step/2)) + 1e-5, 10, 0)

#declare Slab = skein { expressions { scale <2, 1, 1>  envelope { thickness 0.1  edge flat } } closed u  ends flat }
#declare SlabMesh = skein_mesh { expressions { scale <2, 1, 1>  envelope { thickness 0.1  edge flat } } closed u  ends flat }
MeshCheck("envelope slab mesh, front, back, flat rims and caps", Slab, SlabMesh, SlabDistance, 0.003, 10, 0.02)

#declare Taper = function(u, v) { 0.1*sin(pi*u)*sin(pi*v) }
#declare TaperedMesh = skein_mesh { expressions { scale <2, 1, 1>  envelope { thickness function(uv) { Taper(uv.u, uv.v) } } } closed u  ends sealed }
ClosedCheck("tapered envelope mesh, sealed ends", TaperedMesh)

#declare ImageMesh = skein_mesh {
  expressions { extrude { radius sum { 0.4  map { uv  image { png "Mount1.png" }  linear { scale 0.1 } } } }  extrude { axis x radius 2 } }
  closed uv  min_size 0.02  max_angle 15
}
ClosedCheck("image-sampled mesh", ImageMesh)

#if (Failures > 0)
  #error concat("skein_mesh_check: ", str(Failures, 0, 0), " meshes outside tolerance\n")
#end
#debug "skein_mesh_check: every mesh within tolerance\n"
