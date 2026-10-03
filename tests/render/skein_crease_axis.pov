// skein_crease_axis.pov: crease about a straight hinge anywhere in space is exact, about a curved one it rolls by the frame at each station; handedness, signed radius, varying radius and angle, arc length.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 900;
#declare CheckLattice = 0.6;
#include "skein_check.inc"

#macro Report(Name, E, Limit)
  #debug concat(Name, ": off by at most ", str(E*1e9, 0, 3), "e-9\n")
  #if (E > Limit) #declare Failures = Failures + 1; #end
#end

// Probe(A, Q, M): a ray from just off Q along M, back toward Q, meets A at Q; the hit's distance from Q, and from M of its normal when Normal.
#macro Probe(A, Q, M, Normal)
  #local N = <0, 0, 0>;
  #local P = trace(A, Q + 0.03*M, -M, N);
  #if (vlength(N) = 0)
    1
  #else
    max(vlength(P - Q), (Normal ? min(vlength(vnormalize(N) - M), vlength(vnormalize(N) + M)) : 0))
  #end
#end

// Where the hinge through O along T (z its curl normal, the half along T x z moving) puts the sheet point P, and the normal M there.
#macro Rolled(P, O, T, R, Alpha)
  #local A = vcross(T, z);
  #local Sg = (R < 0 ? -1 : 1);
  #local Rho = vdot(P - O, A);
  #local Phi = min(Rho/abs(R), radians(Alpha));
  #local Extra = Rho - Phi*abs(R);
  #declare M = -A*sin(Phi) + Sg*z*cos(Phi);
  (O + T*vdot(P - O, T) + A*(abs(R)*sin(Phi) + Extra*cos(Phi)) + Sg*z*(abs(R)*(1 - cos(Phi)) + Extra*sin(Phi)))
#end

#declare Sheet = expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1> }
#declare Thick = expressions { envelope { thickness function(uv) { 0.08*(1 - pow(2*uv.v - 1, 8)) }  edge round } }

// 1: a straight path is the direction axis moved to the path, and the curved machinery on the same line agrees
#declare ByPath = skein { expressions { Sheet  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius 0.12  angle 300 }  Thick } closed u  ends sealed }
#declare ByDirection = skein { expressions { Sheet  translate <-0.2, 0, 0>  crease { axis y  radius 0.12  angle 300 }  translate <0.2, 0, 0>  Thick } closed u  ends sealed }
#declare ByCurve = skein { expressions { Sheet  crease { axis function(t) { 0.2 }, function(t) { t }, function(t) { 0 }  radius 0.12  angle 300 }  Thick } closed u  ends sealed }
PairCheck("straight path against a direction axis", ByPath, ByDirection, 1e-8, 1)
PairCheck("straight path against three functions on the same line", ByCurve, ByPath, 1e-8, 1)

// 2: handedness: for a path along +y the +x half rolls toward +z; reversed, the -x half; a negative radius curls toward -z
#declare Forward = skein { expressions { Sheet  crease { axis path { <0, 0, 0>, <0, 1, 0> }  radius 0.15  angle 120 } } }
#declare Reversed = skein { expressions { Sheet  crease { axis path { <0, 1, 0>, <0, 0, 0> }  radius 0.15  angle 120 } } }
#declare Mirrored = skein { expressions { Sheet  crease { axis path { <0, 0, 0>, <0, 1, 0> }  radius 0.15  angle 120 }  scale <-1, 1, 1> } }
#declare Under = skein { expressions { Sheet  crease { axis path { <0, 0, 0>, <0, 1, 0> }  radius -0.15  angle 120 } } }
#declare Flipped = skein { expressions { Sheet  crease { axis path { <0, 0, 0>, <0, 1, 0> }  radius 0.15  angle 120 }  scale <1, 1, -1> } }
#declare OnSurface = skein { expressions { Sheet  crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.15  angle 120 } } }
SheetCheck("a sample_path hinge on the sheet against the same line in space", OnSurface, Forward, 1e-8)
SheetCheck("reversed path against the forward crease mirrored", Reversed, Mirrored, 1e-8)
SheetCheck("negative radius against the positive one flipped", Under, Flipped, 1e-8)
#declare E = 0;
#for (I, 0, 8)
  #declare Y = 0.1 + 0.1*I;
  #declare E = max(E, Probe(Forward, <-0.3 - 0.05*I, Y, 0>, z, 1));
  #declare Q = Rolled(<0.05 + 0.04*I, Y, 0>, <0, 0, 0>, y, 0.15, 120);
  #declare E = max(E, Probe(Forward, Q, M, 1));
  #declare Q = Rolled(<0.05 + 0.04*I, Y, 0>, <0, 0, 0>, y, -0.15, 120);
  #declare E = max(E, Probe(Under, Q, M, 1));
#end
Report("near half flat, far half rolled toward +z, or -z for a negative radius, by probes", E, 1e-9)

// 3: the page corner: a diagonal hinge off the origin, curling toward the reader, is exact, and its near side is the sheet
#declare Page = skein { expressions { translate <-0.5, 0, 0>  scale <0.85, 1.1, 1>  crease { axis path { <0.3, 0.6, 0>, <-0.7, 1.6, 0> }  radius -0.07 } } }
#declare PageT = vnormalize(<-1, 1, 0>);
#declare Stream = seed(5);
#declare Near = 0;
#declare Far = 0;
#for (I, 1, 60)
  #declare P = <0.85*rand(Stream) - 0.425, 1.1*rand(Stream), 0>;
  #declare Rho = vdot(P - <0.3, 0.6, 0>, vcross(PageT, z));
  #if (Rho < 0)
    #declare Near = max(Near, Probe(Page, P, z, 1));
  #elseif (Rho < 0.95*2*pi*0.07)
    #declare Q = Rolled(P, <0.3, 0.6, 0>, PageT, -0.07, 1e9);
    #declare Far = max(Far, Probe(Page, Q, M, 1));
  #end
#end
Report("page corner, near side against the flat sheet", Near, 1e-9)
Report("page corner, rolled side against the exact roll", Far, 1e-9)

// 4: a radius varying along the hinge is a unit crease of the sheet scaled by it across the hinge, and lands at arc length on it
#declare RadiusOf = function(v) { 0.1 + 0.15*v }
#declare Varying = skein { expressions { Sheet  crease { axis y  radius function(v) { RadiusOf(v) }  angle 200 } } }
#declare Sandwich = skein { expressions { Sheet  scale { x function(v) { 1/RadiusOf(v) } }  crease { axis y  radius 1  angle 200 }  scale { x function(v) { RadiusOf(v) }  z function(v) { RadiusOf(v) } } } }
SheetCheck("radius varying along the hinge against a unit crease scaled by it", Varying, Sandwich, 1e-8)
#declare E = 0;
#for (I, 0, 8)
  #declare Y = 0.1 + 0.1*I;
  #declare Q = Rolled(<0.6*RadiusOf(Y) + 0.1*I*RadiusOf(Y), Y, 0>, <0, 0, 0>, y, RadiusOf(Y), 200);
  #declare E = max(E, Probe(Varying, Q, M, 0));
#end
Report("radius varying along the hinge, points at arc length on their own circle", E, 1e-9)

// 5: an angle varying along the hinge ends each row's roll there, running on straight with the normal its slope gives
#declare AngleOf = function(v) { 60 + 120*v }
#declare Ending = skein { expressions { Sheet  crease { axis y  radius 0.1  angle function(v) { AngleOf(v) } } } }
#declare E = 0;
#for (I, 0, 8)
  #declare Y = 0.1 + 0.1*I;
  #declare Rho = 0.1*radians(AngleOf(Y)) + 0.05 + 0.02*I;
  #declare Q = Rolled(<Rho, Y, 0>, <0, 0, 0>, y, 0.1, AngleOf(Y));
  #declare Alpha = radians(AngleOf(Y));
  #declare Slope = y + radians(120)*(Rho - 0.1*Alpha)*<-sin(Alpha), 0, cos(Alpha)>;
  #declare M = vnormalize(vcross(<cos(Alpha), 0, sin(Alpha)>, Slope));
  #declare E = max(E, Probe(Ending, Q, M, 1));
#end
Report("angle varying along the hinge, points and normals on the straight run past it", E, 1e-9)

// 6: a curved hinge keeps arc length across it, is the same shape whatever the path's extent past the sheet, and thickened is a closed solid
#declare Bow = function(s) { 0.25*s*s - 0.1 }
#declare BowSheet = skein { expressions { Sheet  crease { axis function(t) { Bow(t) }, function(t) { t }, function(t) { 0 }  radius 0.15  angle 150 } } }
#declare E = 0;
#for (I, 0, 8)
  #declare Y = 0.1 + 0.1*I;
  #declare C = <Bow(Y), Y, 0>;
  #declare T = vnormalize(<0.5*Y, 1, 0>);
  #declare Q = Rolled(<Bow(Y) + 0.03 + 0.025*I, Y, 0>, C, T, 0.15, 150);
  #declare E = max(E, Probe(BowSheet, Q, M, 0));
#end
Report("curved hinge, points at arc length on the turning circle at their station", E, 1e-9)
#declare BowPath = skein { expressions { Sheet  crease { axis path { <-0.1, 0, 0> handle <0, 1/3, 0>, <0.15, 1, 0> handle <1/6, 1/3, 0> }  radius 0.12  angle 300 }  Thick } closed u  ends sealed }
#declare BowLong = skein { expressions { Sheet  crease { axis path { -0.25, <-0.084375, -0.25, 0> handle <-0.0625, 0.5, 0>, 1.25, <0.290625, 1.25, 0> handle <0.3125, 0.5, 0> }  radius 0.12  angle 300 }  Thick } closed u  ends sealed }
PairCheck("curved hinge, the parabola as a path ending at the sheet's edges against one running past them", BowPath, BowLong, 1e-8, 1)
ClosedCheck("curved hinge thickened by an envelope", BowPath)

#if (Failures > 0)
  #error concat("skein_crease_axis: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_crease_axis: every check within tolerance\n"
