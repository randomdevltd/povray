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

Now each media sample tests one point of each area light. The points of a ray follow the R2 low-discrepancy sequence,
shifted at random per ray, so a ray's samples cover the whole light between them; a jittered light is sampled anywhere
in its cells, an unjittered one only at its grid points. Surface lighting is unchanged, bit for bit.

| Render | Before | After | |
|---|---|---|---|
| the window above, trace | 244.6 CPU-s, 1.66 G media samples, 44.1 M shadow rays | 13.1 CPU-s, 92 M, 2.45 M | 19× |
| the whole large scene at 232×133, same media | 7886 Gcycles, trace 2237 CPU-s | 730 Gcycles, trace 179 CPU-s | 12.5× trace |
| the same at `samples 8`, `aa_level 2` | 467 Gcycles, trace 99.6 CPU-s | 193 Gcycles, trace 22.3 CPU-s | 4.5× trace |
| the standard benchmark, 384×384 | 1116.4 Gcycles | 571.6 Gcycles | −49% |
| `tools/bench/media-shafts.pov`, 160×120, `+WT1` | 226.3 Gcycles | 9.2 Gcycles | 25× |

Gcycles include parsing, about 112 G for the large scene. At the same settings the images match within the renderer's
own run-to-run noise at `+WT4`: over the whole large-scene frame the mean difference is 0.19 levels with 1.8% of
pixels off by more than 2, where two runs of the same build differ by 0.19 and 2.2%. The standard benchmark's mean
brightness is unchanged (151.03 against 151.02) and its differences, in the clouds, are as large as between two of
its own runs after the change (0.35 against 0.44 levels).

Dropping the large scene's haze to `samples 8`, `aa_level 2` instead, as a cheaper setting, differs 2.6 times as much
(0.50 levels, 7.1% of pixels over 2, up to 74), concentrated in the sunlit haze.

`media-shafts.pov` is the hard case: dense haze entirely in the soft shadow of a slatted roof. Against a 200-sample
render of the old code, the old code at its 40 samples is off by 3.5 levels on average and 3.1 darker; the new code
at 40 samples by 10.2 and 8.0 darker, at 160 samples by 3.9 and 2.4 darker for a third of the old cost. The darkening
comes from `method 3`'s refinement reacting to the noisier samples: with `aa_level 1` the old and new means agree.
Scenes like it need more `samples` for the same noise, and still come out cheaper.

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
- Media: shadow rays through media still integrate at the media's full sample count, 41% of the haze window's trace
  after the change above; transmittance needs far fewer samples than in-scattering.
- Triangles: test a block's triangle leaves eight at a time.
- Height fields: their own block walk.
- A multi-occluder shadow cache saved 3% but changed shadows in ways not yet explained; it is not in this branch.
