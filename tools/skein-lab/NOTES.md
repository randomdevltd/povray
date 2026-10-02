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

## Round 3: everyday objects, stress tests, winding-rule solids

### Decisions

- **`bend` primitive added** (hinge deformer: roll onto a tangent cylinder past a plane, arc length kept,
  optional angle limit, then straight). Without it, page turning and corner curl had to rotate about a line
  *inside* the sheet, which fans a thick slab around the hinge (888-1514 self-overlaps on the thin page) and
  is not isometric. A real roll needs each point's original in-plane distance after it has moved, which
  ops cannot see. Used by pageturn, pagecurl, pagecurlthin, wizardhat.
- **`noise` / `fbm` added** (smooth value noise). Boulder, paper ball, hat crumple and the horn/trunk
  octaves need smooth multi-octave noise; cellular noise alone gives cones.
- **Self-intersection is handled, not forbidden.** A closed oriented result is a solid by the nonzero
  winding rule. `union` volume integrates exactly along a 256² grid of +x rays (signed crossings);
  divergence `volume` stays as the signed integral. The 40² count is now "self-overlaps (handled)"
  (informational), split from `contacts` between parts of a composite (union joints).
- **Orientability is the error.** Edge-consistency propagation over the welded mesh: Klein is closed and
  non-orientable (error, no union); Möbius is open and non-orientable (reported, not an error).
- **Caps:** non-planar cap rings (displaced or bent ends) use a minimum-area triangulation (O(n³) DP);
  planar ones keep ear clipping, now in a normalised, centred basis with a stall fallback.

### Bugs found and fixed

- **Tentacle cap missing since round 2.** The ear-clip tolerance was scale-inconsistent (unnormalised,
  uncentred basis); on the small tip ring every ear looked collinear and the clipper silently returned
  nothing. Volume moved 0.4385 → 0.4389; the mesh had 64 boundary edges. Found by the new orientation pass.
- **Displaced capped ends** (horn base, trunk ends, staff foot) make non-planar caps that graze the wall:
  the 40² detector said 0, 80² and 160² found 6 and 75 on the trunk. Fixed in the examples by fading the
  displacement to zero at capped ends (or using poles).
- **Aeroplane wings were inside out** (airfoil listed clockwise): negative volume per part, nothing else
  showed it. Curve sections must run counter-clockwise; nothing checks this.
- **`merge` overflowed the stack** on big meshes (array spread); now typed-array copies.

### Winding-rule behaviour

- **Inverted regions cancel.** The spindle torus's inner lemon is swept with negative orientation, so its
  winding is +1 (apple) − 1 = 0: the nonzero rule makes the lemon a *cavity*, and union = divergence
  (11.838). The slice cutaway shows the hole. Expected from the rule, surprising to an author.
- **Overlaps add.** Fat knot: union 80.889 vs divergence 81.036 (winding 2 in the overlaps). Head and
  aeroplane (embedded parts) union 2.981 / 0.483 vs divergence 3.051 / 0.507.
- **Exactly coincident sheets work.** A tube bent 1.25 turns overlaps itself exactly: union 9.864 vs
  analytic 9.870 (0.056%), divergence 12.329. The two coincident chord sets cross each other, so the 40²
  self-overlap count is large while the union is right.
- **Grazing contact** (spindle apex, cap/wall slivers on the thin page) produces large self-overlap counts
  with no volume effect; the count is not a severity measure.
- **Thin features under-sample**: 256² rays give union 0.3-0.9% low on thin walls (box 0.1249 vs 0.1260,
  chain 0.6700 vs 0.6718).

### Stress tests (ram's horn, elephant trunk)

- **Defaults:** library grid 160 × 160 (examples may override); self-overlap test 40²; gallery thumbnails
  at half grid; the viewer builds the example's grid, `?grid=WxH` overrides it.
- **Build cost (Node, with FD normals):** 160² 0.5 s / 0.9 s (horn / trunk, 25.6k verts, 51k tris);
  320×800 5.5 / 9.1 s; 480×1200 12.8 / 21.8 s (576k verts, 1.15M tris); 640×1600 24.7 / 42.5 s
  (1.02M verts, 2.05M tris). Browser at 480×1200: 12-17 s mesh + 8-11 s metrics. Interactive (under ~2 s)
  tops out near 160×400; 320×800 is usable for inspection.
- **Normals:** mean angle between FD vertex normals and the mesh's face normals falls with resolution
  (horn 8.6° → 2.3°; trunk 35° → 9.3° at 640×1600). ~3% of horn vertices stay above 30° at every grid:
  the growth-ring sawtooth steps, where the surface is discontinuous and the FD normal (h = 1e-5) takes
  one side. FD normals themselves hold up; the steps render as small flaps at 160² and slits at 480×1200.
- **Stair-stepping:** horn's finest octave (wavelength 0.025 on a 6.7 × 2.2 surface) stops stepping
  between 320×800 (p95 deviation 24°) and 480×1200 (6.2°), clean at 640×1600 (2.5°): about 4-5 samples
  per finest wavelength. The trunk's finest octave (fbm at 122/unit, crack width 0.006) is not resolved at
  640×1600 (p95 33°); by the same rule it needs about 1200 × 2400 (≈2.9M verts), not measured.
- **Coarse detector:** at 40² it cannot see detail-scale problems; it missed the trunk's cap grazing that
  80² and 160² found. It is a gross-overlap check only.

### Friction and limits (round 3)

- **Branching needs composites**: aeroplane (6 parts), balloon dog (10), arm (7: arm, palm, 5 digits),
  head (6), staff (fork as a second part), basket handle, trumpet valves. Parts interpenetrate at the
  joints; the result is a union by the winding rule, not a blended surface (no fillets).
- **Thin closed solids** work as a square-section slab (fold with `ngon(4)` and `twist: π/4`, then scale);
  `start` shifts the parameterisation while `twist` rotates the section, an easy mix-up. A radial
  superellipsoid slab was tried and rejected: the outer half of each face gets almost no samples (a lens,
  volume 0.0043 instead of 0.0075).
- **Thin bent slabs defeat the coarse detector**: chord sag at 40² (≈0.011) exceeds the 0.008 page
  thickness, so faces cross in the mesh, not in the shape (pagecurlthin: 135 at 40², 72 sliver contacts at
  160²). It sits in the overlap section, flagged.
- **Open versions used** for page turning, corner curl, crumpled page and paper plane (single sheet,
  labelled "open surface (later version)"); closed thin shown for the corner curl (pagecurlthin), leaf
  (flattened lathe), box and basket (closed lathe profiles).
- **Lathe-around-a-path** is how a thin-walled tube (trumpet) would be closed, but the lip turnarounds
  need a piecewise profile; the trumpet stayed an open tube.
- **Square containers via polar `ngon` scaling** thicken the walls by √2 at the corners.
- **Displacement on both faces of a thin solid** moves them in opposite directions (the leaf veins first
  crossed the faces, 340 overlaps); thin-solid detail must be a space warp (`translate` by a field).
- **Curve-section orientation is unchecked**; a clockwise curve gives an inside-out solid silently.
- **Near-duplicates:** pagecurl and pagecurlthin share the `curl` op by import; the `digit` factory (examples/digit.ts, not in the gallery) builds the arm's fingers; the gallery shows a big toe (one closed sheet, nail as a raised displaced plate) instead of a lone finger.
