# Replaying cubic control spheres at different scales

Build `sphere-sweep-file-oracle.cpp` with the flags/libraries used for
`tests/source/sphere_sweep_oracle.cpp`, using `check-prepared-sets.mk`'s
`PREPARED_SOURCE`, `PREPARED_OBJECT`, and `PREPARED_BINARY` variables.
The input is one `x y z radius` control sphere per line; `#` starts a comment.
For example, queue separate invocations at scales 1, 1000, and 0.001:

```
sphere-sweep-file-oracle tests/scenes/sweep-straight-controls.txt 1 128
```

The tool reuses the existing oracle, including its fixed random seed, long-double
reference spheres, precise crossing query, and buried-light exit checks for the
bare sweep and three CSG forms. Scale multiplies coordinates and radii before
construction; sweep tolerance remains the absolute length `1.5e-6`. The final line
reports accepted ray starts, checks, and failures. Failures count assertions,
not distinct rays; the same bad ray can fail several CSG forms. Keep the same ray
count and input for comparisons. It also runs 32 independent inside probes.

An SDL `trace()` test has additional absolute floors. Its ordinary depth admission
and box pretest use `MIN_ISECT_DEPTH=1e-4`, while the sweep uses `Depth_Tolerance`.
Thus tiny-scale SDL misses do not alone diagnose the polynomial solver. A precise
query removes those ordinary floors; the older solver still applies its sweep
tolerance, while `8554844ac` ignores it for precise boundary queries.

For the supplied public straight-segment fixture, 128 accepted starts / 661 checks
produce 37 failures on `232778164`, and zero on `8554844ac`, at each of scales 1,
1000, and 0.001. These are precise queries. The existing
`sphere_sweep_cubic_roots.pov` SDL `Check=1` test instead reports (missed, misplaced)
19/5 on the old build at 1 and 1000, versus 0/0 on the fixed build. At 0.001 it
reports 491/0 and 453/0 respectively because ordinary depth floors also apply.
Neither test enables antialiasing; the SDL checks use `+PR -A`.
