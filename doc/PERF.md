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
- Triangles: test a block's triangle leaves eight at a time.
- Height fields: their own block walk.
- A multi-occluder shadow cache saved 3% but changed shadows in ways not yet explained; it is not in this branch.
