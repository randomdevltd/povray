# Progressive photons

Method 2 replaces the classic photon count, spacing and gather controls with one
work budget:

```pov
global_settings {
    photons { method 2 quality 1 }
}
object { Glass photons { target 2 refraction on } }   // target weight 2
```

`quality` is a finite number greater than zero and at most 16384 (2^32 samples per
set, the length of the shooting sequence), default 1; photon storage is bounded by
memory instead (see below). It sets
`ceil(4 * quality)` shooting batches of 65,536 samples per prepared scene set. All
batches are deposited into one surface map and one media map per set; each deposit
carries its emitted flux divided by the batch count, so the image brightness does not
depend on quality. The maps are built once, after the last batch, before radiosity
and camera tracing start. Camera tracing is the ordinary render: anti-aliasing
(`+A`, any `+AM`), mosaic preview, progressive rendering (`+PR`), render windows and
`+C` all behave as they do without photons, and each camera sample is traced once.
Nothing in the photon budget depends on the image size. `method 1` selects classic
photons. Omitting the method retains classic behavior, including in version 4
scenes. Quality is not an exposure control.

## Aiming

Batches are shot in order, each split into sixteen logical shards. Each light aims
over one domain, built once per prepared set before the first batch: the directions
from the light that can reach any of its targets, or for a parallel or cylinder light
the part of its plane whose beam can. The domain is a square of 128 by 128 cells mapped
onto a cap of directions about the light by the concentric map, which keeps area, so
every cell holds the same solid angle (for a parallel light, onto the rectangle of the
plane holding the targets' shadows). A cell belongs to the domain when it can reach the
bounds of a target's pieces: each member of a target union, recursively, by its box,
and a sphere by its own centre and radius. From an area light, a volume emitter or a
spot, the test widens by the source's radius and keeps to the spot's falloff cone; a
`projected_through` aperture keeps only the cells that can reach its bounds too, and a
cylinder light only those within its falloff radius. Cells that no target can reach
receive no samples, so the domain needs no isotropic share.

Half of a light's samples spread over every cell of the domain in proportion to its
solid angle (or area) times the heaviest weight of the targets reaching it; the other
half are split between the targets by weight, each spread evenly over its own cells, so
a small target keeps its share however large the others are (see Targets). Every
deposit carries the emitted flux divided by the density of its cell, the sum of both
halves; the density is piecewise constant over the cells, so it is exact for every
direction taken, wherever targets overlap, and the image converges to the same light
whatever the split. Samples an aperture rejects are not renormalized. The same domain
serves every batch: it is built from the scene alone, so it does not depend on the
batch or thread count. Why one domain and not a cone per target, with measurements:
see Aiming domain under Measurements.

Shooting is driven by a five-dimensional Sobol sequence, indexed by the global
sample number and digitally shifted once per prepared set from the render seed.
Its dimensions choose the light, the position on an area light (a volume emitter uses
the first for its power-table cell) and the point within the domain's square, which
also picks the half and the target (for a parallel or cylinder light the position
dimensions choose the point of its plane). Every aligned block of 2^m samples, and
therefore every batch, is a digital net, so each batch covers the aiming domain evenly. Choices
after the first surface (reflection, refraction, roulette, dispersion bands, media
scattering) remain hashed per-sample random numbers.

## Estimator

A surface receiver gathers the `k` nearest compatible deposits on the same object.
A deposit is compatible when its surface normal is within 60 degrees of the
receiver's and its offset from the mean of the two tangent planes,
`|0.5 dot(p - x, n_x + n_p)|`, is at most a tenth of that receiver's base radius plus
its intersection tolerance; that offset is zero for any two points on a sphere or a
cylinder, so curvature does not reject photons. Both tests use the normal of the face
hit: a `smooth_triangle`, a smooth mesh triangle, a `smooth` height field cell or a
`bicubic_patch` subpatch triangle interpolates a shading normal that does not lie on its
flat face, and measured against it most neighbours on the face fall outside the slab (a
smooth wavy mesh would receive 0.63 of classic's light, a smooth height field 0.68 and a
wavy patch at `u_steps 1` 0.82). Shading still uses the interpolated
normal. The tolerance is zero for exact solvers; an `isosurface` stops root finding
within its `accuracy` before the surface, and a `parametric`'s interval subdivision
within its `accuracy` of it, so their tolerance is that `accuracy`, scaled by the
object's transform. Without it a focused caustic on a default-accuracy isosurface keeps
0.23 of its light. Deposits arriving from behind a receiver without
`diffuse_back`, or entering a subsurface receiver at grazing angles, are not
counted. The support `r` is the distance to the `k`-th compatible deposit, capped
at eight times the receiver's base radius, which is the median `k`-th-neighbour
distance over up to 128 of its own deposits. Each deposit is weighted by the
Epanechnikov kernel `2 (1 - d^2/r^2) / (pi r^2)`. For a uniform deposit density
the expected estimate equals the true irradiance both when `r` is the `k`-th
distance and when it is the cap; on a sphere, the set within a straight-line
distance `r` has area exactly `pi r^2`.

A receiver holding fewer than `k` deposits takes its base radius from their extent,
the `k`-th neighbour distance of that many deposits spread evenly over a square with
their bounding diagonal; its gathers then run at the cap, which keeps a faint
caustic's energy near where it landed instead of losing it. When no receiver reaches
`k`, the radius for receivers without deposits is the median `k`-th distance over the
whole map, or the extent rule applied to every deposit.

Deposits are indexed per receiver and per normal direction: each receiver's
deposits are split by the dominant axis and sign of their normal into up to six
kd-trees, each with its bounding box and the widest angle of its normals from that
axis. A gather visits only the trees of its receiver that can hold a normal within
60 degrees of its own, nearest-aligned first, so a query on the unlit face of a thin
receiver does not scan the lit face's deposits. Photon normals face the side a photon
arrived on; a finish with `diffuse_back`, or a `double_illuminate` object, also
accepts deposits whose normal is opposed (measuring the tangent-plane offset against
the deposit's own sheet), so a thin sheet lit from behind shows its back-lit
diffusion. `double_illuminate` shades those deposits with `diffuse` and the
highlights, as it does a light behind the surface; `diffuse_back` alone uses its own
coefficient and no highlights.

### CSG receivers

A subsurface receiver that is a CSG has no deposits of its own; its gathers accept
deposits on its members and cap the support at eight times the median of the
members' base radii. Its tangent-plane slab and boundary probe still use the median
base radius over the whole map: on the leaf scene below the members' own radius is
about five times smaller, and a slab that tight rejects enough deposits at subsurface
sample points to darken 836 pixels by up to 0.9 and raise the error against the
classic reference from 0.0035 to 0.0044, while the cap alone changes the image by at
most 0.016.

`k` grows with the batch count as `k = 48 (batches / 4)^(2/3)`, clamped to
8..256, the kernel-density rate for a two-dimensional density: quality 1 uses
48 neighbours, quality 4 uses 121 and quality 16 uses the cap of 256. The same
estimator feeds diffuse, phong, specular and `irid` photon lighting and subsurface entry,
including the shared subsurface point cloud, whose boundary probe uses the base
radius. A subsurface receiver that is a CSG gathers deposits on its members.

Media deposits are finite beam segments. A camera sample gathers the segments
within the distance of the 32nd nearest segment of the same medium list, capped at
four times the median such distance, and divides by the sphere volume; the
clipped segment is integrated analytically. This volume estimator uses a box kernel
and a fixed neighbour count. Its noise does not fall with quality: on
`tests/render/progressive_volume.pov` two seeds differ by about the same RMSE at quality
0.25, 1 and 4. Scaling the neighbour count with the batch count at the
three-dimensional kernel rate (`32 (batches / 4)^(4/7)`) helped at quality 4 and hurt at
quality 0.25, so the count stays fixed; the remaining noise there comes from camera and
media sampling.

## Light power and authoring

Point and spot source colors specify radiant intensity. Area-light intensity is
distributed over the emitting area; parallel source colors specify irradiance.
Sources are sampled through `Emitter`, including media emitters. Each deposit
carries emitted flux divided by the aiming domain's density and the batch
size (see Aiming). Surface
reflection, refraction and spectral splitting retain their transport weights.

Method 2 never changes a light's direct illumination: its color, `fade_distance` and
`fade_power` light the scene exactly as under method 1, and every fade setting is
accepted. Photon power is calibrated to that direct light instead, at every receiver
distance. A point, spot or area light emits photons with radiant intensity `color`, and
every deposit is weighted by `A(L) * L^2`, where `L` is the photon's path length from the
light to the deposit (through every reflection, refraction and `projected_through`
aperture, and at least 1e-10) and `A` is the light's own distance attenuation: 1 unfaded,
`2 / (1 + (L/F)^n)` for `fade_distance F` and `fade_power n`, `L^-n` for `fade_distance 0`.
Photons spreading through space fall as `1/L^2`, so the weight gives every receiver the
direct light's irradiance at its distance, as classic photons are compensated: an
unfaded light's photons do not attenuate, and `fade_power 2 fade_distance 0` makes the
weight exactly 1 and direct light and photons both inverse-square. A media segment
records the path length at its start, and a gather weights the part it clips by the
weight at that part's middle, so constant media still coalesce into long segments.
Parallel and cylinder colors are irradiance; their photons are weighted by the light's
fade `A(L)` alone, at their path length from the light's plane, as classic photons and
their direct light do. Unfaded media lights take no weight (see Media lights).

On `tests/render/progressive_direct_lights.pov` (a radius 0.5 sphere 1.8 from the viewed
floor point toward the light, method 2 quality 4) an ior 1 sphere passes 1.000 of the direct light at
5 and at 245 units from the light, for unfaded and every faded light, and 1.002 for parallel
and cylinder lights; a scattering medium lit through it glows 0.96 (0.960 to 0.967 over six seeds at quality 16, at 5 and at 245 units) of the directly
lit medium, the media estimator's remaining loss. A glass sphere's caustic is 1.005 of
classic's at 5 units and 0.995 at 245. Scaling photon power by `A(d) * d^2` at the
light-to-target distance instead, and letting photons fall as `1/r^2` beyond it, gives
0.53 and 0.98. The weights barely vary across one receiver, so they add no visible
noise. No bound on the weight is needed.

Targets, `collect off`, reflection/refraction switches and `pass_through` retain
their authoring roles; the number after `target` is a share weight (see Targets).
Area lights always emit photons over their
area; the light's `photons { area_light }` option changes nothing under method 2, while
classic photons without it leave the light's center, so ported scenes get softer caustics. Oriented area
lights use the original axes and rectangular photon profile, as classic photons
do; `orient` only changes their direct-light profile.

`projected_through` retains the classic geometric aperture: missed
samples contribute zero, and accepted rays start past its last intersection.
The skipped segment does not cast shadows or attenuate photons. Containing
interiors are found at the new origin. The aperture does not renormalize the
aiming PDF or redistribute the rejected power.
`distribution/scenes/lights/progressive_projector.pov` combines an oriented area
source, a geometric aperture and a glass caustic.

Group lights shoot photons by default in every scene version, as classic photons
do; `light_group { ... photons off }` disables the group's photon transport under
method 2 (method 1 ignores the switch) and `photons on` restates the default.
Photon-only lamps retain group membership without entering the direct-light
list. Target selection and deposits enforce membership and `global_lights off`;
surface gathers also match the receiving object, so nearby objects in another
group cannot receive those deposits. Group cloning and render-tag filtering
preserve this relationship. Photon maps are shared by a prepared scene set,
not allocated per light group.
Media segments follow the same membership, described with media photons below.

Classic count, spacing, gather, radius, jitter, autostop and expansion controls do
not apply and are rejected.

Media photons are enabled by the existing `media` photon setting. Transport stores
finite photon segments with their incoming flux and extinction. A gather clips each
segment to its spherical support and integrates attenuation analytically along the
clipped length. The estimate divides by the sphere volume, with scattering and phase
evaluated at the camera sample. Media glow is deliberately closer to a direct-light
reference than classic's: a scattering medium lit through an ior 1 target glows within
4% of the same medium lit directly under method 2 at quality 16 (see Light power and
authoring) and 72% above it under classic photons, so a ported media scene glows less
than it did.
Along a photon path through a medium the integration step is a fortieth of the bounding
diagonal of the target the photon was aimed at (the heaviest target whose bounds reach
its cell), shrinking as `batches^(-1/7)`, times the `factor` of
`media N, factor`; each media interval takes at most `N` steps and at least the medium's
own minimum samples. As with classic photons a larger factor takes fewer, longer steps:
on `tests/render/progressive_volume.pov` with varying extinction, factor 4 stores 4% fewer
segments at `media 16` and 44% fewer at `media 64`, and changes the mean by under 0.2%.
Where targets of equal weight overlap, a cell's step comes from the smaller, so the
segment count does not depend on the order of targets in the scene.
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
change of medium stack; it does not correct kernel bias at that boundary.

Each segment also records the light that emitted it. A camera sample in a medium
accepts a segment by the rule surface photons and direct media lighting use: the
receiver is the surface that ends the sample's media interval, and a group light's
segment counts only where that surface belongs to the light's group with group
photons on, while a global light's segment counts unless that surface is in a
`global_lights off` group. An interval ending at no surface, in the atmosphere, takes
global lights only. On a curved ray the chords before the last end at no surface, so
they take the surface that ends the whole path. The 32-neighbour support counts
accepted segments only. `tests/render/progressive_volume_groups.pov` lights two
scattering boxes, one in a `global_lights off` group with its own lamp and one with a
global lamp: neither lamp lights the other box, although the same lamps do when the
group is removed, and each box matches its half rendered alone within three 16-bit
output levels (see Limits).
Subsurface photon illumination is reconstructed before diffusion; the subsurface
cache is created once, after the maps are built.

## Which objects receive photons

Photons are aimed at targets, the objects marked `photons { target }`, but they are
deposited on any collecting object they reach after at least one reflection or
refraction; light that hits its first surface directly, or after crossing only
`pass_through` objects, is never deposited, since direct lighting already counts it.
A receiver therefore never needs to be a target, and light can be bent around a corner
by targeting only the first glass or mirror on its way. A photon's first surface must be
a target (after any `pass_through` objects) or the photon stops there, so a path never
starts at a non-target mirror or glass: light through a non-target is direct light with
a filtered shadow, and photons would count it twice. Aiming therefore covers only the
targets' bounds, and an object in front of a target, target or not, needs no
special case: a photon aimed past a hidden target that meets another target first
continues from it, and one stopped by a non-target makes no deposit.
Later surfaces need not be targets: an object reflects or refracts photons when its own
`photons { reflection on }` or `refraction on` says so, or when the light's photons block
turns that on and the object does not turn it off. `collect off` stops an object
receiving, and an object a light does not illuminate (outside the light's group, or
in a `global_lights off` group for a global light) receives nothing from it. A deposit
also needs a finish that uses it: glass with no `diffuse`, `diffuse_back`, `phong`,
`specular` or subsurface finish stores none.

## Targets

```pov
object { Canopy photons { target refraction on } }      // weight 1
object { Blades photons { target 4 refraction on } }    // four times the canopy's share
object { Pebble photons { target 0 } }                  // not a target
```

Under method 2 the number after `target` is the object's share weight. Half of a
light's samples are split between its targets in proportion to their weights, each
target's share spread evenly over the cells its pieces can reach; the other half
spread over the whole domain by solid angle, each cell weighted by the heaviest target
weight reaching it (see Aiming). Equal weights therefore give every target at
least a quarter of an even split, however small it is, and a big target the
photons its size calls for; a bigger weight gives more photons in both halves. Bare
`target` is weight 1 and `target 0` removes the target. A weight that
is negative, infinite or not a number is an error under method 2. The weight moves
samples between targets, never power: every photon carries the emitted flux divided by
the density of its cell, so the image converges to the same light whatever the
weights, and only its noise moves between targets. Give the weight to the
target whose caustic must be sharp, such as thin or patchy glass near the camera. The
weights are the same for every light type, parallel and cylinder sources included,
and do not change between batches. The split is fixed by the scene, not measured: a
split by hit rate starves targets that are hard to hit, and learning a density from where
photons land does not pay for itself (see Aiming domain under Measurements).

A `union`, or any other CSG object, flagged with `photons { target W }` is one target
of weight W, whether or not `split_union` splits it for bounding: the split members
share one entry and their pieces form its cells, so a split and an unsplit union get the
same samples, photons and image.
A target flag on a member inside a flagged union is replaced by the union's.
Members are separate targets only when the union itself is not flagged and they are.
Method 1 shoots each split member separately.

This is the opposite sense of method 1, where the number is a spacing multiplier and
a smaller value means more photons (`target 0.5` halves the spacing). A scene written
for one method needs its target numbers revisited for the other.

The `Photon targets` log line gives, per light, each target's weight, the samples drawn
in cells where it is the heaviest target and the deposits they made.

## Radiosity

Radiosity works with method 2 as it does with classic photons. The maps are complete
before the radiosity pretrace starts, so every radiosity sample, in the pretrace and in
the final trace, gathers photon light where its rays land: a caustic on the floor
lights a nearby wall through radiosity. Radiosity rays take the diffuse photon term
only, without phong or specular highlights, as they do with classic photons and with
light sources. A radiosity cache saved with `+RFO` holds the photon light of the
render that saved it; loading it with `+RFI` reuses that light even if the photons
have changed since. `tests/render/progressive_radiosity.pov` lights a wall that
collects no photons with a glass sphere's floor caustic and requires the bounce to
match classic photons within 25%; the image does not depend on the thread count, and
`+C` continues it.

## Photon files

`photons { save_file "name" }` and `load_file "name"` keep the classic syntax. With
`save_file`, a render writes every prepared set's maps once the last batch is merged,
before they are indexed: the surface deposits with their receivers, and the media
segments with their medium lists and emitting lights. With `load_file`, the render
reads them instead of shooting and indexes them as a shot render would, so a loaded
render of the same scene is identical to the render that saved the file, and a moved
camera sees the same photons. An animation or a series of camera moves can therefore
shoot once.

The file names its format version and the settings that decide its contents: quality,
batch count and size, shards, `k`, the shooting sequence's dimensions, the photon
trace level, `adc_bailout`, media steps and spacing, the colour channel layout and
every target's weight. It
also records how many objects, interiors and lights the scene has, numbering
receivers, media and lights in the scene's own order, and each prepared set's key.
Loading fails with an error naming the difference when any of these differs, when the
file is truncated or damaged, when it is a classic map, or when its photons would
exceed the memory budget (see Memory and reproducibility). A file shot with another
render seed is reused with a warning. A classic render cannot load a method 2 file, nor
method 2 a classic one. With three colour channels the file takes 64 bytes per surface
deposit and 96 per media segment; a loaded render skips shooting and gives the same image.
`tests/render/progressive_photon_file.sh` saves and loads media, subsurface and
dispersion scenes, turns the camera, and checks each error, including a load refused
under a tiny ceiling from the option, the environment and the scene, and accepted when the
option raises a tiny scene ceiling.

## Media lights

Media lights use method 2 through the same `Emitter` interface. Their
`light_source { photons { refraction on reflection off } }` block controls
transport just as it does for ordinary lights. Omitting those switches leaves
the target's settings in control. `brightness` scales emitted photon power and
direct illumination together; it does not scale the glow seen by the camera.
Classic method 1 continues to ignore media-light photon blocks with a warning.
Media and conventional lights share the emitter sampling and flux normalization.
The media light's default direct falloff is inverse-square, clamped within one
source cell; photons acquire inverse-square falloff by spreading through space, so
they take no weight. A media light's own `fade_distance` and `fade_power` keep their
meaning for direct light, and its photon deposits are weighted by them like any light's
(see Light power and authoring).

The position sample's weight already includes its power-table probability;
shooting applies the light-selection and directional probabilities once. The
emitter's own neutral container is crossed before the selected target, applying
transmission and extinction. A refractive emitting container should itself be a
photon target. Glow along a photon path never adds power to the photon. Direct
illumination through a participating target is excluded whenever the prepared set's
surface map holds deposits, so it cannot be counted again alongside the photon estimate;
as with classic photons, a set whose photons all missed keeps its filtered shadows.

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

`distribution/scenes/interior/media/refraction_prisms.pov` sends one parallel photon
beam through four dispersive prisms whose index varies inside the glass through media
`refraction`: from the left, a corkscrew that twists the index along the beam, 3D voronoi
cells with one index each, a spherical gradient lens and laminated strata. Indices
up to 2.0 turn some light back out of the entry face, so the caustics include loops in
front of the prism. Render it with `+W1024 +H576`; `Declare=Case=1` to `4` renders one
prism alone and `Declare=PhotonQuality=` raises the photon count.

## Memory and reproducibility

The surface map stores 64 bytes per deposit (double position, float direction,
normal and flux, a receiver index and the tree axis); a media segment takes 168 bytes
with its bounds. Shooter vectors hold one batch and are released after it is merged.
Memory grows linearly with quality and with deposits per sample, which depend
on the scene (spectral bands, bounces, media steps). After every batch the whole
shoot's storage is projected from the deposits and segments per batch so far, with a
20% margin; if it exceeds the memory ceiling, the render stops with an error giving the
projection and the batch it was made after, before more work is done. By default the
ceiling is 40% of the memory available after the first batch, at most 6 GB; it is read
once per render, so the same scene can be refused on a busy machine and accepted on an
idle one. Three settings replace it with a fixed ceiling in GB (10^9 bytes, any positive
finite number such as `0.5` or `24`), with no reference to free memory:

```pov
global_settings { photons { method 2 quality 8 memory 12 } }
```

```
povray +Iscene.pov Photon_Memory=24       // or Photon_Memory=24 in povray.ini
POV_PHOTON_MEMORY=24 povray +Iscene.pov
```

The first that is given wins: the `Photon_Memory` command-line or INI option, then the
`POV_PHOTON_MEMORY` environment variable (read once per process), then the scene's
`memory`, then the default. The ceiling is a property of the machine, so whoever runs the
render can always raise or lower what a scene asks for; when an option or the environment
overrides a different scene value, the log says so in one line naming the ceiling used.
`memory` is accepted only with `method 2`, like `quality`. A value that is zero, negative,
not finite or not a number is a parse error at the scene line, an option error, or, for the
environment variable, an error when method 2 photons start. The error for a projection or
photon file over the ceiling names where the ceiling came from, and loading a photon file
uses the same ceiling as shooting.
Otherwise the maps reserve the projected capacity, so they grow only when a later batch
deposits more than the earlier ones, and the index is partitioned in place. The log line `Progressive photons:` reports
batches, emitted samples, deposits, `k`, contributions, preparation time and the peak
photon storage.

Deposits are merged in shard order, so the maps, and the image, do not depend on
the number of threads on the same build and platform. Because the maps are
rebuilt identically, `+C` continues a render with the same photons.

## Tests

`tests/source/progressive_photon.cpp` checks the budget, the per-batch memory projection,
determinism and the
stratification of the Sobol dimensions (every aligned block of 2^m values holds one
value per stratum; pairwise star discrepancy against random points).
`tests/source/progressive_surface.cpp` checks that the estimator reproduces a uniform
irradiance within 1% on a plane and on a sphere, including at a support of about 0.3 sphere
radii, and within 2% in a sparse region gathered at the cap; that close receivers keep
separate supports; that a 20-photon caustic, alone or beside a dense receiver, gets a usable
radius and a nonzero estimate; that the partitioned search, one- and two-sided, returns the
same neighbours as a brute-force search; and that the aiming domain's square and disc
maps invert each other, that every direction reaching a sphere or a box from a point or a
small area light lies in a cell with samples, that the density integrates to a sphere's
solid angle and to its shadow's area under a parallel light within 1%, and that a weight
of 3 gives a small box more than its share of the per-target half. `tests/render/progressive_photons.sh`
covers thread-count equality for every light type and media, dispersion, subsurface,
thin, close, curved and reflected cases, anti-aliasing, `+C`, linear power, image-size
independence of the logged budget, the default memory budget error, refusal under a tiny
ceiling and acceptance under a generous one from the scene's `memory`, `Photon_Memory` and
`POV_PHOTON_MEMORY`, their precedence (the option over the environment over the scene, in
both directions, with the override line), `memory` under method 1, and invalid ceilings in
all three (`abc`, 0, negative, NaN) and invalid quality; every other render there runs under
a generous ceiling, so no result depends on free memory; `tests/render/progressive_photons4.sh`
accepts `memory` expressions in a `.pov4` scene and rejects invalid ones;
`tests/render/progressive_surface.sh` also lights a thin `diffuse_back` disc from behind and
requires its underside to match the lit top, as does a `double_illuminate` disc;
`tests/render/progressive_receivers.sh` focuses a caustic on a wavy floor built as a mesh, a
smooth mesh, a height field, a smooth height field, an isosurface, a parametric and a
`bicubic_patch` at `u_steps 1`, and requires each to match classic photons within 5%, the smooth forms to match the
faceted ones and the isosurface and the parametric to match tight-accuracy ones, and requires an `irid`
finish to tint the caustic as classic photons do, each colour channel within 5%; the
emitter, direct-light, volume, volume light-group, media-light, light-group, radiosity, photon-file and `.pov4` scripts run with the new estimator.
`tests/render/progressive_direct_lights.sh` requires an ior 1 lens to pass the direct light
within 3% for every fade, a scattering medium lit through it to glow within 4.5% of the
directly lit medium at quality 16, and a glass caustic to match classic photons within 10%.
`tests/render/photon_target_weights.sh` renders `tests/render/photon_target_rig.pov` with
weights 1:1, 4:1 and 1:4 under a point and a parallel sun and requires a weight of 4 to
raise or lower the patch's logged share by more than a tenth, the image to match across thread counts and the photon-only image mean
to stay within 3%, the grass clump to give the same split and image with `split_union` on as off,
and negative, infinite and NaN weights to be rejected; the photon-file script also
refuses a file saved with another target weight.

## Measurements

Instructions are user-space instruction counts of whole renders (parse, shooting, maps and
camera), four threads unless stated, counted exactly; one run each. Image error is the
linear-light RMSE against a quality 64 render with another seed, over pixels whose
reference is at most 1, each image clipped at 1, so that the few focal pixels that every
camera pattern samples differently do not decide it. Errors are given at one camera sample
per pixel (`-A`), where they include sub-pixel structure that anti-aliasing averages away,
and, where measured, with anti-aliasing `+A0 +AM1 +R4` (about 15 samples per pixel on these
scenes). Each reference is rendered by the same build and with the same sampling as the
images it judges. "At equal instructions" interpolates each method's error in log-log
between quality 0.5, 1 and 2 to the instructions of a cone per target at quality 1.

### Aiming domain

Aiming each sample at one target's bounding sphere instead (a cone of directions, or a disc
for a parallel light), splitting samples between targets by weight, plus a tenth
isotropic for nonparallel lights, leaves most samples depositing nothing at quality 1: the
bounding sphere of a target's box is far larger than the target (a glass sphere fills a
third of its cone, so 66% of the sphere scene's samples meet the floor first), a sparse
union is mostly empty space (89% of the rig's samples and 95% of a canopy of 60 small discs
deposit nothing), an aperture rejects 94% of each prism beam's samples on the leaf, an
opaque object over a target stops 90%, and the isotropic tenth deposits on 0-1% of its
samples. An empty sample costs about 5,000 instructions and a depositing one about
16,000, so the empty samples take 15% (water) to 43% (sphere) of the shooting work on the
sample scenes and over 90% where a target is hidden or sparse.

`tests/render/photon_aim_cases.pov` holds five layouts, photon-only on a white floor:
two glass spheres in line along the light, a glass sphere the light reaches only through a
mirror, an opaque box over a glass sphere, a sparse canopy of 60 glass discs and 64 small
glass spheres that are each a target. Error at equal instructions, the aiming domain over
a cone per target, quality 1 of the cones:

| scene | instructions, cones, quality 1 | RMSE, no AA | ratio | instructions, cones, AA | RMSE, AA | ratio |
|---|---:|---:|---:|---:|---:|---:|
| sphere, `progressive_photons.pov` 192x128 photon-only | 2.47 G | 0.0081 / 0.0068 | 0.84 | 6.3 G | 0.00389 / 0.00196 | 0.50 |
| water, `photon-budget-water.pov` 500x333 | 6.93 G | 0.0283 / 0.0251 | 0.89 | 63.2 G | 0.0253 / 0.0197 | 0.78 |
| target rig, parallel sun, 640x360 photon-only | 28.9 G | 0.0523 / 0.0474 | 0.90 | | | |
| two spheres in line, 320x240 | 3.61 G | 0.0206 / 0.0143 | 0.69 | 15.9 G | 0.0119 / 0.0063 | 0.53 |
| sparse canopy of 60 discs | 3.90 G | 0.0480 / 0.0234 | 0.49 | 44.3 G | 0.0436 / 0.0151 | 0.35 |
| 64 small targets | 4.30 G | 0.0096 / 0.0080 | 0.83 | 12.0 G | 0.0135 / 0.0110 | 0.81 |
| glass ball over 64 by 64 boxes, 256x192 | 2.72 G | 0.0043 / 0.0020 | 0.46 | 7.9 G | 0.00077 / 0.00062 | 0.80 |

The advantage holds with anti-aliasing and grows where the error at one sample per pixel is
set by the camera rather than the photons (the sphere, the spheres in line, the canopy).
The rig is not rendered with anti-aliasing (its camera work at about 15 samples per pixel
exceeds the budget of these runs). Each quality step stores more deposits and costs more:
in the render queue (all threads, quality 1, one run each) the sphere takes 7.53 G against
5.15 G with cones, the water 12.99 G against 12.16 G, the rig 45.0 G against 33.3 G, the
spheres in line 9.95 G against 7.85 G, the canopy 8.46 G against 8.31 G, the 64 small
targets 7.16 G against 8.68 G (every sample there also evaluated every target's cone) and
the ball over the boxes 8.16 G against 5.85 G.

On the leaf (`subsurface_leaf.pov` with `photons { method 2 quality Q }`, 160 by 120, the
prism lights alone, 61 dispersion bands) the domain makes every sample pass the prisms'
apertures, so quality 1 stores 9.9 million deposits (1.2 GB of map) and takes 213 G in the
queue against 41 G with cones; the image there is set by subsurface and camera sampling, so
such a scene wants a lower quality (quality 0.25 stores 2.5 million deposits for 80 G).

Splitting every sample by solid angle alone improves the whole rig image but loses the
small grass patch's caustic (its error rises by a third): a small target gets photons in
proportion to its size. Half by solid angle and half by target keeps the patch's error
within 8% of a cone per target while gaining elsewhere. Learning a density over the domain
between batches from where samples deposited, or loading one learned from a quality 8
render, gives no consistent gain: 0.6 to 1.0 of the domain's error on most scenes, but 1.7
to 3.0 times it under the opaque box, where concentrating photons on the visible part
shrinks the floor's base radius and leaves its faint reflections at a smaller cap. The
density therefore does not change between batches.

A scattering medium lit through an ior 1 lens (`tests/render/progressive_direct_lights.pov`,
quality 16) glows 0.960 to 0.967 of the directly lit medium over six seeds with the domain,
and 0.933 to 0.961 with cones; the remaining loss is the media estimator's.

Building the domain takes 0.05 G instructions for a point light and one target, 0.19 G for
1,300 pieces, 1.2 G for 10,000 and 11.6 G for 100,000 (2.0 G for a parallel light).

### Against classic photons

Classic photons at a ladder of their own settings, at one sample per pixel against one
quality 64 method 2 reference per scene, need these instructions to reach the error of method 2 at
quality 1: the sphere 55 G (method 2 5.0 G; classic counts 262,144, 1,048,576, 4,194,304,
16,777,216 and 67,108,864 photons), the water 20 G (7.8 G; spacing 0.02, 0.01, 0.005,
0.0035 and 0.0025), the rig 368 G (41 G; 200,000, 800,000, 3,200,000 and 5,000,000
photons), the spheres in line 84 G (5.9 G; 100,000, 400,000, 1,600,000, 6,400,000 and
25,600,000 photons) and the sparse canopy 8.7 G (4.3 G; 25,000 to 6,400,000 photons).
Classic photons use `fade_distance sqrt(0.5)` so their power matches.

### Where the work goes

Each scene is rendered three times: the full image, one pixel, and one pixel with photons
off (`+Q8`). Shooting is the one-pixel run minus the photons-off run (shooting, deposits and
map build); camera is the full run minus the one-pixel run.

| scene, quality | photons off | shooting | camera | total | shot | deposits | map peak |
|---|---:|---:|---:|---:|---:|---:|---:|
| sphere 192x128 photon-only, 0.25 | 0.14 G | 1.17 G | 0.23 G | 1.55 G | 65,536 | 89,310 | 22 MB |
| sphere, 1 | 0.14 G | 4.59 G | 0.27 G | 5.00 G | 262,144 | 357,214 | 41 MB |
| sphere, 4 | 0.14 G | 18.47 G | 0.34 G | 18.95 G | 1,048,576 | 1,428,867 | 123 MB |
| water 500x333, 0.25 | 0.18 G | 1.03 G | 2.47 G | 3.67 G | 65,536 | 62,320 | 11 MB |
| water, 1 | 0.18 G | 4.12 G | 3.54 G | 7.84 G | 262,144 | 249,279 | 25 MB |
| water, 4 | 0.18 G | 16.61 G | 6.26 G | 23.05 G | 1,048,576 | 997,120 | 83 MB |
| leaf 160x120, 0.25 | 0.14 G | 49.6 G | 30.1 G | 79.8 G | 65,536 | 2,475,391 | |
| leaf, 1 | 0.14 G | 198.3 G | 30.9 G | 229.4 G | 262,144 | 9,901,569 | 1.2 GB |

Shooting costs 12,800 (sphere) to 20,000 (leaf) instructions per deposit at every quality,
so it is set by the photons asked for; camera work barely depends on quality.

## Limits

- A photon file is checked against the scene's structure and photon settings, not its
  geometry: as with classic files, a scene whose objects or lights moved reuses the
  saved photons. The file is written in the machine's byte order; loading it on another
  platform, or on a build with other colour channels, fails.
- The fixed-`k` rule is global; it is not adapted to local noise or gradients,
  and it smooths features narrower than the support at low quality. Sparse
  regions are estimated at the capped radius and stay noisy.
- Density is truncated at object edges and at changes of medium; there is no boundary
  correction, so a receiver's estimate reads low within one support of its edge (about
  half at a straight edge), and a surface built from many small objects (a tiled floor)
  reads low throughout.
- A medium's base support is the median over all its segments, including segments
  that a light group keeps from it, so a medium crossed by another group's photons can
  differ from the same medium alone by a few 16-bit output levels.
- Direct media lighting on the chords of a curved ray sees the global lights
  only, even in a `global_lights off` group; their media photons follow the surface
  that ends the path.
- Deposits per sample, and therefore memory, are not bounded by quality alone; a
  quality whose projected storage exceeds the memory budget stops the render.
- Receivers with fewer than `k` deposits, and sparse regions of any receiver, are
  estimated at the capped support: their energy is kept but spread over it.
- A CSG subsurface receiver's slab and boundary probe use the whole map's median base
  radius (see CSG receivers).
- Media noise falls little with quality on the tested volume scene; only the cost grows
  (not re-measured with the aiming domain).
- A target behind an aperture receives every sample the domain aims at it, so a scene
  whose targets sit behind apertures, with many dispersion bands, stores many deposits
  per quality step (see Aiming domain).
- Target weights are set by hand; nothing measures which target the image needs, and the
  aiming does not depend on the camera.
- The aiming domain is rastered at 128 by 128 cells, so a target much smaller than a cell
  (many small targets spread over a wide cap) still receives samples over its whole cell;
  building it grows with the number of target pieces (see Aiming domain).

