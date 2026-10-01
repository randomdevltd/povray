# skein-lab notes

Throwaway previewer for the pipeline-of-maps surface grammar. Not the renderer.

Run: `sbx npm install`, `sbx node --test test/*.test.ts`, `sbx npx tsc --noEmit -p .`, `sbx node build.mjs`,
then `python3 -m http.server -b 127.0.0.1 PORT` from `dist/` and open `/?ex=torus&wire=1&normals=1`.

## Decisions where the model was silent

- **Op shape.** An op is `(prev: Surface) => Surface`; a surface is `(u, v) => Vec3`. `ops(...)` is
  composition, so a group is an op. Ops always evaluate pointwise: nothing is tabulated except path frames.
- **Sample inputs.** Every field gets a `Sample` with `u, v, x, y, z, p` and a lazy `n`. The normal is
  central differences on the previous stage (h = 1e-5) and is only computed if the function reads `n`,
  so `({ v }) => ...` never pays for it. Degenerate normals (poles) retry nudged toward the interior in v.
- **Orientation.** Normal = dP/du × dP/dv. The unit quad faces +z, and every fold maps +z to outward,
  so offsets read as "outward" all the way down.
- **What a fold reads.** A fold is a space warp, not a uv operation: it reads the incoming point's raw
  world coordinates in its frame (`along`, `axis`, `along × axis`) as (s, t, w). It does not read u or v.
  Default: axis y, along x, so the first fold sees the quad as s = u, t = v, w = 0.
- **Angle mapping.** theta = start + s · range, raw, no normalisation. A full 2π range only closes if the
  incoming extent in `along` is exactly 1. Scale beforehand with `fn` if it is not.
- **Placement.** Reading is relative to the world origin; `origin` only moves the output axis.
- **Fold of a fold.** Falls out of the warp rule: the second fold bends the first fold's 3D output. The
  first fold's radial position becomes the second fold's w, so torus = tube then bend, with no special case.
- **Radius vector.** `[radius, angleOffset, axialOffset]`; the polar section sees the *base* theta, so
  an angle offset rotates the whole section (that is what makes twist a modifier). Axial offset is added
  to the axial coordinate before placement, i.e. it is a path-parameter shift on a path axis.
- **Curve mode.** `curve(t, sample)` with t = theta / 2π, so a partial range takes part of the curve.
  w offsets along the curve's outward normal, which assumes the curve runs counter-clockwise.
- **Path axis.** The axial coordinate t is the path parameter. Frames: double-reflection RMF on a
  2048-sample table, lerped and re-orthogonalised against the live tangent. Zero angle is `up` (default +z)
  projected off the start tangent. A closed path (path(0) = path(1)) gets holonomy correction spread
  linearly in t; `twist` (radians over the whole path) is added the same way. `correct: false` disables it.
- **Topology is declared, not inferred.** `shape({ wrap, ends, grid }, ...ops)`. Ends apply at v = 0 and
  v = 1 and only matter when u wraps. Welding is by index; the seam-gap metric is measured on the unwelded
  grid so it still sees a mismatch.
- **Metrics.** Area excludes caps. Volume is the divergence sum over surface + cap fans and is reported
  only for closed results. Self-intersection is a 40 × 40 mesh, edge-through-triangle tests with strict
  interiors, skipping pairs that share a vertex index.

## Friction and breakage

- **Unit-extent assumption in fold.** Folding anything whose `along` extent is not 1 needs an explicit `fn`
  rescale first, and the frustum/log/column all start with `fn(({ x, y, z }) => [x, k * y, z])` just to set
  height. A `scale` op, or a fold that reads its angle from u, would read better.
- **Section + twist do not compose by name.** `fold({ radius: (c) => [hex(c), twist(c.v), 0] })` works but
  the user has to know the section helper takes the sample. A `section` / `twist` pair of fold options would
  read better than the positional 3-vector.
- **Positional 3-vector.** `[r, angle, axial]` is compact but not self-describing; nobody reading
  `[0.5 - 0.15 * v, 1.5 * PI * v, 0]` knows the slots without the doc.
- **Fold-of-fold is a space bend, not a surface bend.** Correct and composable, but it stretches: a tube of
  radius 0.5 bent at R = 2 has inner/outer circumference ratio 1.5 / 2.5. That is the torus, but a user
  expecting "bend preserving lengths" will be surprised.
- **Path holonomy at t = 1.** First cut wrapped t = 1 back to t = 0, which made the seam look closed even with
  correction off; the gap moved into the last cell instead. Fixed: only t outside [0, 1] wraps.
- **Spindle torus is not a crossing.** Planned as the deliberate self-intersection case, it reported 0 and
  that is right: a spindle torus only touches itself at the two axis points. Replaced by a tube of radius 0.9
  on the trefoil (strand spacing 1.66), which reports 184 pairs.
- **Grid-aligned crossings are missed.** A figure-eight curve section crosses itself along lines that sit on
  mesh edges at every v level; contacts then land on triangle boundaries and the strict test reports 0.
  The coarse test is a gross-overlap detector, not a proof of embedding.
- **Volume of an intersecting shape is meaningless.** The fat knot reports 81.0, double-counting the overlap.
- **Hard creases are smoothed.** Finite-difference normals at n-gon corners blend across the edge; fine for a
  preview, wrong for a renderer that wants crease normals.
- **Displacement aliasing.** Crackle cracks need a ~240 × 480 grid to stop stair-stepping; the 160² default
  is too coarse for any displace with sharp features.
- **Cost of nesting.** Each stage that reads `n` evaluates the previous stage 5 times; chains of displaces
  are exponential in depth. Fine for the examples here, not a renderer design.
