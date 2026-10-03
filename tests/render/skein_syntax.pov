// skein_syntax.pov: grouped inputs, displace, map, path and declared groups against primitives or implicit surfaces; stops with an error past tolerance.
#version 3.8;
global_settings { assumed_gamma 1 }
#include "skein_shapes.inc"
#include "skein_check.inc"

#declare TubeDistance = function(x, y, z) { max(sqrt(x*x + z*z) - 0.5, -y, y - 2) }
#declare FrustumDistance = function(x, y, z) { max((sqrt(x*x + z*z) - (1 - 0.375*y)) / sqrt(1 + 0.375*0.375), -y, y - 2) }
#declare ThickTorusDistance = function(x, y, z) { sqrt(pow(sqrt(y*y + z*z) - 2, 2) + x*x) - 0.6 }
#declare ThickTubeDistance = function(x, y, z) { max(sqrt(x*x + z*z) - 0.6, -y, y - 2) }
#declare SwayedTube = function(x, y, z) { max(sqrt(pow(x - 0.2*sin(pi*y), 2) + pow(z - 0.1*cos(pi*y), 2)) - 0.5, -y, y - 2) }
#declare RidgedTube = function(x, y, z) { max(sqrt(x*x + z*z) - (0.5 + 0.05*sin(3*atan2(z, x))), -y, y - 2) }
#declare LobedTube = function(x, y, z) { max(sqrt(x*x + z*z) - (0.5 - 0.05*sin(3*atan2(z, x))), -y, y - 2) }
#declare Bicone = function(x, y, z) { max(sqrt(x*x + z*z) - (0.5 + 0.3*(1 - abs(1 - y))), -y, y - 2) }
#declare BulgedTube = function(x, y, z) { max(sqrt(x*x + z*z) - (0.6 + 0.05*x/sqrt(x*x + z*z) + 0.02*y), -y, y - 2) }

Check("frustum, function(uv)", SkeinFrustumUV, PrimFrustum, FrustumDistance)
Check("frustum, function(pos)", SkeinFrustumPos, PrimFrustum, FrustumDistance)
Check("frustum, function(v, y)", SkeinFrustumMixed, PrimFrustum, FrustumDistance)
Check("tube, function(norm)", SkeinTubeNormal, PrimTube, TubeDistance)
Check("torus displaced by 0.1", SkeinTorusDisplaced, PrimTorusDisplaced, ThickTorusDistance)
SurfaceCheck("tube displaced by norm.x and pos.y", SkeinTubeDisplaced, BulgedTube)
Check("frustum, linear map", SkeinFrustumLinear, PrimFrustum, FrustumDistance)
Check("frustum, reversed range", SkeinFrustumRange, PrimFrustum, FrustumDistance)
Check("frustum, scale map after the extrude", SkeinFrustumScaled, PrimFrustum, FrustumDistance)
Check("tube displaced by a length map", SkeinTubeLength, PrimTubeThick, ThickTubeDistance)
SurfaceCheck("tube swayed by sin and cos maps", SkeinTubeSway, SwayedTube)
SurfaceCheck("tube ridged by an atan2 map", SkeinTubeRidged, RidgedTube)
SurfaceCheck("tube lobed by a repeated u", SkeinTubeLobes, LobedTube)
SurfaceCheck("bicone from a mirrored v", SkeinBicone, Bicone)
Check("frustum, scalar path", SkeinFrustumPath, PrimFrustum, FrustumDistance)
Check("tube, straight path axis with handles", SkeinTubeHandles, PrimTube, TubeDistance)
Check("frustum, arclength path axis", SkeinFrustumArc, PrimFrustum, FrustumDistance)

// why: radii of the previewer's path(0.35, ..., 0.38, { interp: 'cubic' }) at v, printed by its own code
#declare VaseV = array[7] { 0.1, 0.25, 0.4, 0.5, 0.6, 0.75, 0.9 }
#declare VaseR = array[7] { 0.479673846, 0.602908654, 0.549553846, 0.42, 0.279313846, 0.215408654, 0.306153846 }
#declare VaseError = 0;
#for (I, 0, 6)
  #declare N = <0, 0, 0>;
  #declare P = trace(SkeinVase, <5, 2.2*VaseV[I], 0>, <-1, 0, 0>, N);
  #declare VaseError = max(VaseError, abs(vlength(<P.x, 0, P.z>) - VaseR[I]));
#end
#debug concat("vase: natural spline radius off the previewer's by at most ", str(VaseError * 1e9, 0, 3), "e-9\n")
#if (VaseError > Tolerance)
  #declare Failures = Failures + 1;
#end

// The knot tube: rays from the centre line, square to it, leave the tube at exactly its radius, at the seam too.
#macro Trefoil(T) <KnotRadius(T)*cos(2*KnotAngle(T)), KnotRadius(T)*sin(2*KnotAngle(T)), -sin(3*KnotAngle(T))> #end
#declare KnotStream = seed(7);
#declare KnotError = 0;
#for (I, 0, 400)
  #declare T = (I < 4 ? I/3 - 1e-9*(I = 3) : rand(KnotStream));
  #declare C = Trefoil(T);
  #declare Tangent = vnormalize(Trefoil(T + 1e-6) - Trefoil(T - 1e-6));
  #declare Across = vnormalize(vcross(Tangent, <rand(KnotStream) - 0.5, rand(KnotStream) - 0.5, rand(KnotStream) - 0.5>));
  #declare N = <0, 0, 0>;
  #declare P = trace(SkeinTrefoil, C, Across, N);
  #declare KnotError = max(KnotError, (vlength(N) = 0 ? 1 : abs(vlength(P - C) - 0.05)));
#end
#debug concat("trefoil: rays from the centre line leave at radius 0.05 within ", str(KnotError * 1e9, 0, 3), "e-9\n")
#if (KnotError > 1e-5)
  #declare Failures = Failures + 1;
#end

Check("tube, declared group", SkeinTubeGroup, PrimTube, TubeDistance)
Check("thick tube, nested group through a macro", SkeinThickGroup, PrimTubeThick, ThickTubeDistance)
Check("frustum, a group used twice and a copied group", SkeinFrustumGroup, PrimFrustum, FrustumDistance)

// The bent square: flat before the hinge, a quarter circle about (0.5, y, 0.2), then straight up at x = 0.7.
#macro Probe(Origin, Direction, Expected, Normal)
  #local N = <0, 0, 0>;
  #local P = trace(SkeinBent, Origin, Direction, N);
  max(vlength(P - Expected), min(vlength(vnormalize(N) - Normal), vlength(vnormalize(N) + Normal)))
#end
#declare CreaseError = max(Probe(<0.3, 0.4, 1>, -z, <0.3, 0.4, 0>, z),
                         Probe(<0.6, 0.5, 1>, -z, <0.6, 0.5, 0.2 - sqrt(0.03)>, <-0.5, 0, sqrt(0.75)>),
                         Probe(<2, 0.7, 0.3>, -x, <0.7, 0.7, 0.3>, x));
#debug concat("crease: flat, arc and straight probes off by at most ", str(CreaseError * 1e9, 0, 3), "e-9\n")
#if (CreaseError > Tolerance)
  #declare Failures = Failures + 1;
#end

#if (Failures > 0)
  #error concat("skein_syntax: ", str(Failures, 0, 0), " shapes outside tolerance\n")
#end
#debug "skein_syntax: every shape within tolerance\n"
