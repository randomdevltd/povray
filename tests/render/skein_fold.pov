// skein_fold.pov: a fold to a straight hinge's normals is that crease, by x, y and z and by perturb, open and thickened, to 120 and 180 degrees; a fold to the incoming normals is the identity.
#version 3.8;
global_settings { assumed_gamma 1 }
#declare CheckRays = 400;
#declare CheckLattice = 0.5;
#include "skein_check.inc"
#include "skein_shapes.inc"

#declare Sheet = expressions { translate <-0.5, 0, 0>  scale <1.6, 1, 1> }
#declare Thick = expressions { envelope { thickness function(uv) { 0.08*(1 - pow(2*uv.v - 1, 8)) }  edge round } }
// the crease turns the sheet past the hinge at u = 0.5 by its distance over the radius, up to its angle
#declare Turn120 = function(u) { min(max(1.6*u - 0.8, 0)/0.12, radians(120)) }
#declare Turn180 = function(u) { min(max(1.6*u - 0.8, 0)/0.12, pi) }

#declare Crease = skein { expressions { Sheet  crease { axis y  radius 0.12  angle 120 } } }
#declare ByNormal = skein { expressions { Sheet  fold { from <0.25, 0.5>  x function(u) { -sin(Turn120(u)) }  y 0  z function(u) { cos(Turn120(u)) } } } }
#declare ByPerturb = skein { expressions { Sheet  fold { from <0.25, 0.5>  perturb { x function(u) { -sin(Turn120(u)) }  z function(u) { cos(Turn120(u)) } } } } }
SheetCheck("hinge by x, y and z against crease", ByNormal, Crease, 1e-8)
SheetCheck("hinge by perturb against crease", ByPerturb, Crease, 1e-8)

#declare ThickCrease = skein { expressions { Sheet  crease { axis y  radius 0.12  angle 120 }  Thick } closed u  ends sealed }
#declare ThickFold = skein { expressions { Sheet  fold { from <0.25, 0.5>  perturb { x function(u) { -sin(Turn120(u)) }  z function(u) { cos(Turn120(u)) } } }  Thick } closed u  ends sealed }
PairCheck("hinge by perturb thickened against crease thickened", ThickFold, ThickCrease, 1e-8, 1)

// past the arc the new normal is exactly opposite the sheet's; the turn there continues the arc's
#declare HalfTurn = skein { expressions { Sheet  crease { axis y  radius 0.12  angle 180 } } }
#declare HalfFold = skein { expressions { Sheet  fold { from <0.25, 0.5>  perturb { x function(u) { -sin(Turn180(u)) }  z function(u) { cos(Turn180(u)) } } } } }
SheetCheck("180 degree hinge by perturb against crease", HalfFold, HalfTurn, 1e-8)

#declare Same = skein { expressions { extrude { radius 0.5 }  extrude { axis x  radius 2 }
  fold { from <0.3, 0.6>  x function(norm) { norm.x }  y function(norm) { norm.y }  z function(norm) { norm.z } } } closed uv }
PairCheck("torus folded to its own normals against the torus", Same, SkeinTorus, 1e-8, 1)

#if (Failures > 0)
  #error concat("skein_fold: ", str(Failures, 0, 0), " checks outside tolerance\n")
#end
#debug "skein_fold: every check within tolerance\n"
