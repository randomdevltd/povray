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

Deferring the gather radius's square root until a tree branch needs it, with the same comparisons, saved 0.4 to 2.3%
of trace instructions (`+PR -A +Q9 +WT1`, one-pixel run subtracted): `phot_met_glass` at 960×480 34.170 to 33.401 G,
the water caustic at 640×360 8.155 to 8.019 G, dense water (spacing 0.01, cap 400) 21.758 to 21.337 G, the sparse
caustic at 1920×960 20.363 to 20.283 G, with the same decoded pixels. Combined with the reuse and reach check above,
trace instructions fell 3.0% on `phot_met_glass`, 15.8% on the water caustic, 11.1% on dense water and 30.1% on the
sparse caustic (one run per scene, same pixels).

## Progressive volume segments

Photon method 2 now stores finite volume segments, with exponential attenuation
integrated inside the gather sphere. Its existing bounded batches and independent
pilot remain; adjacent constant-coefficient cells coalesce only inside one of the
curved marcher's chords. Method 1 is unchanged. See
[transport and ownership limits](progressive-photons.md).

The focused fixture is `tests/render/progressive_volume.pov`, combining a media
emitter, a method-4 scattering receiver and a refractive target. The method-4
lighting stride previously omitted photon gathers: its 24×24 mean linear value
was 0.00481 versus 0.01939 with classic media sampling. The registered regression
fails on that version. Direct light keeps its coarse stride while photon gathers
now run at every coefficient sample.

These measurements used `+W24 +H24 +PR -A +WT1 +FP16 File_Gamma=1`, GCC 15,
and the same scene with `Declare=Heat=0`. The development point fallback is built
with `-DPOV_PROGRESSIVE_VOLUME_POINTS`; it uses the same coefficient preparation,
medium ownership and final lighting stride. A segment render at quality 4 supplies
the reference. Errors are MSE of decoded linear 16-bit RGB. This reference retains
sampling and reconstruction error, so the comparison is a focused diagnostic.

| Estimator / quality | Linear MSE | Full instructions | Shooting CPU | Map CPU | Camera CPU | Peak RSS |
|---|---:|---:|---:|---:|---:|---:|
| Points / 4 | 5.34e-5 | 59.219 G | 4.522 s | 1.828 s | 2.686 s | 229 MiB |
| Segments / 0.5 | 5.29e-5 | 33.237 G | 0.721 s | 0.077 s | 2.823 s | 42 MiB |

Medians of three alternating runs; user-space instruction counters were not
multiplexed. Decoded images repeated exactly. At this matched error, segments used
44% fewer total instructions and 82% less RSS. Camera gathering remains expensive;
the gain comes from fewer emission batches and smaller maps. One-pixel runs used
41.772 G instructions for points and 6.898 G for segments: these include photon
shooting and map construction. Subtracting them leaves 17.448 G and 26.339 G for the
camera increment. Total preparation CPU was 1.984 s and 0.111 s respectively,
including the map subset shown above. Searching the pilot only within the already
capped support reduced segment quality-0.25 instructions from 29.091 G to 18.241 G
with identical decoded pixels.

For heterogeneous extinction (`Declare=Variation=0.1`, quality 0.25, one run),
segments used 6.745 G instructions and 212 MiB versus 7.029 G and 213 MiB for
points. MSE against the corresponding quality-4 segment reference was 1.36e-4
versus 1.55e-3. Varying coefficients prevent coalescing, so there was no memory gain.

With the refractive field enabled (the fixture's default `Heat=0.08`, quality 0.25,
one run), segments used 97.081 G instructions and 249 MiB versus 86.924 G and
498 MiB for points: a 12% instruction regression at the same work budget, with
half the memory. Against a four-pass segment reference (quality 1), MSE was
2.33e-4 for segments and 1.51e-3 for points. This lower-quality reference is an
additional diagnostic, not a converged solution. The refractive marcher dominates
this case. The implementation preserves its piecewise paths; these measurements
do not establish a curved-path performance improvement at equal error.

Reproduce the matched-error pair with separate segment and point executables:

```sh
povray-segments +Itests/render/progressive_volume.pov +W24 +H24 +PR -A +WT1 \
  +FP16 File_Gamma=1 Declare=Heat=0 Declare=PhotonQuality=0.5 +Osegments.ppm
povray-points +Itests/render/progressive_volume.pov +W24 +H24 +PR -A +WT1 \
  +FP16 File_Gamma=1 Declare=Heat=0 Declare=PhotonQuality=4 +Opoints.ppm
```

The analytic tests check clipping, attenuation, subdivision, kernel normalization,
bent paths and indexed gathers against exhaustive queries. The combined render
suite checks method-4 gathering, thread equality, linear power, varying extinction,
curved paths and the existing rejection of volume photons from light groups.
Emitter/target power weighting was left unchanged: comparable source power would
need the spot, area and parallel directional/aperture PDFs, and these single-source
fixtures provide no evidence of a useful sampling gain.
## Progressive surface gathering

Superseded: method 2 now builds one map and traces the camera once; see [progressive-photons.md](progressive-photons.md).

Method 2 now selects bandwidth at each receiver hit from its independent pilot,
with a cap derived from that receiver. Capped nearest-neighbour queries prune
before searching distant caustics. Surface reconstruction evaluates materials
only inside its radiance support; the previous curvature stencil also shaded
photons outside that support. The pilot and each fresh map remain bounded.

Three alternating pairs, `+PR -A +WT1`, user-space instructions with the matching
one-pixel render subtracted; no counters were multiplexed:

| Scene | Size, quality | Trace instructions before → after | Whole render before → after |
|---|---|---|---|
| `tests/render/progressive_surface.pov` | 128×96, 4 | 4.209 → 3.192 G (−24.2%) | 15.464 → 14.522 G (−6.1%) |
| `tests/render/progressive_photons.pov`, photon-only | 256×170, 1 | 3.324 → 1.804 G (−45.7%) | 5.573 → 4.085 G (−26.7%) |

At 64×48 on the close/layered fixture, candidates inside the search support fell
from 1,004,355 to 37,264 and material evaluations from 730,012 to 25,215. On the
96×64 glass caustic they fell from 329,414 to 10,664. These counts came from separate
diagnostic builds whose decoded original images matched the original optimized
build. Median map CPU on the 128×96 fixture was 0.316 → 0.331 seconds; the logged
photon-vector peak stayed 20,050,240 bytes. The additional fixed pilot index is not
included in that vector-capacity statistic. CPU time is supporting evidence on
these small runs.

Against a 128×96 quality-16 method-2 reference, the 64×48 close/layered render's
linear-light MSE fell from `3.807e-6` to `3.180e-6`; energy ratio moved from 1.116 to
0.984. Classic gathers can mix these nearby receivers, so this fixture uses a
higher-quality reference with receiver matching. Against a 4× supersampled
classic glass reference with 1,048,576 photons, decoded HDR quality-4 MSE fell
from `3.128e-4` to `2.822e-4`, and energy ratio moved from 0.967 to 0.972. The glass
comparison uses `LampPower=8`, `PhotonOnly=1`, `FadeDistance=sqrt(0.5)` and
`+AM4 +A0.05` for the reference.

Quality 1 is noisier on that HDR glass comparison: MSE rises from `8.809e-4` to
`1.233e-3`, peak radiance from 5.984 to 6.547, while energy ratio improves from
1.098 to 1.085. This is a speed/quality trade at low work, not a universal quality
improvement. Neither a per-hit curvature stencil, a prepared curvature proxy,
nor a fixed 1.5× support improved both tested scenes, so the implementation keeps
the receiver-local pilot baseline. The focused source regression checks receiver
scales, independence, shrinking support and sparse-query visitation; the render
regression checks thread equality and a higher-quality image reference.
## Large-image photon batches

Superseded: method 2 now builds one map and traces the camera once; see [progressive-photons.md](progressive-photons.md).

The experimental method-2 schedule groups up to four 65,536-sample emission
batches per camera pass for images of at least 8,000,000 pixels. Smaller images
retain one batch per camera pass. This threshold is a work heuristic; these
measurements cover the water-caustic fixture at 4000×2667. Fewer camera samples
can increase edge and texture noise on other scenes. Exact fractional emission
budgets use a partial last map and weight camera radiance by its batch count.

Use `tools/bench/photon-budget-water.pov`, `+W4000 +H2667 +WT4 +Q9 +PR -A -D
+FH Declare=PhotonQuality=2` for both executables. A matching one-pixel run adds
`+SR2 +ER2 +SC2 +EC2`, retaining the full dimensions and large-image schedule.
It includes parsing, setup, photon shooting and map building; subtraction gives
the camera increment. Full-render instruction counts include all those phases.

One alternating pair of user-space instruction measurements with
`tools/bench/pcount.c`; counters were not multiplexed:

| Quality 2 | One batch per camera pass | Four batches per camera pass |
|---|---:|---:|
| Full instructions | 1746.589 G | 566.131 G |
| One-pixel setup instructions | 9.648 G | 8.991 G |
| Camera increment | 1736.940 G | 557.139 G |
| Emission samples, including pilot | 589,824 | 589,824 |
| Camera passes | 8 | 2 |

Full instructions fell 67.59%; the camera increment fell 67.92%. Two phase-timing
runs per build give these supporting CPU measurements; map time is a subset of
preparation time:

| Quality 2 phase | One batch, runs 1 / 2 | Four batches, runs 1 / 2 |
|---|---:|---:|
| Preparation CPU | 0.420 / 0.456 s | 0.319 / 0.328 s |
| Map CPU | 0.127 / 0.155 s | 0.153 / 0.156 s |
| Shooting CPU | 1.364 / 1.531 s | 1.394 / 1.449 s |
| Camera CPU | 203.155 / 201.839 s | 61.278 / 61.343 s |
| Peak anonymous and shared memory | 1062 / 975 MiB | 558 / 543 MiB |

The memory figure is a measured process-group working set excluding file cache,
not an individual process RSS. Logged photon-vector capacity rose from
19,258,512 to 41,278,608 bytes; the fixed pilot and current map remain bounded.
Classic defaults (`Declare=Method=1`, spacing 0.02, gather cap 100) used
0.766 / 0.806 shooting CPU seconds and 33.019 / 32.490 camera CPU seconds.
These phase timings do not establish an instruction-count advantage over classic.

Linear RGB error uses same-resolution Radiance HDR references with classic
`Declare=Method=1 Declare=GatherMax=400` and spacing 0.005 or 0.0025, otherwise
matching the options above. The ground-caustic ROI is all columns and rows
1000–1999, counted from zero.

| Quality 2 error | One batch | Four batches |
|---|---:|---:|
| Whole-frame MSE, spacing-0.005 reference | 0.00402509 | 0.00385498 |
| Whole-frame MSE, spacing-0.0025 reference | 0.00540557 | 0.00519042 |
| Ground ROI MSE, spacing-0.0025 reference | 0.01359338 | 0.01273368 |
| Mean-energy ratio, spacing-0.0025 reference | 1.005720 | 1.004294 |

The references differ by MSE 0.00040240, so they are not a converged physical
solution. Both give the same ordering for quality 2, whose whole-frame error
falls about 4% and ground error 6.3%. Quality 1 is a tradeoff: whole-frame MSE
against the finer reference rises from 0.00663816 to 0.00671547, while ground
error falls from 0.01677036 to 0.01583563. This establishes a quality-2 gain on
this large parallel-light scene, without a universal quality claim. Explicit
anti-aliasing, including enabled method 4, remains unsupported by photon method 2.

The volume regression uses a 16×16 crop of a 3000×2667 canvas to exercise grouped
scheduling, a refractive lens, prepared media and a partial 4+1 batch sequence.
It checks thread equality and linear power; it is functional scheduling-path
coverage, not an 8-megapixel volume performance or quality validation.

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

On `tools/bench/media-functions.pov` at 640×480, one thread, three alternating runs, the bounds took a direct function
density from 18.94 to 9.34 billion user-space cycles (2.03×) and a named function from 22.71 to 9.72 (2.34×) against the
repaired point-sampling path, with media samples down from 52.88 M to 9.90 M in both cases. Cutoff cases and unsupported
fallbacks are pixel-identical; the largest bounded-case difference is 25/65535 per displayed colour channel.

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

The first stage uses a fixed dense grid with a cap of one million density cells and one million cells per optical grid. Up to four optical grids are kept per medium. The field covers the container box; the container geometry clips ray segments and optical-depth construction, so field interpolation does not dilute density at a boundary. An unbounded or degenerate container, an unsupported pigment, non-finite or negative density, negative extinction, or a grid over the cap uses the classic path. Multiple simultaneous media and rays that need over 4096 steps also use classic evaluation. Portals, point lights and area lights do not use a light-aligned optical grid. A density grid is built across the `+WT` threads in z slabs, and each optical-depth grid in light-aligned columns (grids of 4096 cells or more), each cell by one thread, so a grid is the same on any thread count. With eager preparation (the default, see below) a density grid is filled before the render, starting on spare threads during the parse; with it off, for a density that tests an object, and for every optical-depth grid, construction waits for the first ray that needs the grid and is included in that trace. Subsequent rays reuse it. This is a finite-resolution approximation: subcell sampling can miss thin features and softens shadow edges. Reducing the cell width approaches the underlying continuous field where it is well behaved, but need not reproduce classic media's finite quadrature exactly.

`tools/bench/media-self-shadow.pov` selects the mode with `Declare=Fast=1` and its cell width with `Declare=CellSize=0.1`. `tools/bench/media-fast-combinations.pov` exercises several containers, overlapping media, turbulent and function densities, emission, absorption, scattering, two parallel lights and an optional area light (`Declare=Area=1`). Compare paired 16-bit PPM files with `tools/bench/compare-media-fast.py`; use `+PR -A` at the same size and sampling settings. `distribution/scenes/interior/media/media5.pov` is a stock no-opt-in regression scene.

Measured with `+PR -A +WT1` and 16-bit PPM output. The self-shadow scene at 96×54 took 23.414 s classic and 0.623 s
prepared at 0.08 scene units: trace instructions fell from 307.7 to 10.22 billion. A mixed-volume scene at 96×54 took
3.093 s classic and 1.747 s prepared at 0.04. These timings include first-use density and light-grid construction, and
describe these small scenes only: they are the simplest cases, and the gain grows with the detail of the density and
with the cost of what the medium is combined with.

The error figures are differences from a higher-sample classic render, whose own quadrature is an approximation, so they
bound how far method 4 sits from classic sampling and not how accurate either is. Against that reference the mean
channel error was 12.6/65535 prepared against 6.2 classic on the self-shadow scene, and 14.4 against 18.3 on the mixed
scene, where method 4 is the closer. A resolution fine enough for the detail can match or beat the classic result.

Keeping prepared integration across overlapping and nested volumes, and skipping lighting for prepared samples with zero
scattering, saved a further 15.51% of user-space instructions on the self-shadow scene (8.381 to 7.081 billion, identical
pixels) and 2.72% on the mixed-media scene with an area light (23.272 to 22.640 billion), two alternating pairs each at
64×40. Moving an invisible transparent shadow receiver from 2 to 200 units no longer changes the medium's illumination.

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
a `difference` inverting them, and a mesh's follow its winding. A crossing inside on both sides (a union's inner
surfaces, parts sharing a face), outside on both, or between walls closer than the probe floor (a ten-thousandth of the
step) is dropped by itself; the cube keeps the rest. A part with no inside for light to diffuse through, a disc (whose
inside is a half-space), unless it cuts an intersection or difference, or a mesh with an `inside_vector` whose
triangles leave a border (an edge used an odd number of times, its vertices welded by position, as exporters repeat
them per face), adds no points, and a shading point on it gets no diffuse subsurface light, named in a warning; open
skeins, like triangles and meshes without an `inside_vector`, lose subsurface when parsed. Points are lit once, four points of each area light apiece, by the
lights of their own part (a part in a light group has its own), take photons if their own part collects them, and keep
each light's shadow. The spacing is a 128th of the cube's side, or three depths z_r where that is finer (a power of two
at most that, kept in the cube's key, so that points can place a shadow edge or the edge of a surface near the exit
point when the diffusion is long against z_r), a sixteenth to a quarter of the diffusion length, or a pixel of the view's camera where the
cube comes nearest it if that is coarser, and never under a 1024th of the side; a pixel spans a size plus an angle
times the distance, for perspective, orthographic, spherical, fisheye, ultra-wide, omnimax and panoramic cameras
(for spherical and panoramic ones, a pixel at the equator, coarser than one toward the poles). For cylinder, mesh and
user-defined cameras (`projections.inc`) the camera's rays at the four corners of each pixel of a 5×5 grid across the
image give each side's angle and, where the origins move, its size; per side the smaller of the two where both have one,
and over the grid the coarsest, as spherical cameras take the equator, so a cloud needs no more points than the sparsest
part of the view shows. A pixel spanning over 45° is a seam, not a pixel, and is skipped.
Lines along an axis the camera looks across are spaced up to twice as widely, by one over the square root of the
cosine between the axis and the direction to the camera, since surfaces facing that axis are seen obliquely; a point
then stands for 1/Σ(|nₐ|/hₐ²) of surface. The camera is set once for the render, and a cube is lit from a fresh trace
level and at full quality, so it comes out the same whichever thread builds it, from a camera ray, a reflection, the
radiosity pretrace or a radiosity sample (whose trace leaves out area lights). Threads that need a cube while it is
built take jobs from its building, so it does not hold them up; an exception in any job leaves the cube unusable
instead of holding up the threads waiting on it, and the render stops. Every cube is built at its own spacing, from the pixel, diffusion and
object rules alone, so the cloud, and with it the image, is the same whichever threads build it and in whatever order.

Two global levers make a large render fit. `spacing` multiplies every cube's step (the pixel, diffusion and object terms
alike; only the 1/1024-of-a-cube floor, a lower bound, is left as it is), so points and memory go about as
1/spacing²: spacing 4 builds about a sixteenth of the points, and the cloud resolves the diffusion's detail that much
more coarsely. Where the pixel sets the step, memory also goes as 1/pixel², so a proof render's statistics, scaled by
the square of the resolution ratio, predict a print render's. A ring the exit point's cube or one of its 26 neighbours would
make wider than a cube raises the cubes' size by fours before any is built, so a gather spans at most three cubes along an axis; cube
coordinates are 64-bit. The reach stops at the diagonal of the object's bounds, or a thousand depths z_r on an unbounded
object, which also bounds a material with no absorption. A large bounded object of a nearly non-absorbing material
then gets cubes as large as itself with points three z_r apart, so its cloud can be large; where a cube's side passes
1024 steps of three z_r, its finest floor (a 1024th of the side) sets the step instead, a warning counts those cubes,
and their edges are drawn near each exit point as on a coarse cloud.

All clouds together may take `memory` megabytes (`global_settings { subsurface { memory FLOAT } }`, 1024 by default).
Each finished cube's footprint counts against it in real bytes, by the capacity of what it holds: the cube itself,
points (about 56 bytes each), hierarchy nodes, per-light shadows (12 bytes per point and light), radiosity, entry and
photon arrays and read marks. A cube's footprint depends on the scene alone, and every cube the render needs is built,
so whether the clouds pass the cap, and the render stops, is the same on any thread count; only how far the render got
before stopping, and so the figures in the error, depend on thread timing. The rows a cube casts into are counted in the
reported peak but never decide the stop, except that a cube whose own points alone pass the cap stops the render while
it is cast, so one cube cannot outgrow memory either. The error gives the memory held so far, the share of the image
done, the cap, and a cap or spacing that would hold what was built so far. Once half the image is done, a
warning says when the clouds, growing as they did between a quarter and a half of it, would pass the cap. Cells already
built serve the rest of the image, so growth slows: one render that took the first quarter's rate projected 3407 MB
and peaked at 1657 MB. The two-point rate still overshoots; it is an upper estimate, and only passing the cap stops the
render. A cube is never coarsened for memory. An unbounded object such as a plane gets cubes like any other. Method 2's
profile tables are held apart from the clouds, within a fixed 32 MB (**Subsurface method 2's tables**).

The statistics report the spacing in force, the memory counted against the cap, the peak with rows being cast, the cubes
built, their points, the points the gathers read (those under a node a gather used, alone or summed as a group, which
measures how much of the cloud was built for nothing), and the cubes and points whose step the pixel, the diffusion, the
object's size or the floor set. They count the shading points tried and served (those in the radiosity pretrace among them), discs
that bend, shading points with points unfolded across creases, those whose near field was drawn and the shadow rays that
took, those lit as the exit point (no points near, or none facing in the disc), gathers whose cubes were raised, and the
crossings dropped by kind. Method 2 never hands a shading point to method 1: one the cloud cannot serve gets no diffuse
subsurface light, and a warning names its object (shape, first tag and the centre of its bounds) with the count.

**The sum.** Around the exit point, a core half a spacing across is lit as the exit point is, from its own light and
its own shadow test where the nearby points disagree on a light's shadow (so shadow edges stay sharp); out to 1.5
spacings the points' light is averaged by their share of the diffusion profile and multiplied by the profile's
integral over the disc, in closed form; out to 4 spacings the same, times how much nearer the ring's points are than
they would be with the surface unfolded flat (taking the planes through the exit point and each point to meet at a
crease), which is 1 on a flat surface and more across an edge, unless the ring holds more than 1.33 times a flat
ring's area or faces away from the exit point (a thin part's far side), where its points are summed as they are;
beyond, points are summed in groups whose area is small against their distance (`error_bound`, 0.1) and whose normals
agree. A point the sampled method's base point, under the exit point, sees from behind lies across a crease (a
stud's wall seen from the slab around it) or on a parallel face (the slab seen from the stud's top): it is weighed by
the profile at its distance unfolded through the material, taking the planes through the exit point and the point to
meet at a crease, and laid on the exit point's tangent plane for the disc, the ring and the edge window; walls facing
each other, and points whose tangent plane meets the exit point's where the material does not turn the corner (inside
tests a quarter spacing into the corner and under the exit point's face just off the other), stay out, so light does
not cross the gap between two studs. Parallel faces at different heights, seen from behind or in front, count only where
the material steps between them (inside under the higher face at the lower one's height, and halfway from there to the
lower one), so two blocks of one merge with an air gap between them do not light each other across it. A disc that bends
sharply is served as it is. Points across a convex edge near the exit point are averaged over their patch rather than taken at the
point. The disc and ring are sized by the coarsest cube they reach, so where cubes of different spacing meet each
still holds enough points. Diffusion shorter than a pixel (its own length, not the reach capped by the bounds) is taken as lit like the exit
point.

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

**Coarse clouds.** Points still more than four times the larger of a pixel and z_r apart (a coarse `spacing`, or the
finest floor) cannot place a shadow edge or an edge of the surface near the exit point. There, where the disc's points
disagree on a shadow, the exit point's own shadow differs from theirs, or the surface runs out, the disc is drawn
instead: 24 keyed draws on the tangent disc by the profile's near field r/(z_r² + r²)^1.5, each found on the surface by
a ray from the base point as the sampled method finds its points, lit and shadow-tested.

**Radiosity.** With subsurface radiosity on, cubes built in the final trace (kept apart from those without it) add
the radiosity cache's light to each point, entering along the normal as the sampled method takes it, and the core adds
the exit point's own. Points read only the pretrace's and loaded samples, as a final-trace query from a tile no tile
has, so no final-trace sample counts and a cube does not depend on the tile that builds it; where none is near, the
bound is widened up to fourfold, then deeper bounces are tried, and a point still without one takes its cube's mean.
In the pretrace, while the cache is incomplete, the cloud's points carry no radiosity. With radiosity on, the pretrace and the final trace build their cubes apart, so cubes the final trace needs
are built twice, once without the cache's light; sharing the points and adding the light to the second would save that.
With `radiosity { subsurface on }`, gathers
at the deepest bounce use cubes without radiosity, so the same parts of an object can be built twice, once of each
kind, within the one memory cap.

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

## Subsurface method 2 without method 1

Method 2 used to hand shading points it could not serve to method 1, which is grainy, slower, and was the only source
of the difference between the two at creases. It now serves all of them (the rules are under the point cloud above);
`tests/render/subsurface_fallback.pov` forces each former reason, one case at a time, and
`tests/render/subsurface_fallback.sh` checks that every shading point is served, the radiosity pretrace included.

**Shadow edges.** `tools/bench/sslt-shadow-edges.pov` puts a grid of thin rods' shadows, from a small area light and a
point light, on a high-albedo slab and cylinder whose diffusion is long against z_r, so cubes of 128 points per side
lie about ten z_r apart; 640×480, 74, 21, `+WT1`, tuned builds, against method 1 at 592, 168:

| | CPU-s | rms | Worst block |
|---|---|---|---|
| method 1 | 12.7 | 4.69 | 2.8 |
| method 2, handing shadow edges to method 1 (67% of shading points) | 13.0 | 7.14 | 64.1 |
| method 2, steps capped at three z_r, reach at the bounds | 13.3–13.4 | 3.75 | 12.7 |

The worst block before was a flare on the slab's front face below its top edge: points across the convex edge, a
spacing apart beside a z_r a tenth of that, were taken at their centres. Drawing the near field at the shadow edges
instead of capping the step, with 8, 16, 24 and 64 draws, left rms errors of 10.8, 7.9, 8.3 and 6.8, so the draws are
kept only for clouds coarse by `spacing`.

**Creases.** Method 1 cannot be the reference here: its base point's rays never reach a stud's wall or the slab around a
stud, so it leaves out light that diffuses round the corner. `tools/bench/sslt-reference.cpp` (then; now its `Shape=4`,
under **Subsurface reference** below) renders a physically based reference for `tools/bench/sslt-studs.pov`
with `Fillet=0 Reference=1` (point lights, no floor, no highlight, single scattering off): random walks through the
homogeneous medium of the same reduced coefficients, Fresnel boundaries of index 1.5, scored with the light entering
where a walk leaves after two or more scatterings. Its layout is written out apart from the scene's, so its object mask
must match a render's pixel for pixel (it does, at every view used here). On the bare slab's flat top (`Bare=1`) with a
high albedo (`Colour=1.2 Mfp=2`, reflectance 0.9), where the dipole holds, the dipole is 1.79 times the walks in every
channel alike: the two take the light leaving a surface in different units, and 1.79 is that constant (it is not; see
**Subsurface reference**: the bare slab is thin against the diffusion, and those walks lacked a 1/(1 − F̄dr)). With the
default red (reflectance 0.64, 0.09, 0.06) the dipole is 1.81, 4.31 and 5.51 times them: at low albedo the dipole and
its Fresnel approximation are that far from the transport, which this change does not touch, so the red reference is
scaled per channel. At 160×120 and 1024 walks the reference's own noise is about 10 levels rms. Levels of 255 over the
object, 320×240 and 4096 walks for the red studs, 160×120 and 1024 or 2048 walks for the rest:

| | red 1× | red 4× | red slot | red tight row | high albedo 1× | high albedo 4× |
|---|---|---|---|---|---|---|
| method 1, 592 samples | 27.4 / −17.6 | 32.7 / −20.8 | 24.3 / −18.6 | 30.3 / −20.6 | 32.1 / −22.6 | 38.6 / −22.8 |
| method 2, creases to method 1 | 28.1 / −17.3 | 33.9 / −21.0 | | | | |
| method 2, creases unfolded | 13.2 / −3.2 | 12.6 / −2.3 | 13.2 / −3.0 | 10.8 / −1.4 | 15.8 / +3.4 | 13.8 / +6.1 |

(rms / mean.) Choices taken from the reference, at 160×120: unfolding only across creases with a midpoint test keeping
light from crossing gaps gave rms 19.7 on red 1×, without the midpoint test 16.5, weighing every point the base point
sees from behind at its unfolded distance 13.8; leaving parallel faces out made red worse (15.4) and high albedo no
better; capping the ring's gain over a flat ring across a fold at 1, 2, 4 or not at all changed rms by under 0.1, so it
is not capped. Requiring the material to turn each corner on both sides, and parallel faces to step through it, then
gave (rms / mean, 160×120) red 13.5 / −3.2 at 1×, 12.9 / −2.3 at 4×, 13.2 / −3.0 in the slot and 10.8 / −1.4 on the
tight row, and high albedo 15.8 / +3.4, 13.8 / +6.1, 13.6 / +9.0 and 11.7 / +7.7. With `Blocks=1 View=3` (two blocks
of one merge, 0.05 apart, well within reach) the lower block's top beside the gap came out 21% brighter than farther
along it against the reference before (high albedo; red 15%), and 6% (red 1%) after. What remains: method 2 is brighter
than the walks where the diffusion is long against the part (high-albedo stud tops, thin blocks), as the dipole on a
semi-infinite medium is, and stud walls are a little bright in red near their base.

## Subsurface minimum influence

`adc_bailout` only decides whether a ray is traced, so a reflection or refraction of small weight that meets a
subsurface object used to pay for a full cloud gather and for building any cells it reached. With method 2, a hit whose
ray weight times the subsurface layers' opacity is below its finish's `subsurface { minimum_influence F }` is lit as
its exit point is: the light entering there (each light once, an area light at one drawn point), times the flesh's mean
entry factor where the flesh is looked up, times the profile's integral over the plane (`RdTotal`) and the exit Fresnel
term. It has no single scattering, photons or radiosity, reads no cell and builds none; a hit on an open sheet still
counts as unserved. The unscattered ray through the object is traced as before (it carries the incoming weight).

The threshold is an absolute share of the pixel, unrelated to `adc_bailout`. It is read from the topmost subsurface
layer's finish, where `translucency` and `method` are read, so it is set per object, and `#default { finish {
subsurface { minimum_influence F } } }` sets it for every finish made after it. A `subsurface` block that gives only
`minimum_influence` does not switch subsurface on, so that default leaves objects without subsurface alone. The test is
one multiply and compare on a value already loaded. Camera rays are cut too: a camera ray has weight 1, so it is cut
where its subsurface layers are nearly see-through (opacity below `minimum_influence`), and a ray that has passed
through see-through layers or off mirrors carries less weight still. Radiosity gather rays carry (0.5 × brightness)^(depth
+ 1). Method 1 is untouched: `tests/render/version4_subsurface_40.pov`, `tools/bench/sslt-open.pov` case 5 and
`sslt-bailout.pov` case 1, each with method 1, render pixel-identical to the build before.
`tests/render/subsurface_minimum_influence.sh` checks that hits seen only in a faint mirror build no cell with the
default, that `#default` and a finish's own value both reach the hit, that a `#default` giving only `minimum_influence`
changes nothing else, and that the image and cloud are the same on one thread and four.

The estimate is brighter than the cloud on thin see-through leaves, so the default is set by the stacked leaves of
`tools/bench/sslt-bailout.pov`: case 2, eight face-on discs each passing 40% of the light (hidden behind one another,
and with `Direct=1` stepped so a crescent of every disc shows), case 0, six leaves, and case 1, wax spheres between
mirrors of reflection 0.7 over a floor of 0.06. `-A +WT1 +BS8`, 200×150 (case 1 400×300), instructions in G, errors in
levels of 255 against `minimum_influence 0`, worst block the largest 8×8 block mean, cost removed as a share of the
subsurface cost (the render less that with subsurface off: 3.37, 2.47, 1.84 and 3.73 G):

| `minimum_influence` | hidden G | removed | rms / mean / worst | direct G | removed | rms / mean / worst |
|---|---|---|---|---|---|---|
| 0 | 364.50 | | | 237.28 | | |
| 0.001 | 364.48 | 0% | 0.00 / +0.00 / 0.0 | 237.28 | 0% | 0.00 / +0.00 / 0.0 |
| 0.002 | 359.51 | 1% | 0.17 / +0.00 / 0.2 | 235.61 | 1% | 0.13 / +0.00 / 0.1 |
| 0.003 | 323.99 | 11% | 0.46 / +0.14 / 0.9 | 222.18 | 6% | 0.53 / +0.14 / 1.6 |
| 0.005 | 285.77 | 22% | 1.04 / +0.47 / 2.0 | 204.15 | 14% | 1.51 / +0.49 / 5.3 |
| 0.01 | 218.68 | 40% | 2.01 / +0.93 / 5.1 | 166.67 | 30% | 3.45 / +1.35 / 9.6 |
| 0.0314 | 162.13 | 56% | 3.27 / +1.45 / 9.6 | 127.00 | 47% | 6.31 / +2.81 / 15.5 |

| `minimum_influence` | six leaves G | removed | rms / mean / worst | mirrors G | removed | rms / mean / worst |
|---|---|---|---|---|---|---|
| 0 | 92.70 | | | 20.60 | | |
| 0.002 | 92.70 | 0% | 0.00 / +0.00 / 0.0 | 20.60 | 0% | 0.00 / +0.00 / 0.0 |
| 0.005 | 83.09 | 11% | 0.44 / +0.08 / 2.6 | 20.57 | 0% | 0.01 / −0.00 / 0.0 |
| 0.01 | 72.86 | 22% | 0.84 / +0.24 / 3.1 | 20.49 | 1% | 0.03 / +0.00 / 0.1 |
| 0.0314 | 60.96 | 35% | 2.42 / +0.68 / 10.9 | 19.90 | 4% | 0.32 / +0.00 / 3.3 |

0.0314 is the threshold the earlier relative setting gave by default (8 × `adc_bailout` 1/255). The weights through
the stack come in steps of the faces' transmission, so 0.004 and 0.005 cut the same hits. The default is 0.003, the
largest round value whose worst block stays within three levels on both stacks (0.005 reaches 5.3 on the direct stack);
it removes 11% and 6% of the stacks' subsurface cost and changes nothing in the leaves or mirrors. On the many small objects of
`sslt-projection.pov` (`Camera=1 Count=1000`, 320×160, method 2 at spacing 1), where the unscattered ray of one box
meets the next at about 3% weight, 0.003 cuts no hit: 33.21 G, 1757 cells and 1.41 M points, where 0.0314 estimates
2229 hits and builds 1422 cells (1.15 M points) in 30.55 G at rms 0.06 and worst block 0.15 (0.01: 811 hits, 32.14 G,
worst 0.04); a finish on many small boxes can take a larger value of its own. `sslt-skin.pov`, `sslt-shadow-edges.pov`,
`sslt-lamps.pov`, `sslt-studs.pov` (320×240) and `sslt-wide.pov` (a 200×200 window of 4000×2000), each with
`Method=2 Spacing=1`, have no hit under the threshold before or after: identical images, and instructions within 0.2%
of the global setting's (9.03, 28.45, 18.61, 88.05 and 50.50 G against 9.03, 28.45, 18.62, 88.22 and 50.49 G). With
0.031373, the old default's value, the projection scene estimates the same 2229 hits as before in 30.55 G against 30.56 G.

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
method 2, how many shading points the cloud served.

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

## Subsurface reference

Method 2 was judged above against method 1 at eight times its samples, and method 1 is an approximation too: the
dipole, and rays from under the exit point that never reach round a corner. `tools/bench/sslt-reference.cpp` (`c++ -O2
-std=c++17 -pthread`) is an independent reference with nothing of the dipole: random walks through the homogeneous
medium the renderer derives from a finish (reduced scattering 1/translucency per mm, the reduced albedo the renderer's own
table gives the reflectance, isotropic scattering, the interior's ior), Fresnel reflection at every boundary. A walk
starts along the refracted eye ray and scores light scattered two or more times, as the renderer's diffuse term is with
single scattering off. By default it scores where it leaves: the light the surface takes in there (each point light's
colour, cosine and Fresnel transmittance, shadowed), entering as diffuse light of that transmitted flux, so a pixel, π
times the radiance, is Ft(eye)·E[weight · light in] / (1 − F̄dr), F̄dr the hemispherical Fresnel reflectance from outside.
With `Entry=beam` it takes each light's refracted beam at every collision instead, which is exact where the beam enters
through a flat face, apart from the beam's own reflections inside; on a curved face the entry can go unresolved (the
walks count those on stderr: none on the slab and walls, 2 to 49% on the dimple, studs and trunk). `Mode=forward`
shoots photons into a half-space or a layer. Shapes are signed-distance bounds, sphere-traced, matched shape for shape
by `tools/bench/sslt-reference.pov`: a slab 24 units deep (`View=1`: lit and seen along the normal), a 90° V-groove and
a spherical dimple 0.18 deep in a block 2 deep, the ridged trunk of `sslt-wide.pov` (in the scene a 480 × 96 `mesh2`, in
the walks the smooth surface it samples), a 1.2 mm wall, and the studs of `sslt-studs.pov` (`Bare=1`: its slab, 0.4 deep,
alone); materials red and high albedo (`sslt-studs.pov`), wax (`sslt-lamps.pov`), bark (`sslt-wide.pov`) and clay
(`sslt-open.pov`). `tools/bench/sslt-reference.py commands DIR` prints one command per walk image and per render
(`--tag`/`--declare` for another method or build, `--entry beam`), and `score DIR` the tables below, with a sheet per run
(render, walks, difference ×4). Every walk image carries per-pixel standard errors, which the rms subtracts; at 2048
walks per pixel and 160×120 they are 0.8 to 2.5 levels per pixel.

**Checks on a half-space**, ior 1.5, light and view along the normal, per unit irradiance, light scattered two or more
times (beam entry from 16×12 images, standard error 0.2 to 0.3%):

| | high albedo | red: R | G | B |
|---|---|---|---|---|
| photons, exact | 0.782 | 0.4234 | 0.02112 | 0.01091 |
| walks, beam entry | 0.778 | 0.4230 | 0.02094 | 0.01085 |
| walks, diffuse entry | 0.781 | 0.4286 | 0.02194 | 0.01138 |
| `reference-subsurface-plane.py`, the renderer's Fdr | 0.829 | 0.5875 | 0.0829 | 0.0553 |
| the same with Jensen's Fdr (`--jensen`, same medium) | 0.751 | 0.4454 | 0.0724 | 0.0494 |
| method 2 | 0.828 | 0.5837 | 0.0829 | 0.0553 |

Method 2 renders the plane integrator's dipole to within 0.7%, and the two walk estimators agree with the photons to
within 1% (beam) and 5% (diffuse entry, which is high at low albedo). The dipole is 6% above the transport at high
albedo and 1.39, 3.9 and 5.1 times it in red's channels; with Jensen's Fdr it is 4% below at high albedo. On a layer
1.2 mm deep of clay, seen within 8° of the normal, diffuse entry is 16, 12 and 6% above the photons and beam entry 1.6
to 1.9% below them (the beam's reflection off the far face is left out). The scores below use diffuse entry; on the
walls, where beams resolve everywhere, beam entry puts method 2 at 4.52, 2.76, 1.60 (clay) and 3.71 (high albedo)
times the walks instead of 3.97, 2.50, 1.53 and 3.34.

**Scores** at the head of method 2 without method 1, 160×120, no anti-aliasing, levels of 255 over the pixels both
cover (no mask mismatches but 4 on the trunk's silhouette). "Geometry" scales the walks per channel by method 2's
ratio on the deep slab of the same material, so the flat surface's error drops out (its residue, 0.5 to 1.0 rms, is the
slabs' own row); "against bare" scales by the bare studs slab instead, leaving the studs alone. Method 1 at 592 samples
is scaled by its own slab ratio and carries its own noise. rms / mean / worst 8×8 block:

| Run | Method 2 / walks, R, G, B | Method 2 absolute | Method 2 geometry | Method 1 geometry | Method 2 CPU-s |
|---|---|---|---|---|---|
| slab, red | 1.351, 3.586, 4.594 | 35.1 / +34.5 / +38.9 | 0.73 / +0.02 / +1.5 | 1.37 / +0.01 / +1.4 | 0.84 |
| slab, high albedo | 1.075 | 7.38 / +7.36 / +8.5 | 0.48 / +0.01 / −1.2 | 0.65 / 0.00 / +0.7 | 0.90 |
| slab, wax | 1.477, 1.801, 3.855 | 34.9 / +34.7 / +38.4 | 0.82 / +0.02 / +2.4 | 1.57 / +0.01 / +1.6 | 0.76 |
| slab, bark | 1.604, 1.798, 2.081 | 34.4 / +34.3 / +37.2 | 1.04 / +0.01 / +1.9 | 0.94 / +0.01 / +1.8 | 0.83 |
| slab, clay | 1.300, 1.556, 2.084 | 30.6 / +30.1 / +33.2 | 0.95 / +0.01 / +2.6 | 1.04 / +0.01 / +1.5 | 0.83 |
| V-groove and dimple, wax | 1.474, 1.762, 3.888 | 34.0 / +33.6 / +40.7 | 4.27 / −0.32 / −16.7 | 8.76 / −4.52 / −15.7 | 0.88 |
| the dimple alone (40×32 pixels) | | 35.5 / +35.1 / +40.4 | 4.56 / +1.49 / +9.5 | 9.75 / −6.40 / −12.6 | |
| V-groove and dimple, high albedo | 1.279 | 23.1 / +22.6 / +34.6 | 17.1 / +16.3 / +29.1 | 11.6 / +4.07 / +24.3 | 0.91 |
| ridged trunk, bark | 1.474, 1.701, 2.042 | 25.4 / +21.8 / +32.4 | 11.0 / −3.55 / −25.9 | 12.9 / −6.78 / −27.7 | 2.79 |
| 1.2 mm wall, clay | 3.974, 2.495, 1.534 | 68.1 / +59.5 / +74.4 | 53.8 / +32.8 / +48.6 | 53.8 / +33.2 / +48.0 | 0.70 |
| 1.2 mm wall, high albedo | 3.343 | 99.9 / +98.7 / +111.8 | 95.3 / +94.1 / +107.0 | 95.4 / +94.1 / +106.4 | 0.69 |
| bare studs slab, red | 1.740, 3.477, 4.550 | 38.7 / +37.1 / +42.7 | 14.5 / +5.55 / −22.8 | 14.2 / +5.34 / −18.2 | 0.64 |
| bare studs slab, high albedo | 1.676 | 44.0 / +42.1 / +50.0 | 38.8 / +36.7 / +44.3 | 38.4 / +36.3 / +44.3 | 0.65 |
| studs, red | 1.726, 3.159, 4.255 | 35.6 / +32.5 / +45.5 | 17.4 / +3.04 / −21.7 | 22.6 / −11.5 / −31.4 | 1.54 |
| the same against bare | | | 14.4 / −2.58 / −25.1 | 29.7 / −16.9 / −34.7 | |
| studs, high albedo | 1.734 | 44.5 / +40.9 / +62.9 | 40.0 / +36.0 / +58.2 | 26.3 / +9.88 / +34.0 | 1.68 |
| the same against bare | | | 17.7 / +1.92 / −36.4 | 34.0 / −23.9 / −61.9 | |

Where the part is thin against the diffusion, the dipole's semi-infinite medium dominates: the 4 mm studs slab is 1.68
times the walks at high albedo against 1.075 on the deep slab, the 20 mm block 1.28, and the 1.2 mm walls 3.3 to 4.

**Earlier decisions against the walks** (they were judged against method 1 × 8):

| Decision | Then | Against the walks |
|---|---|---|
| Hand no shading points over at bends and creases (`sslt-lamps.pov`); not kept, now shipped | a small concave dimple 14 levels bright, block edges 6 dark | wax dimple: method 2 4.56 / +1.49 / +9.5, method 1 9.75 / −6.40 / −12.6; method 2 is the closer, and method 1 is dark there |
| `sslt-wide.pov` bark "not close at any setting" | up to 8.6 levels light in 8-pixel blocks | ridged trunk: method 2 11.0 / −3.55 / −25.9, method 1 12.9 / −6.78 / −27.7; both dark in bands along the ridges, method 2 the closer |
| Thin rod and wall close at spacing 0.5 (`sslt-open.pov` case 6) | rms 1.60 against 1.88 | 1.2 mm wall: methods 2 and 1 alike and 1.5 to 4 times the walks; neither is close |
| Creases unfolded (`sslt-studs.pov`, 1×) | red 13.2 / −3.2, high 15.8 / +3.4; method 1 27.4 / −17.6, 32.1 / −22.6 | against bare: red 14.4 / −2.58, high 17.7 / +1.92; method 1 29.7 / −16.9, 34.0 / −23.9: holds |
| The dipole 1.79 times the walks on the bare slab, "a constant of units" | | not units: 1.075 on the deep slab, 1.68 on the 0.4-deep bare slab (thin against a 34 mm diffusion length) |

## Subsurface method 2's profile

Method 2 builds its medium and profile for the shading point's relative index η from the Fresnel reflectance from
inside, F(μ): its moments c1 = ∫₀¹ F μ dμ and c2 = ∫₀¹ F μ² dμ, integrated once per index (`SubsurfaceBoundary` in
`source/core/lighting/subsurface.cpp`; total reflection below the critical cosine, midpoint sums above it). Method 1
keeps everything here as it was.

**Fresnel diffuse reflectance.** The classic approximation (`FresnelDiffuseReflectance`, which method 1 keeps) is far
below the diffuse reflectance from inside, F̄dr = 2 c1, which Jensen's polynomial fits; with A = (1 + F̄dr) / (1 − F̄dr)
it puts the dipole's virtual source at z_r (1 + 4A/3):

| η | classic F̄dr | Jensen's polynomial | 2 c1 | virtual source, classic → 2 c1 |
|---|---|---|---|---|
| 1.3 | 0.059 | 0.4448 | 0.4445 | 2.50 → 4.47 z_r |
| 1.4 | 0.074 | 0.5295 | 0.5290 | 2.55 → 5.33 z_r |
| 1.45 | 0.082 | 0.5650 | 0.5645 | 2.57 → 5.79 z_r |
| 1.5 | 0.089 | 0.5967 | 0.5963 | 2.59 → 6.27 z_r |

Method 2 takes 2 c1, for its dipole and for the table that inverts the colour to a reduced albedo, so the total diffuse
reflectance stays the colour (the head-on slabs below render Ft² times the colour to within 1.2%) while the medium
absorbs less: the high-albedo material's reduced albedo goes from 0.99885 to 0.99971 and its diffusion length from 17
to 34 reduced mean free paths. 2 c1 holds for any η, where the polynomial fails below 1.

**The profile.** Method 2's profile is now photon beam diffusion (Habel, Christensen and Jarosz 2013): real sources of
d'Eon's better dipole (2012) spread along the refracted beam, falling off as e^(−t) with depth t and damped near the
surface by 1 − e^(−2(d_r + t)), with the albedo squared, as pbrt's. The better dipole takes Grosjean's diffusion
coefficient D = (2σa + σs′) / 3σt′², a virtual source 4AD above each real one with A = (1 + 3 c2) / (1 − 2 c1), and
both the fluence and the flux at the surface, weighted (1 − 2 c1) / 4 and (1 − 3 c2) / 2; in closed form it gives Rd,
its integral over a disc and over the plane. Beam diffusion is tabulated per index as its ratio to the better dipole at
one depth: 128 rows by u = √(3(1 − albedo)), 217 columns of squared optical radius, four to an octave from 2⁻²⁷,
each the ratio of Rd and of Rd's disc integral (Simpson's rule over the log radius), and per row the ratio of the
totals. A lookup reads the octave and quarter from the bits of the squared radius and interpolates bilinearly, so Rd,
`RdDisc` and `RdEdgeShare` stay the better dipole's closed forms times a ratio, and the far field keeps the dipole's
exact decay. The colour inverts through the same total. Building the table takes about 0.07 CPU-s per index; see **Subsurface method 2's tables** for how many are kept.

The media the profiles derive differ: each inverts the same colour to a reduced albedo, and a profile whose total is lower
at a given albedo needs less absorption to reach it. `sslt-reference.py`'s MEDIA lists them for the five reference
materials; the walks below are made in each profile's own medium (four media from one set of walks: flights scatter
only and each medium weighs a walk by e^(−σa·length), the analog walk's expectation for it; its classic medium matches
the earlier walk images to 0.1% in every run's channel means, χ² per pixel 0.88 to 1.05).

| reduced albedo, R G B | classic table | 2 c1 dipole | better dipole | beam diffusion |
|---|---|---|---|---|
| red | 0.9792, 0.5204, 0.4069 | 0.9934, 0.5637, 0.4388 | 0.9937, 0.7212, 0.6021 | 0.9949, 0.7782, 0.6945 |
| high albedo | 0.99885 | 0.99971 | 0.99968 | 0.99985 |
| wax | 0.9533, 0.8764, 0.4553 | 0.9811, 0.9316, 0.4926 | 0.9831, 0.9500, 0.6356 | 0.9859, 0.9572, 0.7177 |
| bark | 0.9067, 0.8497, 0.7647 | 0.9507, 0.9065, 0.8269 | 0.9595, 0.9302, 0.8800 | 0.9657, 0.9407, 0.8992 |
| clay | 0.9776, 0.9225, 0.7647 | 0.9912, 0.9613, 0.8269 | 0.9913, 0.9671, 0.8800 | 0.9931, 0.9723, 0.8992 |

**Half-space**, light and view along the normal, multiple scattering. In the walks' earlier (classic) medium, each
profile's head-on pixel (Ft² times its total) over the photons' (`Mode=forward`, plane integrals):

| profile | high albedo | red: R | G | B |
|---|---|---|---|---|
| classic dipole (method 1, method 2 before) | 1.061 | 1.388 | 3.93 | 5.07 |
| dipole with 2 c1 | 0.963 | 1.052 | 3.43 | 4.53 |
| better dipole | 0.968 | 0.992 | 2.01 | 2.67 |
| beam diffusion | 0.933 | 0.936 | 1.151 | 1.220 |

Method 2 as each build renders it (Ft² times the colour, which the head-on slabs match to within 1.8%) over 2 million
photons in the medium that build derives:

| build | high albedo | red | wax | bark | clay |
|---|---|---|---|---|---|
| classic Fdr (before) | 1.060 | 1.389, 3.90, 5.08 | 1.548, 1.92, 4.29 | 1.724, 1.94, 2.27 | 1.346, 1.66, 2.27 |
| 2 c1 dipole | 0.957 | 0.984, 3.12, 4.18 | 1.047, 1.25, 3.45 | 1.168, 1.32, 1.60 | 1.017, 1.13, 1.60 |
| better dipole | 0.963 | 0.976, 1.38, 1.69 | 1.008, 1.04, 1.62 | 1.057, 1.09, 1.14 | 1.014, 1.04, 1.14 |
| beam diffusion | 0.929 | 0.937, 1.004, 1.051 | 0.950, 0.957, 1.061 | 0.975, 0.983, 0.996 | 0.959, 0.973, 0.996 |

**Scores** (`sslt-reference.py score`, 160×120, as in the table of the subsurface reference section): before = the
classic-Fdr head against the classic walks (that table's numbers); each later build against walks in its own medium,
1024 walks per pixel (noise 1.2 to 3.0 levels, subtracted from the rms). Absolute rms / mean / worst 8×8 block:

| Run | before | 2 c1 dipole | better dipole | beam diffusion (shipped) | beam diffusion / walks, R, G, B |
|---|---|---|---|---|---|
| slab, red | 35.0 / +34.5 / +39 | 28.0 / +22.1 / +29 | 9.6 / +6.5 / +8 | 4.5 / −3.7 / −4 | 0.925, 0.933, 0.967 |
| slab, high albedo | 7.4 / +7.4 / +8 | 3.9 / −3.8 / −6 | 3.3 / −3.2 / −6 | 6.7 / −6.7 / −9 | 0.938 |
| slab, wax | 34.9 / +34.7 / +38 | 20.2 / +15.2 / +12 | 7.7 / +3.4 / +1 | 5.4 / −4.7 / −7 | 0.925, 0.913, 0.968 |
| slab, bark | 34.4 / +34.3 / +37 | 14.7 / +13.5 / +14 | 2.2 / +1.4 / +2 | 5.2 / −5.1 / −6 | 0.928, 0.924, 0.927 |
| slab, clay | 30.6 / +30.1 / +33 | 12.4 / +8.6 / +7 | 2.2 / +0.9 / +2 | 4.7 / −4.6 / −6 | 0.950, 0.938, 0.931 |
| slab head on, red | 35.8 / +35.1 / +38 | 28.9 / +22.9 / +28 | 10.8 / +7.6 / +9 | 4.1 / −2.4 / −3 | 0.927, 0.975, 1.016 |
| slab head on, high albedo | 6.0 / +5.9 / +7 | 4.1 / −4.1 / −6 | 3.6 / −3.6 / −5 | 6.9 / −6.9 / −9 | 0.936 |
| V-groove and dimple, wax | 34.0 / +33.6 / +41 | 20.4 / +15.6 / +17 | 8.6 / +2.9 / −14 | 6.5 / −4.6 / −19 | 0.932, 0.896, 0.969 |
| V-groove and dimple, high albedo | 23.1 / +22.6 / +35 | 7.8 / +6.2 / +20 | 6.3 / +4.6 / +16 | 4.6 / +1.7 / +14 | 1.017 |
| ridged trunk, bark | 25.4 / +21.8 / +32 | 13.4 / +6.1 / −19 | 9.9 / −2.5 / −18 | 12.1 / −7.5 / −22 | 0.849, 0.865, 0.895 |
| 1.2 mm wall, clay | 68.1 / +59.5 / +74 | 59.3 / +47.7 / +63 | 57.9 / +44.0 / +63 | 56.0 / +39.6 / +60 | 3.327, 1.932, 0.858 |
| 1.2 mm wall, high albedo | 99.9 / +98.7 / +112 | 87.4 / +86.2 / +102 | 88.1 / +86.6 / +105 | 84.3 / +82.9 / +101 | 2.84 |
| bare studs slab, red | 38.7 / +37.1 / +43 | 30.5 / +28.6 / +34 | 15.1 / +12.0 / −14 | 12.5 / +2.9 / −24 | 1.258, 0.889, 0.946 |
| bare studs slab, high albedo | 44.0 / +42.0 / +50 | 28.7 / +26.8 / +35 | 26.5 / +24.9 / +34 | 24.0 / +22.3 / +31 | 1.332 |
| studs, red | 35.6 / +32.5 / +46 | 28.4 / +24.8 / +39 | 16.8 / +9.6 / +22 | 16.3 / +1.1 / −22 | 1.268, 0.821, 0.874 |
| studs, high albedo | 44.5 / +40.9 / +63 | 31.5 / +27.2 / +48 | 30.2 / +25.4 / +48 | 28.2 / +23.0 / +46 | 1.386 |

Geometry (scaled by each build's own slab ratio; the studs by the bare slab's), rms / mean / worst block:

| Run | before | 2 c1 dipole | better dipole | beam diffusion |
|---|---|---|---|---|
| slabs, the five materials | 0.5 to 1.0 | 0.8 to 1.0 | 0.7 to 0.9 | 0.6 to 0.9 |
| V-groove and dimple, wax | 4.3 / −0.3 / −17 | 4.0 / +0.7 / −14 | 3.8 / −0.5 / −14 | 3.7 / −0.1 / −13 |
| V-groove and dimple, high albedo | 17.1 / +16.3 / +29 | 10.5 / +9.4 / +23 | 8.4 / +7.3 / +18 | 8.3 / +7.2 / +18 |
| ridged trunk, bark | 11.0 / −3.5 / −26 | 11.0 / −3.8 / −26 | 10.0 / −3.5 / −18 | 9.9 / −3.6 / −19 |
| 1.2 mm wall, clay | 53.8 / +32.8 / +49 | 58.8 / +39.0 / +59 | 58.2 / +42.9 / +63 | 58.2 / +43.2 / +63 |
| 1.2 mm wall, high albedo | 95.3 / +94.1 / +107 | 89.8 / +88.5 / +104 | 90.1 / +88.6 / +107 | 88.4 / +87.0 / +105 |
| bare studs slab, red | 14.5 / +5.5 / −23 | 16.8 / +7.5 / −22 | 13.9 / +5.6 / −19 | 14.6 / +5.9 / −21 |
| bare studs slab, high albedo | 38.8 / +36.6 / +44 | 31.3 / +29.5 / +38 | 28.7 / +27.2 / +36 | 28.5 / +27.0 / +36 |
| studs against bare, red | 14.4 / −2.6 / −25 | 13.8 / −2.3 / −25 | 13.1 / −1.9 / −21 | 13.1 / −2.0 / −23 |
| studs against bare, high albedo | 17.7 / +1.9 / −36 | 16.1 / +2.4 / −31 | 16.7 / +2.3 / −31 | 16.5 / +2.3 / −32 |

**Cost**, method 2 at 640×480 on the reference scene, CPU-s, mean of two interleaved renders, with
per-block heat maps for each:

| Run | classic Fdr (before) | 2 c1 dipole | better dipole | beam diffusion |
|---|---|---|---|---|
| studs, high albedo | 22.9 | 23.2 | 23.8 (+4%) | 26.0 (+13%) |
| ridged trunk, bark | 46.5 | 47.1 | 49.1 (+6%) | 51.6 (+11%) |
| 1.2 mm wall, clay | 10.2 | 10.0 | 10.6 (+4%) | 12.2 (+20%) |
| V-groove and dimple, wax | 13.6 | 13.4 | 14.1 (+4%) | 16.4 (+20%) |

The cloud is the same in every build but the trunk's (bark's longer diffusion sets coarser cells: 8 cells and 78877
points instead of 67 and 467170, for the same time); the added time is the profile's, spread over the subsurface
pixels in the heat maps with no hot spot (they are wall-clock per block, so they vary with the box's load). A first
beam table looked the radius up by its logarithm through a call into another file: +16 to +36%.

**Why beam diffusion.** The better dipole is closer on the high-albedo slabs (3.3 against 6.7 levels) and on bark and
clay, but it holds 1.2 to 1.7 times the light in the low-albedo channels (red's green and blue, wax's blue), a change of
hue, as the dipole with 2 c1 does at 2.8 to 4.2 times. Beam diffusion is within 0.90 to 1.02 of the walks in every
channel on the slabs and grooves (the trunk 0.85 to 0.90), and its error is nearly the same in every channel: a 3 to 9%
deficit on the slabs. That deficit is
the exit term rather than the profile: Rd counts light that has left the surface, and the renderer's directional term,
Ft(out) / π, carries only 1 − F̄dr of it from outside (0.908 at η 1.5, 0.923 at 1.4). `score --normalise-exit` divides
the renders by it, as pbrt's normalised term would: beam diffusion then scores 1.9, 3.5, 1.5, 0.9 and 1.9 on the five
slabs (ratios 0.998 to 1.065), 4.1 on the wax groove and 10.2 on the trunk, but 11.2 on the high-albedo groove, 35.0
on the high-albedo studs and 93.7 on the high-albedo wall, where the semi-infinite profile on a thin part already gives
too much. The renderer keeps Ft(out) / π here.

Thin parts are the largest error left: the 1.2 mm walls at 2.8 to 3.3 times the walks and the 4 mm studs slab at 1.33
(high albedo) are the semi-infinite profile on a part thinner than the diffusion, unchanged in kind by the profile.

Method 1 renders the same pixels, bit for bit, before and after: `version4_subsurface_{37,38,40}.pov` with methods 0 and
1 and `_no_version` at `+MV4.0`, `subsurface_leaf`, `subsurface_photons` (one thread; with several, its photon map
differs from run to run on one build), `subsurface_photon_prism`, and the reference scene's studs and trunk.

## Subsurface method 2 on smooth meshes

A cloud point stands for h²/|n|₁ of surface and is compared with its neighbours by its normal; both took the normal
the surface reported, which on a smooth mesh triangle is the one interpolated from its vertices, not the triangle's own.
A point on a face whose vertex normals lean away from it then stood for too little surface: on
`tests/render/subsurface_photon_flux.pov` (`Photons=0 Direct=1`, a light straight down), whose box mesh has a top of two
smooth triangles with vertex normals leaning up to 37° before the mesh's scale, the top came out 10% dark against the
same box with flat triangles, beyond the cosine its vertex normals give the light. (`doc/SUBSURFACE_PHOTONS.md` measured 18.7% on the same
face's photons, which then went to method 1.) Method 2 now takes the triangle's face normal for a point's side, area,
facing and hierarchy, and for the exit point's tangent plane, and the interpolated normal only for the cosine of the light
entering there, which the point keeps in its irradiance until it is lit (no memory). The coarse cloud's near-field
draws take the face normal for the area a draw stands for in the same way. Other shapes report their geometric normal
and are unchanged, bit for bit; method 1 is untouched.

Smooth top over flat top, mean of the central 32×24 of 64×48 pixels (the prediction: each pixel's cosine from its
vertex normals, through the mesh's scale, times the entry and exit Fresnel transmittance):

| | before | after | predicted |
|---|---|---|---|
| smooth / flat | 0.898 | 0.9965 | 0.993 |

`tests/render/subsurface_cloud.sh` checks the ratio. `tools/bench/sslt-reference.pov` takes `Smooth=1` for the ridged
trunk with its surface's own normals (`trunk-smooth-bark` in `sslt-reference.py`, scored against the same walks as the
flat trunk, which follow the smooth surface): 12.09 / −7.55 / −22.0 before and 12.06 / −7.51 / −22.4 after (geometry 9.91
and 9.89), as the 480 × 96 facets lean little against their vertices; the other runs render the same pixels. At
640×480 the smooth trunk takes 52.5 and 52.5 CPU-s before and 50.5 and 52.0 after (heat maps alike). Method 1 renders
the same pixels before and after: `version4_subsurface_{37,38,40}.pov` with methods 0 and 1 and `_no_version` at
`+MV4.0`, `subsurface_leaf`, `subsurface_photons` (one thread), `subsurface_photon_prism`, the flux scene's smooth mesh,
and the reference scene's studs and both trunks.

## Subsurface method 2 on thin parts

Beam diffusion, like the dipole, takes the medium under the exit point as a half-space. On a part thinner than the
diffusion, light that reaches the far side leaves there instead of diffusing back, and a layer thin against a reduced
mean free path passes much of its light straight through without scattering; method 2 summed the near face as a
half-space and added the far face's points by the profile at the straight distance through the wall. Light leaving the
lit face of a layer after two or more scatterings, from photons (`sslt-reference Mode=forward Thickness=`, 0.4 to 2
million each, in the medium method 2 derives, light along the normal), over the half-space's, against the model below:

| | 1.2 mm | 1.7 mm | 4 mm | 12 mm |
|---|---|---|---|---|
| high albedo, photons | 0.217 | 0.282 | 0.470 | 0.707 |
| model | 0.244 | 0.309 | 0.465 | 0.731 |
| clay R, G, B, photons | 0.206, 0.393, 0.827 | | 0.510, 0.790, 0.997 | |
| model | 0.242, 0.443, 0.834 | | 0.527, 0.744, 0.986 | |
| red R, photons / model | | | 0.514 / 0.525 | |

The model: the depth d of material under the exit point, along the inward normal to where the material ends (one ray
per shading point; a union's inner faces, inside on both sides, do not end it), τ = d in reduced mean free paths, and
Donner and Jensen's multipole for a layer τ deep with the better dipole's fluence and flux terms (extrapolated
boundaries 2AD out from both faces), from one source at depth min(1, τ/2) carrying the light that scatters in the
layer, 1 − e^(−τ). Its ratios to the better dipole at depth 1 — of Rd and of Rd's integral over a disc on the near face,
and of Rd on the far face over the dipole at the straight distance through the layer — multiply method 2's profile: a
point less than d/2 under the exit point's tangent plane by the first at its distance (unfolded where it is unfolded),
the core, disc and ring integrals by the second, and a deeper point (the far face) by the third at its distance along
the tangent plane. The ratios are tabulated in the beam table's layout (217 columns of squared optical radius), per
index, albedo (u to 1/4096) and quarter octave of τ from 2⁻¹⁰ to 2¹², interpolated between quarter octaves; images run
until σ·n·period passes 12, at most 64 a side, and a table is built on first use and shared by all
threads (within a cap; see **Subsurface method 2's tables**). A channel more than 12 reduced mean free paths and 10 diffusion lengths deep is left as it was (the multipole moves the light within the reach of a part that deep by at most 1e-4 of it, over albedos 0.5 to 0.999), so the deep
slabs score as before to within 0.03 levels. In the model a thin part's light falls off within a few depths along it instead of over
the diffusion length, so a finite wall no longer clips a profile that should reach past it.

**Stepped faces.** Two parallel faces at different heights counted each other where the material is inside under the
higher face at the lower one's height and at the midpoint between there and the lower point. Two blocks of one merge
with an air gap between them (`sslt-reference.pov Shape=4 Blocks=1 View=3`, `gap-red` and `gap-high`, scored against new
walks) passed that test wherever the midpoint fell inside the taller block, so the taller block's top lit the lower
block's top across the gap. From the lower face the material must now run all the way from under the exit point to under
the higher point, at a quarter spacing deep: runs to where it leaves the object along 8 directions of the tangent plane,
taken once for a shading point that meets such a pair, interpolated as 1/run between directions (a straight edge at
distance a lies a / cos θ away). The lower top's render/walks ratio 2 to 8 pixels beside the gap against 30 to 45
pixels along it:

| | high albedo | red |
|---|---|---|
| before | +46% | +24% |
| thin parts, midpoint test | +29% | +16% |
| thin parts, material runs | −4.0% | −0.8% |

What is left there is a strip one or two pixels wide, the taller block's side above the gap (+31 levels at its worst
pixel; the worst block, +27 here and +34 with the exit term below): a crease from it to the lower top passes both corner tests, which land in
the taller block.

**Scores** (`sslt-reference.py score`, 160×120, 1024 walks per pixel in the beam-diffusion medium, noise subtracted),
absolute rms / mean / worst 8×8 block, before = the head after the smooth-mesh change (identical to the profile
section's beam diffusion column but for the smooth trunk); the gap runs take the bare slab's scale for geometry:

| Run | before | thin parts |
|---|---|---|
| slab, red | 4.5 / −3.7 / −4 | 4.5 / −3.7 / −4 |
| slab, high albedo | 6.7 / −6.7 / −9 | 6.8 / −6.7 / −9 |
| slab, wax | 5.4 / −4.7 / −7 | 5.4 / −4.7 / −7 |
| slab, bark | 5.2 / −5.1 / −6 | 5.2 / −5.1 / −6 |
| slab, clay | 4.7 / −4.6 / −6 | 4.7 / −4.6 / −6 |
| slab head on, red | 4.1 / −2.4 / −3 | 4.1 / −2.4 / −3 |
| slab head on, high albedo | 6.9 / −6.9 / −9 | 6.9 / −6.9 / −9 |
| V-groove and dimple, wax | 6.5 / −4.6 / −19 | 6.5 / −4.6 / −19 |
| V-groove and dimple, high albedo | 4.6 / +1.7 / +14 | 4.7 / −2.2 / −13 |
| ridged trunk, bark | 12.1 / −7.5 / −22 | 12.1 / −7.5 / −22 |
| the same, smooth normals | 12.1 / −7.5 / −22 | 12.1 / −7.5 / −22 |
| 1.2 mm wall, clay | 56.0 / +39.6 / +60 | 7.6 / −2.4 / −9 |
| 1.2 mm wall, high albedo | 84.3 / +82.9 / +101 | 7.0 / −5.0 / −13 |
| bare studs slab, red | 12.5 / +2.9 / −24 | 8.2 / −6.2 / −23 |
| bare studs slab, high albedo | 24.0 / +22.3 / +31 | 12.3 / −11.1 / −21 |
| studs, red | 16.3 / +1.1 / −22 | 10.3 / −5.5 / −21 |
| studs, high albedo | 28.2 / +23.0 / +46 | 8.5 / −2.2 / −22 |
| blocks with an air gap, red | 14.0 / +2.4 / −24 | 8.8 / −6.2 / −23 |
| blocks with an air gap, high albedo | 25.3 / +21.7 / +53 | 10.3 / −5.9 / +27 |

Geometry, scaled by each build's own slab ratio (the studs and gaps by the bare slab's):

| Run | before | thin parts |
|---|---|---|
| slab, red | 0.6 / +0.0 / +1 | 0.6 / +0.0 / +1 |
| slab, high albedo | 0.9 / +0.0 / −2 | 0.9 / +0.0 / −2 |
| slab, wax | 0.7 / +0.0 / +1 | 0.7 / +0.0 / +1 |
| slab, bark | 0.8 / +0.0 / +2 | 0.8 / +0.0 / +2 |
| slab, clay | 0.6 / +0.0 / +1 | 0.6 / +0.0 / +1 |
| slab head on, red | 1.5 / +1.2 / +2 | 1.5 / +1.2 / +2 |
| slab head on, high albedo | 0.7 / −0.1 / −2 | 0.7 / −0.1 / −2 |
| V-groove and dimple, wax | 3.7 / −0.1 / −13 | 3.6 / −0.1 / −13 |
| V-groove and dimple, high albedo | 8.3 / +7.2 / +18 | 5.3 / +3.4 / +14 |
| ridged trunk, bark | 9.9 / −3.6 / −19 | 9.9 / −3.6 / −19 |
| the same, smooth normals | 9.9 / −3.6 / −19 | 9.9 / −3.6 / −19 |
| 1.2 mm wall, clay | 58.2 / +43.2 / +63 | 7.0 / +1.2 / +7 |
| 1.2 mm wall, high albedo | 88.4 / +87.0 / +105 | 5.0 / −0.9 / −9 |
| bare studs slab, red | 14.6 / +5.9 / −21 | 5.7 / −3.2 / −20 |
| bare studs slab, high albedo | 28.5 / +27.0 / +36 | 8.2 / −6.4 / −16 |
| studs, red | 13.1 / −2.0 / −23 | 8.9 / −0.0 / −15 |
| studs, high albedo | 16.5 / +2.3 / −32 | 11.3 / +7.9 / +19 |
| blocks with an air gap, red | 10.6 / −0.9 / −24 | 5.8 / −0.6 / −17 |
| blocks with an air gap, high albedo | 13.7 / −0.2 / +35 | 9.5 / +4.7 / +35 |

**Cost**, 640×480, CPU-s of two interleaved renders each, with per-block heat maps:

| Run | before | thin parts and exit term | |
|---|---|---|---|
| studs, high albedo | 26.1, 25.6 | 29.7, 29.5 | +15% |
| ridged trunk, bark | 52.8, 51.8 | 55.8, 56.7 | +7% |
| 1.2 mm wall, clay | 12.0, 12.3 | 14.2, 14.1 | +16% |
| V-groove and dimple, wax | 16.3, 16.0 | 18.0, 17.9 | +11% |
| blocks with an air gap, high albedo | 13.0, 13.2 | 14.3, 14.6 | +10% |

The added time is the ratio lookups at every point summed (one table read per channel, shared between equal channels),
the depth ray and, on stepped faces, the 8 runs; the heat maps spread it over the subsurface pixels.

Method 1 renders the same pixels, bit for bit, before and after (the list in the smooth-mesh section).

## Subsurface method 2's exit term

Rd counts all the light that leaves the surface, while the directional term Ft(out) / π carries only 1 − F̄dr of it
from outside, η² (1 − 2 c1): 0.908 at η 1.5, 0.923 at 1.4 (see **Subsurface method 2's profile**). Dividing by it, as
pbrt's normalised term does, brought the slabs to within 1 to 7% of the walks, but made the thin parts worse while the
half-space profile still gave them too much (rms 11.2 on the high-albedo groove, 35.0 on the studs, 93.7 on the wall).
With the thin parts' layer model it no longer does, so method 2 now takes Ft(out) / (π (1 − F̄dr)) for its diffuse term,
the minimum-influence estimate and uniform emission; single scattering keeps Ft(out). A thick flat piece now reflects its
colour times the light entering it in all, as its albedo inversion intends, so seen head on it is about a tenth brighter
than with method 1, which keeps Ft(out) / π. Absolute rms / mean / worst block:

| Run | before, exit term alone | thin parts | thin parts and exit term |
|---|---|---|---|
| slab, red | 1.9 / +1.7 / +3 | 4.5 / −3.7 / −4 | 1.9 / +1.7 / +3 |
| slab, high albedo | 3.5 / +3.4 / +5 | 6.8 / −6.7 / −9 | 3.5 / +3.3 / +5 |
| slab, wax | 1.5 / +0.9 / +2 | 5.4 / −4.7 / −7 | 1.5 / +0.9 / +2 |
| slab, bark | 0.9 / +0.3 / +2 | 5.2 / −5.1 / −6 | 0.9 / +0.3 / +2 |
| slab, clay | 1.9 / +1.5 / +3 | 4.7 / −4.6 / −6 | 1.9 / +1.5 / +3 |
| slab head on, red | 3.1 / +2.9 / +4 | 4.1 / −2.4 / −3 | 3.1 / +2.9 / +4 |
| slab head on, high albedo | 3.3 / +3.2 / +5 | 6.9 / −6.9 / −9 | 3.3 / +3.2 / +5 |
| V-groove and dimple, wax | 4.1 / +0.8 / −13 | 6.5 / −4.6 / −19 | 4.0 / +0.8 / −13 |
| V-groove and dimple, high albedo | 11.2 / +10.3 / +22 | 4.7 / −2.2 / −13 | 7.6 / +6.3 / +17 |
| ridged trunk, bark | 10.2 / −3.6 / −19 | 12.1 / −7.5 / −22 | 10.2 / −3.6 / −19 |
| the same, smooth normals | 10.2 / −3.6 / −20 | 12.1 / −7.5 / −22 | 10.2 / −3.6 / −20 |
| 1.2 mm wall, clay | 61.2 / +45.7 / +66 | 7.6 / −2.4 / −9 | 7.7 / +2.3 / +9 |
| 1.2 mm wall, high albedo | 93.7 / +92.3 / +110 | 7.0 / −5.0 / −13 | 5.3 / +1.1 / −8 |
| bare studs slab, red | 15.8 / +7.6 / −21 | 8.2 / −6.2 / −23 | 5.4 / −1.9 / −19 |
| bare studs slab, high albedo | 31.9 / +30.5 / +40 | 12.3 / −11.1 / −21 | 6.9 / −4.3 / −14 |
| studs, red | 18.4 / +5.4 / −20 | 10.3 / −5.5 / −21 | 9.2 / −1.5 / −18 |
| studs, high albedo | 35.0 / +30.6 / +54 | 8.5 / −2.2 / −22 | 9.6 / +4.4 / +16 |
| blocks with an air gap, red | 16.8 / +6.9 / −21 | 8.8 / −6.2 / −23 | 6.5 / −2.1 / −20 |
| blocks with an air gap, high albedo | 32.5 / +29.6 / +61 | 10.3 / −5.9 / +27 | 8.7 / +0.9 / +34 |

Against the thin parts alone it is better on every run but three: the high-albedo groove (4.7 to 7.6; the layer model left it at 0.97 of the walks,
the exit term puts it at 1.07), the high-albedo studs (8.5 to 9.6, at 1.07) and the clay wall (7.6 to 7.7). The
renderer over the walks, per channel:

| Run | thin parts | thin parts and exit term |
|---|---|---|
| slab, red | 0.925, 0.933, 0.967 | 1.019, 1.028, 1.065 |
| slab, high albedo | 0.938, 0.937, 0.937 | 1.032, 1.032, 1.032 |
| slab, wax | 0.925, 0.913, 0.968 | 1.010, 0.998, 1.057 |
| slab, bark | 0.928, 0.924, 0.927 | 1.005, 1.001, 1.005 |
| slab, clay | 0.950, 0.938, 0.931 | 1.029, 1.016, 1.009 |
| slab head on, red | 0.927, 0.975, 1.016 | 1.021, 1.073, 1.118 |
| slab head on, high albedo | 0.936, 0.936, 0.936 | 1.031, 1.031, 1.031 |
| V-groove and dimple, wax | 0.930, 0.897, 0.969 | 1.015, 0.980, 1.058 |
| V-groove and dimple, high albedo | 0.974, 0.974, 0.974 | 1.072, 1.072, 1.072 |
| ridged trunk, bark | 0.850, 0.865, 0.895 | 0.920, 0.937, 0.970 |
| the same, smooth normals | 0.851, 0.866, 0.896 | 0.922, 0.938, 0.970 |
| 1.2 mm wall, clay | 1.042, 0.971, 0.885 | 1.129, 1.052, 0.959 |
| 1.2 mm wall, high albedo | 0.928, 0.927, 0.924 | 1.022, 1.020, 1.017 |
| bare studs slab, red | 0.861, 0.856, 0.945 | 0.948, 0.942, 1.040 |
| bare studs slab, high albedo | 0.858, 0.858, 0.858 | 0.945, 0.945, 0.944 |
| studs, red | 0.919, 0.827, 0.879 | 1.012, 0.911, 0.968 |
| studs, high albedo | 0.970, 0.969, 0.969 | 1.068, 1.067, 1.067 |
| blocks with an air gap, red | 0.870, 0.842, 0.929 | 0.958, 0.927, 1.023 |
| blocks with an air gap, high albedo | 0.921, 0.919, 0.918 | 1.015, 1.012, 1.011 |

Method 1 renders the same pixels, bit for bit (the list in the smooth-mesh section). The scale is one multiply per shading
point; the cost is that of the thin parts' section.

## Subsurface method 2's tables

Method 2's boundary (beam table, Fresnel moments, albedo inversion: 222 KB, about 0.07 CPU-s to build) was kept for every
distinct single-precision index, and a thin part's layer table (6944 bytes) for every albedo step and quarter octave of
depth it met, both for the life of the process and built under a lock all threads shared. Neither had a bound: a pigment
that varies continuously over a thin part asks for a table at nearly every shading point (up to 7094 albedo steps by 88
depths per index, 4.3 GB), and media refraction (`refraction` in an interior's media) varies the index with position, so
a boundary was built at nearly every shading point.

Both now live in the render's subsurface cache, within a fixed 32 MB (`kSubsurfaceTablesMB`) that is not counted against
the clouds' `memory`: past it the least recently used are dropped. A table is a function of its key alone, so dropping
and rebuilding one changes no value, and the image stays independent of thread count and of what the cache held. Tables
are built outside the cache's lock, which is held only to look up and insert (a thread that built one another thread
inserted first discards its copy). Each thread keeps the last 32 layer tables it used, and its current boundary, so most
lookups take no lock; those, and the tables a shading point is using, stay alive after the cache drops them, under 1 MB a
thread. The boundary is built for the index rounded to 12 bits after its leading one (a step of 2⁻¹², 1/4096 between 1 and
2): 1.5 is unchanged, 1.45 and 1.4 move by 4.9·10⁻⁵ and 9.8·10⁻⁵. The render statistics give the boundaries and layer
tables built, those dropped and the most alive at once (table bytes, with any a thread still holds).

`tools/bench/sslt-tables.pov` is the load: Case 0 a wedge 0.2 to 7.9 mm deep (ior 1.45) whose colour runs along it, at
160×120; Case 1 a sphere of ior 1.4 whose media add `refraction 0.2` across it, at 64×48. CPU-s and peak resident memory
of the whole render (one run each), table bytes from the statistics:

| | before | layer tables capped | and boundaries |
|---|---|---|---|
| wedge, CPU-s / peak RSS | 7.41 / 208 MB | 8.24 / 99 MB | 9.09 / 100 MB |
| wedge, tables built / dropped / peak alive | | 26276 / 21444 / 32.05 MB | 2 + 26338 / 21508 / 32.26 MB |
| varying index, CPU-s / peak RSS | 61.3 / 258 MB | 66.0 / 261 MB | 33.2 / 101 MB |
| varying index, tables built / dropped / peak alive | | 2724 / 0 / 18.0 MB | 492 + 1478 / 1429 / 32.68 MB |

Before, the wedge held about 109 MB more (by peak RSS against the capped build) and the sphere a boundary per shading
point. Capping the layer tables alone renders the wedge, which drops 21444 of them, the same pixels; with the index
rounded the sphere is within 0.0078 levels of the exact index everywhere (rms 0.0010), and the wedge, at ior 1.45, rms
0.052 with 97 pixels over half a level (at most 2.86), all in a band at its dark end, consistent with where a channel's
depth passes the thick-part threshold of the thin parts' section and the profile switches from the layer to the
half-space, which the index's shift moves by under a pixel.

The reference runs (`sslt-reference.py`, 160×120, as scored above): with the layer tables capped all 19 render the same
pixels; with the index rounded too the 12 at ior 1.5 do, and the 7 at 1.45 and 1.4 (wax, bark, clay) differ by rms 0.0015
to 0.0024 levels, mean within 0.0015, at most 0.20 at a pixel, which bounds each score's change (rms and mean by those,
the worst block by 0.2), so the scores in the sections above stand as printed. These scenes build one boundary and 4 to 19 layer tables,
0.34 to 0.45 MB, as before. At 640×480, CPU-s of two interleaved renders each, with per-block heat maps:

| Run | before | after |
|---|---|---|
| studs, high albedo | 29.3, 29.0 | 29.4, 30.1 |
| 1.2 mm wall, clay | 14.1, 14.3 | 14.1, 14.3 |
| blocks with an air gap, high albedo | 14.6, 14.3 | 14.9, 14.5 |
| V-groove and dimple, wax | 18.1, 18.3 | 18.0, 18.3 |

`tests/render/subsurface_tables.sh` renders the wedge at 80×60 on one thread and two: the tables must pass their cap,
stay within it but for what the threads hold, and the two images must match.

**A channel that reflects nothing.** A colour channel of reflectance 0 (`pigment { rgb <1, 0, 0> }` with any `diffuse`)
inverts to a reduced albedo of 0, so the reduced extinction, scattering over albedo, is infinite and the profile NaN;
clipped, the channel came out at full brightness on every pixel of the object, with methods 1 and 2 alike, in every build
since the cloud (a red sphere, 64×48, `diffuse` 0.6 and 1: green and blue 255 on all 1848 of its pixels). Method 2 now
gives a channel a reduced albedo of at least 10⁻⁴ (`kSubsurfaceAlbedoFloor`): the extinction stays finite (10⁴ times the
scattering), the channel scatters next to nothing, and only channels the colour table would put below 10⁻⁴ change. The
sphere's green and blue are now 0 on every pixel, and its red has a mean of 113.64 and 137.44 levels against 113.61 and
137.40 for `rgb <1, 0.002, 0.002>` (rms 1.17 and 1.07 between them, at most 18 levels at a pixel). A colour without a
zero channel renders the same pixels as before. Method 1 is left as it was.

The other end, a channel of albedo 1, has no absorption, so its depth never passes the thick-part test and every such
part takes the layer model; its table stops at τ = 2¹², so a deeper part reads a layer 2¹² deep. An albedo just below 1
does the same until σ·τ passes 15, so this is not a case of its own. Against a layer 10⁴ deep (the table's formulas at
η 1.5), the 2¹² layer's near-face Rd ratio is within 0.04% out to 362 reduced mean free paths, 0.8% low at 1024 and 15%
at 2896; its disc integral is within 10⁻⁴ out to 2896, and
its ratio through to the far face 2.6% low. It is left as it is.

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

A declaration records a deferred mesh without building it. Copying, instancing, transforming and texturing it do not
build it. Operations that actually inspect geometry (`trace`, `inside`, extents, mesh cameras, and bounding, clipping
or object-pattern objects) build it on the parser thread; otherwise a mesh is built after parsing, once the tag
filters show some view keeps an object using it (see [render tags](render-tags.md#phases-and-deferred-meshes)), unless
[eager preparation](#eager-preparation) starts it earlier on a spare thread while the parser runs. A
pending mesh's box is a placeholder, so each compound holding one is rebounded from scratch once the mesh resolves;
tracing never checks for pending meshes. Diagnostics are emitted in declaration order. Each generated build gets a
frozen function-VM snapshot, so later function declarations and assignments neither race nor wait for it. Reusing a
declaration shares its completed mesh data; redeclaring it creates a distinct task and leaves earlier instances
attached to the earlier result.

One isolated run of `tools/bench/parse-mesh-generation.pov`, native optimized build:

| Fixture | Work | `+WT1` wall / CPU | `+WT4` wall / CPU | Wall difference |
|---|---:|---:|---:|---:|
| eight skein tori, `max_angle 1` | 8 × 262,144 triangles | 5.649 / 5.695 s | 1.663 / 6.235 s | -70.6% |
| eight noise-displaced isosurfaces | 8 × 135,734 triangles | 15.463 / not recorded | 15.780 / not recorded | +2.0% |

The isosurface fixture saturated the test host: four concurrent builds each slowed by about four times and
used 267 MB peak instead of 119 MB, so concurrency did not reduce latency there. The skein fixture scaled strongly.
A second run of the isosurface fixture took the eight builds from 17.212 s wall at `+WT1` to 3.621 s at `+WT8` (-79.0%;
CPU 17.215 and 25.389 s), so whether concurrent isosurface builds help depends on the host.
This is workload- and machine-dependent; `+WT` controls the memory/throughput tradeoff, while `+WT1`
retains serial construction.

The render checks build the same triangle and vertex counts at one and four threads and repeat their ray, normal,
inside and chord-error comparisons. The skein check also builds an image-sampled mesh. A separate fixture instances
pending meshes, redeclares the same name with changing geometry options, and observes distinct 262,144- and
131,072-triangle results. `.povm` checks cover the final barrier and an immediate `max_extent` barrier.

Other ordinary `mesh`/`mesh2` syntax remains on the parser thread because token consumption, texture ownership and
vertex hashing are interleaved. Image and font loading are also outside this pool.

A `skein_mesh` skips the per-patch interval ranges, flatness and bounding tree that only ray tracing a `skein` reads;
it keeps the grid size, which sets its base lattice, so the triangles are unchanged. The rest of the preparation still
runs on the parser thread, before the deferred build. On the displaced tube in `tools/bench/parse-mesh-generation.pov`
(`MeshKind=3 MeshAngle=9`, 529,540 triangles, `+WT1`) parsing drops from 44.8 to 22.0 G user cycles; a finer-relief
tube whose grid reaches the 256-patch limit but meshes to 70k triangles drops 6×, from 60.5 to 10.0 G.

With `global_settings { mesh_cache }` (doc/povm.md) a generated mesh is built once per build of POV-Ray and
read back afterwards. A 200-pixel render of a scene with two isosurface meshes (77k and 287k triangles) and twelve
skein meshes parses in 40.4 s uncached, 42.4 s when it fills the cache and 0.21 s from it, with identical pixels.

## Eager preparation

The parser runs on one thread, so the other `+WT` threads used to sit idle until the parse ended. Eager preparation
(`Eager_Preparation`, INI, on by default; `+WE`/`-WE`) hands them the deferred work as soon as it exists: a pool of
`+WT` - 1 threads (none at `+WT1`) builds each `isosurface_mesh`, `skein_mesh` and `mesh2 { povm }` as the parser
records it, and fills z slabs of a method 4 medium's density grid once its top-level container is complete. When the
parse ends, the mesh build phase finishes what is left on all `+WT` threads and hands the grids to the render.

- A build evaluates a snapshot of the parser's functions with the noise generator, working gamma and wave count
  frozen when it was queued. If it read one that a later `global_settings` changed, the thread that joins it builds it
  again, so images match eager preparation off. Functions that test an object cannot be snapshotted and wait for the
  end of the parse.
- A parse-time geometry query uses a finished build, waits for a running one or runs a queued one on the parser thread.
- With `Filter_Tags` on the command line or in an INI file, a mesh build is queued only when a kept object places the
  mesh. Filters set in the scene are known only at its end, so there every recorded mesh builds and the builds no
  prepared set keeps are discarded; a CPU-second or more of discarded builds raises a warning with the remedy (give the
  filter on the command line, remove unused declarations, or `-WE`).
- Eager grids are sized by the render's cell limit; a grid whose container bounds, cell limit or settings read changed
  later is left for the render to build as before. Eager preparation also fills the grids of kept containers that no
  ray reaches, which a lazy build skips. Optical-depth grids stay with the render.
- With it off, meshes are built after the parse (parse-time queries on the parser thread) and density grids on the
  first ray, as before.

Measured on a quiet host (load 3.6-4.5), 4 CPUs via `taskset`, `+WT4`, three runs each, eager preparation off against
on:

| Fixture | Wall | User-space instructions |
|---|---:|---:|
| `tools/bench/parse-mesh-overlap.pov`: eight `isosurface_mesh` declared before a parse-heavy macro loop | 8.3 → 6.7 s | 177.0 → 176.7 G |
| a 1M-cell method 4 density grid declared before a long parse loop | 6.0 → 4.3 s | +1.6% |

The parse itself takes as long beside the meshes and up to 7% longer beside the grid. Keeping one of the eight meshes
with a command-line filter costs +0.4% instructions; before builds were gated on placement it cost +193%, as the
unused builds were made and thrown away.

Limits: discarded builds cost CPU when the filter is only in the scene; meshes built side by side raise peak memory;
`+WT` above the CPUs available makes the eager threads compete with the parser.

`tests/render/eager_preparation.sh` compares eager preparation on and off at `+WT1`, `+WT4` and `+WT8`: the same
images and deferred-mesh counts, builds redone after late settings, discards, placement gating, the warning, `.povm`,
media grids, parse errors and stops.

## Classic parser: lex once

Each scene or include file is lexed once into an array of 40-byte tokens: reserved words resolved, texts kept once
per file, words' symbol-table hashes computed, numbers converted (with `strtod`, rejecting what `sscanf` rejected),
strings decoded. A bookmark is an index into that array, so `#while`, `#for`, macro calls and returns and a repeated
`#include` replay tokens instead of seeking the file and lexing it again; a macro keeps its file's array alive.
Lexing a file stops at 256k tokens or 16 MB (1M tokens or 64 MB per parse) or at the first lexeme it cannot
convert; from there the file is read from the scanner as before, macros declared there or running into it keep the
byte copy, a large text mesh costs a small prefix, and lexing errors surface where they did. Entries are keyed by
the file's canonical path and reused only while its size and modification time match, so a file opened for writing
under any spelling, or changed on disk, is lexed afresh at its next `#include`. The bounds count every array still
alive, including those macros hold; when they are reached, the least recently used arrays nothing else holds are
dropped first. The per-token accessors are inlined, and popping a macro's symbol table visits only the buckets it
used.

Medians of five runs per build, builds alternating, `+W8 +H6 -A +WT1`: user plus system CPU of the whole run from
`getrusage`, POV-Ray's parse time, and peak RSS. Instruction counts were not available.

| Scene | CPU before | lex once | all steps | Parse before → after | Peak RSS |
|---|---:|---:|---:|---:|---:|
| `tools/bench/parse-macro-loop.pov` | 1.174 s | 0.345 s | 0.234 s | 1.153 → 0.208 s | 15 → 15 MB |
| a 50,000-pass `#for` calling a macro from an include | 0.643 s | 0.235 s | 0.155 s | 0.632 → 0.149 s | 15 → 15 MB |
| `language/tracevines.pov` | 2.407 s | 1.482 s | 1.053 s | 2.342 → 1.013 s | 24 → 25 MB |
| `advanced/isocacti.pov` | 0.340 s | 0.291 s | 0.224 s | 0.290 → 0.171 s | 41 → 42 MB |
| `advanced/benchmark/benchmark.pov` | 1.622 s | 1.539 s | 1.485 s | 0.270 → 0.174 s | 22 → 22 MB |
| `advanced/landscape.pov` | 0.209 s | 0.170 s | 0.174 s | 0.174 → 0.148 s | 22 → 22 MB |
| `language/trace-wicker.pov` | 0.127 s | 0.091 s | 0.080 s | 0.082 → 0.046 s | 24 → 24 MB |
| `advanced/abyss.pov` | 0.201 s | 0.189 s | 0.190 s | 0.115 → 0.095 s | 50 → 50 MB |
| 59 MB text `mesh2` (`tools/bench/text-mesh.py 800 m.inc`) | 2.737 s | 2.729 s | 2.086 s | 2.785 → 2.096 s | 123 → 143 MB |

Before, most of a loop's cost was system time: every pass seeked the file and refilled a 64 KB buffer. Lexing once
alone left the text mesh no faster and 57 MB larger, which the bounds and `strtod` turned around.

## Ray size

`Ray` is copied whole for every reflected, refracted and transmitted ray, every shadow ray and each piece of a
shadow ray that crosses a surface, and every media and subsurface sample ray, and copies sit in the frames of the
recursive trace. Every byte a member adds is written and read again on each of those copies, in every scene,
whether or not the scene uses the feature it serves. So nothing joins `Ray` without counting instructions over the
whole scene set before and after.

| Build | `sizeof(Ray)` (x86-64, GCC) |
|---|---|
| POV-Ray 3.8 | 80 |
| ray differentials, path keys, media budget and light, prepared set | 224 |
| differentials flag and child counter packed beside the ray type bits | 208 |

What belongs elsewhere: state for one call, such as a query, a budget or a flag for one test, goes in an argument,
in `Trace` or in `TraceTicket`; state of a hit goes in `Intersection`. A member belongs in `Ray` only when the
copies made from a ray need it. A `static_assert` in `ray.h` fails the build on x86-64 GCC and Clang where `Ray`
grows past 208 bytes.

To measure the size, compile a probe in the build environment: `template<int N> struct SizeIs;` and
`SizeIs<sizeof(pov::Ray)> probe;` after including `core/render/ray.h` fail with the size in the message. To measure
the cost, count user-space instructions (Method, below) on the standard benchmark, a photon scene, a radiosity scene,
a media scene, a subsurface scene and a refraction scene, before and after.

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

- Media method 4: re-benchmark on dense, finely detailed and combined media (with area lights, radiosity, photons,
  subsurface), where classic sampling was impractical. The small fixtures above understate the gain: one check on a
  complex medium showed at least 500×, but it was not recorded with its scene and settings, so no figure is claimed.
  Compare against a classic render at a much higher sample count, with the error taken from that, and record
  resolution, memory and first-use construction cost too.
- Isosurfaces: vectorised noise, so that batches pay where functions are mostly noise; batches of rays rather than
  of points. An occupancy grid from max_gradient, skipping empty cells while root finding, was tried: it moved roots,
  gained 7 points only on a leaning trunk in a loose box and cost up to 6 on tighter ones; a tighter `contained_by`
  does better.
- Noise: 55% of the standard benchmark; AVX-512 or a vectorised octave loop.
- Media: extinction along shadow rays, 363 M density evaluations in the haze window, is still its largest cost;
  skipping any safely needs bounds on the density.
- Subsurface: method 2 samples single scattering from lights
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
