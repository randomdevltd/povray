# Cylindrical projections

`projections.inc` builds cylindrical maps with the existing `user_defined` camera:

```pov
#include "projections.inc"
#declare View = CylindricalProjectionCamera(
  ProjectionMercator, -64, 77,
  ProjectionSingleOrigin, 0, 0,
  <0, 1.2, 0>, z, x, y
);
camera { View }
```

The final three vectors are the centre, forward, right and up basis. Longitude spans 360 degrees. `Lower` and `Upper`
are the bottom and top latitude in degrees. The origin kind is `ProjectionSingleOrigin`, `ProjectionSphereOrigin` or
`ProjectionCylinderOrigin`; the following radius is ignored for a single origin. The direction flag is zero for
outward rays and one for inward rays.

The profiles are `ProjectionEquirectangular`, `ProjectionMercator`, `ProjectionMiller`, `ProjectionStereographic`
and `ProjectionEqualArea`. Their ordinates for latitude φ are φ, asinh(tan φ), 1.25 asinh(tan 0.8φ), 2 tan(φ/2)
and sin φ respectively. Panini is not included because it also changes horizontal mapping.

The camera functions are evaluated at the sample centre and at the four half-pixel corners when texture footprints
are required. The resulting origin and direction differentials carry through ordinary and UV-mapped screens, so the
view behind a screen filters its own textures without tracing the whole view once per filtering tap. Subsurface uses
the same per-ray surface footprint, falling back to the older camera-wide estimate where no differential is available.

`tools/bench/cylindrical-projections.pov` compares the profiles over identical markers, poles and a checker ground.
Profile 5 moves the origin around a radius-four cylinder. This circular pushbroom view changes perspective rather
than merely redistributing latitude; it remains library code because `user_defined` adds no measurable wall-time cost
in the live-map fixture and only about two percent simulated instructions before function-VM optimization.

The native and function-defined Mercator implementations used during the decision rendered decoded-pixel-identical
images. Native syntax was removed: a standard include keeps the camera grammar small while allowing the same method
to define other projections and moving origins.
