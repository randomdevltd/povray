# Sphere-sweep instruction measurements

The fork point is `8554844ac`; its parent implementation is `232778164`.
Use separate checkouts: the generated counter source is diagnostic code, never a production build.

Build `tests/source/sphere_sweep_cost.cpp` with the same flags and libraries as
`tests/source/sphere_sweep_oracle.cpp` (the `check-prepared-sets.mk` target accepts
`PREPARED_SOURCE`, `PREPARED_OBJECT` and `PREPARED_BINARY`). Run the binary with
`10000` repetitions, alternating executables three times. It counts user-space
instructions only and rejects multiplexed measurements. The loop includes call
and intersection-stack draining overhead. There is no rendering or scene parsing
in this timed interval. Preparation (`SphereSweep::Compute`) is outside it.

The test has one segment, constant radius, and projected spine `(u,u)`, `0 <= u <= 1`.
Its depth is curved for cubic/B-spline tests. `ratio` is radius / projected chord
length (sqrt(2)). Classes are 0: a line farther than twice the radius but inside
the object box; 1: distance 1.01 radii; 2: distance 0.5 radii; 3: the same hit line pointing away from the whole segment
(a forced direct call, normally excluded by its box). Ordinary and precise
linear queries are separate. A box rejection never calls `All_Intersections`.

For render attribution, run `python3 tools/bench/sphere-sweep-counters.py REPO OUT`.
This emits old/new source and instrumented variants. In each isolated checkout,
replace only `source/core/shape/spheresweep.cpp` with its instrumented variant and
rebuild. Every thread owns an instruction counter. The reported body interval
covers `All_Intersections`; a separate interval measures classifier/bookkeeping
cost. Diagnostics print on process exit, including threads still owned by the
renderer. Render class 3 means the new reference solver finds an infinite-line
span, but the measured implementation returns no accepted crossing (behind the
origin, tolerance or clipping). Class 1 excludes class 3 and intersects only after
doubling every segment's radius polynomial; class 0 misses even that enlargement.
Class 2 is an accepted crossing. For variable radii this is a pointwise radius
ratio, not a fixed world-space distance. Classification is outside the body timer.
The reference classifier is fixed to `8554844ac` for both measured revisions.

Use `tests/scenes/sweep-cost-{dense,sparse}.pov`, `+W160 +H120 +BS4 +WT1 +PR -A -D`.
The scenes have 400 thin curved sweeps or three fat sweeps, a shadow-casting light
and a reflective floor. `Declare=ParseOnly=1` intentionally stops with `#error`
after parsing: this measures parsing, not subsequent preparation/BVH construction.
Use two pairs in opposite build order, with counters over every process/thread.
Compare decoded pixels. The public ordinary check is
`distribution/scenes/interior/media/light_intersections.pov` at 64x48 with the same
flags; it combines curved sweeps, overlapping emitting/scattering media, glass,
reflections and multiple lights.

`sphere-sweep-cost-results.json` contains the public measurements. Matrix columns
in `profile.classes` are class, calls, instructions, SweepEnds nodes, subdivisions,
SweepFurthest nodes, and segments. Counter-build attribution used GCC 15,
`-O3 -march=native -fno-finite-math-only -pthread`; installed production executables
were measured separately. Approximate body shares divide summed body counts by
whole-job instructions minus measured instrumentation intervals; small unmeasured
probe overhead remains (up to about three percentage points on the dense scene).
Do not substitute those shares for an exact production-build profile.

The conservative hull-plane prototype adds no preparation or persistent storage.
It changes only intersection work. At the same sampling, repeated full counter-build
runs fell by 8.17% (dense) and 0.84% (sparse); the ordinary scene fell by 4.97%.
All three decoded image pairs matched exactly. The 24-round random oracle passed
160,033 checks with zero failures. The scale-image suite's first case failed the
same existing brightness-ratio threshold on both builds (0.879787, minimum 0.9).
