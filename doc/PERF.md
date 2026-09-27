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
the sampled method above. Method 2 is smooth where method 1 is grainy, and faster; it is not bit-exact, and its
brightness is within a fraction of a level of method 1 at many samples (tables below).

**The cloud.** Space is cut into cubes whose side is a power of four at least the diffusion's reach (8 diffusion
lengths of the channel that diffuses farthest), so a texture that varies the diffusion uses few sizes. A cube's points
are built the first time a shading point needs it: lines along the three axes through a jittered grid cross the
object, and each crossing is a point standing for h²/|n|₁ of surface (h the grid step, n its normal), which gives one
point per grid square on a patch facing an axis and no clumps. All crossings along a line are collected by testing it
again past the farthest one found, since one test of a blob returns only the nearest interval. A point's normal is
turned to point out of the object, by testing which side of it is inside: primitives report normals with no regard to
a `difference` inverting them, and a mesh's follow its winding. Crossings whose sides cannot be told apart (open
shapes, parts thinner than about a tenth of the spacing) are dropped, and a cube that drops more than one in sixteen is
left to method 1. Points are lit once, four points of each area light apiece, and keep each light's shadow. The
spacing is a 128th of the cube's side, a sixteenth to a quarter of the diffusion length, or a pixel of the view's
camera where the cube comes nearest it if that is coarser, and never under a 1024th of the side. The camera is set
once for the render, so a cube comes out the same whichever thread builds it, from a camera ray, the radiosity
pretrace or a radiosity sample. Threads that need a cube while it is built take jobs from its building, so it does not
hold them up; an exception in any job leaves the cube unusable instead of holding up the threads waiting on it. A cube
of more than 262144 points is left to method 1, as are unbounded objects such as planes. A budget of about a million
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
keeps light from crossing gaps between parts of an object. Where the disc bends (edges, parts thinner than about a
spacing, tight curves), the ring holds under 0.75 times a flat ring's area (open borders) or holds a point the base
point sees from behind (creases), the shading point uses method 1. The disc and ring are sized by the coarsest cube
they reach, so where cubes of different spacing meet each still holds enough points. Diffusion shorter than a pixel is
taken as lit like the exit point.

**Single scattering.** A light in front of the surface whose shadow the disc agrees on needs no samples: on a flat
surface the path in from the light is a fixed multiple of the path out, which gives the sampled estimate's expectation
in closed form. Other lights (behind, or at shadow edges) are sampled as in method 1, with the shadow taken from
nearby points where they agree.

Results, `+WT4`, user-space cycles (on an otherwise idle machine; `tools/bench/pcount.c`), and error against method 1 with
many samples (16 times the diffuse samples at 320×240, 8 times at 800×600) in levels of 255, over the pixels that
subsurface light changes:

| Render | Method 1 | Method 2 | | Method 1 mean, rms | Method 2 mean, rms |
|---|---|---|---|---|---|
| `tools/bench/sslt-lamps.pov`, 320×240 | 11.7 Gcycles | 7.4 | 1.6× | −0.04, 2.88 | −0.17, 2.06 |
| the same at 800×600 | 71.9 Gcycles | 45.0 | 1.6× | −0.03, 2.92 | −0.21, 1.95 |
| `scenes/subsurface/subsurface.pov`, 320×240 | 48.5 Gcycles | 16.7 | 2.9× | −0.02, 1.22 | −0.06, 1.05 |
| the same at 800×600 | 306.6 Gcycles | 109.2 | 2.8× | −0.02, 1.24 | −0.05, 0.92 |

Over the whole image, `sslt-open.pov` Case 0 (the top of a slab) is 0.25 levels darker than the reference with rms 2.0
against method 1's 3.0; Case 2 (a clipped shell) is within 0.01 levels, rms 0.83 against 0.86. Shadow rays halve on
both bench scenes. Without subsurface light the scenes trace in 2.1 and 0.6 Gcycles at 320×240, so the subsurface part
goes from 9.6 to 5.3 Gcycles (1.8×) and from 47.9 to 16.1 (3.0×). Where it differs from the reference most, 8-pixel
blocks at 800×600 are 2 to 4 levels off: the rounded foot of the candle in `subsurface.pov`, beside its wax drip, and
on the wax sphere in `sslt-lamps.pov`; method 1's blocks are within 1.2.

Tried and not kept:

- Points where random lines cross the object: their clumps showed as faint mottling on wax and, summed raw near the
  exit point, as blotches.
- Summing the ring raw wherever its normals turn by more than 45°: mottling on the candle's rounded rim.
- One cloud per object laid out by the first shading point: a floor seen from close to far gets one spacing, and the
  first shading point's material fixes it for a texture that varies the diffusion.

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

`-ffast-math` needs `-fno-finite-math-only` after it or some intersections break.

## Forks worth pulling

Swept 2026-09-24: all 293 visible forks and the known derivatives.

| Source | What matters | Verdict |
|---|---|---|
| [EdgeOfAssembly/povray](https://github.com/EdgeOfAssembly/povray) | `d706eb11` bbox queue, `c62ff4bb` mesh queue ([#363](https://github.com/POV-Ray/povray/issues/363)) | in this branch |
| [wfpokorny/povray](https://github.com/wfpokorny/povray) branches | solver accuracy, blob accuracy, FS324 mesh2 fix, shadow-cache tolerance, media method 3 up to 5.5% | port as commits |
| yuqk (Pokorny, tarballs on news.povray.org) | sphere-sweep root fixes, solvers, mesh2, lathe/sor, radiosity background fix, AA method 3, image lookup speed, PGO recipe | port by subsystem |
| [LeForgeron/povray `hgpovray38`](https://github.com/LeForgeron/povray/tree/hgpovray38) | FS324 fix, radiosity load, text segfault; new patterns, sphere-sweep UV, NURBS | fixes yes, features selectively |
| upstream `release/v3.8.0` | spline duplicate fix `3b43b71a42`, TGA, OpenEXR 3 | merge |
| [PR #452](https://github.com/POV-Ray/povray/pull/452) AVX-512 noise | noise and turbulence | the benchmark scene is 55% noise; its XCR0 check is wrong |

## Next

- Isosurfaces: threaded dispatch or native code for the function interpreter, whose speed now depends on code
  placement; root finding that uses `max_gradient`.
- Noise: 55% of the standard benchmark; AVX-512 or a vectorised octave loop.
- Media: extinction along shadow rays, 363 M density evaluations in the haze window, is still its largest cost;
  skipping any safely needs bounds on the density.
- Subsurface: method 2 still hands edges, thin parts and creases to method 1, and samples single scattering from
  lights behind the surface.
- Radiosity: irradiance gradients (Ward and Heckbert, over a stratified gather) were tried. Against converged renders
  they cut the lawn's blurred error by a third at `error_bound 0.6` and the Cornell box's by 37% for 5–7% more
  cycles, and ended the grass's brightening, but left the patio unchanged, made a close-up of the large scene 14–25%
  worse at `count 30` (10% from the stratified gather alone) and no better at `count 60`. They are not in this branch.
- Triangles: eight-lane tests pay only where blocks open onto more leaves; test rays eight at a time instead.
- Height fields: their own block walk.
- Mesh memory: the tree is now 55% of a mesh; 8-bit blocks are the next saving.
- A multi-occluder shadow cache saved 3% but changed shadows in ways not yet explained; it is not in this branch.
