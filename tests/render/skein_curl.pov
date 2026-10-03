// skein_curl.pov: a curl lands the sheet at its own arc length on the section its travel rolls, either hand and either side, thickened is closed, and a vanishing travel is near the crease.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 900;
#declare CheckLattice = 0.6;
#include "skein_check.inc"

// Travel(Turns, Step): a straight travel of Step each turn over Turns turns; the path holds still past its end, as any path does.
#macro Travel(Turns, Step) path { 0, <0, 0, 0>, Turns, Step*Turns } #end

#macro Report(Name, E, Limit)
  #debug concat(Name, ": off by at most ", str(E*1e9, 0, 3), "e-9\n")
  #if (E > Limit) #declare Failures = Failures + 1; #end
#end

// Probe(A, Q, M, Normal): a ray from just off Q along M, back toward Q, meets A at Q; the hit's distance from Q, and from M of its normal when Normal.
#macro Probe(A, Q, M, Normal)
  #local N = <0, 0, 0>;
  #local P = trace(A, Q + 0.004*M, -M, N);
  #if (vlength(N) = 0)
    1
  #else
    max(vlength(P - Q), (Normal ? min(vlength(vnormalize(N) - M), vlength(vnormalize(N) + M)) : 0))
  #end
#end

// The rolled section for a straight travel K per radian in (T, A, N'), from radius R0: C, C' and the speed |C'|.
#macro Section(Phi, R0, K) <K.x*Phi, K.y*Phi + (R0 + K.z*Phi)*sin(Phi), (R0 + K.z*Phi)*(1 - cos(Phi))> #end
#macro Slope(Phi, R0, K) <K.x, K.y + K.z*sin(Phi) + (R0 + K.z*Phi)*cos(Phi), K.z*(1 - cos(Phi)) + (R0 + K.z*Phi)*sin(Phi)> #end
#macro Speed(Phi, R0, K) vlength(Slope(Phi, R0, K)) #end

// Arc length of the section from From to To by Simpson at about 400 panels a turn.
#macro Arc(From, To, R0, K)
  #local Panels = 2*ceil(200*(To - From)/(2*pi) + 1);
  #local H = (To - From)/Panels;
  #local Sum = Speed(From, R0, K) + Speed(To, R0, K);
  #for (J, 1, Panels - 1)
    #local Sum = Sum + (mod(J, 2) = 1 ? 4 : 2)*Speed(From + J*H, R0, K);
  #end
  (Sum*H/3)
#end

// LengthCheck: the sheet point at material distance rho past the foot F lies at arc length rho along the section, with its normal, from angle First up to Last.
#macro LengthCheck(Name, S, F, T, A, Np, R0, K, Y, First, Last)
  #local E = 0;
  #local Rho = 0;
  #local From = 0;
  #for (I, 0, 8)
    #local To = (I = 0 ? First : Last*(I - 0.5)/8);
    #local Rho = Rho + Arc(From, To, R0, K);
    #local From = To;
    #local C = Section(To, R0, K);
    #local C1 = Slope(To, R0, K);
    #local Q = F + T*(Y + C.x) + A*C.y + Np*C.z;
    #local M = vnormalize(vcross(T, T*C1.x + A*C1.y + Np*C1.z));
    #local E = max(E, Probe(S, Q, M, 1));
  #end
  Report(concat(Name, ", ", str(Last/(2*pi), 0, 2), " turns, ", str(Rho, 0, 3), " of sheet: points and normals at arc length"), E, 1e-8)
#end

#declare Sheet = expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1> }
#declare Long = expressions { translate <-0.5, 0, 0>  scale <3, 1, 1> }
#declare Thick = expressions { envelope { thickness function(uv) { 0.08*(1 - pow(2*uv.v - 1, 8)) }  edge round } }
#declare Thin = expressions { envelope { thickness function(uv) { 0.02*(1 - pow(2*uv.v - 1, 8)) }  edge round } }

// 1: sanity check, not an equivalence: a travel of 1e-7 a turn barely widens the roll, so it lies within 1e-4 of the crease of the same radius, either sign, as a path and as a sample_path
#declare Up = skein { expressions { Sheet  curl { pivot path { <0.2, 0, 0.12>, <0.2, 1, 0.12> }  travel Travel(4, <0, 0, 1e-7>) }  Thick } closed u  ends sealed }
#declare UpCrease = skein { expressions { Sheet  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius 0.12  angle 3600 }  Thick } closed u  ends sealed }
#declare UpSample = skein { expressions { Sheet  curl { pivot sample_path { <0.625, 0, 0.12>, <0.625, 1, 0.12> }  travel Travel(4, <0, 0, 1e-7>) }  Thick } closed u  ends sealed }
#declare Down = skein { expressions { Sheet  curl { pivot sample_path { <0.625, 0, -0.12>, <0.625, 1, -0.12> }  travel Travel(4, <0, 0, -1e-7>) }  Thick } closed u  ends sealed }
#declare DownCrease = skein { expressions { Sheet  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius -0.12  angle 3600 }  Thick } closed u  ends sealed }
#declare Wound = skein { expressions { Long  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel Travel(4, <0, 0, 1e-7>) } } }
#declare WoundCrease = skein { expressions { Long  crease { axis path { <-1, 0, 0>, <-1, 1, 0> }  radius 0.1  angle 3600 } } }
PairCheck("a travel of 1e-7 a turn against the crease, under a turn thickened", Up, UpCrease, 1e-4, 1)
PairCheck("the same with a sample_path pivot", UpSample, UpCrease, 1e-4, 1)
PairCheck("a sample_path pivot below the sheet against the negative crease", Down, DownCrease, 1e-4, 1)
SheetCheck("a travel of 1e-7 a turn against the crease, three turns of sheet", Wound, WoundCrease, 1e-4)

// 2: with a travel the material lands at its own arc length on the section: a scroll (away from the sheet), a coil (along the pivot), a slide (along the sheet)
#declare Scroll = skein { expressions { Long  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel Travel(3, <0, 0, 0.03>) } } }
LengthCheck("scroll", Scroll, <-1, 0, 0>, y, x, z, 0.1, <0, 0, 0.03>/(2*pi), 0.6, 0.8, 2.6*2*pi)
#declare Held = skein { expressions { Long  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel path { <0, 0, 0>, <0, 0, 0.03> } } } }
#declare HeldOut = skein { expressions { Long  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel path { 0, <0, 0, 0>, 1, <0, 0, 0.03>, 4, <0, 0, 0.03> } } } }
SheetCheck("a travel of one turn holds still past its end, against a path that holds it", Held, HeldOut, 1e-8)
#declare Coil = skein { expressions { Long  scale <1, 0.1, 1>  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel Travel(4, <0, 0.25, 0>) } } }
LengthCheck("coil", Coil, <-1, 0, 0>, y, x, z, 0.1, <0.25, 0, 0>/(2*pi), 0.05, 0.01, 3.5*2*pi)
#declare Slide = skein { expressions { Sheet  curl { pivot path { <0, 0, 0.1>, <0, 1, 0.1> }  travel Travel(1, <0.05, 0, 0>) } } }
LengthCheck("slide", Slide, <0, 0, 0>, y, x, z, 0.1, <0, 0.05, 0>/(2*pi), 0.4, 0.01, 0.9*2*pi)

// 3: the roll starts tangent to the sheet it leaves; reversing the pivot rolls the other half, a pivot below the sheet curls the other way
#declare E = 0;
#for (I, 0, 8)
  #declare E = max(E, Probe(Scroll, <-1.25 - 0.025*I, 0.1 + 0.1*I, 0>, z, 1));
#end
Report("scroll, flat clear of the roll", E, 1e-9)
#declare Forward = skein { expressions { Sheet  curl { pivot path { <0, 0, 0.1>, <0, 1, 0.1> }  travel Travel(3, <0, 0, 0.03>) } } }
#declare Reversed = skein { expressions { Sheet  curl { pivot path { <0, 1, 0.1>, <0, 0, 0.1> }  travel Travel(3, <0, 0, 0.03>) } } }
#declare Mirrored = skein { expressions { Sheet  curl { pivot path { <0, 0, 0.1>, <0, 1, 0.1> }  travel Travel(3, <0, 0, 0.03>) }  scale <-1, 1, 1> } }
#declare Under = skein { expressions { Sheet  curl { pivot path { <0, 0, -0.1>, <0, 1, -0.1> }  travel Travel(3, <0, 0, -0.03>) } } }
#declare Flipped = skein { expressions { Sheet  curl { pivot path { <0, 0, 0.1>, <0, 1, 0.1> }  travel Travel(3, <0, 0, 0.03>) }  scale <1, 1, -1> } }
SheetCheck("reversed pivot against the forward roll mirrored", Reversed, Mirrored, 1e-8)
SheetCheck("pivot below the sheet against the roll above it flipped", Under, Flipped, 1e-8)

// 4: thickened, a scroll under a turn and a coil of three are closed solids; a scroll of three turns, whose turns all touch at the foot, matches itself with its pivot given on the surface
#declare Opening = skein { expressions { Long  curl { pivot path { <1, 0, 0.1>, <1, 1, 0.1> }  travel Travel(4, <0, 0, 0.05>) }  Thick } closed u  ends sealed }
#declare Spring = skein { expressions { Long  scale <1, 0.2, 1>  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel Travel(4, <0, 0.3, 0>) }  Thin } closed u  ends sealed }
#declare Rolled = skein { expressions { Long  curl { pivot path { <-1, 0, 0.1>, <-1, 1, 0.1> }  travel Travel(4, <0, 0, 0.05>) }  Thin } closed u  ends sealed }
#declare RolledSample = skein { expressions { Long  curl { pivot sample_path { <1/6, 0, 0.1>, <1/6, 1, 0.1> }  travel Travel(4, <0, 0, 0.05>) }  Thin } closed u  ends sealed }
ClosedCheck("scroll under a turn, thickened", Opening)
ClosedCheck("coil of three turns, thickened", Spring)
PairCheck("scroll of three turns thickened, sample_path pivot against the path", RolledSample, Rolled, 1e-8, 1)

#if (Failures > 0)
  #error concat("skein_curl: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_curl: every check within tolerance\n"
