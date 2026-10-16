# Progressive photons

Method 2 replaces the classic photon count, spacing and gather controls with one
work budget:

```pov
global_settings {
    photons { method 2 quality 1 }
}
```

`quality` is a finite number greater than zero and at most 64, default 1. It sets
`ceil(4 * quality)` emission batches of 65,536 samples per prepared scene set. All
batches are deposited into one surface map and one media map per set; each deposit
carries its emitted flux divided by the batch count, so the image brightness does not
depend on quality. The maps are built once, after the last batch, before radiosity
and camera tracing start. Camera tracing is the ordinary render: anti-aliasing
(`+A`, any `+AM`), mosaic preview, progressive rendering (`+PR`), render windows and
`+C` all behave as they do without photons, and each camera sample is traced once.
Nothing in the photon budget depends on the image size. `method 1` selects classic
photons. Omitting the method retains classic behavior, including in version 4
scenes. Quality is not an exposure control.

## Emission

Batches are shot in order, each split into sixteen logical shards. Each target's
hit rate in one batch sets its selection probability in the next, with an
exploration floor; the first batch selects targets uniformly. Ten percent of
samples from a nonparallel source are isotropic. Every deposit carries emitted flux
divided by the complete mixture density of the direction actually taken, including
overlapping target cones; samples rejected by an aperture are not renormalized.

Emission is driven by a seven-dimensional Sobol sequence, indexed by the global
sample number and digitally shifted once per prepared set from the render seed.
Its dimensions choose the light, the position on an area light or a parallel
source's target disc (a volume emitter uses the first for its power-table cell),
the polar and azimuthal direction within the selected target cone or the sphere,
the targeted-or-isotropic choice and the target. Every aligned block of 2^m samples,
and therefore every batch, is a digital net, so each batch covers the emission
domain evenly however the earlier batches adapted target probabilities. Choices
after the first surface (reflection, refraction, roulette, dispersion bands, media
scattering) remain hashed per-sample random numbers.

## Estimator

A surface receiver gathers the `k` nearest compatible deposits on the same object.
A deposit is compatible when its surface normal is within 60 degrees of the
receiver's and its offset from the mean of the two tangent planes,
`|0.5 dot(p - x, n_x + n_p)|`, is at most a tenth of that receiver's base radius;
that offset is zero for any two points on a sphere or a cylinder, so curvature does
not reject photons. Deposits arriving from behind a receiver without
`diffuse_back`, or entering a subsurface receiver at grazing angles, are not
counted. The support `r` is the distance to the `k`-th compatible deposit, capped
at eight times the receiver's base radius, which is the median `k`-th-neighbour
distance over up to 128 of its own deposits. Each deposit is weighted by the
Epanechnikov kernel `2 (1 - d^2/r^2) / (pi r^2)`. For a uniform deposit density
the expected estimate equals the true irradiance both when `r` is the `k`-th
distance and when it is the cap; on a sphere, the set within a straight-line
distance `r` has area exactly `pi r^2`.

`k` grows with the batch count as `k = 48 (batches / 4)^(2/3)`, clamped to
8..256, the kernel-density rate for a two-dimensional density: quality 1 uses
48 neighbours, quality 4 uses 121 and quality 16 uses the cap of 256. The same
estimator feeds diffuse, phong and specular photon lighting and subsurface entry,
including the shared subsurface point cloud, whose boundary probe uses the base
radius. A subsurface receiver that is a CSG gathers deposits on its members.

Media deposits are finite beam segments. A camera sample gathers the segments
within the distance of the 32nd nearest segment of the same medium list, capped at
four times the median such distance, and divides by the sphere volume; the
clipped segment is integrated analytically. This volume estimator keeps the
box kernel and fixed neighbour count of the earlier prototype, now over the
single map.

## Light power and authoring

Point and spot source colors specify radiant intensity. Area-light intensity is
distributed over the emitting area; parallel source colors specify irradiance.
Sources are sampled through `Emitter`, including media emitters. Each deposit
carries emitted flux divided by the complete emission mixture probability and
batch size. Overlapping target cones are included in that mixture. Surface
reflection, refraction and spectral splitting retain their transport weights.

An unfaded conventional point light therefore becomes inverse-square under
method 2. An existing `fade_power 2 fade_distance F` is interpreted by its
far-field intensity, `2*F*F*color`; its artificial near-field softening is removed.
Other conventional fade powers are rejected. Parallel photon emitters must be
unfaded. These are intentional differences from the distance-compensated classic
map. Fill lights, zero-power sources and lights with both photon reflection and
refraction disabled retain their direct-light settings, including oriented area
lights and projected-through sources.

Targets, `collect off`, reflection/refraction switches and `pass_through` retain
their authoring roles. Numerical target density is replaced by measured targeting:
previous-batch hit rates guide the next batch, with an exploration floor and an
isotropic component for nonparallel sources. Area emitters are sampled over their
area without requiring the classic photon `area_light` option. Oriented area
lights use the original axes and rectangular photon profile, as classic photons
do; `orient` only changes their direct-light profile.

`projected_through` retains the classic geometric aperture: missed emission
samples contribute zero, and accepted rays start past its last intersection.
The skipped segment does not cast shadows or attenuate photons. Containing
interiors are found at the new origin. The aperture does not renormalize the
emission PDF or redistribute the rejected power.
`distribution/scenes/lights/progressive_projector.pov` combines an oriented area
source, a geometric aperture and a glass caustic.

`light_group { ... photons on }` enables group photon transport; `photons off`
disables it. The default is off before scene version 4.0 and on from 4.0.
Photon-only lamps retain group membership without entering the direct-light
list. Target selection and deposits enforce membership and `global_lights off`;
surface gathers also match the receiving object, so nearby objects in another
group cannot receive those deposits. Group cloning and render-tag filtering
preserve this relationship. Photon maps are shared by a prepared scene set,
not allocated per light group.
Volumetric photon collection from group lights is currently rejected; the
surface membership checks do not substitute for medium ownership.

Classic count, spacing, gather, radius, jitter, autostop, expansion and map-file
controls do not apply and are rejected. Radiosity is currently rejected.

Media photons are enabled by the existing `media` photon setting. Transport stores
finite photon segments with their incoming flux and extinction. A gather clips each
segment to its spherical support and integrates attenuation analytically along the
clipped length. The estimate divides by the sphere volume, with scattering and phase
evaluated at the camera sample.
The existing media step limit and sampling settings control extinction integration;
method 4 shares its prepared coefficient field with camera transport. Its coarse
direct-light sampling still gathers photons at every coefficient sample.

Curved photon paths retain every chord produced by the refractive field marcher.
Segments may coalesce only inside one chord, when adjacent integration cells have
identical extinction. No segment joins separate curved-ray chords or crosses an
interior boundary. The spatial index bounds the actual segment endpoints. Constant
media can consequently use fewer deposits, while varying extinction retains the
integration cells. Field interpolation and constant extinction inside each cell
remain approximations; a small number of photons can still miss a narrow caustic.

Each segment records the exact active medium list and modifier ownership/order.
Gathering and support selection require that same list, avoiding transfer
between nearby unrelated containers. This deliberately truncates support at a
change of medium stack; it does not correct kernel bias at that boundary. The
existing rejection of volume photons from light-group sources remains in place.
Method 2 does not add medium ownership support for light groups or change
`global_lights off` behavior. Shared media must still follow the existing scene
ownership restrictions.
Subsurface photon illumination is reconstructed before diffusion; the subsurface
cache is created once, after the maps are built.

## Media lights

Media lights now use method 2 through the same emission interface. Their
`light_source { photons { refraction on reflection off } }` block controls
transport just as it does for ordinary lights. Omitting those switches leaves
the target's settings in control. `brightness` scales emitted photon power and
direct illumination together; it does not scale the glow seen by the camera.
Classic method 1 continues to ignore media-light photon blocks with a warning.
Media and conventional lights share the emitter sampling and flux normalization.
The media light's default direct falloff is inverse-square, clamped within one
source cell; photons acquire inverse-square falloff by spreading through space.
There is no independent photon fade override yet.

The position sample's weight already includes its power-table probability;
shooting applies the light-selection and directional probabilities once. The
emitter's own neutral container is crossed before the selected target, applying
transmission and extinction. A refractive emitting container should itself be a
photon target. Glow along a photon path never adds power to the photon. Direct
illumination through a participating target is excluded even in an empty batch,
so it cannot be counted again alongside the photon estimate.

The candle sample includes an optional water glass:

```sh
povray +Idistribution/scenes/interior/media/blend_candle.pov \
  +W360 +H270 Declare=WaterGlass=1 Declare=PhotonQuality=1
```

`Shimmer`, `Smoke`, `Plume` and `Mist` allow separate transport checks. The
existing view and lighting remain the sample's default when `WaterGlass=0`.
`tests/render/progressive_media_light.pov` compares an emissive sphere with an
equal-power point light, an identity lens with direct lighting, source controls,
self-attenuation, refractive heat and arrays of small emitters.

## Memory and reproducibility

The surface map stores 64 bytes per deposit (double position, float direction,
normal and flux, a receiver index and the tree axis); media segments keep their
full record. Shooter vectors hold one batch and are released after it is merged.
Memory therefore grows linearly with quality and with deposits per sample, which
depend on the scene (spectral bands, bounces, media steps); quality is capped at 64
for that reason. The log line `Progressive photons:` reports batches, emitted
samples, deposits, `k`, contributions, preparation time and the peak map size.

Deposits are merged in shard order, so the maps, and the image, do not depend on
the number of threads on the same build and platform. Because the maps are
rebuilt identically, `+C` continues a render with the same photons.

## Changes from the earlier prototype

The earlier method 2 traced one full camera pass per emission batch (four at
quality 1), averaged the passes with a shrinking box-kernel radius chosen from a
pilot batch, required `+PR -A`, and grouped batches differently at 8,000,000
pixels and above. Each pass repeated camera, direct-light and subsurface work. Its
surface acceptance slab, `|dot(p - x, n_x)| <= 0.05 r`, rejects deposits on
receivers curved at a radius smaller than about ten gather radii: on a sphere the
gathered fraction is 0.50 at `r = 0.2 R` and 0.25 at `r = 0.4 R`. That schedule,
the pilot, the per-pixel estimator state and the image-size threshold are gone.

## Limits

- Radiosity and classic photon map files are rejected.
- The fixed-`k` rule is global; it is not adapted to local noise or gradients,
  and it smooths features narrower than the support at low quality. Sparse
  regions are estimated at the capped radius and stay noisy.
- Density is still truncated at object edges and at changes of medium; there is
  no boundary correction.
- Volumetric photons from light-group sources are rejected.
- Deposits per sample, and therefore memory, are not bounded by quality alone.

