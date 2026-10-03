# Skein: what is left

Skein is a beta feature: its syntax, parameter names and behaviour are subject to significant change and it probably contains more bugs than are listed here.

State at commit `d1c4a152` and the docs pass after it. Companion to `doc/skein-decisions.md` (the
decisions, in the owner's words) and `doc/skein-reference.md` (the expression set as built).

## Where the documents are

| document | what it is |
|---|---|
| `doc/skein-reference.md` | **the syntax.** Every expression, every property, its type in a word, one line on what it does. The thing to read first. |
| `doc/skein-decisions.md` | why it is that way. The owner's decisions quoted, in the order made; what was chosen while building and can be reversed; what is not decided. |
| `doc/skein.md` | the design record: the model, the POC and build-out records, every measurement, every **Flag** for anything not conservative. The build-out sections are history in the order built. |
| `doc/html/r3_9.html` | the manual chapter for the skein object (commits `0990b46f`, `ba124be4`), written from the build; `curl` added in `face72ed`. |

## Open decisions for the owner

1. **Whether `crease` loses its `radius`** and takes the turning circle's centre from the axis's distance
   above or below the sheet, as `curl`'s pivot does (`sample_path`'s third number already means that
   distance). To decide after the fold renders (the paper aeroplane). `crease` stays as built meanwhile:
   signed `radius`, optional `angle`.
2. **Every "Chosen, not decided" line** in `doc/skein-decisions.md` stands until the owner reverses it.

`curl` is in this merge (the owner: "we'll land all this as is"), with its flags below.

Before merge, not decisions: a light review on a different model; the full-size render of
`skein_envelope_uv.pov` (it matches pixel for pixel at 160 × 120); push, the PR into `performance`, CI.

## Defects

- **A crease radius of exactly zero with no `angle`** turns by the unlimited default of 1e30 degrees,
  whose sine and cosine are noise.
- **A varying crease `angle` that dips below zero** is not checked (a constant one is a parse error):
  there every point past the hinge takes the negative limit, so the sheet jumps off the hinge instead of
  rolling. At −90° and radius 0.1 the run starts at (−0.1, −0.057) from the hinge and falls along −z.
- **A function axis's frame is noisy under an `envelope`.** `AxisPoint` differences three
  `function(t)`s at a step of 1e-5, so the second derivative and the frame's turn carry rounding noise
  over the step squared; an envelope after the step differences those slopes again. A thickened crease
  about a parabola as functions has normals 6.1e-3 off the same parabola as a `path` (points 2.0e-9).
  Probably affects `bend` too. Analytic derivatives of the functions, or a wider step for the second,
  would close it.
- **A flat sheet has no decidable outward side.** Orientation comes from the flux of `p − centre`
  through the surface, identically zero for a planar sheet, so its sign is rounding noise.
- **`curl`** (flagged, landing as is): every turn of a scroll touches the sheet along the line under
  the pivot (the radius is the pivot's height), so a thickened scroll over about one turn is not a
  closed solid (212 of 216 lattice points agree); a travel along the pivot, or an open travel path
  ending off a whole turn, leaves a kink where the roll leaves the sheet; past the travel table (which
  reaches the incoming box's extent) the roll runs straight on; the "no bounded extent" error is
  untested.
- **The counters baseline cannot be rebuilt** as it stands: its sixteen frozen scenes are on the old
  keywords, which no longer parse.
- **Envelope topology.** Three cases are refused that should not be: two envelopes both along u with
  flat ends, a sphere along v with no edge, and a sphere along u. The fix is to classify each envelope's
  sheet ends geometrically (edge, pole, periodic) instead of from the declared `closed`.

Solver (none are regressions):

- **`envelope { axis v }` on a curved extrude stalls.** Radius 0.1 + 1.1 sin(πv/2) with an envelope
  along v takes 1.4M Newton steps and leaves 416k unresolved at 8 × 8; with a linear radius, 1,000
  steps; along u, fine; with a `bend` translate it times out. The stalling case:

      skein { expressions { extrude { radius function(v) { 0.1 + 1.1*sin(pi/2*v) } }
                            envelope { thickness 0.12  axis v  edge flat } } closed u  ends open }

  This blocks the `doc/skein.md` recipe "open profile plus envelope" for the bowl and the wine glass.
- **`ends flat` needs a star-shaped end ring** ("needs an end ring that winds once around its centre"),
  so a section such as a C-beam cannot be capped flat (the port caps it with prisms); flat ends exist
  only for the v ends.
- **High unresolved-patch counts**, with or without an `expression_map`: the docs-example scenes
  `r_crease` 84.6M, `w07_fold` 40.9M, `r_map_image` 27.9M, and an inline `expression_map` 24M to 28M
  while rendering correctly.
- **Slow fbm displacement.** A sphere displaced by 5-octave fbm (amplitude 0.3) took 142M Newton steps
  and 28 min at 320 × 240 (the boulder port).
- **Black pixels remaining after the crease fix**; the rays are recorded in the follow-up list.

## Syntax awkwardness to iterate

From writing the manual chapter, the ports and the build. For the owner to iterate; nothing here is
decided or proposed.

- `extrude` adds the incoming z to the radius for `axis y` but takes it off for `axis x`.
- `axis` means three things: `extrude`'s x or y, `envelope`'s u or v surface direction, and a full axis
  on `crease`, `curl` and `bend`.
- `axis -y` and `axis <0, 2, 0>` are accepted on `extrude`; `-y` runs up +y with the angle reversed.
- A straight direction axis and a straight `path` axis differ by a quarter turn (their frames seed from
  x and z), so `bend along` a straight path is a quarter turn from the same line as a vector. Unifying the
  seed would rotate every path-axis shape. `crease` and `curl` seed both from z.
- A curved hinge or `bend` axis finds a point's place from the point's y, so the sheet must be turned to
  run along y first; the station differs from the nearest point on the hinge by about half κa².
- A crease's frame comes from z, so a sheet not in the xy plane must be creased before it is rotated.
  A straight `sample_path` hinge takes the curved rule, not the exact straight one.
- A plain `path` pivot's height is measured from the plane through the origin; a `sample_path`'s from
  the surface.
- `along` places material by arc length, and SDL cannot measure a path, so each scene states its own
  length (the knot's 31.8986).
- A number path as a value is read only at v and takes no parameter values.
- Map modifier parameters (`frequency`, `amplitude`, `scale`) are plain floats, not values.
- `bend`'s `rotate { angle V }` and `translate V` differ in shape from the step forms
  `rotate { axis angle about }` and `translate { x V ... }`; `rotate` keeps its `angle` word while
  `translate` and `scale` lost theirs.
- Declared functions cannot read the `uv`/`pos`/`norm` groups or take both u and x; inline ones can.
- `closed` after an object modifier fails with "No matching }, undeclared identifier".
- A Moebius strip fails only as "seam open by 0.4".
- `ends flat` needs a star-shaped end ring and exists only for the v ends.
- Outside the skein: `function { spline { Id } }` silently turns a spline linear.

## Proposals awaiting the owner (none built)

- From the `curl` build: measure a plain `path` pivot from the incoming surface by a nearest-point solve,
  as a `sample_path` is; a `Travel(Turns, Step)` scene macro for a travel of N turns, its units (turns,
  radians, sheet length) open.
- From the `crease` build: a closest-point station on a curved hinge; a `sample_path` hinge taking its
  frame from the surface normal.
- From the solids porter, all composed with no new API: a profile through an SDL spline wrapped in a
  function; a `CurveLength` macro (the painful case is a `path { }` axis written twice); non-polar
  sections as two closed scalar paths; the sphere as a declared group. Open question: a vector `path`
  value with `.x`/`.y` members, so a profile stays inside the skein.
- The ports' proposals for solids and folds, kept with the ports outside the repo. The folds porter is
  still running, so the folds results are pending.

## Follow-ups carried to the next ticket

The follow-up list holds them. By name:

- the helper library; vector-valued expressions; solver speed and conservative bounds; seamless patterns
  on a wrapped axis; textures under the surface; promoting the sub-keywords; profiling;
- the `image` map modifier replaced by an expression that reads a pigment's RGB (the owner's words in
  `doc/skein-decisions.md`, 2026-10-03);
- remapping an axis input and distribution curves as mappings, both wanted and not built;
- `abs`, `pow` and `ridged` map modifiers (drafted outside the repo);
- the previewer's examples: about 30 ported outside the repo;
  skipped as duplicates or too slow: starknot, fatknot, dna, basket, banded, staff, trunk, arm, digit;
  bigtoe wants frame control `bend along` lacks; `klein` and `mobius` out of scope;
- showcase scenes never started: rocky boulder rebuild, bark with a second octave, magic scroll, bowl,
  wine glass, thick printed paper front and back, three-section `expression_map` tube (circle, square,
  star), bark rod with the `noise` modifier;
- shapes still on the old syntax and not in the suite: the preview scenes boulder, cracks, heightmap and
  slab, and the two-envelope experiment;
- the envelope topology fix and the remaining black pixels (under **Defects**);
- C++ names still saying the old words (`kFold`, `Parse_Skein_Fold`, `Fold()`, `Parse_Skein_Axial`,
  `Axial()`, `kAxialStep`, `kBend` for the crease).

## What is built and verified

`sample_path`; `bend` with `rotate`, `translate`, `scale` and `along`; `extrude` with `axis` (x or y),
`radius` and a varying `arc`; `crease` with its `axis`, either hand and a signed, varying `radius` and
`angle`; `curl` with a straight `pivot` and a required `travel`; the `closed` rename; `origin`, `up`,
`start`, `along`, `axial` and `twist` gone from `extrude`; `expression_map` boxing every entry, a
repeated declared group with a function value included; `along`'s run-on past an open path's end. The manual chapter. Full `skein.sh`
green at every step. Commits `01ce0377`, `bb18ea29`, `805756cc`, `a09998ba`, `e7c6ef94`, `f0bfce9a`,
`7b68b8fe`, `44f5253b`, `5e606444`, `1bdea3c2`, `0990b46f`, `ba124be4`, `749667f3`, `d1c4a152`, `65e2514c`.
