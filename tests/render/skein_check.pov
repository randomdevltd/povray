// skein_check.pov: skein hits, normals and inside() against the equal primitives and the exact surface; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "skein_shapes.inc"
#include "skein_check.inc"

#declare TorusDistance = function(x, y, z) { sqrt(pow(sqrt(y*y + z*z) - 2, 2) + x*x) - 0.5 }
#declare TubeDistance = function(x, y, z) { max(sqrt(x*x + z*z) - 0.5, -y, y - 2) }
#declare FrustumDistance = function(x, y, z) { max((sqrt(x*x + z*z) - (1 - 0.375*y)) / sqrt(1 + 0.375*0.375), -y, y - 2) }
#declare HexDistance = function(x, y, z) {
  max(max(abs(cos(pi/6)*x - 0.5*z), abs(z), abs(cos(pi/6)*x + 0.5*z)) - 0.5*cos(pi/6), -y, y - 3)
}

Check("torus", SkeinTorus, PrimTorus, TorusDistance)
Check("tube", SkeinTube, PrimTube, TubeDistance)
Check("frustum", SkeinFrustum, PrimFrustum, FrustumDistance)
Check("twisted tube", SkeinTwistedTube, PrimTube, TubeDistance)
Check("hexagonal column", SkeinHexColumn, PrimHexColumn, HexDistance)

#if (Failures > 0)
  #error concat("skein_check: ", str(Failures, 0, 0), " shapes outside tolerance\n")
#end
#debug "skein_check: every shape within tolerance\n"
