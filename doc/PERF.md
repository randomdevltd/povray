# Performance

What this fork changes to make large scenes trace faster, how it was measured, and what is left.

## Results

Measured on a 4-vCPU AMD EPYC Genoa VM (Ubuntu 26.04, GCC 15) with `tools/bench`: user-space cycles of a render
at `+WT4`, minus a 1-pixel render of the same scene, so tracing and parsing separate. Medians of two to six
repeats; repeats agree within 2%. The reference is Ubuntu's packaged POV-Ray 3.7.0.10.

**A large private scene**: 74 thousand finite objects, mostly meshes, rendered as a band without anti-aliasing.
Shadow rays are 97% of all rays.

| Build | Trace Gcycles | vs 3.7 | Parse Gcycles | Peak memory |
|---|---|---|---|---|
| Ubuntu 3.7.0.10 | 257.6 | — | 148.2 | 2086 MB |
| upstream 3.8 master + cpuid and queue fixes | 310.5 | +20.5% | 139.8 | 1589 MB |
| this branch | 122.0 | −52.6% | 114.5 | 1527 MB |
| this branch, PGO | 117.3 | −54.5% | 105.1 | 1526 MB |

**The whole frame** of the same scene at low resolution, one run each:

| Build | Gcycles | Wall | Trace CPU | Peak memory |
|---|---|---|---|---|
| Ubuntu 3.7.0.10 | 2509 | 225.8 s | 660 s | 2065 MB |
| upstream 3.8 master + cpuid and queue fixes | 2567 | 224.1 s | 679 s | 1568 MB |
| this branch | 1244 | 119.2 s | 317 s | 1506 MB |
| this branch, PGO | 1166 | 111.9 s | 297 s | 1505 MB |

PGO here was trained on other parts of the same scene.

**The standard benchmark scene** (3.7's `benchmark.pov`, 384×384, its own `benchmark.ini`), where noise, media and
isosurfaces dominate and bounding barely registers:

| Build | Gcycles | vs 3.7 | Wall |
|---|---|---|---|
| Ubuntu 3.7.0.10 | 1116.5 | — | 86.7 s |
| upstream 3.8 master + cpuid and queue fixes | 1103.0 | −1.2% | 85.8 s |
| this branch | 1125.9 | +0.8% | 87.7 s |
| this branch, PGO | 1102.1 | −1.3% | 85.6 s |

**The heaviest scenes in 3.7's distribution**, one run each at `+A0.3`, `+WT4`, cycles:

| Scene | Size | Ubuntu 3.7 | 3.8 master + fixes | this branch | vs 3.7 |
|---|---|---|---|---|---|
| `advanced/blocks/stackertransp.pov` | 800×600 | 397.2 G | 408.5 G | 256.7 G | −35% |
| `advanced/abyss.pov` | 800×600 | 138.4 G | 145.4 G | 113.8 G | −18% |
| `advanced/teapot/teapot3.pov` | 1920×1440 | 66.3 G | 66.8 G | 55.7 G | −16% |
| `bsp/Tango.pov` | 800×600 | 106.7 G | 136.6 G | 90.3 G | −15% |
| `language/trace-wicker.pov` | 1920×1440 | 82.4 G | 76.6 G | 74.0 G | −10% |
| `language/tracevines.pov` | 1920×1440 | 21.2 G | 24.2 G | 21.2 G | 0% |
| `advanced/isocacti.pov` | 800×600 | 291.6 G | 280.8–298.3 G | 334.9–348.2 G | +17% |

`isocacti` is not slower through anything this branch computes: it runs fewer instructions, and 3.8 master built
with `-falign-functions=64 -falign-loops=32` is as slow (362.1 G). The isosurface function interpreter,
`POVFPU_RunDefault`, is front-end bound (stalled cycles 11.5 G against 31.2 G between two placements of identical
code), so its speed depends on where the linker happens to put it.

Single-threaded renders repeat bit for bit. Against 3.8 master this branch changes 0.15% of pixels in a test band,
0.01% by more than 2 levels and none by more than 11: rays that meet the shared edge of two triangles get the
same depth from both, and which one supplies the normal depends on the order they are tested in. PGO alone
changes about as many.

## Subsurface photons

Multiple-scattering diffusion now receives incident surface photons with methods 1 and 2.
The public focused-caustic fixture adds about 44% instructions with the default cloud spacing
to supply previously missing light; finer spacing trades additional gathers and memory for accuracy.
See [the model, fixtures and measured costs](SUBSURFACE_PHOTONS.md).

Each photon deposit's boundary check (a ray through the receiver and two inside tests) is now computed once per receiver
and render and reused by every later gather, instead of once per gather. On the leaf fixture at 96x72 with 20000
photons the photon share of trace CPU falls 21x on an isosurface receiver (234 s to 10.9 s), 4.3x on a closed mesh,
2.1x on a CSG with holes and 1.4x on the analytic lens. Images are identical except 51 pixels of the isosurface,
where the root solve shifts 108 of 16.9 million accepted deposits across the match tolerance (at most 130 of 65535).

## Ordinary photon gathering

On `lights/phot_met_glass.pov` at 960×480, one thread, `+PR -A`, photon preparation used about 0.15 CPU seconds
(0.113 shooting and 0.038 map preparation in a diagnostic split),
while tracing used a median 4.512 CPU seconds with photons and 0.709 without them. Raising the gather cap
from 100 to 400 raised trace CPU to 14.201 seconds, with the same 465,290 gather calls. The search and
per-photon work deserve attention before the shooting scheduler on a scene like this. These are
phase timings from the render statistics, not a function-level profile.

Surface shading now reuses a gatherer for each recursion level of a tracer and resets it at each hit.
Adaptive radius retries reuse their scratch arrays. The photon map records the stored photons' bounds
when its tree is built; a gather whose furthest possible radius cannot reach those bounds returns empty.
This is exact for a spherical or flattened gather, including a failed first radius followed by retries.
Maps without prepared bounds keep the existing search.

| Scene and gather cap | Base trace CPU | Gatherer reuse | Reuse and reach check |
|---|---:|---:|---:|
| `phot_met_glass`, cap 100, 960×480 | 4.512 s | 4.381 s | 4.387 s |
| `phot_met_glass`, cap 400, 960×480 | 14.201 s | 14.095 s | — |
| `glassthing`, 960×480 | 8.685 s | 8.443 s | 8.433 s |
| `tools/bench/photon-sparse-caustic.pov`, 1920×960 | — | 1.596 s | 1.212 s |

The two `phot_met_glass` rows are medians of three alternating renders per build; the other rows are
single runs. Reach checks help when a large receiver extends well past a small caustic patch and are
neutral in these stock scenes. Decoded pixels matched across builds on all listed scenes. Every
render used `+PR` without anti-aliasing.

## Where the time went

A cycle profile of 3.8 on the large scene put about 70% of tracing in the bounding hierarchy: `Check_And_Enqueue`
(box test and priority-queue insert, 36% of the whole run), `RemoveMin`, `Intersect_BBox_Tree`, and the same code
walking each mesh's own tree. Box tests ran at 4.9e9 for the band, against 2.9e9 for 3.7.

3.7 split on the axis where boxes' lower corners spread (a bug in `find_axis`); 3.8 fixed it to use extents
(`c52c176d`), which on this scene built a tree needing 70% more box tests.

## Changes

Each effect is against the build before it, on the band above.

| Change | Effect on the large scene |
|---|---|
| cpuid `"memory"` clobber (`2cb3ed7e`) | GCC dropped the cpuid stores from -O2: AMD CPUs got portable noise |
| AVX2/FMA3 noise on any CPU with it (`050ac7c7`) | 1–3% |
| sphere-sweep scratch buffers per thread (`b7b6ad32`) | under 0.5% |
| EdgeOfAssembly's queue fixes (`d706eb11`, `c62ff4bb`) | not measured separately |
| exact surface-area split over all three axes, not one axis chosen by extent | −20% trace, −44% box tests |
| each pass sorted once, splits kept by stable partition | parse 23% faster than 3.7, same trees |
| flattened trees: eight child boxes per block, tested together in single precision, walked with a stack | −39% trace |
| shadow rays stop at the first opaque hit | −12% trace |
| blocks filled by opening the largest child nodes; leaves tested as soon as nothing nearer waits | −7% trace |
| the scene tree walked in global depth order for nearest hits, as 3.8 did | −1% here, −0.3% on the standard benchmark |
| mesh pointer trees freed once flattened | −30% peak memory |
| boxes beyond the best hit so far never queued (the pointer-tree path) | small |
| PGO | 4–10% trace; separate PGO builds of the same code differ by up to 5% |
| leaves the flat walk has box-tested skip the object's own box pretest, and the walk cuts boxes a ray leaves before `MIN_ISECT_DEPTH`, as that pretest did | trace instructions: −4.5% on a 200 px close-up of a large scene, −3.8% `stackertransp.pov`, −1.7% `mesh-features.pov`, −1.4% `mesh-cylinder.pov`, −0.7% the standard benchmark; pixels unchanged |

The figures are trace-only (a full run minus a parse-only run), medians of three, against `performance` before the change. Instruction counts repeat to within 0.1%; cycles on this box do not (spread 2–10%), so they are given as a check and not as the result: −1.7% on `stackertransp.pov`, −1.6% on the close-up and −0.1% on the standard benchmark, all within spread, but +2.9% on `mesh-cylinder.pov` (spread 3%) and +4.4% on `mesh-features.pov` (one run), where instructions fell and time did not. The mesh scenes may gain nothing in time.

Without the `MIN_ISECT_DEPTH` cut (the walk keeping its `EPSILON` cut) the instruction change was −2.0% on `stackertransp.pov`, −1.1% on `mesh-cylinder.pov`, −0.5% on the standard benchmark, −4.4% on the close-up and +1.2% on `mesh-features.pov`: the pretest had been rejecting boxes a ray leaving a surface had already exited, and the padded flat-walk boxes let them through to be tested. Without the cut the standard benchmark first measured +1.8% in cycles; that did not reproduce in later runs (−0.4%).

Bounding boxes in double precision (`BBoxScalar` as `DBL`, as povr does) were measured on top of the pretest change before the cut was restored, `+WT1`, three runs each: trace cycles −1.8% on `stackertransp.pov`, within spread on the close-up, +0.2% on the standard benchmark and +1.9% on `mesh-cylinder.pov`, with the mesh bench's parse peak up from 161 to 220 MB and the close-up's parse 4% slower; it also changed 361 of 9,216 pixels of the standard benchmark, by up to 29 levels, identically in all three runs. Not taken.

## Media lit by area lights

Every media sample on a camera ray used to test each area light in full: the adaptive grid, 9 to 81 shadow rays per
light, and every one of those rays that left the media container integrated the media again for its extinction. In a
100×60 window of the large scene with scattering haze (`method 3`, `samples 88`, `aa_level 4`) and two 9×9 adaptive
area suns, that came to 7,000 shadow rays and 263,000 media samples per camera ray: tracing the window cost 630 times
as much as without the haze. A cycle profile put 40% of it in media along those shadow rays and 25% in their traversal.

Now each media sample tests four points of each area light. The points of a ray follow the R2 low-discrepancy
sequence, shifted at random per ray, so a ray's samples cover the whole light between them. They are distributed as
the full grid weights its points, edge rows and columns at half weight; sampling the light uniformly instead drew a
bright line where camera rays run along shadow edges.

A shadow ray through media needs only its transmittance, yet it went through the full sampling path, per-sample
lighting set-up and `method 3` recursion included, at 42% of the window's remaining trace. It now evaluates only the
extinction, at exactly the points and weights the old path used (89 a ray in the haze), carries the shared end of each
interval into the next, and stops once the transmittance is below 1/1024. `method 2` and `method 3` shadows come out
bit for bit as before with `jitter 0` (with jitter their points are now stratified, not jittered); `method 1` shadows
use its sample count stratified instead of at random, so they lose their speckle. A rule that refined from a coarse
grid until the transmittance settled took 15 points a ray and was twice as fast, but stepped over any feature between
its first grid points: `tools/bench/media-puff.pov`'s puff at `Height` 2.2 cast no shadow at all, while at 3.0 it
lands on a grid point. Resolution below the media's own is not safe, so none is skipped. The early stop assumes
extinction is never negative, which a density `color_map` with negative entries breaks. Surface lighting is unchanged,
bit for bit.

Shadow density batches preserve the point path's arithmetic. Supported density patterns also provide
conservative extinction ranges, so a stretch may use the range midpoint when its optical-depth error
fits a shared 1/1024 budget. Bounds are used only when a whole-segment upper bound, inflated for rounding,
proves at least one colour channel cannot reach the opacity cutoff. Other rays retain the point path's
per-point cutoff: moving it to a stretch boundary changes bright-light shadows substantially.
`tools/bench/media-opacity.pov` exercises this case; `Method`, `Intervals`, `Samples`, `Absorption` and
`StartDensity` select threshold and sampling cases. The budget follows the shadow ray through separate
media containers; area-light samples have independent budgets and reserve their worst remainder for the
shared atmospheric tail. `tools/bench/media-segments.pov` tests accumulated errors across containers. The
rounding allowance is reserved from the same budget; unusually large sample counts may use the exact path.

Simple function densities also provide ranges: finite constants, coordinates, addition, subtraction,
multiplication, constant division, absolute value and min/max clamps. The VM compiles a bounded-size
range plan once, preserving its instruction order and rounded constants; supported named scalar
functions are included in their callers' plans. Range queries allocate nothing and do not alter point
evaluation. Arithmetic bounds round outward, while min/max retain exact saturation at zero and one.
The pattern's wrap above one, its wave and its colour map still apply. Unsupported calls, noise,
general conditionals, variable division, nonfinite ranges and oversized plans use the point path.
`tools/bench/media-functions.pov` covers direct and named functions, transforms, clamps and fallback.
Run `python3 tools/bench/check-media-opacity.py <reference-binary> <candidate-binary> <output-directory>`
to compare cutoff cases exactly and stacked-media cases within 64/65535 per displayed colour channel.
Run `python3 tools/bench/check-media-functions.py <reference-binary> <candidate-binary> <output-directory>`
to check function-density shadows across sampling methods and interval counts with the same tolerance.
`tools/bench/check-function-ranges.sh <configured-build-directory> <output-directory>` checks range
containment against scalar VM execution using that build's compiler flags and libraries.

| Render | Before | After | |
|---|---|---|---|
| the window above, trace | 244.6 CPU-s, 1.66 G media samples, 44.1 M shadow rays | 36.9 CPU-s, 363 M, 9.4 M | 6.6× |
| the whole large scene at 232×133, same media | 7886 Gcycles, trace 2237 CPU-s | 1706 Gcycles, trace 457 CPU-s | 4.9× trace |
| the standard benchmark, 384×384 | 1116.4 Gcycles | 681.8 Gcycles | −39% |
| `tools/bench/media-shafts.pov`, 160×120, `+WT1` | 226.3 Gcycles | 25.8 Gcycles | 8.8× |

Area light points alone gave 50.1 CPU-s, 2167 Gcycles, 690.6 and 35.7 on these four. The window without haze traces in
0.39 CPU-s, so haze now costs 95 times as much, down from 630.

Gcycles include parsing, about 112 G for the large scene. Over its whole frame the new image differs from the old by
0.14 levels on average, 1.1% of pixels by more than 2; two `+WT4` runs of one build differ by 0.19 to 0.23. The
standard benchmark differs by 0.21 levels and keeps its mean brightness. Dropping the large scene's haze to
`samples 8`, `aa_level 2` as a cheaper setting differs from the full setting by 0.50 levels, up to 74, in the sunlit
haze.

`media-shafts.pov` is the hard case: dense haze entirely in the soft shadow of a slatted roof. At 320×240, `+WT2`,
40 samples, it took 252.6 CPU-s before and 28.7 after. Against a 400-sample `method 2` render its mean is 138.3; the
old code gives 139.3 and differs per pixel by 7.5 levels once the mean is taken out, the new 138.1 and 8.1. One point
per sample gave 12.3, three 8.7, six 8.0 at 1.5 times the cost of four. In the dense soft shadow at upper right the new image is
about 2.7 levels (1%) darker than the old, with about 10% more rms grain; neither is visible.

`method 3` chooses where to subdivide from the sample values, so a noisy visibility estimate makes its weights
correlate with the values: a systematic bias, not noise. With one point per sample it brightened mostly lit haze and
darkened mostly shadowed haze, by up to 12% in a one-level model; four points bring it within the old code's own error
above. Choosing the subdivision from the unshadowed light instead removes the correlation, but then shadow edges are
never refined and this scene came out 4 levels brighter than both references.

`method 1` with `samples 10, 100`, which adds samples until their variance settles, averaged 10.6 samples per
interval against 10.1 before, at 1.3 Gcycles against 7.3 for a 64×48 render, with the same error.

## Prepared media fast mode

`method 4 resolution 0.1` inside a `media` block selects a prepared world-space density field for one static medium. The number is the maximum cell width in scene units. Omitted `resolution` selects an automatic width. Effective language versions before 4.0 retain their classic default; version 4.0 defaults new media to method 4 with automatic resolution. Explicit methods 1, 2 and 3 retain their existing meanings in every language version. A bounded object with one medium prepares RGB density as the mean of eight subcell samples per cell. Camera rays march at steps no longer than a third of the cell width, integrating emission and scattering in order; cells whose sampled variation could change optical depth by more than 1/1024 use the source density at the camera point. Shadow rays to parallel lights with `media_attenuation on` use a cumulative optical-depth grid made from the same field. Other shadow directions march through that field at the cell width.

`method 3` explicitly selects classic adaptive sampling, including under version 4.0. The automatic mode uses an internal unset width until container bounds are known, then chooses the larger of the longest box side divided by 128 and the cube root of box volume divided by 500000. This is a memory-limited starting resolution, not a bound on image error. `resolution` accepts a finite positive value only with method 4; it can appear before or after `method 4`, and is an error with a classic method.

While preparation succeeds, `resolution` is the quality dial for camera and shadow integration: a smaller positive width keeps more detail and takes more preparation, memory and steps; a larger width is cheaper and coarser. An explicit positive value overrides the auto width. The classic sampling controls (`intervals`, `samples`, `jitter`, `aa_level`, `aa_threshold`, `confidence`, `variance` and `ratio`) are parsed but ignored on the prepared path. If preparation falls back or the ray is a photon ray, classic method 3 evaluation uses those controls without changing the stored method. Explicit methods 1, 2 and 3 select classic behavior throughout.

The first stage uses a fixed dense grid with a cap of one million density cells and one million cells per optical grid. Up to four optical grids are kept per medium. The field covers the container box; the container geometry clips ray segments and optical-depth construction, so field interpolation does not dilute density at a boundary. An unbounded or degenerate container, an unsupported pigment, non-finite or negative density, negative extinction, or a grid over the cap uses the classic path. Multiple simultaneous media and rays that need over 4096 steps also use classic evaluation. Portals, point lights and area lights do not use a light-aligned optical grid. Grid construction is lazy, shared among render threads and included in the first trace; subsequent rays reuse it. This is a finite-resolution approximation: subcell sampling can miss thin features and softens shadow edges. Reducing the cell width approaches the underlying continuous field where it is well behaved, but need not reproduce classic media's finite quadrature exactly.

`tools/bench/media-self-shadow.pov` selects the mode with `Declare=Fast=1` and its cell width with `Declare=CellSize=0.1`. `tools/bench/media-fast-combinations.pov` exercises several containers, overlapping media, turbulent and function densities, emission, absorption, scattering, two parallel lights and an optional area light (`Declare=Area=1`). Compare paired 16-bit PPM files with `tools/bench/compare-media-fast.py`; use `+PR -A` at the same size and sampling settings. `distribution/scenes/interior/media/media5.pov` is a stock no-opt-in regression scene.

## Subsurface light on open surfaces

This section is a correction, not a speed-up, and it changes images. Subsurface scattering finds its diffuse sample
points by casting rays from a point just under the surface and divides their sum by the number of rays cast, so a ray
that meets nothing is a sample of nothing. Dividing by the rays that hit something instead is right for a closed
object, where every ray hits, but gives an open surface such as a plane twice its subsurface light, since half the rays
from under it leave.

`tools/bench/sslt-open.pov`, 160×120, `+WT1`, mean levels (red, green, blue):

| Case | Dividing by hits | Dividing by rays cast |
|---|---|---|
| 0, the top of a closed slab | 218.5, 184.1, 137.1 | the same, bit for bit |
| 1, a plane of the same material | 255.0, 239.4, 177.7 (clipped) | 218.5, 184.1, 137.1 |
| 2, a sphere clipped open over a floor | 121.8, 118.6, 114.0 | 120.7, 117.6, 113.2 |

Closed objects render as before. The cost is unchanged.

`scenes/subsurface/subsurface.pov` was tuned with doubled light on its open marble floor. The floor's `diffuse`, which
sets its subsurface albedo, is 1.6 on the dark tiles and 1.2 on the light ones, which brings the dark
tiles and the far floor within 2 to 3 levels of the doubled render. The light tiles stay about 23 levels darker (204 to
181 in red): the doubling made them reflect 1.33 times the light falling on them, and a subsurface finish reflects at
most all of it, which `diffuse 1.2` reaches.

## Subsurface light on objects with no inside

Subsurface scattering needs a volume under the surface. On an object with no inside it is wrong, not just slow: the
sampled method's rays from under the surface find nothing to cross, and a finish with subsurface drops its ordinary
diffuse light, including `diffuse FRONT, BACK` light through the back of a sheet. The parser now removes subsurface from
the finishes of such objects and warns once per object:

    Subsurface scattering needs a solid object; this mesh has no inside_vector, so subsurface is ignored.
    Subsurface scattering needs a solid object; this object's triangle parts have no inside, so subsurface is ignored on them.

Objects with no inside are meshes without `inside_vector`, triangles, smooth triangles, polygons, bicubic patches and
parametric surfaces, whether alone or as pieces of a CSG object. The check runs once the object is complete, so a texture
set on a parent union or on a copy of a declared object counts, as do a mesh2's per-triangle textures and interior
textures. The stripped texture is a copy made once per object, so a texture shared with solid objects keeps its subsurface
there. Method 1 renders solid objects bit for bit as before.

`tools/bench/sslt-sheet.pov`, 480×360, a leaf as a mesh2, the same leaf as a union of triangles and a sphere sharing
their texture (`diffuse 0.6, 0.3` with subsurface), lit from behind: before, the leaves showed their triangles through
grain, at 127.6, 160.7, 97.7 on average; now they take ordinary diffuse light, 106.5, 148.0, 82.0, smooth, with 0.3 of
the light behind them showing through. The sphere's pixels are unchanged. The render takes 0.48 CPU-seconds, against 1.39.

## Subsurface light transport

A subsurface shading point (`samples D, S` in `global_settings`) takes D diffuse sample points on the surface and S
single-scattering points along the refracted eye ray. For each point and each light it first works out the light the
point would pass on if nothing shadowed it, from the light's colour and direction and the diffusion profile; that needs
no rays. Shadow rays then go where that light is: D rays for each light that reaches any point (S for single
scattering), half shared evenly between those lights and half by their share of the unshadowed light. Within a light,
each ray picks a point with probability proportional to its unshadowed light, and the result is weighted by the inverse
of that probability, so the shadowing is unbiased. Lights with negative colour count by magnitude. A ray to an area
light tests one point of the light, taken from a low-discrepancy (R2) sequence so that successive rays cover it evenly,
instead of the light's full adaptive grid of 9 to 81 rays.

Single scattering takes one point per sample for all three colour channels, drawn from the average of the channels'
distance distributions; each channel is weighted by its own density over that average, which keeps every channel
unbiased with a third of the rays. The diffusion profile's per-channel constants are computed once per shading point.

The rays that find diffuse sample points, and where light enters the object for single scattering, test only the
subsurface object (its outermost CSG, if any), not the whole scene. A diffuse ray that meets another object first
passes through it to the subsurface object.

Results with both parts, `+WT4`, user-space cycles:

| Render | Before | After | |
|---|---|---|---|
| `tools/bench/sslt-lamps.pov`, 160×120 | 18.0 Gcycles, 15.4 M shadow rays | 3.0 Gcycles, 1.8 M | 6.0× |
| the same at 320×240 | 72.4 Gcycles, 61.5 M shadow rays | 11.7 Gcycles, 7.1 M | 6.2× |
| `scenes/subsurface/subsurface.pov`, 160×120 | 24.8 Gcycles, 7.2 M shadow rays | 12.3 Gcycles, 3.8 M | 2.0× |
| the same at 320×240 | 102.1 Gcycles, 28.7 M shadow rays | 48.2 Gcycles, 15.3 M | 2.1× |

`sslt-lamps.pov` has two soft area suns behind slats, a spotlight and two fading lamps over wax and marble.
`subsurface.pov` has one point light and 400 diffuse samples, so most of its remaining cost is the sample rays
themselves; both columns use its rebalanced floor. Without subsurface scattering the scenes trace in 2.1 and 0.6
Gcycles at 320×240, so the subsurface part went from 70.3 to 9.6 Gcycles (7.3×) and from 101.5 to 47.6 (2.1×).
Testing only the object's own surface accounts for 1.1× and 1.4× of that.

Noise, the rms difference between two renders that differ only in their random samples (`+BS7` against the default
block size) divided by √2 to give one render's noise, goes from 1.15 to 1.23 levels on `sslt-lamps.pov` and from 1.17
to 1.20 on `subsurface.pov`. Against renders of the previous code at 16 and 4 times the samples, the rms error goes from
1.21–1.23 to 1.28 and from 1.26–1.27 to 1.31, with the same mean within 0.01 levels.

Tried and not kept, measured on `sslt-lamps.pov` unless noted:

- One area-light point per sample without drawing by unshadowed light: about a quarter more error against the reference.
- One pool of shadow rays for all lights, drawn by unshadowed light: 1.77 rms against the reference, against 1.20; a sun
  hidden behind the slats takes rays that the lamps reaching the surface need.
- Testing only some lights for single scattering, chosen by their light at the scattering point: two of five cost 9%
  less at 1% more noise, one of five 12% less at 7% more.
- Diffuse points sampled from the diffusion profile itself, projected along the normal and two tangents: unbiased, but
  2–3 times as noisy at the same cost on both scenes, and 1.5 times on a flat slab, where rays from under the surface
  already follow the profile's core.
- Aiming half the diffuse sample rays at the exit point in a cosine-power lobe: more noise here and on `subsurface.pov`.
- Half a shadow ray per sample: 6% more noise for 19% less cost. Two: 2% less noise for 19% more cost.
- Three single-scattering points per sample: 2% less noise for 58% more cost.
- Skipping shadow rays for negligible unshadowed light: subsumed by drawing in proportion to it.
- Evaluating the diffusion profile with SIMD: it is 2.5% of `subsurface.pov`'s cycles.

## Subsurface point cloud

`subsurface { method 2 }` in `global_settings`, or in a finish's `subsurface` block, finds the diffuse subsurface
light from a cloud of points shared by all shading points instead of rays under each one. The default stays method 1,
the sampled method above. Method 2 is smooth where method 1 is grainy and not bit-exact. At the quality of method 1 with
many samples it is 1.2 to 3.8 times as fast on most bench scenes, about as fast on a slab and a plane, slower on a
small mesh where it needs spacing 0.25, and short of that quality at every setting tried on one wide view (tables
below).

**The cloud.** Space is cut into cubes whose side is a power of four at least the diffusion's reach (8 diffusion
lengths of the channel that diffuses farthest), so a texture that varies the diffusion uses few sizes. A cube's points
are built the first time a shading point needs it: lines along the three axes through a jittered grid cross the
object, and each crossing is a point standing for h²/|n|₁ of surface (h the grid step, n its normal), which gives one
point per grid square on a patch facing an axis and no clumps. All crossings along a line are collected by testing it
again past the farthest one found, since one test of a blob returns only the nearest interval. A point's normal is
turned to point out of the object, by testing which side of it is inside: primitives report normals with no regard to
a `difference` inverting them, and a mesh's follow its winding. Crossings whose sides cannot be told apart (parts
thinner than about a twentieth of the spacing, a union's inner surfaces) are dropped, and a cube that
drops more than one in sixteen is left to method 1. Points are lit once, four points of each area light apiece, and keep each light's shadow. The spacing is a
128th of the cube's side, a sixteenth to a quarter of the diffusion length, or a pixel of the view's camera where the
cube comes nearest it if that is coarser, and never under a 1024th of the side; a pixel spans a size plus an angle
times the distance, for perspective, orthographic, spherical, fisheye, ultra-wide, omnimax and panoramic cameras
(for spherical and panoramic ones, a pixel at the equator, coarser than one toward the poles); cylinder, mesh and
user-defined cameras get no pixel size, so their cubes take the finest spacing, and a plane under one can spend the
point budget before its shading points fall back to method 1.
Lines along an axis the camera looks across are spaced up to twice as widely, by one over the square root of the
cosine between the axis and the direction to the camera, since surfaces facing that axis are seen obliquely; a point
then stands for 1/Σ(|nₐ|/hₐ²) of surface. The camera is set once for the render, and a cube is lit from a fresh trace
level and at full quality, so it comes out the same whichever thread builds it, from a camera ray, a reflection, the
radiosity pretrace or a radiosity sample (whose trace leaves out area lights). Threads that need a cube while it is
built take jobs from its building, so it does not hold them up; an exception in any job leaves the cube unusable
instead of holding up the threads waiting on it. A cube of more than 262144 points is left to method 1, as is an
unbounded object such as a plane where its cubes would be coarse (below). A budget of about a million
points (about 120 bytes each, and 12 per light) covers all objects; once it is spent, cubes built later go to method 1,
and which cubes those are depends on the order threads reach them, so it can vary between renders.

**The sum.** Around the exit point, a core half a spacing across is lit as the exit point is, from its own light and
its own shadow test where the nearby points disagree on a light's shadow (so shadow edges stay sharp); out to 1.5
spacings the points' light is averaged by their share of the diffusion profile and multiplied by the profile's
integral over the disc, in closed form; out to 4 spacings the same, times how much nearer the ring's points are than
they would be with the surface unfolded flat (taking the planes through the exit point and each point to meet at a
crease), which is 1 on a flat surface and more across an edge, unless the ring holds more than 1.33 times a flat
ring's area or faces away from the exit point (a thin part's far side), where its points are summed as they are;
beyond, points are summed in groups whose area is small against their distance (`error_bound`, 0.1) and whose normals
agree. A point counts only if the sampled method's base point, under the exit point, sees it from the inside, which
keeps light from crossing gaps between parts of an object. Where the disc bends by more
than 60° (edges, tight curves) or a point within 2.5 spacings is seen from behind (creases), the shading point uses
method 1. The disc and ring are sized by the coarsest cube they reach, so where cubes of different spacing meet each
still holds enough points. Diffusion shorter than a pixel is taken as lit like the exit point.

**Edges.** Where the surface runs out (an open border), the closed-form core, disc and ring integrals
would count surface that is not there. A window (1 − r²/R²)² over the ring measures how much of a plane the points
cover and where the covered part's centroid lies, taking each point at the centre of the grid square its line
crosses, so the lines' jitter does not show (its share varies by 0.4% on a flat surface, against 5% at the points
themselves). A straight edge at distance t·R leaves a share F(t) = ½ + (16/5π)(t(8t⁴ − 26t² + 33)√(1 − t²)/48 +
(5/16) asin t) with its centroid (16/105)(1 − t²)^3.5 / (F(t)·π/3) from the centre; the centroid gives t for an open
border, the share alone two edges at ±t for a strip, and the two are blended by how far off centre the centroid is
against what a single edge leaving that share would give. Each of core, disc and ring then keeps the share of its Rd
integral inside the edges, summed along the far side of the chord. Shares above 0.95 count as whole and the correction
reaches full strength at 0.85, so curved and bumpy surfaces are left alone. Single scattering near an edge is sampled.

**Coarse clouds.** A material whose diffusion is long against its depth z_r (marble seen from a distance) gets cubes
whose points lie far apart. Where they are more than four times the larger of a pixel and z_r apart, the exit point
tests its own shadows, and a shading point whose shadow differs from its neighbours' by over a quarter, or near which
they disagree, uses method 1; an unbounded object gets no such cubes at all.

**Radiosity.** With subsurface radiosity on, cubes built in the final trace (kept apart from those without it) add
the radiosity cache's light to each point, entering along the normal as the sampled method takes it, and the core adds
the exit point's own. Points read only the pretrace's and loaded samples, as a final-trace query from a tile no tile
has, so no final-trace sample counts and a cube does not depend on the tile that builds it; where none is near, the
bound is widened up to fourfold, then deeper bounces are tried, and a point still without one takes its cube's mean.
The pretrace uses method 1, since the cache is incomplete while it runs. With `radiosity { subsurface on }`, gathers
at the deepest bounce use cubes without radiosity, so the same parts of an object can be built twice, once of each
kind, from the one point budget.

**Single scattering.** A light in front of the surface whose shadow the disc agrees on needs no samples: on a flat
surface the path in from the light is a fixed multiple of the path out, which gives the sampled estimate's expectation
in closed form. Other lights (behind, or at shadow edges) are sampled as in method 1, with the shadow taken from
nearby points where they agree.

**Results at equal quality.** For each bench scene the reference is method 1 at the samples below, and the yardstick,
against which errors are measured, is method 1 at 8 times those samples. Method 2 is close to the reference when, against
the yardstick, its rms error over the pixels that subsurface light changes is no worse than the reference's, its worst
8×8-pixel block (mean luminance) is within a level of the reference's worst, and a sheet (yardstick, reference, method 2,
their differences ×4 and an enlarged crop of method 2's worst block) shows no artefact. Method 2 was rendered at 1/16,
1/8, 1/4, 1/2 and 1 times the reference's samples (diffuse and single scattering alike; the diffuse count applies only
where it hands a shading point to method 1) at the default spacing, then at finer spacings where that missed; the
table gives the least that is close. 320×240 unless noted, `+WT4`, user-space cycles (`tools/bench/pcount.c`) and
CPU-seconds of one run each, errors in levels of 255:

| Scene | Reference samples | Method 2 | Gcycles | CPU-s | | rms | Worst block |
|---|---|---|---|---|---|---|---|
| `scenes/subsurface/subsurface.pov` | 400, 40 | 400, 40, spacing 0.7 | 46.3 → 17.0 | 13.9 → 5.2 | 2.7× | 1.15 → 1.04 | 0.8 → 1.5 |
| `tools/bench/sslt-lamps.pov` | 74, 21 | 19, 5, spacing 0.5 | 11.4 → 7.0 | 3.3 → 2.0 | 1.6× | 2.77 → 2.23 | 0.8 → 1.5 |
| the same, radiosity and subsurface radiosity on | 74, 21 | 74, 21 | 86.0 → 22.5 | 24.8 → 6.4 | 3.8× | 3.43 → 2.12 | 1.6 → 2.5 |
| `tools/bench/sslt-open.pov` Case 0, a slab's top | 200, 12 | 13, 1 | 12.8 → 12.3 | 3.6 → 3.5 | 1.04× | 2.72 → 1.22 | 0.8 → 1.2 |
| Case 1, a plane | 200, 12 | 200, 12 | 8.5 → 8.9 | 2.4 → 2.5 | 0.95× | 2.70 → 2.09 | 0.8 → 0.9 |
| Case 2, a clipped shell | 200, 12 | 200, 12, spacing 0.35 | 2.37 → 1.94 | 0.64 → 0.55 | 1.2× | 2.08 → 1.80 | 0.8 → 1.2 |
| Case 6, a thin rod and wall | 200, 12 | 200, 12, spacing 0.5 | 2.54 → 2.39 | 0.83 → 0.65 | 1.06× | 1.88 → 1.60 | 1.1 → 1.6 |
| `tools/bench/sslt-wide.pov`, 1600×800 | 74, 12 | not close; 74, 12 | 106.1 → 83.7 | 30.2 → 24.1 | | 4.78 → 5.74 | 1.7 → 8.6 |
| the same, 4000×2000, a 200×200 window on a trunk | 74, 12 | 37, 6 | 52.4 → 16.7 | 14.5 → 4.3 | 3.1× | 5.51 → 4.37 | 3.1 → 3.7 |
| the same, a 200×200 window on the small mesh | 74, 12 | 74, 12, spacing 0.25 | 43.4 → 47.9 | 11.9 → 13.2 | 0.91× | 2.81 → 1.47 | 1.7 → 1.2 |

The yardsticks take 3200, 320 on `subsurface.pov`, 592, 168 on `sslt-lamps.pov`, 1600, 96 on `sslt-open.pov` and 592,
96 on `sslt-wide.pov`. Method 2's mean is within 0.45 levels of the yardstick's, except on the slab's top (−0.73; the
reference: −0.13). Where the default spacing is not enough:

- The clipped shell's rim: a band just below it has blocks 3.5 levels off at spacing 1, 2.1 at 0.5 and 1.2 at 0.35.
  Spacing 0.35 costs method 2 60% more CPU time than spacing 1, and still less than the reference (1.94 Gcycles
  against 2.37).
- The small mesh's underside: blocks 6.7 levels off at spacing 1 and 5.3 at 0.5; at 0.25, close to the reference but
  10% slower than it, and near the point budget (two runs differed in one pixel).
- `sslt-wide.pov`'s whole frame at 1600×800: a trunk's ridged bark comes out up to 8.6 levels light in 8-pixel blocks
  at spacings 1, 0.5 and 0.25 alike, so not for want of points; at 4000×2000 the same trunk is within 3.7 levels. Method
  2 is not close there at any setting tried.
- `subsurface.pov` and `sslt-lamps.pov` are close in rms at the default spacing, but blocks on the candle and the wax
  sphere stay 2.3 to 2.4 levels off at any sample count; spacings 0.7 and 0.5 bring them within 1.5.

On the slab and the plane method 2 costs what method 1 does at equal quality; on the thin rod and wall, whose parts
method 2 largely hands to method 1, it is 6% cheaper.

In about the reference's time instead, method 2 with more samples, CPU-seconds from single runs (the reference's in
brackets):

| Scene | Method 2 | CPU-s | rms | Worst block |
|---|---|---|---|---|
| `subsurface.pov` | 1600, 160, spacing 0.7 | 12.3 (12.6) | 0.89 (1.15) | 1.5 (0.8) |
| `sslt-lamps.pov` | 148, 42, spacing 0.5 | 3.0 (3.1) | 1.57 (2.77) | 1.4 (0.8) |
| the same, with radiosity | 296, 84 | 13.6 (24.2) | 1.74 (3.43) | 2.6 (1.6) |
| `sslt-wide.pov`, trunk window | 296, 48 | 15.8 (13.8) | 3.07 (5.51) | 3.7 (3.1) |

Lower rms with more samples comes from the shading points handed to method 1 and from single scattering; the worst
blocks, which are method 2's own bias, do not move. `sslt-wide.pov` at 1600×800 peaks at 100 MB with method 2 and
83 MB with method 1.

Tried and not kept:

- Shadow-testing a cloud point's lights brightest first, leaving the faintest 5% untested with the tested lights'
  visibility (as radiosity gather rays do): it saved under 1% of shadow rays on `sslt-wide.pov`, and lights whose
  shadows differ (the slats over `sslt-lamps.pov`) came out up to 9 levels dark.
- Measuring the covered share with the window taken at the points themselves: their jitter varies it by 5% on a flat
  surface, which showed as blotches wherever the correction came into play.
- Handing no shading points over for bends and creases: a small concave dimple in `sslt-lamps.pov` came out 14 levels
  bright and block edges 6 levels dark.
- Points where random lines cross the object: their clumps showed as faint mottling on wax and, summed raw near the
  exit point, as blotches.
- Summing the ring raw wherever its normals turn by more than 45°: mottling on the candle's rounded rim.
- One cloud per object laid out by the first shading point: a floor seen from close to far gets one spacing, and the
  first shading point's material fixes it for a texture that varies the diffusion.

## Subsurface flesh and skin

A texture's subsurface layers are blended into the colour the viewer sees, by each layer's opacity and filter as the
diffuse term takes them, and scattered once with the topmost subsurface layer's finish. Each layer used to scatter on
its own at full strength, so a half-transparent layer looked opaque and a texture of n layers paid n evaluations.

`volume_sampling { depth D spread S }` reads the flesh and emission patterns in a slice below the surface: each light-entry
point looks up a depth drawn from a normal curve of mean `D` and standard deviation `S` (mm, through `mm_per_unit`), cut
off at the surface by drawing only from the part of the curve below it, so no lookup falls outside the object. Both
default to half the `translucency` (its grey mean). `translucency` is how far light spreads and `depth`/`spread` choose
which part of the pattern it picks up, so a small `spread` shows a thin slice of a 3D pattern instead of all of it. The
bare `volume_sampling` and `volume_sampling 1` forms still parse, with the defaults. When `D` exceeds the wall measured
at sampled crossings the render warns.

The point cloud tells a crossing's side of the surface by testing a step along its normal, at most a twentieth of the
point spacing; that step is cut to a quarter of the wall to the neighbouring crossings on the line, so walls much thinner
than the spacing keep their points. The point step is also capped so that at least 32 lines cross an object's longest
side: it was sized from the diffusion reach and the pixel alone, so a small object beside a long reach got a few points
and cells with none, and every shading point near them went to method 1. A point lying on a cell's face (an object
resting on y = 0) is matched to the nearest gathered cell instead of the empty one beside it. The render reports, for
method 2, how many shading points the cloud served and why the rest went to method 1.

A finish's `colour` or `pigment` gives the flesh inside a colour of its own; the texture's pigment is then a skin light
crosses on the way in and out, each crossing letting through the pigment to the power of half the relative
`thickness`. There is still one diffusion profile per shading point, built from a reference flesh colour: the
`colour`, or, with `volume_sampling`, the mean of four lookups spread over two depth-plus-spreads around the shading point, so
that nothing depends on the flesh under the exit point alone. What varies between entry points is a multiplier on the
light entering there: the looked-up flesh over the reference (divided once, at the end), times the skin's transmittance
there, plus the flesh's emission times its colour. The lookups are made at a depth drawn from a normal curve (see
`volume_sampling` above): method 1 at each diffuse sample point, from the shading point's keyed draws; method 2 once per
cloud point as it is lit, from an R3 lattice over the grid squares so that neighbouring points' depths spread evenly,
into a cube keyed by the texture layer as well as the object. Method 2's core, which is lit as the exit point is,
takes the multiplier and emission of the disc's points, weighted by the profile, instead of those under the exit
point; the local path takes the four spread lookups. Emission the same at every entry point is added in closed form
(the profile's integral over the plane). Without the new keywords none of this runs: `subsurface.pov`,
`sslt-lamps.pov`, `sslt-open.pov` and the skin scene's case 0 render bit-identical to before with both methods (method 2 on
`subsurface.pov` compared at `+WT1`; at `+WT4` it varies between runs of either build, as the point budget runs out in
thread order).

Cost, 320×240, `+WT4`, user-space Gcycles (`tools/bench/pcount.c`) of one run each, 60 diffuse samples on
`tools/bench/sslt-skin.pov`:

| Scene | Method 1 | Method 2 |
|---|---|---|
| `scenes/subsurface/subsurface.pov` | 44.3 → 44.8 | 15.7 → 15.9 |
| `tools/bench/sslt-lamps.pov` | 12.4 → 11.2 | 7.3 → 7.0 |
| skin, a dark pigment (case 0) | 3.1 → 3.1 | 3.3 → 3.2 |
| skin, a half-transparent layer over flesh (case 1) | 5.8 → 3.1 | 5.8 → 2.7 |
| skin, a flesh pigment and no skin (case 6) | 3.0 | 2.9 |
| skin, `colour` (case 2) | 3.1 | 2.8 |
| skin, `pigment` veins, `volume_sampling` (case 3) | 5.4 | 3.4 |
| the same, plain pigments for the veins | 3.4 | 2.8 |
| skin, veins with an `emission` pigment (case 4) | 6.6 | 3.7 |
| the same, plain pigments | 3.5 | 2.9 |
| skin, `thickness` as a `bozo` pattern (case 5) | 4.1 | 3.0 |

Instructions on the first two scenes are within 0.8% (104.5 → 104.8 and 40.4 → 40.7 G on `subsurface.pov`); the
cycle differences are run-to-run. The layered case halves, being scattered once. The skin itself costs nothing
(case 2 against case 6). With plain pigments the lookups add 10% to method 1 (up to four `Compute_Pigment` calls, a
logarithm and a power per diffuse sample) and nothing measurable to method 2, which makes them once per cloud point;
the rest of cases 3 to 5 is the patterns themselves (`marble` with turbulence, twice with emission). Here the brighter
flesh (case 6 against case 0) spreads light further without costing more.

Quality against method 1 at 1920 samples, rms error in levels of 255 (mean shift in brackets), with the method-1
cycles that reach method 2's error, interpolated between 30, 60, 120 and 240 samples:

| Case | Method 1, 60 | Method 2 | Method 1 at equal error |
|---|---|---|---|
| 2, `colour` | 1.07 (−0.01), 3.1 G | 0.71 (−0.02), 2.8 G | 5.0 G, 1.8× |
| 3, veins | 1.12 (−0.01), 5.4 G | 0.80 (+0.03), 3.4 G | 8.5 G, 2.5× |
| 4, glowing veins | 2.15 (−0.05), 6.6 G | 3.16 (−0.18), 3.7 G | worse than method 1 at 30 |
| the same, `spacing 0.5` | | 2.13 (−0.10), 4.9 G | 6.7 G, 1.4× |
| the same, `spacing 0.25` | | 1.16 (−0.05), 8.7 G | 20 G, 2.3× |
| 5, `thickness` pattern | 1.10 (−0.01), 4.1 G | 0.76 (−0.02), 3.0 G | 5.9 G, 2.0× |
| 1, layers (performance: 11.2, +4.2) | 2.07 (−0.04), 3.1 G | 1.52 (−0.07), 2.7 G | |

Method 2 resolves what the lookups vary only down to its point spacing: the glowing veins, narrower than the default
spacing between cloud points here, come out as blotches until `spacing` is lowered, and at 0.25 match the reference. Spreading the points' depths over a lattice instead of drawing them independently lowered
case 4's error from 3.29 to 3.16 and case 3's from 0.82 to 0.80.

Subsurface light still does not cross between separate objects: a flesh object inside a skin object does not glow
through it.

## Mesh memory

Large meshes ran out of memory before render time mattered: a 10.4-million-triangle mesh took about 1.4 GB. Each
triangle stored 76 bytes (fourteen indices whether or not the mesh had UVs, smooth normals or per-triangle textures,
plus a precomputed plane and smoothing frame) and added its face normal to the normals array; the flattened tree cost
another 60 bytes; and while a mesh was built its pointer tree and flat tree coexisted.

Now the flat tree is built straight from the triangles' boxes by the same surface-area passes, with no pointer tree,
and a mesh groups its leaves eight at a time, where the scene keeps four, so blocks fill. A triangle is its three
vertex indices with its flags in their top bits, so a mesh holds at most 2^30 vertices. Normal, UV and texture indices
live in per-mesh columns that exist only when they vary and do not simply follow the vertex indices. The plane is
recomputed from the vertices in the ray test and the smoothing frame at the hit. Mesh blocks store their children's
boxes as 16-bit steps of a grid spanning the block, rounded outward, with a step of margin at each end of the grid and
on each box, since decoding cancels terms of the block's size: 156 bytes a block against 232. A vertex that is
infinite or not a number is a parse error, since it would make its block's grid infinite.

Measured on a 1.5-million-triangle `mesh2` with a normal per vertex (`tools/bench/mesh-cylinder.py 600 1251 mesh-
cylinder.inc`, then `mesh-cylinder.pov`), parsed alone; heap is `mallinfo` in use once the mesh is built, trace a
`+WT1` render at 960×720, `+A0.0 +R3`:

| Build | Heap per triangle | Peak RSS | Parse Gcycles | Trace Gcycles |
|---|---|---|---|---|
| before | 159 bytes | 468 MB | 14.4 | 45.1 |
| tree built from boxes | 151 | 294 | 12.7 | 45.0–45.8 |
| 12-byte triangles, columns | 79 | 190 | 11.7 | 45.1–46.1 |
| 16-bit boxes | 61 | 166 | 12.2 | 47.5 (+6.6%) |
| leaf groups of up to eight | 54 | 148 | 12.1 | 46.8 (+6.0%) |

The last two rows are means of nine and four runs, each interleaved with as many of the first row's build (44.5 and
44.2 there); instructions rise 6.1% and 2.8%, and groups of eight cut box tests by 10%. Of the 54 bytes, 12 are the
triangle, 6 its share of vertices, 6 of vertex normals and 30 the tree (0.19 blocks a triangle).

Building from boxes and quantising change no image. Recomputing the plane changes 47 of 691,200 single-thread pixels,
the large ones isolated shadow specks of the stored plane that are now lit like their neighbours; groups of eight
change 4 more, by up to 6 levels, where rays meet an edge two triangles share. `tools/bench/mesh-features.pov` covers
`mesh` and `mesh2` with per-triangle, interpolated and UV-mapped textures, smooth normals and inside tests, and
renders identically.

Decoding a quantised block costs a widening and a multiply-add per bound. The portable loop let GCC split it into
128-bit halves around the widening, 9% slower, so GCC gets vector types; the block's offset is its origin × inverse
direction minus the ray origin's, the latter formed once per ray.

Left, per triangle: 8-bit boxes would save about 8 bytes and octahedral 32-bit vertex normals 4. Placing children
contiguously saves under a byte, since most lanes are leaves, whose triangle ids stay explicit while mesh cameras
index triangles in file order. A `mesh {}` of smooth triangles keeps a 12-byte normal-index column, which hashing
normals by vertex would remove. Building peaks at about 100 bytes a triangle of heap, mostly the pass's boxes and sort
orders.

## Radiosity

The same 100×60 window of the large scene, single-threaded, traces in 0.32 CPU-s without radiosity, 12.1 with
`count 60, error_bound 0.6` (38×) and 2.6 with `count 30, error_bound 1.5` (8×). In a window the pretrace takes
two samples; the final pass gathers the rest, one sample per four pixels at `error_bound 0.6`, since the foliage's
normals keep samples from being reused across neighbouring leaves. Each gather ray that hit a surface then traced
a shadow ray to every light in the scene, most of them small lamps whose light fades within a few units: 24 shadow
rays per gather ray. On `tools/bench/radiosity-lawn.pov` (a sun and 24 fading lamps over a lawn of thin blades)
those shadow rays were 72% of the render.

Gather rays at first ranked the lights at their hit by unshadowed contribution and shadow-tested them brightest
first, until the untested ones would carry at most 5% of it; those were scaled by the share of the tested light that
got through. A sample averages 30 to 60 such rays, so the estimate's error stayed far below the sample's own noise.
"A sampled light for the faint rest" below replaces the 5% with a quarter and a drawn light. Reflections and
refractions spawned by a gather ray are radiosity rays too and are lit the same way; camera rays and everything they
spawn are lit exactly as before.

| Change | Effect, single-threaded |
|---|---|
| gather rays shadow-test lights brightest first | lawn 34.6 → 18.9 Gcycles, shadow rays 9.77 M → 4.03 M; the window 12.05 → 6.74 CPU-s at `count 60`, 2.58 → 1.32 at `count 30` |
| a light's cached occluder is left out of the scene walk once it has missed | every shadow ray of every render: lawn without radiosity 4.45 → 4.09 Gcycles, with it 18.9 → 17.0; images identical |
| each light's ray and unshadowed term computed once per gather-ray hit | lawn 33.6 → 32.5 G instructions; the stock Cornell box, a grid of equal lights none of which can be skipped, from 13% more instructions than before this branch to 3% |
| cache files keep nine significant digits, quality and brilliance, skip malformed or too-deep records and report what loaded | below |
| under `+HR` each tile restarts the gather directions at a point set by its serial number, and in the pretrace its pass (since replaced by keyed directions, see "Random draws") | the stock `patio-radio_37.pov`, rendered twice with `+HR` at 4 threads, was 1.4 levels apart and is now identical |

Together, on the lawn at 320×180, two runs of each back to back: 34.3 and 33.6 Gcycles before, 16.2 and 16.5 after
(67.4 → 32.5 G instructions). The window's image moves by 0.09 levels on average, 4 at most; the lawn's by 0.03, 2
at most. Two 4-thread runs of one build differ by 1.8 levels on average and up to 47 on the lawn, because which
samples exist depends on thread timing. With only the brightest-first change, where leaving 5% untested gave 18.9
Gcycles on the lawn, 2% gave 23.5 and 10% gave 16.4 with differences up to 4 levels; on the window 10% and 20% moved
pixels by up to 6 and 12 levels.

### At print density, and reusing a cache

The window's 38× and 8× above are low-resolution figures. Samples are spaced in the scene, not on the screen, so
the finer the pixels, the more of them share each sample. Two 200×200 close-ups of the large scene at 200 DPI and
its whole frame at 10 DPI, `count 30, error_bound 1.5`, 4 threads, trace CPU-s:

| | Close-up 1 | Close-up 2 | Whole frame, 10 DPI |
|---|---|---|---|
| without radiosity | 14.5 | 12.6 | 96.1 |
| radiosity, before this branch | | | 413.7 (4.3×) |
| radiosity computed in the render | 32.8 (2.3×), 3,363 samples | 19.2 (1.5×), 927 | 282.9 (2.9×), 35,025 |
| radiosity loaded from the 10 DPI frame (`+RFI`) | 17.6 (1.2×), 777 | 14.9 (1.2×), 264 | |

The 10 DPI frame that saved the cache (`+RFO`) is the 282.9 CPU-s above. The close-ups take three quarters of the
samples they need from it; the rest lie on detail too small to be hit at 10 DPI. They differ from close-ups
computed in place by 1.8 and 1.9 levels on average, about as much as two 4-thread renders of the lawn differ.

Cache files were not dependable before this branch: illuminance was written with four decimals, so dark samples lost
precision and anything under 0.00005 turned black; quality and brilliance were not saved, so low-quality samples came
back at full weight; a file whose samples went deeper than the loading scene's `recursion_limit` read past the end of
a per-depth settings array; a malformed record put uninitialised or non-finite values in the octree; and a missing
file loaded silently. Samples are kept in scene space and carry no tile or thread, so a loaded cache serves any
window, resolution or thread count. With `recursion_limit 1` they also hold illuminance before `brightness`, which
can change between saving and loading.

### Settings

On the lawn at 320×180, 4 threads, against `count 800, error_bound 0.15`: `error_bound`, not `count`, sets the
blotches and the bias. At about 15 Gcycles, `count 30, error_bound 0.3` differs from the reference by 0.77 levels
after a 7-pixel blur and is 0.4 brighter; `count 120, error_bound 1.5` differs by 1.90 and is 1.6 brighter. Larger
bounds brighten the grass most, 3 levels at 1.5, as samples near the tips are reused down the blades.
`low_error_factor`, `nearest_count` and `minimum_reuse` moved nothing measurably. `pretrace_end 0.005` halved the
error for twice the cycles, no better than a smaller `error_bound`. Against a three-bounce reference, a second
bounce at `error_bound 0.6` cost twice as much and came closest (1.12 against 1.81 for one); at 1.5 it bought
nothing.

### Gather rays' reflections and refractions

A gather ray is one of 30 to 60 averaged into its sample, but what it spawned at a reflective or translucent hit was
traced to the camera's adaptive depth, as if it alone filled the pixel. Each ray below a gather ray now carries its
share of the sample, and a reflected or refracted ray whose share falls short of `adc_bailout` survives with
probability in proportion, scaled up to keep the mean. The draw hashes the hit point and ray direction, so it does
not depend on thread order.

| Render | Before | After |
|---|---|---|
| close-up 1 at 200 DPI, `count 30, error_bound 1.5`, single-threaded | 30.0 CPU-s; 97 K reflected, 104 K transmitted rays | 24.8 CPU-s; 55 K, 54 K |
| lawn with reflective canopies and translucent blades (`Declare=Shiny=1`), 4 threads | 58.4 and 58.2 Gcycles | 43.9 and 44.3 |

The close-up moves by 0.08 levels on average and 1 at most. Against a converged render the shiny lawn's error goes
from 1.58 to 1.64 levels, with the same mean brightness.

### A sampled light for the faint rest

A gather-ray hit lit mostly by lamps still tested most of them to leave no more than 5% of its light untested. It
now tests them brightest first only until the untested ones carry at most a quarter of its unshadowed light. Unless
those carry 5% or less, one of them, drawn in proportion to its light, then stands for them all, scaled by their total
over its own. The estimate is unbiased, and the draw hashes the hit point and ray direction, so it does not depend on
thread order.

| Render, one thread unless noted | Before | After |
|---|---|---|
| large scene, 200 DPI close-up, radiosity overhead, two runs | 57.5 and 58.0 Gcycles, 78.2 G instructions | 41.2 and 42.2 Gcycles, 61.9 G |
| large scene, whole frame at 10 DPI, 4 threads, radiosity overhead, two runs | 536 and 554 Gcycles, 849 G instructions | 461 and 415 Gcycles, 640 G |
| lawn, 320×180 | 17.0 Gcycles, 3.95 M shadow rays | 13.2 Gcycles, 2.46 M |
| a room lit by four lamps at `count 30` | 506 K shadow rays | 487 K |

Against converged renders the lawn's error stays at 1.99 levels and the room's at 0.59; the close-up's goes from
1.82 to 1.87, and that of a second 200 DPI window, of smooth ground, from 0.97 to 0.93.

On stock scenes at 320×240, single-threaded: the Cornell box's nine nearly equal area lights leave the untested
ones near the quarter at most hits, so a hit tests seven and draws one of the last two where it tested all nine. Its
shadow rays go from 7.39 M to 6.69 M and its cycles from 15.2 G to 14.9 G; the image moves by 0.01 levels on
average, and no step between tiles exceeds 0.03 levels, 0.23 at `count 15`; its error against a converged render
stays at 0.82 levels, 1.58 at `count 15`. `patio-radio_37.pov` has one light, and `biscuit.pov` with radiosity added
has one light that casts shadows; both render bit-identical.

Drawing a light as soon as the brightest was tested cut the close-up's radiosity overhead by 42%, but the image
showed the render's tiles. A sample's direct light at its gather-ray hits, against testing every light, then varied
by 26% (standard deviation over the close-up's 3,328 samples), against 4.4% when testing to 5%, which reads 2%
dark. The final pass shades a pixel from whatever cached samples reach it and takes a new one only where none does,
tile after 32-pixel tile, so pixels either side of a tile edge are shaded from different samples: three tiles of
the close-up came out 2.4 levels darker, with sharp edges. It was variance, not a correlated draw. Neighbouring
samples' errors were uncorrelated (0.01), their mean was 0.1%, and another salt moved the steps to other tiles.
Unbiased rules tried on the close-up, with the draw hashed per ray or spread evenly across a sample's rays under one
shift per sample:

| Rule | Spread | Radiosity shadow rays |
|---|---|---|
| test to 5% (before) | 4.4%, 2% dark | 1.59 M |
| the brightest, then one hashed | 26% | 0.70 M |
| the brightest, then one spread | 21% | 0.70 M |
| the brightest, then two spread | 14% | 0.93 M |
| to 40%, then one spread | 13% | 0.94 M |
| to 25%, then one hashed | 9% | 1.14 M |
| to 15%, then one hashed | 6% | 1.33 M |

At 25% the largest step between tiles, against the image before, is 0.55 levels on the close-up and 0.14 on the
second window; the shadow-cache changes already in this branch's base moved the close-up's tiles by up to 1.4. Two
spread lights took 32% of the overhead's instructions off against 21%, but left the close-up's error at 1.95 levels,
as high as one.

### Tried and dropped

Two caches of direct light at gather-ray hits. Irradiance from classic lights, kept per object, cell and normal, with
cells sized from the gather ray's reach, was reused by 6–12% of hits on the lawn and saved nothing. Per-light
visibility, kept per object and cell with cells sized from the distance to the camera and shared between threads, was
reused by 38–55% of shadow tests and cut the lawn's shadow rays by up to 25%, but its lookups and locks cost as much
as the rays they saved. With few lights tested per hit, a shadow ray is too cheap to cache.

A hierarchy over the lights for ranking them: with one pass to find the brightest, ranking took under 2% of the
close-up's render threads, and a hit here has at most 13 lights to sort. Skipping the pretrace when a loaded cache
covers the frame: the pretrace takes under a second of the close-up.

Variance-driven gather counts were not attempted. On the lawn `count` moved the error less than `error_bound` at the
same cost (see Settings), and the tile steps above came from per-sample variance, which fewer rays on quiet samples
would raise.

`shadow_threshold`, a global setting that let camera, reflected and refracted rays test their lights brightest first
and stop once the untested ones could add at most a given share of the light found visible. Single-threaded trace
CPU-s at 0 and 0.05: the lawn without radiosity 0.59 and 0.44, the stock `drums.pov` 0.80 and 0.61, the large scene's
close-up 20.4 and 19.6. Dropped (owner): the gain was marginal on some scenes (4% on the large scene) and its visual
cost was unpredictable without comparison renders; even 0.05 lost a real coloured shadow on `drums.pov`.

## Mesh triangles and shadow rays

On the mesh bench above (`+WT1`, 960×720, `+A0.0 +R3`), three runs of each, interleaved, without the skip of a
missed cached occluder described under Radiosity:

| Build | Trace Gcycles | Instructions |
|---|---|---|
| before | 46.41 | 123.4 G |
| this change | 44.69 (−3.7%) | 119.6 G (−3.1%) |

3.7's `chess2.pov`, which has no meshes, renders identically, in the same cycles to 0.1% and 0.2% more instructions.

Cycle profiles of the 12-byte triangles' cost here (+2.0 G of trace, three profiles each way) put a third in the
mesh's box walk, which decodes 16-bit boxes; a quarter in the triangle test, which fetches three vertices and
forms a cross product before it can reject, where the stored plane rejected at once; a quarter in the smooth
normal at each hit, whose smoothing frame is rebuilt every time; and a sixth in the scene walk, whose code did not
change. Each thread now keeps 256 decoded triangles, in slot index mod 256, tagged with the index and a serial no
other mesh data shares: vertices and face normal for the test, and the smoothing frame once a hit asks for it.
Images are unchanged, bit for bit. Rays close together test the same triangles, all the more with anti-aliasing:

| Slots | Per thread | Lookups served | Trace Gcycles, with the rest of this change |
|---|---|---|---|
| 64 | 9.5 KB | 92.0% | 45.61 |
| 128 | 19 KB | 96.1% | 44.94 |
| 256 | 38 KB | 97.8% | 44.69 |
| 512 | 76 KB | 98.6% | 45.05 |
| 1024 | 152 KB | 99.2% | 45.23 |

three runs each, with `performance` at 46.41 in the same rounds. Slots of 96 bytes, holding the vertices in single
precision as the mesh stores them, cost 2.5 G more instructions in conversions and traced 1.6–3.3% slower than
152-byte slots at every size. Slots without the smoothing frame saved 1.3% of cycles, against 2.3% with it.

A mesh walks its tree nearest box first and skips boxes that start beyond its nearest hit so far. That hit could be
one its caller then rejects: callers drop hits nearer than `MIN_ISECT_DEPTH`, and shadow rays those within
`SMALL_TOLERANCE`. A ray leaving a mesh can meet its own triangle that near, and the walk then skipped everything
behind it, including a triangle that blocks the ray. Only hits beyond `SMALL_TOLERANCE`, which every caller accepts,
now cull. On the bench 2,426 more shadow rays are blocked and 1,543 of 691,200 pixels get darker, 1,312 by one
level and none by more than five; none get lighter.

Any hit on an opaque mesh short of the light blocks a shadow ray, yet each test walked the mesh's tree for the
nearest. Each thread remembers, per mesh and octant of directions, the triangle nearest the start of its last
shadow ray through the mesh, and tests it first in the light's test of its cached blocker and in the scene walk
that stops at the first opaque hit, but not inside CSG. With the walk culling only behind accepted hits, it blocks
exactly when the walk would have: the bench renders identically with and without it, and at `+WT4` as at `+WT1`.
With a light's cached occluder left out of the walk once it has missed (see Radiosity), it and the decoded
triangles took the bench from 38.4 to 36.2 Gcycles, measured before the culling fix.

A light caches only objects flagged opaque, but a CSG object whose own texture is opaque can have a component with
a see-through texture of its own. A cached test that met that component filtered the light without stopping, and
the scene walk that followed, from the same origin, filtered it again, so its shadow turned twice as dark once the
object was cached, depending on render order (`tools/bench/shadow-cached-csg.pov`). Such a hit now leaves the light
as it was. The walk also stopped at a hit on the cached object, which would have hidden anything behind a partial
blocker, but it never fired: a cached primitive always blocks outright, and a hit's object is a primitive, never
the cached CSG.

## Binary meshes

A large generated mesh spent its parse in the SDL tokenizer and expression parser, one number at a time: a
7.7-million-triangle `mesh2` is about 640 MB of text. A `mesh2` can now load its geometry from a `.povm` file of
little-endian arrays (`doc/povm.md`), and caches its bounding tree beside it in a `.povt` file keyed on the `.povm`
content and the build, so a second render neither parses nor builds anything. `tools/povm` writes `.povm` from Node
and converts OBJ, `mesh` and `mesh2` files.

On the 1.5-million-triangle mesh bench above, one-pixel renders, three runs of each, interleaved:

| Source | Parse Gcycles | Instructions | Peak RSS | File |
|---|---|---|---|---|
| text `mesh2` | 11.66–12.32 | 36.67 G | 148 MB | 80.0 MB |
| `.povm`, no cache | 5.00–5.14 | 9.43 G | 148 MB | 36.0 MB |
| `.povm`, warm cache | 0.170–0.176 | 0.44 G | 90 MB | 36.0 MB + 43.7 MB `.povt` |

Building the tree is almost all of the uncached load, and its working set sets the peak either way. Scaled by
triangle count to a 7.7-million-triangle mesh: about 62, 26 and 0.9 Gcycles, and
700, 700 and 400 MB peak (POV-Ray alone peaks at 15 MB), with a 185 MB `.povm` and a 224 MB `.povt`. The bench
renders identically at `+WT1` from all three, and `tools/povm/test/test.sh` checks that converted OBJ, `mesh` and
`mesh2` files render as their sources.

## SIMD

`core/math/simd.h` gives kernels fixed-width vectors, `simd::Vec<T, N>` and its mask type, over xsimd 13.2.0 (in
`libraries/xsimd`, its last release needing only C++11). The build's `-march` picks the form: one register where the
target has N lanes of T, two halves where it has fewer, and plain scalars under `--disable-simd`, whose `fma` is fused
where the target has FMA and whose `min` and `max` treat ties and NaN as x86's do, so on x86 both give the same results.
Only this header includes xsimd, so a move to C++26 `std::simd` rewrites it alone. `tools/bench/simd-disasm.sh` compiles
a 16-bit block's slab test through `simd::` and in raw intrinsics: the same 62 instructions for `x86-64-v3`, and 57 for
AVX-512.

Flat blocks test their eight boxes through it; one compare mask gives the lanes to append, lowest first, as the loop
took them. Against `performance`, trace with parse subtracted, medians of runs whose spread is 2–6%:

| Scene | Runs | Trace Gcycles | Instructions |
|---|---|---|---|
| the large scene, a 200 px close-up at 200 DPI, `+WT4` | 3 | 61.6 → 56.6 (−8.2%) | −7.8% |
| the same with radiosity | 3 | 116.6 → 113.9 (−2.4%) | −9.3% |
| the same, 400 px, without radiosity | 2 | 251.8 → 242.9 (−3.5%) | −8.6% |
| `radiosity-lawn.pov`, 320×180, `+WT1` | 5 | 15.20 → 13.99 (−7.9%) | −8.8% |
| the mesh bench, 960×720, `+WT1` | 5 | 36.43 → 36.54 (+0.3%) | −9.8% |

Single-threaded renders of all of them are identical to `performance`'s, with SIMD and with `--disable-simd`.

Testing a block's triangle leaves eight at a time was tried and is not in this branch. Each thread kept 64 mesh
blocks decoded lane by lane in double precision, and an eight-lane test, computed per lane exactly as the scalar one,
dropped leaves whose triangle the ray missed before they were queued; images were unchanged. A block opens onto 3.1
leaves on average on the close-up, and only one in ten of those is hit, so decoding and testing all lanes cost about
what it saved: against the box change alone, −0.7 to +5.5% on the close-ups, 0 to +1.9% on the lawn and −0.9 to
−1.1% on the mesh bench, across variants that decoded lanes only as rays reached them, skipped blocks opening onto a
single leaf, or kept 256 blocks a thread.

## Random draws

Area-light jitter, media, `crand`, rainbow jitter, subsurface light, photons and radiosity gather directions drew from
streams that ran on across each thread's work, so a pixel depended on what its thread had traced before: a detail
window, another thread count or another render order changed the image. Every draw is now a hash of a path key, an
effect and a sample index. A camera ray's key hashes its origin and direction (and `+SS`, which is 0 unless set);
a child ray's hashes its parent's, its kind and its index; a radiosity sample hashes its point and normals, and a
photon its light, target and place in the shooting grid. Photons are sorted into one order before the tree is built,
radiosity samples are kept in each octree node in an order of their own, and `+HR` is the default. A subsurface point
cloud's cell traces its shadows from a ticket of its own, not that of the ray that first needed it.

A test scene with every effect (`tests/render/random_effects.pov`) at 320×240 renders bit for bit the same with 1 and 8
threads, with `+RP5`, `+BS7` and a mosaic preview, and in a 128×120 window; before, 85% of its pixels differed between
1 and 8 threads. The three radiosity scenes below also match at 1 and 8 threads. Radiosity still changes in a window,
where the pretrace samples only what the window shows.

The same scene at 1280×720 without anti-aliasing shows the old table's structure: its subsurface sphere is covered in
vertical stripes, in POV-Ray 3.7.0 as well as before this change, because neighbouring pixels drew correlated runs of
the table. With hashed draws the sphere's noise is even. With `+HR` as the default, a radiosity sample gathered during
the final pass is not seen by other tiles, so a surface that crosses a tile edge can show a faint block-shaped step,
as `+HR` did before; it moves with `+BS`.

Flat regions, 256×256, means (and each build's spread over four renders): a soft-shadow penumbra 0.61230 (0.00010)
before, 0.61240 (0.00017) after; `crand` 0.80415, 0.80428; media 0.54093 (0.00001), 0.54040 (0.00007). Pixel variances
agree within 2%. The old generator repeated a table of 32,768 values, so its renders agree with each other almost
exactly and carry the table's own error, about 0.0003 on the media region. Measured against that near-zero spread,
the media differences look significant (z of −4 and −6), but the z-scores are inflated: the old renders are not
independent samples, and the gap is within the table's error.

Radiosity at 320×240 against converged renders, several renders each:

| Scene | RMSE before | after | Mean before | after |
|---|---|---|---|---|
| `cornell` (`recursion_limit 3`) | 0.00306 | 0.00338 | +0.21% | −0.49% |
| `radiosity` | 0.00965 | 0.00948 | +0.30% | +0.25% |

With `recursion_limit 1` the Cornell box's mean moves by 0.03%. The deeper bounces lose something from the old stream,
where consecutive samples took consecutive runs of the direction pool; the per-tile restart under `+HR` that this
replaces showed the same −0.5%. Choosing aligned runs, sibling-disjoint runs or a shifted Halton prefix instead moved
it only to −0.3%.

The large scene's band, four counted runs of each build, trace only: instructions −0.11%, cycles −0.25% (runs spread
±2%).

## Progressive rendering

`+PR` (`Progressive_Render=on`) resolves the whole frame at doubling resolution instead of block by block, so the image
is a complete best-so-far picture at every point and a final render doubles as its own preview. Samples sit on one
lattice anchored at the image origin: the first level traces the points a step apart, where the step is the smallest
power of two covering the frame, and each level after halves the step and traces only the points the coarser levels
have not. Every pixel is traced once, and each sample fills its cell of the image until a finer level overwrites the
parts it does not own. Anti-aliasing runs as one more pass: method 1 compares each pixel with its four real
neighbours, including those in other blocks, and method 2 puts the lattice on pixel corners and subdivides from them,
reusing samples on an edge that the pixel to the left or above already traced. That reuse stays within a block: a
subdivision sample on a block border is still traced by both blocks. Method 3 is refused with `+PR`. In a render
window away from the origin (`+SC`, `+SR`), the coarsest levels may have no sample inside the window, so it stays
empty until the lattice is fine enough to reach it.

The anti-aliasing pass keeps every lattice sample, 16 bytes per pixel beside the image buffer (about 133 MB at
3840×2160). Continuing such a render with `+C` reads the samples back from the state file; while it starts, a few
transient copies of that size exist in the frontend and in the render options.

A progressive render continues with `+C` from the level and block where it stopped. Its state file (version 0002) marks
each block with its level and keeps the lattice samples the anti-aliasing pass needs; its radiosity cache is kept
beside it (or in the `+RFO` file) and loaded on continuing, so no pretrace runs again; a cache with no samples
(stopped before any were saved) is pretraced again. Continuing with other `+A` or `+R` values applies them to the
anti-aliasing pass, as a block-order `+C` does to the blocks left.

Against block order at 333×127 with 4 threads, `+A0.1 +R3`, on a plain test scene and on
`tests/render/random_effects.pov`:

| | Block order | `+PR` |
|---|---|---|
| no anti-aliasing, image | | bit for bit the same; a trace counter shows each pixel traced once |
| method 2, rays (plain / every effect) | 1,102,627 / 853,152 | 909,592 / 685,648, bit for bit the same image |
| method 2, subdivision samples | 293,976 / 433,847 | 236,531 / 339,322 |
| method 1, pixels supersampled (9 samples each) | 25,542 / 24,075 | 23,200 / 25,088, the same with `+BS8` and `+BS32` |

Block order traces method 2's corners on block edges twice and every subdivision sample on an edge shared by two
refined pixels twice; method 1 traces a line of pixels above and left of each block and compares each pixel with
neighbours that may already be anti-aliased. Renders stopped at several points, during a level and during the
anti-aliasing pass, and continued with `+C` match an uninterrupted one bit for bit with one thread.

## Isosurfaces

Functions ran on a switch over about a thousand opcodes, each with its registers baked in, behind one indirect
jump; `advanced/isocacti.pov` was as slow as 3.7 or slower, and its cycles moved with code placement. Now:

- Programs are decoded once, and the one-point interpreter has a handler per opcode and register pair, as the
  switch had, each ending in its own computed-goto dispatch. Handlers that took their registers as fields and kept
  them in an array were tried first: every dependent op then waited on a store being forwarded, and GCC folded
  their dispatch tails back into one jump, so they were slower than the switch. The function carries
  `optimize("no-crossjumping", "no-gcse", "no-tree-slp-vectorize")` to keep the tails apart.
- `ExecuteBatch` evaluates four points per call on `simd::Vec<DBL, 4>`. Lanes that branch apart keep their own
  program counters and the lowest runs next, calls run under the caller's lane mask, and anything the batch does not
  model falls back to one point at a time. Normals take one batch, and the root finder its interval's two ends.
- sin, cos, tan, their inverses and hyperbolics, exp, log, log10, pow and atan2 come from xsimd in both paths; the
  one-point path runs the same four-lane call on a broadcast value, so both return the same bits. xsimd picks its
  trigonometric range reduction from all lanes at once, so `simd::sin`, `cos` and `tan` run lanes in groups that
  need the same one. floor, ceil, int, div and mod stay exact library calls; xsimd's pow is exp(y log x), whose
  error grows with |y log x|. xsimd takes signs from −0.0, so `configure` adds `-fno-associative-math
  -fsigned-zeros` after `-ffast-math`; without them `sin` and `cos` lose their sign.
- An opaque isosurface tested by a shadow ray, which only needs to know whether the light is blocked, searches for
  any root short of the light: where an interval's far end is inside, bisection tries that half first. Either
  half's test depends only on its ends, so it finds a root exactly when the full search does.

Single-threaded counted runs of whole frames at a quarter of their pixels, medians of two, parse included, against
`performance` at 785bee41. The last three scenes are `tools/bench/isosurface-noise-sphere.pov` (a sphere under five
octaves of strong noise) with its box as written and three times wider, and `tools/bench/isosurface-caverns.pov`.

| Scene | Ubuntu 3.7 | `performance` | this branch | Function calls |
|---|---|---|---|---|
| `isocacti`, 400×300 `+A0.3` | 74.1 G | 78.9 G | 53.7 G (−32%) | 85.5 M → 83.3 M |
| noise sphere, 320×240 | | 13.4 G | 7.4 G (−45%) | 5.69 M → 5.18 M |
| noise sphere, box ×3, 320×240 | | 18.2 G | 9.6 G (−47%) | 7.49 M → 6.96 M |
| caverns, 200×150 | | 9.7 G | 4.9 G (−50%) | 6.46 M → 5.99 M |

The two new flags alone move `performance` by −8% to +5% across these scenes, within what code placement does to
it; against `performance` built with them, the branch saves 35–45%. On `isocacti`, one run each on earlier builds:
`-falign-functions=64 -falign-loops=32` moves `performance` by −1.9% and this branch by −0.3%; library maths instead
of xsimd costs +4.7%, one point at a time instead of batches +0.2%. With library maths and `-fno-fast-math
-ffp-contract=off`, the branch spends 30% fewer cycles per function call than `performance` for the same calls. The
any-root shadow search saves 1.7% on `isocacti`, 9% on the noise sphere and 8% on the caverns.

Built with the same flags, single-threaded, this branch draws every scene above exactly as `performance` does, pixel
for pixel, and so does it for `incdemo/i_internal.pov` (frames 10–15), the noise sphere with max_gradient halved, and
`distribution/scenes/objects/superel-iso.pov`, which draws superel1 and superel2 as `f_superellipsoid` isosurfaces
alone and under reflection, glass, bumps, area lights, media, radiosity and photons; their coverage matches the
native superellipsoid at every pixel. Batched and one-point evaluation return the same bits, and `tests_fnbatch.cpp`
checks the VM's library maths against `std::` to 1e-12 over ±200. The caverns, with a red background, show no red,
open or closed, or at accuracy 0.0001 with max_gradient 6: no ray escapes, and the scene's black patches are rock in
shadow.

Four-threaded counts on the shared box spread by 20–30% between runs of one build, so these are single-threaded.

### Anti-aliasing method 4

`+AM4` (with `+PR`) spends a sample budget, `+AB` extra samples per pixel on average, where it helps most. It fits
straight edges to the centre samples already traced (no rays), traces probes only where a fitted line is still
uncertain, and averages extra samples into pixels whose neighbourhood is too noisy to fit. Each pass is planned up
front: candidates go into priority buckets and whole buckets are taken best-first until the budget is reached, so
the last bucket may overshoot, and the image does not depend on the thread count. The fit and planning passes are
single-threaded and cost about 2,000–4,000 instructions a pixel over plain progressive rendering, and about 140 bytes
a pixel of memory.

A checkerboard floor with a mirror sphere, 640×480, `+A0.02`, mean absolute error against a 144-sample reference:

| | extra samples/px | sphere | far floor | near floor | all pixels |
|---|---|---|---|---|---|
| none | 0 | 0.0433 | 0.0199 | 0.0108 | 0.0413 |
| method 2 (`+A0.1 +R3`) | 3.56 | 0.0094 | 0.0039 | 0.0017 | 0.0109 |
| method 4, `+AB1` | 1.0 | 0.0201 | 0.0050 | 0.0019 | 0.0158 |
| method 4, `+AB2` | 2.0 | 0.0151 | 0.0040 | 0.0017 | 0.0114 |
| method 4, `+AB4` | 2.8 | 0.0122 | 0.0034 | 0.0015 | 0.0089 |

The mirror sphere's noisy reflection takes most of the budget and stays worse than method 2 at every budget tried.

## Texture filtering

`+TF` evaluates each filtered pigment 8 times where the first eight taps agree and the footprint is near round, and at
most 72 times (the eight, or only the first four when those already disagree, then a lattice of up to 64 along a stretched or disagreeing footprint); with
`Texture_Filter_Taps=3`, 3 times where the first three agree and at most 75. No extra rays are traced, so scenes whose
time goes to intersections and lighting barely notice it. With it off, no differentials are computed. On a checker
plane running to the horizon, 640×480, one ray a pixel, the mean OkLab error against a 64-sample reference falls from
9.2 to 3.1 (×1000), and to 3.3 with three starting taps.

Diffuse, ambient, emitted and radiosity light are linear in the pigment, so lighting the averaged pigment gives the
mean colour supersampling gives. A layer that filters or transmits needs three means, not one: its colour weighted by
its opacity (its own light), its opacity, and colour × filter + transmit (what reaches the layers and objects behind
it). One averaged colour cannot stand for both: on a plane of the stock `T_Wood20`, a half-filtering grain over wood,
it tints the wood by the grain's opaque streaks, mean green −14% and blue −20% against a 64-sample reference (OkLab
20.8 ×1000); the three means leave 0.2, and opaque pigments are unchanged to the byte. Still approximate: layers are
averaged apart, so light through an upper layer times the layer under it is a product of means, exact only where the
two patterns are unrelated; metallic reflection reads the averaged colour; `texture_map` and `material_map` pick their
texture at the hit point (aliased like an unfiltered ray, not biased). Scenes with `assumed_gamma` other than 1
average, like antialiasing, in their working space.
## Function-density point batches

Function densities reuse one function context for each density batch. Eligible functions with SIMD
transcendental operations and at most one noise call site in their expanded call graph use the existing
four-point VM; other functions, and batches shorter than four points, use scalar execution on that context.
The noise-call gate counts both sides of branches conservatively. Noise itself remains scalar
per distinct lane. This is separate from conservative function-range bounds: complex noise functions
can evaluate in batches even when no useful interval bound is available.

Eligibility is computed once with the function's decoded program. It follows named calls, admits only
arithmetic and the deterministic `f_noise3d`/`f_noise_generator` traps, and checks initialized locals,
call depth and stack requirements. Globals, stateful or unknown traps and unsupported instructions
retain the original point path. The check precedes execution so rejected callbacks cannot be replayed
after a partial batch. Existing isosurface batch selection is unchanged.

Sample coordinates, order, values greater than one wrapped with `fmod`, waves, colour maps, density
multiplication and optical-depth integration retain their existing operations. Turbulence and nonlinear
colour maps still use the pigment fallback. There is no new approximation or scene-language setting.

`tools/bench/media-function-simd.pov` exercises direct and named noise, four octaves, nested clouds,
branching, explicit noise generators, wrapping, arithmetic and a stateful trap fallback. Run
`tools/bench/check-media-function-simd.py REFERENCE_BINARY CANDIDATE_BINARY OUTPUT_DIRECTORY` for
89 image pairs across all noise generators and media methods, including pigment fallbacks. The standalone
`tools/bench/check-function-batches.sh BUILD_DIRECTORY OUTPUT_DIRECTORY` checks scalar/batch values,
waves, tails, local initialization, resource limits, context selection and function lifecycle.
On the matched native builds, all 89 rendered pairs were pixel-identical at 16 bits. The standalone
suite passed 245,916 pattern comparisons and 231,012 direct SIMD comparisons over 40 eligibility cases.

Single-threaded trace counts against `2255e747`, with matching native compiler flags, no AA,
noise generator 3, media method 3, and medians of three alternating runs minus a matching one-pixel
run. Function fixtures are 240×180; the standard benchmark is 128×128.

| Fixture | Before Gcycles | After Gcycles | Cycles | Instructions |
|---|---|---|---|---|
| Case 0, one noise call | 3.428 | 3.435 | +0.2% | −3.2% |
| Case 1, noise and SIMD maths | 5.068 | 4.691 | −7.4% | −22.7% |
| Case 2, four named noise octaves | 8.361 | 8.367 | +0.1% | −1.1% |
| Case 3, nested cloud, scalar context reuse | 11.113 | 10.558 | −5.0% | −0.9% |
| Case 4, branching noise | 3.717 | 3.713 | −0.1% | −2.7% |
| Case 7, SIMD maths | 6.603 | 4.628 | −29.9% | −30.1% |
| Case 8, stateful callback fallback | 8.822 | 8.638 | −2.1% | +0.1% |
| Standard benchmark | 15.785 | 15.785 | 0.0% | 0.0% |

Noise-only cases are effectively neutral. The unchanged callback fallback's cycle difference and the
cloud's small instruction saving illustrate why these timings are not universal speedup predictions.
The initial unrestricted SIMD selection regressed noise-only fixtures by 5–7%; a maths-only preference
still regressed the nested cloud by 4%. The conservative gate avoids those measured regressions.
Actual multi-point noise remains a separate kernel experiment.

On the current `performance` base, three full renders of each build at `+PR -A +WT1`, minus a
matching one-pixel parse run, measured the following user-space instructions. The function fixtures
were 240×180; the stock `interior/media/micro.pov` scene was rendered at the same size. The
four-volume scene was one complete 96×54 render per build, with the same parse subtraction. All
compared 16-bit pixel payloads matched, and no counter events were multiplexed.

| Scene | Base G instructions | This change | Difference |
|---|---:|---:|---:|
| Function density, SIMD maths (case 7) | 17.455 | 12.205 | −30.08% |
| Function density, noise plus maths (case 1) | 15.444 | 11.944 | −22.66% |
| Function density, noise only (case 0) | 8.837 | 8.557 | −3.16% |
| Function density, nested cloud (case 3) | 32.673 | 32.394 | −0.85% |
| Stock `micro.pov` media scene | 24.225 | 24.225 | 0.00% |
| Four overlapping volumes, two media-aware lights (`media-combined.pov`) | 3872.078 | 2913.591 | −24.75% |

## Parallel parse-time mesh construction

Each complete `isosurface_mesh`, `skein_mesh` or binary `.povm` load is a task on a pool sized by `+WT`. One task has
one thread context. Generated meshes evaluate their frozen function
state there; `.povm` tasks read and validate the file, prepare its triangles, and load or build its cached bounding
tree there. Parser tokens, declarations and object modifiers remain on the parser thread.

A declaration publishes a deferred mesh immediately. Copying, instancing, transforming and texturing it do not wait.
Operations that actually inspect geometry (`trace`, `inside`, extents, mesh cameras, and bounding, clipping or
object-pattern objects) join that build; otherwise the parser joins all builds after the last token. A pending mesh's
box is a placeholder, so each compound holding one is rebounded from scratch once the mesh resolves; tracing never
checks for pending meshes. Diagnostics are emitted in declaration order. Each generated build gets a frozen
function-VM snapshot, so later function declarations and assignments neither race nor wait for it. Reusing a
declaration shares its completed mesh data; redeclaring it creates a distinct task and leaves earlier instances
attached to the earlier result.

One isolated run of `tools/bench/parse-mesh-generation.pov`, native optimized build:

| Fixture | Work | `+WT1` wall / CPU | `+WT4` wall / CPU | Wall difference |
|---|---:|---:|---:|---:|
| eight skein tori, `max_angle 1` | 8 × 262,144 triangles | 5.649 / 5.695 s | 1.663 / 6.235 s | -70.6% |
| eight noise-displaced isosurfaces | 8 × 135,734 triangles | 15.463 / not recorded | 15.780 / not recorded | +2.0% |

The isosurface fixture saturated the test host: four concurrent builds each slowed by about four times and
used 267 MB peak instead of 119 MB, so concurrency did not reduce latency there. The skein fixture scaled strongly.
This is workload- and machine-dependent; `+WT` controls the memory/throughput tradeoff, while `+WT1`
retains serial construction.

The render checks build the same triangle and vertex counts at one and four threads and repeat their ray, normal,
inside and chord-error comparisons. The skein check also builds an image-sampled mesh. A separate fixture instances
pending meshes, redeclares the same name with changing geometry options, and observes distinct 262,144- and
131,072-triangle results. `.povm` checks cover the final barrier and an immediate `max_extent` barrier.

Other ordinary `mesh`/`mesh2` syntax remains on the parser thread because token consumption, texture ownership and
vertex hashing are interleaved. Image and font loading are also outside this pool.

## Method

`tools/bench/pcount.c` counts user-space instructions, cycles and branch misses of a process and every thread it
starts, through `perf_event_open`; it needs `kernel.perf_event_paranoid` of 2 or less. Cycles count only while
POV-Ray runs, so other load on the machine barely moves them, and instruction counts repeat to 0.01%.
`tools/bench/bench.sh` subtracts a 1-pixel render so parse and trace separate.

Profiles came from `perf record -e cycles:u` on unstripped builds (`--disable-strip`, `-g`).

## Container

    podman build -f unix/Containerfile -t povray .
    podman run --rm -v "$PWD:/work" povray +Iscene.pov +W800 +H600

Build arguments: `MARCH` and `MTUNE` (default `native`; `MARCH=x86-64-v3 MTUNE=generic` runs on any AVX2 CPU),
`HARDENING` (default 1; 0 drops the stack protector, stack-clash and CET code the benchmarks above were built
without), `PGO` (default 0), `PGO_SCENE` and `PGO_ARGS` (the training render, a path in the build context; default
the standard benchmark at 256×256), `BASE` (the Ubuntu image).

On the large scene the container without PGO traces at 120.9 Gcycles with `HARDENING=0` and 123.5 with the
default. Train PGO on scenes like the ones you render: trained on the standard benchmark it made the large scene
4% slower to trace and 47% slower to parse than no PGO, since that scene has almost no meshes and their code was
built as cold. Trained on other parts of the large scene itself, PGO saved 4–10%.

`-ffast-math` needs `-fno-finite-math-only` after it or some intersections break, and `-fno-associative-math
-fsigned-zeros` for the function VM's maths.

## Forks worth pulling

Swept 2026-09-24: all 293 visible forks and the known derivatives.

| Source | What matters | Verdict |
|---|---|---|
| [EdgeOfAssembly/povray](https://github.com/EdgeOfAssembly/povray) | `d706eb11` bbox queue, `c62ff4bb` mesh queue ([#363](https://github.com/POV-Ray/povray/issues/363)) | in this branch |
| [wfpokorny/povray](https://github.com/wfpokorny/povray) branches | solver accuracy, blob accuracy, FS324 mesh2 fix, shadow-cache tolerance, media method 3 up to 5.5% (`5edfa92807`, in this branch) | port as commits |
| yuqk (Pokorny, tarballs on news.povray.org) | sphere-sweep root fixes, solvers, mesh2, lathe/sor, radiosity background fix, AA method 3, image lookup speed, PGO recipe | port by subsystem |
| [LeForgeron/povray `hgpovray38`](https://github.com/LeForgeron/povray/tree/hgpovray38) | FS324 fix, radiosity load, text segfault; new patterns, sphere-sweep UV, NURBS | fixes yes, features selectively |
| upstream `release/v3.8.0` | spline duplicate fix `3b43b71a42`, TGA, OpenEXR 3 | merge |
| [PR #452](https://github.com/POV-Ray/povray/pull/452) AVX-512 noise | noise and turbulence | the benchmark scene is 55% noise; its XCR0 check is wrong |

## Next

- Isosurfaces: vectorised noise, so that batches pay where functions are mostly noise; batches of rays rather than
  of points. An occupancy grid from max_gradient, skipping empty cells while root finding, was tried: it moved roots,
  gained 7 points only on a leaning trunk in a loose box and cost up to 6 on tighter ones; a tighter `contained_by`
  does better.
- Noise: 55% of the standard benchmark; AVX-512 or a vectorised octave loop.
- Media: extinction along shadow rays, 363 M density evaluations in the haze window, is still its largest cost;
  skipping any safely needs bounds on the density.
- Subsurface: method 2 still hands sharp edges and creases to method 1, and samples single scattering from lights
  behind the surface. At equal quality it needs spacing 0.35 at the rim of `sslt-open.pov` Case 2 and 0.25 under the
  small mesh in `sslt-wide.pov`, and at 1600×800 that scene's bark comes out up to 8.6 levels light at any spacing.
- Radiosity: irradiance gradients (Ward and Heckbert, over a stratified gather) were tried. Against converged renders
  they cut the lawn's blurred error by a third at `error_bound 0.6` and the Cornell box's by 37% for 5–7% more
  cycles, and ended the grass's brightening, but left the patio unchanged, made a close-up of the large scene 14–25%
  worse at `count 30` (10% from the stratified gather alone) and no better at `count 60`. They are not in this branch.
- Triangles: eight-lane tests pay only where blocks open onto more leaves; test rays eight at a time instead.
- Height fields: their own block walk.
- Mesh memory: the tree is now 55% of a mesh; 8-bit blocks are the next saving.
- A multi-occluder shadow cache saved 3% but changed shadows in ways not yet explained; it is not in this branch.
