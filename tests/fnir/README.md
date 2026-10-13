# Function IR differential tests

Random classic-function scenes rendered by two builds, a reference and a candidate, to check that a
change to function lowering, the IR passes or the interpreter keeps every pixel, and to measure what it
costs. Neither script runs in `make check`: both compare against a reference build you supply.

- `gen.py SEED OUT.pov` writes one scene and prints its kind and the constructs it uses. Scenes mix
  user functions (named and `x,y,z` parameters, nested and repeated calls, calls in conditions),
  `select` with three and four arguments, comparisons, `min`/`max`, `sum`/`prod`, the maths built-ins,
  `functions.inc` wrappers, pattern functions and pigment, transform and spline functions with member
  access. The function is consumed by a pigment, an isosurface, a parametric, scalar media density, or
  media density evaluated through the batch entry (`pigment-batch`, odd seeds on one thread, even seeds
  on two). A seed always writes the same scene.
- `fuzz.sh BASE NEW SRCDIR FIRST LAST [JOBS]` renders seeds FIRST..LAST at 48x36 with both builds and
  compares the trailing pixel payloads of `+fp16` PPM output, never the encoded file. It prints `ok`,
  `DIFFER`, `CRASH<status>` or `TIMEOUT` per seed and exits 1 if any seed is not `ok`. Seeds the
  reference build cannot render within `BASE_TIMEOUT` seconds (40) are reported and skipped. Scenes,
  logs and the images of failing seeds stay in `$FNIR_OUT`.
- `count.sh BASE NEW SRCDIR FIRST LAST [JOBS]` counts the user-space instructions of each render on one
  thread with `ICOUNT` (build `tools/bench/icount.c`; needs Linux perf events), minus a 1x1 render of the
  same scene, so parsing is left out.
- `table.py COUNTS.tsv` prints, per kind, how many scenes got faster, stayed within 1% or got slower,
  the median and the worst change, and the aggregate change.

Example, comparing a build of the parent commit with the current one:

    tests/fnir/fuzz.sh ../parent/unix/povray unix/povray . 1 400 4
    cc -O2 -o icount tools/bench/icount.c
    ICOUNT=./icount tests/fnir/count.sh ../parent/unix/povray unix/povray . 1 400 4 > counts.tsv
    tests/fnir/table.py counts.tsv

A known false positive: `f_ridged_mf` builds its exponent array on its first call and keeps it, so a
scene that calls it again with more octaves reads past the array, and two builds with different heap
layouts can render it differently (seed 416). Check a `DIFFER` with `f_ridged_mf` calls of unequal
octaves by making the octaves equal.

Instruction counts repeat to within a few thousandths of a percent, so they need neither a quiet
machine nor repetitions; a run with several jobs counts the same as a serial one.
