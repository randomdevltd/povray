// skein_sample_path.pov: an axis given as (u, v, distance along the normal) places where the same axis written absolutely does.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 900;
#declare CheckLattice = 0.6;
#include "skein_check.inc"

// the paths are placed against the tube the bend is handed: radius 0.3 about y, so u = 0.5 is the line x = -0.3
#declare Flat = expressions { extrude { radius 0.3 } }

// 1: the axis lies in the surface, so d = 0
#declare Abs0 = skein { expressions { Flat  bend { axis y  along path { linear_spline <-0.3, 0, 0>, <-0.3, 1, 0> } } } closed u  ends open }
#declare Uv0  = skein { expressions { Flat  bend { axis y  along sample_path { linear_spline <0.5, 0, 0>, <0.5, 1, 0> } } } closed u  ends open }
PairCheck("sample_path in the surface against the same axis written absolutely", Uv0, Abs0, 1e-9, 0)

// 2: lifted 0.45 along the normal, which points out of the tube
#declare Abs1 = skein { expressions { Flat  bend { axis y  along path { linear_spline <-0.75, 0, 0>, <-0.75, 1, 0> } } } closed u  ends open }
#declare Uv1  = skein { expressions { Flat  bend { axis y  along sample_path { linear_spline <0.5, 0, 0.45>, <0.5, 1, 0.45> } } } closed u  ends open }
PairCheck("sample_path lifted 0.45 along the normal", Uv1, Abs1, 1e-9, 0)

// 3: a ramp before the extrude widens the tube into a cone, so where (u, v) sits in space takes evaluating the chain
#declare Ramp = expressions { displace function(v) { 0.2*v }  extrude { radius 0.3 } }
#declare Abs2 = skein { expressions { Ramp  bend { axis y  along path { linear_spline <-0.3, 0, 0>, <-0.5, 1, 0> } } } closed u  ends open }
#declare Uv2  = skein { expressions { Ramp  bend { axis y  along sample_path { linear_spline <0.5, 0, 0>, <0.5, 1, 0> } } } closed u  ends open }
PairCheck("sample_path on a coned sheet, in the surface", Uv2, Abs2, 1e-9, 0)

// 4: the same cone, lifted 0.3 along its own normal, which is no longer radial
#declare N = vnormalize(<-1, -0.2, 0>);
#declare Abs3 = skein { expressions { Ramp  bend { axis y  along path { linear_spline <-0.3, 0, 0> + 0.3*N, <-0.5, 1, 0> + 0.3*N } } } closed u  ends open }
#declare Uv3  = skein { expressions { Ramp  bend { axis y  along sample_path { linear_spline <0.5, 0, 0.3>, <0.5, 1, 0.3> } } } closed u  ends open }
PairCheck("sample_path lifted along a tilted normal", Uv3, Abs3, 1e-9, 0)

// 5: a declared group holding a sample_path is placed afresh at each use: spliced twice in one skein, and used by two skeins
#declare Fold = expressions { crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.1  angle 90 } }
#declare Twice = skein { expressions { translate <-0.5, 0, 0>  scale <3, 1, 1>  Fold  translate <0.7, 0, 0>  Fold } }
#declare TwiceInline = skein { expressions { translate <-0.5, 0, 0>  scale <3, 1, 1>  crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.1  angle 90 }  translate <0.7, 0, 0>  crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.1  angle 90 } } }
#declare First = skein { expressions { translate <-0.5, 0, 0>  Fold } }
#declare Second = skein { expressions { translate <-0.5, 0, 0>  scale <4, 1, 1>  translate <2, 0, 0>  Fold } }
#declare FirstInline = skein { expressions { translate <-0.5, 0, 0>  crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.1  angle 90 } } }
#declare SecondInline = skein { expressions { translate <-0.5, 0, 0>  scale <4, 1, 1>  translate <2, 0, 0>  crease { axis sample_path { <0.5, 0, 0>, <0.5, 1, 0> }  radius 0.1  angle 90 } } }
PairCheck("a sample_path group spliced twice, against it inline", Twice, TwiceInline, 1e-9, 0)
PairCheck("a sample_path group in the first of two skeins, against it inline", First, FirstInline, 1e-9, 0)
PairCheck("the same group in the second skein, against it inline", Second, SecondInline, 1e-9, 0)

#if (Failures > 0)
  #error concat("skein_sample_path: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_sample_path: a uv axis places where the absolute axis does\n"
