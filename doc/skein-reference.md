# Skein: the expression set

Skein is a beta feature: its syntax, parameter names and behaviour are subject to significant change and it probably contains more bugs than are listed here.

What a skein offers an author. Derived from `doc/skein-decisions.md`, which holds the
decisions in the owner's own words — check against that where this reads wrong.

This is the syntax as built, except what **Built so far** at the end marks as to do. The
user documentation is the manual chapter `doc/html/r3_9.html`.

```
skein {
  expressions { <expression> ... }
  [closed u | v | uv]
  [ends flat | pole | open | sealed [, ...]]
}
```

Topology is declared, never inferred: without `closed` the skein is not closed, and `ends`
defaults to `open`. Both are checked at parse time. A skein is a solid when it is
`closed uv`, or `closed u` with both ends `flat`, `pole` or `sealed`.

---

## extrude

Takes the sheet and brings it around a straight line, joining it at the edges. One
direction of the sheet becomes the angle; the other runs along the line. Makes tubes,
tori, lathes, spheres, cones, vases and fans.

| property | type | what it does |
|---|---|---|
| `axis` | `x` or `y`, default `y` | the straight line through the origin the sheet comes around |
| `radius` | a value, default 1 | how far out from the line the sheet sits; vary it to bulge, taper, or give a cross-section that is not round |
| `arc` | a value, degrees, default 360 | how far round the line the sheet goes. 360 closes it, less leaves it open, more overlaps. Varying it over v fans or cones the shape |

The axis is `x` or `y` only (any vector parallel to one of them is accepted, so `axis -y`
parses and runs the angle the other way). Which direction of the sheet becomes the angle
follows from the axis, so there is nothing to state: with `axis y` the incoming x becomes
the angle, y is kept along the line and the incoming z is added to the radius; with
`axis x` the incoming y becomes the angle, x is kept along the line and the incoming z is
taken off the radius.

Following a curve is not `extrude`'s job — that is `bend`'s `along`.

---

## crease

Creases the sheet along a line, over a turning circle. A small radius gives a sharp
crease, a large one a gentle curve; it is not sharp by default. The sheet carries on
straight once it has turned through the angle.

```
crease { axis <axis>  radius V  [angle V] }
```

| property | type | what it does |
|---|---|---|
| `axis` | an axis, required | the line the sheet creases along; straight or curved. Any form in **Axes** below |
| `radius` | a value, required | the turning circle: small for a sharp crease, large for a gentle one. Its sign says which way the sheet curls. Can change along the axis |
| `angle` | a value, degrees | how far round the crease goes before the sheet carries on straight; unlimited when left out. Can change along the axis |

`radius` means something different here than on `extrude` — there it is how far out the
sheet sits, here it is how tight the turn is. Kept deliberately, because radius is the
word sheet metal bending uses. Whether `radius` stays at all is open: the owner raised
replacing it with the axis's distance from the sheet, as `curl`'s pivot works
(`doc/skein-decisions.md`, **Not decided**). Until then crease is as described here.

On what it is for, the owner (whose "bend" here is `crease`): "The paper with the corner
curled should be using the new curl. The bend should be used for the paper aeroplane for
instance."

### How a crease behaves, in detail

Three phases along the sheet: **flat**, then an **arc**, then **straight**.

- Everything on the near side of the axis is returned **completely unchanged**.
- Past the axis, material rolls onto a cylinder of `radius`. The angle it has turned
  through is its distance past the axis divided by `radius`, so **arc length is
  preserved** — 10 cm of sheet gives 10 cm of material, curved. This is what distinguishes
  a crease from an `extrude`, which stretches the sheet to fit the radius it is given.
- Once it has turned through `angle` it leaves the cylinder and **runs straight on**,
  tangentially, for whatever material is left.
- A point already offset in the curl direction sits at a **tighter radius**, so a thick
  sheet creases correctly: the inner face compresses and the outer stretches, as real
  material does. (In the current code this is the `radius − h` term.)
- A very small `radius` is therefore a sharp crease and a large one a gentle curve; it is
  not sharp by default.

### The axis

- **A straight hinge** is exact anywhere in space. It is a direction (`x`, `y` or any
  vector), which passes through the world origin, or a `path` of collinear points with
  linear interpolation that does not close — two points are enough, and how far they
  reach does not matter: the hinge is the whole line through them. Every point is
  measured from the line directly.
- **A curved hinge** is any other axis: a curved `path`, a `sample_path` (placed against
  the surface entering the crease, as for `bend`), or three `function(t)`s. It is read as
  running along y, parameterised by y: a point's station on it is its own y coordinate,
  the rule `bend` uses, so the hinge point a point creases about is the axis at that y.
  To crease along another direction, rotate the sheet. Past an open end the hinge
  carries on straight along its end tangent.

### Which half moves, and which way it curls

Chosen, not decided (`doc/skein-decisions.md`, **Chosen, not decided**). The hinge has
a direction T — the path's own direction, from its first point to its last, or the
vector's — and a curl normal N: z made square to T (x when T is along z), the same frame
a path axis carries. The half that moves is the one on the T × N side. For a sheet in the
xy plane with the hinge along +y, T × N is +x, so the **+x half moves** and the −x half
stays; with the hinge along −y the −x half moves. **Reversing the path moves the other
half.**

A positive `radius` turns the moving half toward +N, so toward +z for a sheet in the xy
plane; a **negative radius turns it toward −z**. The turning circle's centre sits at the hinge
plus N times the radius, and its size is the radius's magnitude. A radius passing through zero
is a sharp crease there, and only means something with an `angle`; a radius of exactly
zero with no `angle` turns by the unlimited default and gives noise. `angle` is a
magnitude: how far round, never which way; a negative constant angle is a parse error.

### Varying along the axis

`radius` and `angle` are values evaluated at the point being creased, like every other
value, so they may read `u`, `v`, the point and the surface direction. A value that
depends on position along the axis therefore varies along it: on a sheet creased along y,
`radius function(v) { 0.2 + 0.25*v }` widens the turn from one end of the hinge to the
other.

---

## curl

Rolls the sheet up, keeping its length. A roll is not an `extrude`: the material is not
stretched around, it is wound. The travel is what makes it a curl; a roll with a fixed
centre is the hinge step, `crease`.

```
curl { pivot <path>  travel <path> }
```

| property | type | what it does |
|---|---|---|
| `pivot` | a straight `path` or `sample_path`, required | the line the sheet rolls around. How far it sits from the sheet is how fat the roll is |
| `travel` | a `path` of vectors, required | where that line moves to as the roll goes on, read in turns. Travelling away from the sheet widens each turn, which is what makes a scroll |

### How a curl behaves, in detail

A roll **keeps the material's length**. That is the whole difference from `extrude`: an
extrude maps the sheet's extent onto an angle and puts it at whatever `radius` says,
stretching it; a curl winds the material up, so a metre of paper gives a metre of roll.

- Everything on the near side of the pivot is returned **completely unchanged**.
- The pivot moves as the roll proceeds. Because the pivot's distance from the sheet *is*
  the radius, a travel that leads away from the sheet makes the radius grow as the roll
  goes on, so each successive turn is wider: a **rolled scroll**. That is the capability
  nothing else in the set has.
- A travel that runs **along** the pivot rather than away from it gives a helix — so a
  **coil spring is a curl**, the same mechanism with the travel pointing a different way.
- A travel along the sheet, away from the pivot, slides the roll forward as it turns.

**How far round a point sits.** Under a travel the radius varies, so the angle is whatever
makes the rolled section's arc length equal the point's distance past the pivot. The
section is tabulated by arc length once, when the skein is prepared, and inverted per point,
so a roll costs one table lookup per sample and its slopes are analytic.

**Why this is cheaper than the shape it replaces.** Rolling paper with an `extrude` needs a
radius growing with the angle *and* an arc-length remap integrated by hand, and that
pattern was measured on the bark rod at **5429 s against 915 s** — "the slowest thing here,
the 33-term sum running in every displacement call". A table lookup replaces the
integration.

### The chosen details

Chosen, not decided (`doc/skein-decisions.md`, **Chosen, not decided**).

- **The pivot is a straight line**: an open, linearly interpolated `path` of collinear
  points without handles, or a `sample_path` that is straight once placed. A curved pivot
  is an error.
- **The frame is the crease's.** T is the pivot's direction, N is z made square to T (x
  when T runs along z), and the half on the T × N side (the A side) rolls: for a sheet in
  the xy plane and a pivot along +y, the +x half; along −y, the −x half. Reversing the
  pivot rolls the other half. A pivot above the sheet (z > 0) rolls it toward +z, one
  below toward −z.
- **The radius at the start** is the pivot's signed height along N. For a `path` the sheet
  is taken to lie in the plane through the world origin facing N, so a pivot at z = 0.1
  over a sheet in the xy plane rolls with radius 0.1; for a `sample_path` it is the third
  number, the height above the surface, which must be the same all along the pivot. A
  pivot below the sheet rolls the other way.
- **`travel` is read in turns** of the roll, as a displacement from where it is at 0
  turns, so a two-point path with no parameter values moves the pivot over the first
  turn. Past the path's ends the path's own rule holds: an open path holds the pivot
  still, so the roll carries on at its last radius; a closed one repeats. A scroll of N
  turns wants a travel that spans N turns, such as `path { 0, <0, 0, 0>, N, N*<0, 0, s> }`.
- **The section** is the circle of the current radius about the moved pivot, so the
  radius at a given angle is the starting radius plus the travel's component away from
  the sheet, and must stay above zero. Material at distance ρ past the pivot's foot lands
  at arc length ρ along it; a point offset toward the pivot moves along the section's
  normal, so a thickened roll's inner face is the tighter.
- **Errors**: a missing pivot or travel; a curved pivot; a pivot on the sheet; a
  `sample_path` pivot whose height varies; a travel that brings the radius to zero, or
  slides along the sheet faster than the roll turns so the sheet doubles back; more than
  256 turns; a sheet with no bounded extent past the pivot.

**Every turn of a scroll touches the sheet beneath the pivot**, because each turn's circle
has the pivot's height as its radius. A travel straight away from the sheet therefore
gives turns that all meet along the foot, and a thickened scroll of more than about a turn
overlaps itself there and is not a closed solid. A travel along the pivot, or an open
travel ending off a whole turn, kinks the sheet where the roll leaves it.

**Dropped deliberately:** a per-point limit on how far each part of the sheet rolls. "This
doesn't feel particularly useful, I could stretch with a different transform." Per-point
variation lives on `crease`'s `radius` and `angle` instead.

---

## fold

Folds the surface so its normal at each point is a new one you give, keeping lengths
along the surface: a bending, never a stretch. The fold is solved outward from one point
of the sheet, `from`, which stays where it is.

```
fold { from <U, V>  x V  y V  z V }
fold { from <U, V>  perturb { x V  y V  [z V] } }
```

| property | type | what it does |
|---|---|---|
| `from` | `<u, v>`, required | where the solve starts: that point keeps its place and its tangent plane turns straight to the new normal there |
| `x`, `y`, `z` | values, each 0 when left out | the new normal in space, as `translate { x ... y ... z ... }` takes its parts. Its length does not matter |
| `perturb` | `x`, `y` and `z`, values; `z` defaults to 1 | the new normal in the surface's own frame, as a normal map gives it: `x` along the incoming u direction, `y` along its v direction (both made square to the normal) and `z` along the incoming normal. `perturb { }` keeps the surface as it is |

Give either `x`, `y` and `z` or a `perturb`, not both. The values take the same inputs as
any other: `uv`, and `pos` and `norm` of the surface entering the fold. A new normal of
zero length keeps the incoming one there.

**What is exact.** When the new normals are those of a surface that the incoming one can
be bent into without stretching (a developable field: one straight hinge, a stack of
creases, a paper dart), the fold is that surface, whichever way it is solved. A fold
whose normal is the hinge's normal is that `crease` to 1e-8 (`tests/render/skein_fold.pov`).
A fold to the incoming surface's own normals is the identity.

**What is best effort.** Other fields (a dome on a flat sheet, noise) have no surface that
keeps every length, so the result depends on the path it was solved along. It is never
refused. The convention: from `from` along its row to each of 65 columns across the sheet,
then up and down each column; a point between columns is solved along its row from the
column on each side and the two blended, so the surface stays whole. Whether the field
closed is measured at Prepare: when every loop across the columns closes, a point is
solved from the nearest tabulated point alone.

**How the turn is chosen.** At `from`, the tangent plane takes the shortest turn to the new
normal (a half turn about the incoming u direction if the new normal is exactly the
opposite). Everywhere else the turn is carried from point to point along the solve, so
it is continuous: a normal that swings smoothly through 180 degrees across a hinge folds
the sheet flat back on itself with no ambiguity, by `perturb` (`z` negative) or by `x`,
`y` and `z`. A normal that jumps (a hinge of zero width) takes the shortest turn across
the jump, and a jump of exactly 180 degrees a half turn about the u direction.

**Not guaranteed.** The folded sheet may pass through itself, as paper folded flat
touches itself. A kink or a jump in the field is found by bisecting the field and crossed
in one step, so a crumpled sheet of jumps and kinks costs a few times a smooth one.

**Cost.** Each evaluation integrates from a tabulated point (or three short runs when the
field does not close), so a fold costs some tens to a few hundred evaluations of the
surface entering it. Enclosures are the folded centre of a patch padded by the longest
path to its corners, which shrink with the patch.

`crease`, `curl`, `bend` and `extrude` are unchanged; a fold is the general form a
crease's or a roll's normals can be given in, and an extrusion is a fold followed by a
displacement.

---

## bend

Moves material that is already there, relative to a line. Unlike the three above it does
not create a shape — it transforms one. The axis says only where the transform's origin
lies; the transform itself is supplied as functions.

| property | type | what it does |
|---|---|---|
| `axis` | an axis, default `y` | where the transform's origin lies |
| `rotate { angle V }` | a value, degrees, default 0 | turns material about the axis |
| `translate V` | a value, default 0 | moves material along the axis |
| `scale V` | a value, default 1 | scales distance out from the axis |
| `along <axis>` | an axis | moves material to follow a path: at each station, to that path's position and orientation. This is how a straight extrusion is bent onto a curve |

At least one of `rotate`, `translate`, `scale` and `along` is required, in any order.
Built-ins cover the common cases; a user function is the escape hatch for anything they
do not.

Order matters. A `bend` before an `extrude` transforms the flat sheet; after it,
the tube. On a curved axis, "about the axis" means about the line's local direction,
which nothing else in the language can express; a point's station on a curved axis is its
own y, as for a curved crease hinge.

`along` matches stations by arc length, so the material must be as long as the stretch
of path it covers. Past the end of an open path the material runs on straight along
the end tangent (chosen, not decided); a closed path wraps. A straight `path` used as `along` comes out a quarter turn
about the axis from the same line given as a vector, because the two seed their frames
from z and x.

---

## Axes

An axis is wanted by `crease` (its hinge), `curl` (its pivot, straight only) and `bend`
(its axis and its `along`), and means the same thing throughout: a line that may be
straight or curved. `extrude`'s axis is not one of these: it is `x` or `y` only.

| form | what it is |
|---|---|
| a vector: `x`, `y`, `<1, 1, 0>` | a straight line through the origin in that direction |
| `path { ... }` | a curve through points in space (vector points) |
| `function(t){...}, function(t){...}, function(t){...}` | a curve given analytically |
| `sample_path { ... }` | a line placed **on the surface** — see below |

### sample_path

Each point is three numbers: the first two pick a spot on the surface the way a texture
coordinate does, and the third is how far out from the surface to sit. It lets a line be
placed against the sheet without knowing where the sheet ended up in space, which after a
chain of extrudes and displacements is hard to work out by hand.

For a `curl`, that third number is the roll's radius, because it is how far the pivot sits
from the paper. A `sample_path` lets a fold be placed on the sheet where it now lies, so
several folds in a row need no global axis positions worked out by hand.

### Remapping an axis (not built)

An axis is a function of a number from 0 to 1, so remapping its input changes how the
material is distributed along it — it need not run evenly. That remap is a `map`, so
anything a map can do serves as the distribution, including a scalar `path` used as a
curve.

---

## Values

Any property above written "a value" takes any of these, and may vary with the surface
coordinates, the point, or the surface direction:

- a number
- `function(...) { ... }` inline, or a declared function named without parentheses
- `map { ... }` — an input through a chain of modifiers
- `path { ... }` — a path of numbers, read at v
- `sum`, `product`, `min`, `max` of any of those

A skein holds at most 32 function values (a 33rd is a parse error); numbers, paths, maps
and functions given as an axis do not count.

Map modifiers: `linear`, `sin`, `cos`, `range`, `path`, `length`, `atan2`, `cells`,
`image`, `noise`, `fbm`. `image` gives an image's greyscale brightness; the owner wants it
replaced by an expression that reads a pigment's RGB (`doc/skein-decisions.md`,
2026-10-03), and it stays as built until then. **Wanted and not built:** distribution
curves — normal, exponential, Poisson — usable as mappings. As a mapping of a 0-to-1
input a distribution is its cumulative form, or the inverse of it, rather than its density.

---

## Unchanged by all of this

`scale`, `rotate`, `translate`, `matrix`, `transform`, `displace`, `sample`, `envelope`,
`expression_map`, and declared `expressions` groups.

---

## Built so far

| | state |
|---|---|
| `sample_path` | built and checked |
| `bend` — `rotate`, `translate`, `scale` | built and checked |
| `extrude` — dropping `origin`, `up`, `start` | built and checked |
| `extrude` — the rename, dropping `along` and `axial`, `arc` as a value | built and checked |
| `crease` — the rename | built and checked |
| `closed` replacing `wrap` | built and checked |
| `bend` — the rename from `axial` | built and checked |
| `bend` — `along`, and dropping the parameter words from `translate` and `scale` | built and checked |
| `crease` — axis as a path, `radius` and `angle` varying along it | built and checked |
| `curl` | built and checked; in this merge and in the manual |
| `expression_map` — every entry sampled for its box (a repeated declared group with a function value) | fixed and checked (`749667f3`) |
| `bend` — run-on past the end of an open `along` path | fixed and checked (`65e2514c`) |
| `fold` — a new normal per point, by `x`, `y` and `z` or `perturb` | built and checked |
| remapping an axis input | to do |
| distribution curves | to do |
