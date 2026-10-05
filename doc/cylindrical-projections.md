# Cylindrical camera projections

The `spherical` camera can apply a named vertical map while retaining its longitude across the image:

```pov
camera {
  spherical
  projection mercator
  angle 360
  latitude -64, 77
  location <0, 1.2, 0>
  look_at <0, 1.2, 1>
}
```

`latitude lower, upper` gives the bottom and top latitude in degrees. It is required for every named projection.
Mercator endpoints must be strictly inside −90 and +90 degrees. The existing spherical camera is unchanged when
`projection` and `latitude` are absent. Its second `angle` value remains the vertical field of view in that form.

## Profiles

For latitude φ in radians, the output ordinate is:

| Profile | Camera syntax | Ordinate | Character |
|---|---|---|---|
| equirectangular | omit `projection`, or `projection spherical` | φ | uniform angular rows |
| Mercator | `projection mercator` | asinh(tan φ) | conformal; stretches high latitudes |
| Miller | `projection miller` | 1.25 asinh(tan 0.8φ) | less polar stretch, not conformal |
| cylindrical stereographic | `projection cylindrical_stereographic` | 2 tan(φ/2) | moderate polar stretch |
| cylindrical equal-area | `projection cylindrical_equal_area` | sin φ | preserves area; compresses high latitudes |

At 77 degrees the vertical scale relative to the horizon is 4.45 for Mercator, 2.10 for Miller, 1.63 for
stereographic and 0.22 for equal-area. All retain straight verticals and a straight horizon. Panini is not included:
it changes the horizontal mapping as well, so it is not another vertical profile in this family.

| Option | Local aspect | Ground grid | 360-degree seam | Placement |
|---|---|---|---|---|
| Mercator | preserved | expands rapidly toward the limits | continuous | nonlinear in distance and latitude |
| Miller | vertically compressed against Mercator | less polar expansion | continuous | nonlinear |
| stereographic | moderate vertical expansion | moderate polar expansion | continuous | nonlinear |
| equal-area | strongly flattened high in the frame | high latitudes compressed | continuous | nonlinear |
| circular pushbroom fixture | no shared perspective scale | repeated turntable-like foreground | origin and direction meet | linear by azimuth, but hidden surfaces may repeat |
| relaid-out single centre | ordinary Mercator aspect | ordinary perspective grid | continuous | easiest when subjects share a large radius |

The camera traces the final output coordinates directly. Crop windows, progressive rendering and anti-aliasing
therefore operate in the mapped image rather than an intermediate equirectangular image. The camera reports the
smallest angular pixel footprint over its latitude range, so subsurface point-cloud spacing remains conservative at
the most densely sampled part of the image.

## Comparison

`tools/bench/cylindrical-projections.pov` places the same asymmetric jointed marker at radii 3, 6 and 12, with a
checker ground, horizon and vertical poles. `Declare=Profile=0` through `4` selects the rows in the table;
`Declare=Profile=5` selects a scene-defined circular pushbroom whose viewpoint moves around a radius-18 ring, and
`Declare=Relayout=1` moves the single-centre composition toward equal apparent scale by scaling the distant rings.

At 800×400, a native Mercator render and a bilinear Mercator remap of the equirectangular render differed by a mean
2.82 levels of 255; 5.62% of pixels differed by more than 8 levels, concentrated on resampled edges and the ground
grid. Native rays avoid that blur. In the motivating 360-degree canvas, the old source is sampled about 1.47 times
more densely than needed at the horizon but about 2.9 times too sparsely at +77 degrees and 1.5 times too sparsely at
−64 degrees.

Single-threaded `+PR -A` instruction counts for the fixture, each the median of four runs minus a matching one-pixel
parse, were 2.200 G equirectangular, 2.368 G Mercator, 2.407 G Miller, 2.245 G stereographic and 2.315 G equal-area.
The map arithmetic is negligible beside tracing; the different totals are primarily the different scene regions
and object sizes each profile assigns to the same output rows. Hardware counters were not multiplexed.

## Recommendation

Use native Mercator when a conformal cylindrical print is required, and Miller or stereographic when less polar
stretch matters more than local angle preservation. Equal-area is useful for diagnostic or map-like output, not for
hero subjects near the frame limits.

No single-centre panorama removes perspective. A 2.4-unit-long subject centred 3.2 units away varies 2.2× in scale
between its near and far ends; centred 12 units away it varies 1.22×. The most effective composition is therefore a
larger-radius ring with subjects scaled up, and a latitude band near ±20 degrees. This attacks the cause; changing
the vertical profile only moves distortion. A circular pushbroom or outside-in orthographic `user_defined` camera
is appropriate when turntable-like placement is more important than a shared viewpoint, but should remain an
explicit scene camera because its duplicated hidden surfaces and lighting are a different imaging model.
