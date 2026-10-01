# skein-lab notes

Throwaway previewer for the pipeline-of-maps surface grammar. Not the renderer.

Run: `sbx npm install`, `sbx node --test test/*.test.ts`, `sbx npx tsc --noEmit -p .`, `sbx node build.mjs`,
then `python3 -m http.server -b 127.0.0.1 PORT` from `dist/` and open `/gallery.html` or `/?ex=torus&wire=1&normals=1`.
`sbx node scripts/survey.ts [names]` prints metrics for every example. API reference: `API.md`.

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
- **Spindle torus is not a crossing.** It only touches itself at the two axis points: the strict detector
  said 0, the inclusive one says 6000 (every ring converges there). Both are defensible; it now sits in the
  failure gallery as the "touching" case.
- **Grid-aligned crossings were missed** by the first, strict detector (contacts on triangle boundaries).
  Round 2 made it inclusive; see below.
- **Volume of an intersecting shape is meaningless.** The fat knot reports 81.0, double-counting the overlap.
- **Hard creases are smoothed.** Finite-difference normals at n-gon corners blend across the edge; fine for a
  preview, wrong for a renderer that wants crease normals.
- **Displacement aliasing.** Crackle cracks need a ~240 × 480 grid to stop stair-stepping; the 160² default
  is too coarse for any displace with sharp features.
- **Cost of nesting.** Each stage that reads `n` evaluates the previous stage 5 times; chains of displaces
  are exponential in depth. Fine for the examples here, not a renderer design.

## Round 2: primitives, series, gallery

### Decisions

- **Transforms are ops.** `scale` / `rotate` take an optional origin; `translate` / `matrix` are absolute.
  All accept a constant (fast path, no sample built) or a value of the per-sample inputs.
- **A series is callable two ways.** `path(...)(0.3)` evaluates at 0.3; called with a sample it evaluates
  at `v`. That makes every series structurally a `Field`, so `Value<T>` stays `T | Field<T>` and arrow
  parameters keep their contextual types. A three-way union with a `(t: number) => T` member broke
  inference in every example (`({ v }) =>` became implicit `any`).
- **`by(f, key | expr)`** is the one way to drive a series from anything but v. It also works on plain
  one-argument functions.
- **`path(p0, p1, ...)`** takes points at uniform parameters (linear by default); **`spline([t, p], ...)`**
  takes knots with parameter values (Catmull-Rom by default). Both take a trailing options object:
  `closed`, `interp` (`linear | catmull | cubic`), `arclength`. Closed knots live in [0, 1); 1 closes.
- **Named fold slots.** `twist` and `axial` are fold options; `radius` may also return
  `{ radius, angle, axial }` or the positional array. `twist` now works for curve sections too.
  The path-axis extra roll was renamed `roll` to free `twist`.
- **Models may be arrays of shapes.** Metrics sum area/volume and run the intersection test on the merged
  coarse meshes, so parts that touch or interpenetrate count.
- **Seam identifications** `shift` and `flip` on the v seam: welded by index with u remapped, measured by
  the seam gap with the same remap. Flip turns volume off (non-orientable).
- **Flat caps are ear-clipped**, not fanned: the C-beam's centroid sits outside its section, and the fan
  produced 102 intersecting pairs (volume was still right; signed fans cancel).
- **The intersection test is inclusive** (boundary contacts count, 1e-12 slack). It catches the
  grid-aligned Klein crossing (992 pairs) and counts touching contact. Ear clipping had to treat
  near-collinear ears with a tolerance, or the inclusive test flagged cap/wall T-contacts on n-gon edges.

### Friction (round 2)

- **Height-setting is now `scale([1, k, 1])`**, which reads well. Uniform function-valued scale is a trap:
  `scale(by(flare, ...))` also scaled y, folded the column back over itself (102 pairs). Fixed with a Vec3
  path `[k, 1, k]`, but the uniform form looks right and is wrong.
- **Lathe profiles with straight runs** (capsule) need a piecewise function; there is no arc/segment profile
  builder. A `profile(line(...), arc(...))` helper would make the capsule one line.
- **Instancing is plain TypeScript.** DNA rungs, chain links and rope strands are `Array.from` / factory
  functions over `rotate` + `translate`. Natural in TS; a grammar needs an explicit repeat/instance construct.
- **Closure conditions are on the author.** A twisted square torus closes only if the twist is a multiple
  of the section symmetry and `shift` matches it; Möbius and Klein need `flip`. Nothing infers these.
- **Union by touching is an intersection.** The pumpkin stem had to float 2 mm above the body or the
  detector counted 941 contacts. There is no union/CSG notion; parts are just listed.
- **Sprinkles are bumps, not objects.** `displace` can only push along the normal; real sprinkles would be
  instanced shapes scattered on the surface.
- **Catmull-Rom overshoots on uneven spacing**: a 0.1 segment next to a 2.9 one doubles back (cusp). Found by
  the arclength test (28% chord spread); the test now measures along the curve (0.15% spread, same 1%
  tolerance). A centripetal variant would avoid the cusp.
- **`series((t): Vec3 => ...)` needs the return annotation**, or TypeScript infers `number[]`.
- **Flip seams render with a crease**: welded vertices take one normal across a non-orientable seam (Klein).

### Limits: what the model cannot express

- **Genus above 1 / branching.** One (u, v) sheet gives sphere, disc, tube, torus, Möbius or Klein topology.
  A double torus, a pretzel with three holes, a Y-junction or a tree limb needs several shapes or a
  different construct. The figure-8 loop and chain are genus-1 tubes; the pretzel was left out.
- **Coincident sheets** (a fold with range > 2π at constant radius) overlap exactly; the detector ignores
  coplanar pairs, so this is not reported.
- **Hard creases**: normals are always finite differences; n-gon and star edges shade soft.
- **Profiles through the axis mid-surface** (a closed lathe profile touching r = 0) are pinch points the
  mesh does not weld; the bowl was written as an open profile from pole to pole instead.

### What would be awkward in POV-Ray syntax (input to the grammar work, not a design)

- **Destructured sample records** `({ v, theta, n }) =>`: POV functions take positional float parameters;
  there is no record of named inputs, and no "previous stage" normal.
- **Non-scalar returns**: radius as `[r, angle, axial]` or `{ radius, angle, axial }`, vector-valued scale
  and translate. POV `function {}` returns one float; vector results need three functions or spline/
  transform tricks.
- **Higher-order values**: `ops(...)`, `morph(keys, t)`, `by(f, map)`, factories like `link(...place)`.
  POV macros cover this at parse time only; they are not values.
- **Series**: POV `spline { linear_spline | cubic_spline | natural_spline ... }` with explicit parameters is
  a close match to `spline([t, p], ...)`, and calling `S(t)` is native. Missing: closed handling (repeat
  points by hand), arc-length reparameterisation, scalar series (use `.x` of a vector spline).
- **Topology flags** (`wrap`, `ends`, `shift`, `flip`, `grid`) are an object literal here; in POV they would be
  keywords inside the object block, which is probably easier to read than the TS form.
- **Models as arrays** map directly onto `union { }`.
