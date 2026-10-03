// skein_check.inc: Check(Name, Skein, Primitive, Distance) compares a skein with the primitive it equals; SurfaceCheck(Name, Skein, Implicit) tests it against an implicit surface; PairCheck(Name, A, B, Limit, Solid) compares two skeins and SheetCheck(Name, A, B, Limit) two open sheets; ClosedCheck(Name, A) tests that a solid is closed; InsideCheck and BoundaryCheck compare and probe CSG.
#declare Tolerance = 1e-6;
#declare Failures = 0;
#declare CrossingOverflow = 0;
#declare MaxCrossings = 64;
// a scene whose shapes are very costly may scale the sampling down before including this
#ifndef (CheckRays) #declare CheckRays = 4000; #end
#ifndef (CheckLattice) #declare CheckLattice = 1; #end

#macro Check(Name, A, B, Distance)
  #local Lo = min_extent(B) - 0.1;
  #local Hi = max_extent(B) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Rays = CheckRays;
  #local Hits = 0;
  #local Odd = 0;
  #local Apart = 0;
  #local PrimitiveOff = 0;
  #local SkeinOff = 0;
  #local PointError = 0;
  #local NormalError = 0;
  #local SkeinSurface = 0;
  #local PrimitiveSurface = 0;
  #local Inward = 0;
  #for (I, 1, Rays)
    #local Origin = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local Target = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
    #local NA = <0, 0, 0>;
    #local NB = <0, 0, 0>;
    #local PA = trace(A, Origin, Target - Origin, NA);
    #local PB = trace(B, Origin, Target - Origin, NB);
    #if ((vlength(NA) > 0) != (vlength(NB) > 0))
      #local Odd = Odd + 1;
    #elseif (vlength(NA) > 0)
      #local Hits = Hits + 1;
      #local DA = abs(Distance(PA.x, PA.y, PA.z));
      #local DB = abs(Distance(PB.x, PB.y, PB.z));
      #local SkeinSurface = max(SkeinSurface, DA);
      #local PrimitiveSurface = max(PrimitiveSurface, DB);
      #if (vlength(PA - PB) > Tolerance)
        #local Apart = Apart + 1;
        #if (DB > DA)
          #local PrimitiveOff = PrimitiveOff + 1;
        #else
          #local SkeinOff = SkeinOff + 1;
        #end
      #end
      #local PointError = max(PointError, vlength(PA - PB));
      #local NormalError = max(NormalError, min(vlength(vnormalize(NA) - vnormalize(NB)), vlength(vnormalize(NA) + vnormalize(NB))));
      #local Outward = <Distance(PA.x + 1e-4, PA.y, PA.z) - Distance(PA.x - 1e-4, PA.y, PA.z),
                        Distance(PA.x, PA.y + 1e-4, PA.z) - Distance(PA.x, PA.y - 1e-4, PA.z),
                        Distance(PA.x, PA.y, PA.z + 1e-4) - Distance(PA.x, PA.y, PA.z - 1e-4)>;
      #local Inward = Inward + (vdot(NA, Outward) < 0);
    #end
  #end
  #local Cells = max(4, int(20 * CheckLattice));
  #local Agree = 0;
  #local InsideA = 0;
  #for (I, 0, Cells - 1)
    #for (J, 0, Cells - 1)
      #for (K, 0, Cells - 1)
        #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
        #local InsideA = InsideA + inside(A, Q);
        #local Agree = Agree + (inside(A, Q) = inside(B, Q));
      #end
    #end
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(Rays, 0, 0), " rays hit both, ", str(Odd, 0, 0), " hit only one\n")
  #debug concat("  off the exact surface: skein ", str(SkeinSurface * 1e9, 0, 3), "e-9, primitive ", str(PrimitiveSurface * 1e9, 0, 3), "e-9\n")
  #debug concat("  skein vs primitive: max point ", str(PointError * 1e9, 0, 3), "e-9, max normal ", str(NormalError * 1e9, 0, 3), "e-9 (either sign)\n")
  #debug concat("  skein normals pointing inward: ", str(Inward, 0, 0), "\n")
  #debug concat("  hits apart by more than tolerance: ", str(Apart, 0, 0), " (primitive further off: ", str(PrimitiveOff, 0, 0), ")\n")
  #debug concat("  inside agrees at ", str(Agree, 0, 0), " of ", str(Cells * Cells * Cells, 0, 0), " points, ", str(InsideA, 0, 0), " inside\n")
  #if ((Hits = 0) | (Odd > 0) | (SkeinSurface > Tolerance) | (SkeinOff > 0) | (NormalError > Tolerance) | (Inward > 0) | (Agree < Cells * Cells * Cells))
    #declare Failures = Failures + 1;
  #end
#end

#macro SurfaceCheck(Name, A, F)
  #local Lo = min_extent(A) - 0.1;
  #local Hi = max_extent(A) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Rays = CheckRays;
  #local Hits = 0;
  #local Off = 0;
  #local NormalError = 0;
  #local H = 1e-5;
  #for (I, 1, Rays)
    #local Origin = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local Target = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
    #local N = <0, 0, 0>;
    #local P = trace(A, Origin, Target - Origin, N);
    #if (vlength(N) > 0)
      #local Hits = Hits + 1;
      #local Off = max(Off, abs(F(P.x, P.y, P.z)));
      #local G = <F(P.x + H, P.y, P.z) - F(P.x - H, P.y, P.z), F(P.x, P.y + H, P.z) - F(P.x, P.y - H, P.z), F(P.x, P.y, P.z + H) - F(P.x, P.y, P.z - H)>;
      #local NormalError = max(NormalError, vlength(vnormalize(N) - vnormalize(G)));
    #end
  #end
  #local Cells = max(4, int(20 * CheckLattice));
  #local Agree = 0;
  #local InsideA = 0;
  #for (I, 0, Cells - 1)
    #for (J, 0, Cells - 1)
      #for (K, 0, Cells - 1)
        #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
        #local InsideA = InsideA + inside(A, Q);
        #local Agree = Agree + (inside(A, Q) = (F(Q.x, Q.y, Q.z) < 0));
      #end
    #end
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(Rays, 0, 0), " rays hit\n")
  #debug concat("  off the implicit surface: ", str(Off * 1e9, 0, 3), "e-9, max normal ", str(NormalError * 1e9, 0, 3), "e-9 from its gradient\n")
  #debug concat("  inside agrees at ", str(Agree, 0, 0), " of ", str(Cells * Cells * Cells, 0, 0), " points, ", str(InsideA, 0, 0), " inside\n")
  #if ((Hits = 0) | (Off > Tolerance) | (NormalError > Tolerance) | (Agree < Cells * Cells * Cells))
    #declare Failures = Failures + 1;
  #end
#end

#macro PairCheck(Name, A, B, Limit, Solid)
  #local Lo = min_extent(A) - 0.1;
  #local Hi = max_extent(A) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Rays = CheckRays;
  #local Hits = 0;
  #local Odd = 0;
  #local PointError = 0;
  #local NormalError = 0;
  #for (I, 1, Rays)
    #local Origin = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local Target = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
    #local NA = <0, 0, 0>;
    #local NB = <0, 0, 0>;
    #local PA = trace(A, Origin, Target - Origin, NA);
    #local PB = trace(B, Origin, Target - Origin, NB);
    #if ((vlength(NA) > 0) != (vlength(NB) > 0))
      #local Odd = Odd + 1;
    #elseif (vlength(NA) > 0)
      #local Hits = Hits + 1;
      #local PointError = max(PointError, vlength(PA - PB));
      #local NormalError = max(NormalError, vlength(vnormalize(NA) - vnormalize(NB)));
    #end
  #end
  #local Cells = (Solid ? max(4, int(12 * CheckLattice)) : 0);
  #local Agree = 0;
  #for (I, 0, Cells - 1)
    #for (J, 0, Cells - 1)
      #for (K, 0, Cells - 1)
        #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
        #local Agree = Agree + (inside(A, Q) = inside(B, Q));
      #end
    #end
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(Rays, 0, 0), " rays hit both, ", str(Odd, 0, 0), " hit only one; max point ",
                str(PointError * 1e9, 0, 3), "e-9, max normal ", str(NormalError * 1e9, 0, 3), "e-9; inside agrees at ", str(Agree, 0, 0),
                " of ", str(Cells * Cells * Cells, 0, 0), "\n")
  #if ((Hits = 0) | (Odd > 0) | (PointError > Limit) | (NormalError > Limit) | (Agree < Cells * Cells * Cells))
    #declare Failures = Failures + 1;
  #end
#end

// SheetCheck: PairCheck for an open sheet, whose outward side is undecidable (its flux about its own centre is zero), so normals count up to sign.
#macro SheetCheck(Name, A, B, Limit)
  #local Lo = min_extent(A) - 0.1;
  #local Hi = max_extent(A) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(1616);
  #local Hits = 0;
  #local Odd = 0;
  #local PointError = 0;
  #local NormalError = 0;
  #for (I, 1, CheckRays)
    #local Origin = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local Target = Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)>;
    #local NA = <0, 0, 0>;
    #local NB = <0, 0, 0>;
    #local PA = trace(A, Origin, Target - Origin, NA);
    #local PB = trace(B, Origin, Target - Origin, NB);
    #if ((vlength(NA) > 0) != (vlength(NB) > 0))
      #local Odd = Odd + 1;
    #elseif (vlength(NA) > 0)
      #local Hits = Hits + 1;
      #local PointError = max(PointError, vlength(PA - PB));
      #local NormalError = max(NormalError, min(vlength(vnormalize(NA) - vnormalize(NB)), vlength(vnormalize(NA) + vnormalize(NB))));
    #end
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " of ", str(CheckRays, 0, 0), " rays hit both, ", str(Odd, 0, 0),
                " hit only one; max point ", str(PointError * 1e9, 0, 3), "e-9, max normal ", str(NormalError * 1e9, 0, 3), "e-9 (either sign)\n")
  #if ((Hits = 0) | (Odd > 0) | (PointError > Limit) | (NormalError > Limit))
    #declare Failures = Failures + 1;
  #end
#end

// Crossings of the ray from P along D, each counted once; a ray still hitting after MaxCrossings is counted in CrossingOverflow.
#macro Crossings(A, P, D)
  #local Count = 0;
  #local O = P;
  #local N = D;
  #while ((vlength(N) > 0) & (Count < MaxCrossings))
    #local Q = trace(A, O, D, N);
    #if (vlength(N) > 0)
      #local Count = Count + 1;
      #local O = Q + 1e-7*vnormalize(D);
    #end
  #end
  #if (vlength(N) > 0) #declare CrossingOverflow = CrossingOverflow + 1; #end
  Count
#end

// ClosedCheck(Name, A): inside() on a lattice agrees with the parity of crossings along two directions, as for any closed solid.
// why: a leak shows along both directions; one direction alone is a grazing ray the solver misses (doc/skein.md, batch two)
#macro ClosedCheck(Name, A)
  #local Lo = min_extent(A) - 0.05;
  #local Hi = max_extent(A) + 0.05;
  #local Cells = max(4, int(10 * CheckLattice));
  #local Agree = 0;
  #local Neither = 0;
  #declare CrossingOverflow = 0;
  #local InsideA = 0;
  #for (I, 0, Cells - 1)
    #for (J, 0, Cells - 1)
      #for (K, 0, Cells - 1)
        #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
        #local In = inside(A, Q);
        #local InsideA = InsideA + In;
        #local AX = (mod(Crossings(A, Q, x), 2) = In);
        #local AS = (mod(Crossings(A, Q, <0.31, 0.52, -0.79>), 2) = In);
        #local Agree = Agree + (AX & AS);
        #local Neither = Neither + (!AX & !AS);
      #end
    #end
  #end
  #debug concat(Name, ": inside() agrees with crossing parity along two directions at ", str(Agree, 0, 0), " of ",
                str(Cells * Cells * Cells, 0, 0), " points (", str(Neither, 0, 0), " along neither), ", str(InsideA, 0, 0), " inside\n")
  #if (CrossingOverflow > 0) #debug concat("  rays with more than ", str(MaxCrossings, 0, 0), " crossings: ", str(CrossingOverflow, 0, 0), "\n") #end
  #if ((CrossingOverflow > 0) | (Neither > 0) | (Agree < 0.99 * Cells * Cells * Cells) | (InsideA = 0))
    #declare Failures = Failures + 1;
  #end
#end

// InsideCheck(Name, A, B, Cells): inside() of two solids agrees on a Cells^3 lattice over A's box.
#macro InsideCheck(Name, A, B, Cells)
  #local Lo = min_extent(A) - 0.05;
  #local Hi = max_extent(A) + 0.05;
  #local Agree = 0;
  #local InsideA = 0;
  #for (I, 0, Cells - 1)
    #for (J, 0, Cells - 1)
      #for (K, 0, Cells - 1)
        #local Q = Lo + (Hi - Lo) * <I + 0.5123, J + 0.4871, K + 0.5317> / Cells;
        #local InsideA = InsideA + inside(A, Q);
        #local Agree = Agree + (inside(A, Q) = inside(B, Q));
      #end
    #end
  #end
  #debug concat(Name, ": inside() agrees at ", str(Agree, 0, 0), " of ", str(Cells * Cells * Cells, 0, 0), " points, ", str(InsideA, 0, 0), " inside\n")
  #if ((InsideA = 0) | (Agree < Cells * Cells * Cells))
    #declare Failures = Failures + 1;
  #end
#end

// BoundaryCheck(Name, A, Rays): every crossing traced along random rays flips inside(A), so no hit lies on an interior sheet.
#macro BoundaryCheck(Name, A, Rays)
  #local Lo = min_extent(A) - 0.1;
  #local Hi = max_extent(A) + 0.1;
  #local Centre = (Lo + Hi) / 2;
  #local Reach = vlength(Hi - Lo);
  #local Stream = seed(77);
  #local Hits = 0;
  #local Bad = 0;
  #local Overflow = 0;
  #for (I, 1, Rays)
    #local O = Centre + Reach * vnormalize(<rand(Stream) - 0.5, rand(Stream) - 0.5, rand(Stream) - 0.5>);
    #local D = vnormalize(Lo + (Hi - Lo) * <rand(Stream), rand(Stream), rand(Stream)> - O);
    #local N = D;
    #local Count = 0;
    #while ((vlength(N) > 0) & (Count < MaxCrossings))
      #local P = trace(A, O, D, N);
      #if (vlength(N) > 0)
        #local Count = Count + 1;
        #local Bad = Bad + (inside(A, P - 1e-5*D) = inside(A, P + 1e-5*D));
        #local O = P + 1e-7*D;
      #end
    #end
    #local Hits = Hits + Count;
    #local Overflow = Overflow + (vlength(N) > 0);
  #end
  #debug concat(Name, ": ", str(Hits, 0, 0), " crossings along ", str(Rays, 0, 0), " rays, ", str(Bad, 0, 0), " not between inside and outside\n")
  #if (Overflow > 0) #debug concat("  rays with more than ", str(MaxCrossings, 0, 0), " crossings: ", str(Overflow, 0, 0), "\n") #end
  #if ((Overflow > 0) | (Hits = 0) | (Bad > 0.002 * Hits))
    #declare Failures = Failures + 1;
  #end
#end
