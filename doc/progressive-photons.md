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
flat face, and measured against it most neighbours on the face fell outside the slab (a
smooth wavy mesh received 0.63 of classic's light, a smooth height field 0.68 and a wavy
patch at `u_steps 1` 0.84). Shading still uses the interpolated
normal. The tolerance is zero for exact solvers; an `isosurface` stops root finding
within its `accuracy` before the surface, and a `parametric`'s interval subdivision
within its `accuracy` of it, so their tolerance is that `accuracy`, scaled by the
object's transform. Without it a focused caustic on a default-accuracy isosurface kept
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
clipped segment is integrated analytically. This volume estimator keeps the
box kernel and fixed neighbour count of the earlier prototype, now over the
single map. Its noise does not fall with quality: on `tests/render/progressive_volume.pov`
at 96 by 96, two seeds differ by an RMSE of 0.0053 at quality 0.25, 1 and 4 alike, while
the render costs 216 G, 760 G and 2,855 G instructions. Scaling the neighbour count with
the batch count at the three-dimensional kernel rate (`32 (batches / 4)^(4/7)`) moved
that difference to 0.0065 at quality 0.25 and 0.0049 at quality 4, for 4% more work, so
the count stays fixed; the remaining noise there comes from camera and media sampling.

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
and cylinder lights; a scattering medium lit through it glows 0.96 of the directly lit
medium at quality 16 at both distances, 0.961 to 0.966 over six seeds (with a cone per
target it was 0.93 to 0.96 over the same seeds and 0.99 only at the default seed, so the
remaining 4% is the media estimator's). A glass sphere's caustic is 1.01 of classic's at 5 units and 1.00 at 245;
the earlier rule, which scaled photon power by `A(d) * d^2` at the light-to-target distance
and let photons fall as `1/r^2` beyond it, gave 0.53 and 0.98. The weights barely vary
across one receiver, so they add no visible noise: the near glass sphere at 160 by 160,
quality 1, has relative RMS 0.0356 against quality 64 under this rule and 0.0355 under
the earlier one, at 2.77 G instructions each. No bound on the weight is needed.

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
With several targets the step used to come from the light's first target whatever the
photon was aimed at, so the segment count depended on the order of targets in the scene
(406,000 or 470,000 segments for the same scene there); it now differs by 0.1%.
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
and do not change between batches. The split is fixed by the scene, not measured: an
earlier version set it from the previous batch's hit rate, which starved targets that
are hard to hit, and learning a density from where photons land did not pay for itself
(see Aiming domain under Measurements).

A `union`, or any other CSG object, flagged with `photons { target W }` is one target
of weight W, whether or not `split_union` splits it for bounding: the split members
share one entry and their pieces form its cells, so a split and an unsplit union get the
same samples, photons and image.
A target flag on a member inside a flagged union is replaced by the union's, as before.
Members are separate targets only when the union itself is not flagged and they are.
Earlier versions made each member of a split union its own target, so a four-blade
clump took four shares where one was meant. Method 1 still shoots each split member
separately.

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
deposit and 96 per media segment: on the leaf of the measurements below, at quality 1,
591,415 deposits take 37.9 MB, saving adds 1.1 G instructions to the 168.7 G render,
and a loaded render gives the same image in 164.1 G.
`tests/render/progressive_photon_file.sh` saves and loads media, subsurface and
dispersion scenes, turns the camera, and checks each error, including a load refused
under a tiny ceiling from the option, the environment and the scene, and accepted when the
option raises a tiny scene ceiling.

## Media lights

Media lights now use method 2 through the same `Emitter` interface. Their
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

## Changes from the earlier prototype

The earlier method 2 traced one full camera pass per shooting batch (four at
quality 1), averaged the passes with a shrinking box-kernel radius chosen from a
pilot batch, required `+PR -A`, and grouped batches differently at 8,000,000
pixels and above. Each pass repeated camera, direct-light and subsurface work. Its
surface acceptance slab, `|dot(p - x, n_x)| <= 0.05 r`, rejects deposits on
receivers curved at a radius smaller than about ten gather radii: on a sphere the
gathered fraction is 0.50 at `r = 0.2 R` and 0.25 at `r = 0.4 R`. That schedule,
the pilot, the per-pixel estimator state and the image-size threshold are gone.

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
`tests/render/photon_target_weights.sh` renders `tests/render/photon_target_rig.pov` with
weights 1:1, 4:1 and 1:4 under a point and a parallel sun and requires a weight of 4 to
raise or lower the patch's logged share by more than a tenth, the image to match across thread counts and the photon-only image mean
to stay within 3%, the grass clump to give the same split and image with `split_union` on as off,
and negative, infinite and NaN weights to be rejected; the photon-file script also
refuses a file saved with another target weight.

## Measurements

User-space instruction counts of whole renders (parse, shooting, maps and camera), four
threads, counters not multiplexed; one run each. Errors are linear-light RMSE over
pixels whose reference value is at most 1 (the few focal pixels above 1 are sampled
differently by every camera pattern and would otherwise dominate). "Earlier method 2"
is the multi-pass prototype described above. Classic photons use `fade_distance
sqrt(0.5)` so their power matches. The tables before Aiming domain were measured with
the earlier aiming of a cone or disc per target; the aiming domain wastes far fewer
samples, so each quality now costs more and stores more deposits (see Aiming domain).

Glass sphere caustic, `tests/render/progressive_photons.pov`, 96 by 64, photon-only.
The no-anti-aliasing reference is quality 64 with another seed; two such seeds differ
by 0.0009, and a classic 4,194,304-photon render differs from it by 0.0026.

| render | instructions | RMSE, no AA reference | RMSE, 36-sample reference |
|---|---:|---:|---:|
| classic 1,048,576 photons | 3.53 G | 0.0048 | 0.0048 |
| earlier method 2, quality 1 | 3.13 G | (4 jittered samples) | 0.0073 |
| quality 1 | 2.26 G | 0.0027 | 0.0042 |
| quality 1, `+A0 +AM1 +R4` | 3.14 G | | 0.0016 |
| quality 4 | 8.55 G | 0.0014 | 0.0039 |
| earlier method 2, quality 4 | 10.94 G | (16 jittered samples) | 0.0036 |

Water caustic, `tools/bench/photon-budget-water.pov`, 1000 by 667, no anti-aliasing.
References: quality 16 with another seed (two seeds differ by 0.0040) and classic
spacing 0.005; the two references differ by 0.0149, mostly at the shadow edge of the
water box, where the kernel spreads deposits that classic gathers keep sharp.

| render | instructions | RMSE vs quality 16 | RMSE vs classic 0.005 |
|---|---:|---:|---:|
| classic spacing 0.02 | 24.4 G | 0.0247 | 0.0351 |
| classic spacing 0.01 | 41.1 G | 0.0090 | 0.0169 |
| earlier method 2, quality 1 | 64.3 G | 0.0647 | 0.0705 |
| quality 1 | 17.9 G | 0.0176 | 0.0285 |
| quality 4 | 38.5 G | 0.0092 | 0.0216 |

Shooting and kernel ablations, RMSE against the same-camera reference:

| change at quality 1 (quality 4) | sphere | water |
|---|---:|---:|
| as described | 0.0027 (0.0014) | 0.0176 (0.0092) |
| hashed random shooting | 0.0041 (0.0025) | 0.0625 (0.0377) |
| box kernel | 0.0045 (0.0033) | 0.0296 |
| cone kernel | 0.0027 (0.0014) | 0.0158 |
| `k` base 16 / 96 instead of 48 | 0.0047 / 0.0035 | 0.0284 / 0.0279 |
| cap 4 / 16 base radii instead of 8 | 0.0058 / 0.0019 | 0.0175 / 0.0176 |

Stratified shooting is the largest single gain: on the water caustic, random shooting
at quality 4 (0.0377) is no better than stratified shooting at quality 0.25 (0.0362),
with sixteen times fewer photons. The cap changes no image here but costs work on the
water scene: 16.2 G at a cap of 4, 17.9 G at 8 and 23.3 G at 16; on the leaf below it
is 170 G, 169 G and 178 G.

Leaf, `tests/render/subsurface_leaf.pov` with `photons { method 2 quality Q }` in
place of its classic block, 320 by 240, both prisms, subsurface method 2 with 64
samples. Here image error is dominated by subsurface and camera sampling, not photons:
against a 9-sample classic 1,600,000-photon reference the RMSE is 0.028 for classic,
quality 1 and quality 4 alike, 0.019 for the earlier prototype (four camera samples) and
0.014 for quality 1 with four anti-aliasing samples. With the sun, fill and sky off, the
lit leaf's mean is within 2.3% of classic per channel (quality 1: +1.2%, +0.6%, +2.1%;
the earlier prototype +0.6%, +1.0%, +0.2%). Before deposits were indexed by normal
direction its quality 1 camera work was 244 G, as much as classic, because gathers on
the unlit face scanned the lit face's deposits within the cap.

### Aiming domain

Shooting used to aim each sample at one target's bounding sphere (a cone of directions,
or a disc for a parallel light), splitting samples between targets by weight, plus a tenth
isotropic for nonparallel lights. Measured at quality 1, most samples deposited nothing:
the bounding sphere of a target's box is far larger than the target (a glass sphere fills
a third of its cone, so 66% of the sphere scene's samples met the floor first), a sparse
union is mostly empty space (89% of the rig's samples and 95% of a canopy of 60 small
discs deposited nothing), an aperture rejected 94% of each prism beam's samples on the
leaf, an opaque object over a target stopped 90%, and the isotropic tenth deposited on 0-1%
of its samples. Where cones overlap, up to 27% of the samples aimed at one target met
another first (two spheres in line; 8% on the rig); the density was exact, so this cost
nothing in bias. An empty sample costs about 5,000 instructions (a scene whose samples are
95% empty shoots at 4,900 per sample) and a depositing one about 16,000 (a scene whose
samples are 95% depositing shoots at 15,900), so the empty samples took about 15% (water)
to 43% (sphere) of the shooting work on the sample scenes and over 90% where a target was
hidden or sparse. Every sample also evaluated every target's cone: with 64 small targets
the cone aiming cost 4.35 G at quality 1 for 87,000 deposits, the domain 3.08 G for
120,000.

`tests/render/photon_aim_cases.pov` adds five layouts, photon-only on a white floor:
two glass spheres in line along the light, a glass sphere the light reaches only through a
mirror, an opaque box over a glass sphere, a sparse canopy of 60 glass discs and 64 small
glass spheres that are each a target. Image error against a quality 64 reference with
another seed, over pixels whose reference is at most 1, each image clipped at 1 so that
the few focal pixels the camera samples differently do not decide it; at equal
instructions, interpolated in log-log between quality 0.5, 1 and 2 of each aiming to
the cost of the cone aiming at quality 1 (four threads, counts exact):

| scene | cone aiming, quality 1 | instructions | aiming domain at equal cost | ratio |
|---|---:|---:|---:|---:|
| sphere, `progressive_photons.pov` 192x128 | 0.0066 | 2.5 G | 0.0049 | 0.74 |
| water, `photon-budget-water.pov` 500x333 | 0.0264 | 7.0 G | 0.0231 | 0.87 |
| target rig, parallel sun, 640x360 | 0.0466 | 29.0 G | 0.0413 | 0.89 |
| target rig, its grass patch only | 0.0411 | 29.0 G | 0.0444 | 1.08 |
| target rig, sun 200 units away | 0.0481 | 27.8 G | 0.0414 | 0.86 |
| target rig, sun 200 units away, grass patch | 0.0441 | 27.8 G | 0.0440 | 1.00 |
| two spheres in line | 0.0182 | 3.7 G | 0.0108 | 0.59 |
| sphere seen through a mirror | 0.0062 | 3.6 G | 0.0049 | 0.79 |
| sphere under an opaque box | 0.00046 | 1.8 G | 0.00050 | 1.08 |
| sparse canopy of 60 discs | 0.0367 | 3.9 G | 0.0083 | 0.23 |
| 64 small targets | 0.0095 | 4.3 G | 0.0078 | 0.83 |

On the leaf (`subsurface_leaf.pov` with `photons { method 2 quality Q }`, 160 by 120, the
prism lights alone) the error is 0.0249 to 0.0252 for both aimings at every quality, set by
subsurface and camera sampling; there the domain makes every sample pass the apertures, so
quality 0.25 shoots 2.5 million deposits for 80 G instructions where the cone aiming shot
0.6 million at quality 1 for 44 G. On the prism sample at 128 by 72 against a quality 4
reference the domain at quality 0.25 (1,278 G, 0.0133) matches the cone aiming at quality
0.5 (1,100 G, 0.0136). The deposited power summed over every sample, which the image
converges to, agrees with the cone aiming within 0.01% for point, area, spot, parallel and
cylinder lights over three seeds each.

Splitting every sample evenly by solid angle alone gave 0.84 on the rig and 0.83 with the
far sun, but 1.35 and 1.20 on the grass patch: a small target got photons in proportion to
its size and lost its caustic. Half by solid angle and half by target keeps the patch
within 8% and loses at most 5% elsewhere. Learning a density over the domain between
batches from where samples deposited (from deposit counts or from delivered power, with a
uniform half or fifth as a floor), or loading one learned from a quality 8 render
beforehand, gave no consistent gain: from 0.6 to 1.0 of the domain's error on most scenes,
but 1.7 to 3.0 times it under the opaque box, where concentrating photons on the visible
part shrank the floor's base radius and left its faint reflections at a smaller cap.
Learning is therefore not used, and the density does not change between batches.

Against classic photons at classic's own instruction counts (1, 4 and 16 million photons
on the sphere and the layouts, 0.2, 0.8 and 3.2 million on the rig, spacing 0.02, 0.01 and
0.005 on the water), the domain's error is 0.3 to 0.7 of classic's, except on the water at
spacing 0.005 (100 G), where classic's 0.0081 beats the domain's 0.0105 extrapolated from
quality 2.

Building the domain takes 0.05 G instructions for a point light and one target, 0.19 G for
1,300 pieces, 1.2 G for 10,000 and 11.6 G for 100,000 (2.0 G for a parallel light).

### Where the work goes

Each scene and configuration is rendered three times: the full image, one pixel, and one
pixel with photons off (`+Q8`, the earlier prototype with `+PR`). Shooting is the one-pixel
run minus the photons-off run (shooting, deposits and map build); camera is the full run
minus the one-pixel run. Instructions are exact; wall seconds are single runs at four
threads on a shared machine, within about 0.3 s, and the start-up of each run is about
0.8 s. "Per shot" and "per deposit" divide the shooting instructions by the photons shot
and stored.

Sphere, 96 by 64, photon-only:

| render | photons off | shooting | camera | total | peak RSS | shot | deposits | per shot | per deposit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| classic 1,048,576 | 0.14 G | 3.20 G / 0.1 s | 0.2 G / 0.2 s | 3.5 G / 1.4 s | 42 MB | 157,873 | 441,368 | 20,279 | 7,254 |
| earlier method 2, quality 1 | 0.97 G | 1.90 G / 0.3 s | 0.3 G / 0.0 s | 3.1 G / 1.4 s | 38 MB | 327,680 | 132,029 | 5,788 | 14,366 |
| quality 0.25 | 0.14 G | 0.48 G / 0.0 s | 0.0 G / 0.0 s | 0.7 G / 0.8 s | 31 MB | 65,536 | 26,390 | 7,347 | 18,246 |
| quality 1 | 0.14 G | 2.02 G / 0.0 s | 0.1 G / 0.1 s | 2.3 G / 1.1 s | 41 MB | 262,144 | 105,536 | 7,714 | 19,160 |
| quality 4 | 0.14 G | 8.36 G / 1.0 s | 0.1 G / 1.8 s | 8.6 G / 3.9 s | 62 MB | 1,048,576 | 422,196 | 7,971 | 19,797 |

Water, 1000 by 667:

| render | photons off | shooting | camera | total | peak RSS | shot | deposits | per shot | per deposit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| classic spacing 0.02 | 0.29 G | 6.01 G / 0.8 s | 18.1 G / 1.6 s | 24.4 G / 3.2 s | 51 MB | 568,787 | 352,315 | 10,567 | 17,060 |
| classic spacing 0.01 | 0.29 G | 23.88 G / 2.1 s | 17.0 G / 0.8 s | 41.1 G / 3.7 s | 93 MB | 2,272,472 | 1,409,299 | 10,510 | 16,946 |
| earlier method 2, quality 1 | 1.48 G | 2.88 G / 0.1 s | 59.9 G / 4.2 s | 64.3 G / 5.5 s | 84 MB | 327,680 | 203,653 | 8,784 | 14,133 |
| quality 0.25 | 0.29 G | 0.72 G / 0.1 s | 10.2 G / 0.9 s | 11.2 G / 1.8 s | 50 MB | 65,536 | 40,691 | 11,000 | 17,716 |
| quality 1 | 0.29 G | 3.05 G / 0.6 s | 14.6 G / 1.6 s | 17.9 G / 3.0 s | 61 MB | 262,144 | 162,775 | 11,649 | 18,760 |
| quality 4 | 0.29 G | 12.39 G / 2.1 s | 25.9 G / 1.0 s | 38.5 G / 3.9 s | 91 MB | 1,048,576 | 651,087 | 11,813 | 19,025 |

Leaf, 320 by 240:

| render | photons off | shooting | camera | total | peak RSS | shot | deposits | per shot | per deposit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| classic 400,000 | 0.16 G | 37.2 G / 3.6 s | 252 G / 26.4 s | 290 G / 30.8 s | 225 MB | 658,246 | 1,803,296 | 56,513 | 20,629 |
| earlier method 2, quality 1 | 1.23 G | 14.4 G / 2.0 s | 626 G / 63.4 s | 642 G / 66.6 s | 214 MB | 327,680 | 745,239 | 44,075 | 19,380 |
| quality 0.25 | 0.16 G | 3.0 G / 0.3 s | 155 G / 15.6 s | 158 G / 16.7 s | 137 MB | 65,536 | 147,862 | 45,809 | 20,304 |
| quality 1 | 0.16 G | 12.3 G / 1.2 s | 156 G / 14.4 s | 169 G / 16.4 s | 170 MB | 262,144 | 591,415 | 46,985 | 20,826 |
| quality 4 | 0.16 G | 49.6 G / 6.0 s | 159 G / 14.7 s | 209 G / 21.5 s | 277 MB | 1,048,576 | 2,363,821 | 47,252 | 20,961 |

Shooting costs 17,700 to 21,000 instructions per deposit at every quality on all three
scenes, so it is set by the photons asked for. Camera work with the single map barely depends on
quality (the leaf's 155 G to 159 G from quality 0.25 to 4), while the earlier prototype
repeated it once per pass. The prototype's photons-off control still runs its progressive
camera loop, hence its larger value.

## Limits

- A photon file is checked against the scene's structure and photon settings, not its
  geometry: as with classic files, a scene whose objects or lights moved reuses the
  saved photons. The file is written in the machine's byte order; loading it on another
  platform, or on a build with other colour channels, fails.
- The fixed-`k` rule is global; it is not adapted to local noise or gradients,
  and it smooths features narrower than the support at low quality. Sparse
  regions are estimated at the capped radius and stay noisy.
- Density is still truncated at object edges and at changes of medium; there is
  no boundary correction.
- A medium's base support is the median over all its segments, including segments
  that a light group keeps from it, so a medium crossed by another group's photons can
  differ from the same medium alone by a few 16-bit output levels.
- Direct media lighting on the chords of a curved ray still sees the global lights
  only, even in a `global_lights off` group; their media photons follow the surface
  that ends the path.
- Deposits per sample, and therefore memory, are not bounded by quality alone; a
  quality whose projected storage exceeds the memory budget stops the render.
- Receivers with fewer than `k` deposits, and sparse regions of any receiver, are
  estimated at the capped support: their energy is kept but spread over it.
- A CSG subsurface receiver's slab and boundary probe use the whole map's median base
  radius (see CSG receivers).
- Media noise does not fall with quality on the tested scene; only the cost grows.
- Target weights are set by hand; nothing measures which target the image needs, and the
  aiming does not depend on the camera.
- The aiming domain is rastered at 128 by 128 cells, so a target much smaller than a cell
  (many small targets spread over a wide cap) still receives samples over its whole cell;
  building it grows with the number of target pieces (see Aiming domain).

