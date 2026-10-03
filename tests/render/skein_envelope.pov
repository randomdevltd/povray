// skein_envelope.pov: the sample step and envelope against primitives, implicit surfaces and exact volumes; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "skein_check.inc"
#include "skein_volume.inc"

#macro Report(Name, E, Limit)
  #debug concat(Name, ": off by ", str(E, 0, 9), "\n")
  #if (abs(E) > Limit) #declare Failures = Failures + 1; #end
#end

#declare HalfBySample = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5 }  sample { at <map { uv.u  linear { scale 0.5 } }, map { uv.v }> } } }
#declare HalfByScale = skein { expressions { scale <0.5, 2, 1>  extrude { radius 0.5 } } }
PairCheck("half tube from sample at <u/2, v> against a half extrude", HalfBySample, HalfByScale, 1e-7, 0)
#declare ThickBySample = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5 }  sample { at <function(uv) { uv.u }, function(v) { v }> }  displace 0.1 } closed u  ends flat }
#declare Thick = skein { expressions { scale <1, 2, 1>  extrude { radius 0.5 }  displace 0.1 } closed u  ends flat }
PairCheck("sample at <u, v> then displace against displace", ThickBySample, Thick, 1e-7, 1)

// a flat sheet 2 x 1 thickened by 0.1 with flat edges is the box, within the root tolerance, and holds 2 x 1 x 0.1
#declare Slab = skein { expressions { scale <2, 1, 1>  envelope { thickness 0.1  edge flat } } closed u  ends flat }
#declare SlabBox = box { <0, 0, -0.05>, <2, 1, 0.05> }
#declare SlabDistance = function(x, y, z) { max(-x, x - 2, -y, y - 1, abs(z) - 0.05) }
Check("flat sheet, envelope with flat edges", Slab, SlabBox, SlabDistance)
Report("flat sheet, volume against area times thickness 0.2", SolidVolumeIn(Slab, <0, 0, 0>, <2, 1, 0>, 40) - 0.2, 1e-9)

#declare Rounded = skein { expressions { scale <2, 1, 1>  envelope { thickness 0.1  edge round } } closed u  ends flat }
#declare RoundedSlab = function(x, y, z) { max(sqrt(pow(x - min(max(x, 0), 2), 2) + z*z) - 0.05, abs(y - 0.5) - 0.5) }
SurfaceCheck("flat sheet, envelope with round edges", Rounded, RoundedSlab)

// a thickness tapering to zero at all four edges closes the solid by itself; its volume is the integral of the thickness
#declare Taper = function(u, v) { 0.1*sin(pi*u)*sin(pi*v) }
#declare Tapered = skein { expressions { scale <2, 1, 1>  envelope { thickness function(uv) { Taper(uv.u, uv.v) } } } closed u  ends sealed }
ClosedCheck("tapered envelope", Tapered)
#declare TaperVolume = 2*0.1*pow(2/pi, 2);
Report("tapered envelope, volume against 2 x 0.1 x (2/pi)^2 = 0.0810569", (SolidVolume(Tapered, 120) - TaperVolume)/TaperVolume, 0.005)

// a sheet curled by a crease and thickened is a closed solid; with the thickness below the crease radius its volume is unchanged
#declare Curl = expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1>  crease { axis path { <0.2, 0, 0>, <0.2, 1, 0> }  radius 0.12  angle 300 } }
#declare CurledThin = skein { expressions { Curl  envelope { thickness function(uv) { 0.08*sqrt(sin(pi*uv.v)) }  edge round } } closed u  ends sealed }
#declare FlatThin = skein { expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1>  envelope { thickness function(uv) { 0.08*sqrt(sin(pi*uv.v)) }  edge round } } closed u  ends sealed }
ClosedCheck("curled envelope", CurledThin)
#declare FlatVolume = SolidVolume(FlatThin, 100);
Report("curled envelope, volume against the same envelope uncurled (relative)", (SolidVolume(CurledThin, 100) - FlatVolume)/FlatVolume, 0.01)
#declare CurledThick = skein { expressions { Curl  envelope { thickness function(uv) { 0.3*(1 - pow(2*uv.v - 1, 8)) }  edge round } } closed u  ends sealed }
ClosedCheck("curled envelope thicker than the crease radius (self-overlap)", CurledThick)

#if (Failures > 0)
  #error concat("skein_envelope: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_envelope: every check within tolerance\n"
