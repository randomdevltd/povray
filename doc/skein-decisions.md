# Skein expression set: decisions

The owner's decisions, recorded as made, so they are not reconstructed from memory.
Quoted text is the owner's own wording. Anything not decided is in the second half,
and is not to be filled in by inference.

## Decided

In the order made; a later section supersedes an earlier one where it says so.

### Removed from the old `fold`
`origin`, `up` and `start` are gone. No scene set any of them and each was expressible
with what remained. Implemented and verified (commit `01ce0377`).

### No migration errors
Removed words get no parse error naming a replacement. The syntax has never shipped,
so there is nothing to migrate: "this is not the right approach AT ALL for cleaning up
and finishing a WIP PR". `twist`'s branch and the four error scenes went too.
Implemented (commit `01ce0377`).

### The expression set
Five expressions, kept distinct because the overlap between them was the whole problem:
"There is so much overlap between these functions, that's why it's really important to
make them distinct."

- **`extrude`** — what the old `fold` becomes. **Straight line only.**
- **`bend`** — "doing the bending part that fell out of fold", i.e. the curved axis.
- **`crease`** — the single fold.
- **`curl`** — rolling up.
- **`axial`** — moving material relative to an axis.

`bend` and `crease` (what the code currently calls `bend`) stay separate expressions:
"Keep them as two separate things. The words communicate their purposes much better."
Conflating them was rejected because the parameters stop meaning anything: "What does
angle mean when we're making a cylindrical roll? What does radius mean when the axis is
already a distance above the page?"

### Topology keyword
`wrap u|v|uv` becomes `closed u|v|uv`. Agreed because the old word "was confusing
certainly", and because the step needed the name less than the topology needed clarity.

### `crease`
"a single bend around an axis by a specified angle and with a specified radius of
turning circle (so it's not a sharp bend by default but a sharp crease can be made with
a very small radius) and again, axis is a path, and angle and radius can both be varied
along that path."

The word for its turning radius is **not** settled — only that `radius` is wrong,
because it means something different on `extrude`. (Superseded below: **`crease`'s
turning radius is `radius`**.)

### `curl`
Rolls the sheet up. An axis placed where you want it, whose distance from the sheet is
how fat the roll is, and a `travel` path for where that axis moves to as the roll
proceeds: travelling away from the sheet widens each turn, which is the scroll.

Its per-point limit is **dropped**: "This doesn't feel particularly useful, I could
stretch with a different transform."

### `arc`
Stays, and becomes a value: "Arc can still live on wrap but it should be able to be a
function varying over v." It is a plain float today.

### Axes given on the surface
An axis or path may be given as points of three numbers — two surface coordinates and a
distance out along the normal — so it can be placed without knowing where the surface
ended up in space. Mechanism is `sample_path`. Implemented and verified (commit
`805756cc`).

### `axial`
A transform, not a construction. "axial defines where the transform origin lies, the
transform is supplied as functions, we need built in ones for the basics, or you can
drop to user fns. It doesn't need to be limited to specific options."

`bend` was built with `rotate { angle }`, `translate` and `scale` properties. That was an
author's reading of the quote above as properties, not a decision of the owner's: the
owner has since said they meant the mapping functions, transforms and expressions
themselves (see "Working rules and recent decisions" below). Open: `bend` should apply the
existing expressions about its axis rather than define its own.

Its station rule stands as the old fold's was, deformation included: "Of course there
will be some deformation while applying a transformation."

### Syntax generally
Expressed in POV's existing grammar for now. A proper arrow-function syntax is a later,
separate piece of work.

### There is no `bend` expression: bending is an `axial` transform
Superseded. "Axial and bend are the SAME THING. Or rather, bend can be described by axial
along with a transform that describes a path to bend the axis along. It's still a cross
section transform, just transforming to match the position/orientation along the path."

So bending onto a curve is a built-in of `axial`, taking the path (the `target`) and
producing the rigid motion that matches the path's position and orientation at each
station. This is the first `axial` built-in that takes a path rather than a value, and the
first that produces position and orientation together rather than one component.

The merged expression is called **`bend`**: "we're actual going to merge them both as
bend because it's a nicer name than axial". So the built `axial` step is renamed `bend`,
and gains the path-following built-in. The word is freed by the old `bend` becoming
`crease`.

The path-following built-in is called **`along`**, and "can take any axis/path/function".
The word is free because `extrude` dropped it.

Collision to resolve: `along` is also the parameter name inside `bend`'s `translate
{ along V }` and `scale { along V }`, so it would be a built-in and a parameter of two
other built-ins at once.

### Scaling along the axis goes; the axis parameter can be remapped instead
"Scaling along the axis actually makes no sense. The axis already has length. What does
make sense is being able to map the axis itself so it doesn't scale linearly over 0..1."

Borne out by measurement: the lengthwise factor was found to be inert in the common case,
because on a path axis parameterised by y the offset along the tangent is zero by
construction. It only had an effect on a bowed axis.

So `scale` keeps only the radial factor, which leaves it one meaning and no need for a
parameter word. Separately, how the station advances along the axis becomes mappable, so
it need not run linearly over 0..1. That is not a transform of the material but a
reparameterisation of the axis, so it is a different kind of thing from the built-ins.

The remap belongs to the axis, not to `bend`: "Ultimately an axis is just a function that
takes an n 0..1 and returns a function. Remapping its inputs is the operator." So axis
scaling is "a map with a distribution function" applied to the axis's own input.

The mechanism largely exists: `map` already chains `linear`, `range` (with `repeat`,
`mirror`, `clamp`), `sin`, `cos` and a scalar `path` lookup, so a path is already usable as
a distribution curve.

**Distribution functions are wanted as mappings** in their own right: "We also need a
distribution function that can give you normal, exp, Poisson type curves and those could be
used as mappings." This was already on the deferred follow-up list and in the design
notes' later vocabulary; it is now wanted rather than speculative.

Note on what that means in practice: a distribution used as a *mapping* of a 0..1 input is
its cumulative function, or the inverse of it, not its density — and Poisson is discrete, so
only its cumulative form maps a continuous parameter.

### Parameter words in the built-ins
`translate` needs none: inside `bend` the axis is given, so a translate has only one
meaning available. `scale` needs none either, once the lengthwise factor is gone.

The expression set is therefore four: `extrude`, `crease`, `curl`, `bend`.

### `arc` lives on `extrude`
"Arc can still live on wrap but it should be able to be a function varying over v."
Decided before `extrude` was named; the step is the same step.

### `axial` is renamed `bend`
Superseded by the merge above: the transform expression and bending are one thing, and it
takes the nicer name.

### `curl`'s axis is `pivot`
"I would fold origin+radius+side into 'pivot' and describe the real pivot *as a path*."
Not `axis` — that was my substitution, not a decision.

### `crease`'s turning radius is `radius`
"for crease the only word I can find is radius which is what they use in sheet metal
bending". It therefore means one thing on `crease` and another on `extrude`, accepted
deliberately because sheet metal bending is the domain the word comes from.

### `curl` always travels
"There is no no-travel case for curl. The travel is what produces the curl." A static roll
with a fixed centre is a bend, which `crease` already is, so `travel` is required. "The
paper with the corner curled should be using the new curl."

### Working rules and recent decisions (2026-10-03)

In the owner's speech on this date "bend" means the hinge step, whose keyword is `crease`.

On adding to the core set, given how many new keywords the skein already brings:
"I am wary of overloading the core function set as it makes it extremely difficult to
understand what's available when we're already introducing a large amount of new keywords
in a language that has no formally expressed grammar (yet)"

On gaps found while porting or building:
"If you spot gaps - cases where a primitive expression should exist to support a
particularly common operation - don't start making up APIs. Implement it with user
function syntax and propose an API if one falls out (and is sufficiently flexible to cover
a wide range of cases) but verify with me so we can iterate the props and definitions as
these things nearly always come out wrong first time."

On convenience parameters added during the build-out:
"I've also noticed a tendency to invent new convenience parameters when existing
transforms / maps / paths etc already provide the same thing easily by composition. It is
already extremely powerful and expressive."

On what "built in ones for the basics" meant, and on expressions generally:
"When I said built in ones for the basics, I wasn't talking about props, I am talking about
mapping functions / transforms / expressions. I have been quoted out of context. I have
repeatedly said the expressions should be building blocks with maximally minimal apis, not
repeating things that can be done compositionally. Otherwise everything grows every option
by extension."

On what the hinge step (`crease`) does:
"A 90 degree bend should bend 90 degrees then be flat again. The radius is the bend radius
- how tight the bend is - a sharp crease or a loose curve."

On which step a roll belongs to:
"bend sort of can produce a roll with the right settings and a large angle, but it's not
really what it's supposed to be used for. The paper with the corner curled should be using
the new curl. The bend should be used for the paper aeroplane for instance."

On the sign of a roll or a fold:
"A negative curl or bend is achieved by moving the axis below the surface instead of above
it"

On the hinge step as built, with its signed `radius` (**open**, not decided; see **Not
decided**): "Ok leave it as is for now and we'll see how it looks. But possibly that was
right and radius should behave the same way for bend, eliminating the radius parameter and
using the axis distance instead"

On placing the folds of the paper aeroplane:
"the aeroplane should use sample_path to easily align the axis on the sheet with multiple
bends rather than figuring out a global axis position"

On the `image` map modifier (carried to the follow-up ticket; `image` stays as built for
now): "it's actually another specialised case that ignored existing mechanisms. What we
ACTUALLY want is an expression that can take a value or values out of a pigment pattern,
for which image is already an existing pattern. Taking a greyscale value is not correct, we
want an RGB vector which we could then map to a scalar if we wanted either picking one or
averaging for greyscale, or use all three values somehow like in a displacement."

On landing the branch, with `bend along`'s run-on as a known issue and `curl` included:
"Yep we'll land all this as is along bend along as is - note it in the docs. Skein is a
distinctly beta feature subject to significant further change and probably containing more
bugs so should be marked as such. It's a good checkpoint."

On the run-on's proposed one-line fix: "fix bend along if it's simple". It was one line, and it
is in (`65e2514c`).

## Chosen, not decided

Choices made while building, where nothing above settles the point. Each can be reversed.

- **`crease`'s handedness:** the half on the T × N side of the hinge moves: for a sheet in the xy plane with the hinge along +y that is the +x half, along −y the −x half; so reversing the path moves the other half. Not taken: a word naming the half, as the old `side` did.
- **`crease`'s curl direction is the radius's sign:** positive turns toward +N (+z for a sheet in the xy plane), negative toward −N, the centre at the hinge plus N times the radius. Not taken: a separate `toward` word, or a flip keyword. (The owner's remark of 2026-10-03 on the axis distance bears on this; open, see **Not decided**.)
- **`crease`'s curl normal N is z made square to the hinge** (x when the hinge runs along z), seeded as a path axis's frame is, for a direction axis and a straight path alike. Not taken: the x seed of `extrude`'s straight pair, which would have put a direction axis a quarter turn from a path.
- **A straight crease hinge** is a direction axis through the origin, or an open, linearly interpolated `path` of collinear points without handles, measured from the whole line exactly; every other axis, a straight `sample_path` included, takes the curved rule. Not taken: an `origin` on the axis, or a separate `line { }` axis form.
- **A curved crease hinge's station is the point's y**, clamped to an open path's ends, with the hinge running on along its end tangent past them: `bend`'s rule. Not taken: the nearest point on the hinge, a nonlinear solve per point with no unique answer inside a bend.
- **`crease`'s `axis` is required**, with no default. Not taken: `bend`'s default of y.
- **`curl`'s pivot is a straight line** (a straight `path`, or a `sample_path` straight once placed); a curved pivot is an error. Not taken: a curved pivot, which would need a station rule as a curved crease hinge does.
- **`curl`'s frame is the crease's:** T the pivot's direction, N z made square to T, and the T × N side rolls: for a sheet in the xy plane and a pivot along +y, the +x half (along −y, the −x half), toward +z when the pivot is above the sheet; reversing the pivot rolls the other half. Not taken: a word naming the side.
- **`curl`'s starting radius is the pivot's signed height along N**, measured for a `path` from the plane through the world origin facing N, and for a `sample_path` from the surface (constant along the pivot); its sign says which way the sheet rolls. Not taken: measuring a plain path from the incoming surface, a nearest-point solve; a separate direction word.
- **`travel` is read in turns, as a displacement from its value at 0 turns**, with the path's own behaviour past its ends (an open path holds still, a closed one repeats). Not taken: radians, or the length of sheet rolled, as the parameter; running on along the end tangent; a position in space rather than a displacement.
- **`curl`'s section is the circle of the current radius about the moved pivot**, the radius being the start's plus the travel's component away from the sheet, so every turn touches the sheet beneath the pivot. Not taken: a spiral about a fixed centre, which no pivot height can describe.
- **Material at distance ρ past the foot lands at arc length ρ along the section**, and an offset toward the pivot moves along the section's normal. Not taken: the angle as distance over the starting radius, which stretches the sheet once the radius changes.
- **A curl that cannot be rolled is an error at parse time:** a radius reaching zero, a travel along the sheet that outruns the roll, more than 256 turns, or a sheet with no bounded extent past the pivot. Not taken: clamping the radius, or truncating the roll.
- **`extrude` accepts any axis vector parallel to x or y**, so `axis -y` and `axis <0, 2, 0>` parse (`-y` runs the angle the other way). Not taken: refusing anything but `x` and `y`.
- **Material past the end of an open `along` path carries on along the end tangent**, as a crease runs straight past its angle; a closed path wraps. Not taken: a plain clamp, which flattened the Jacobian at the cap ring and lost rays there. (History: from `7b68b8fe` part of that run-on was missing, a doubled run-on slope in `Evaluator::Target`; fixed in `65e2514c`.)

## Not decided

- **Whether `crease` loses its `radius`.** The owner: "possibly that was right and radius
  should behave the same way for bend, eliminating the radius parameter and using the axis
  distance instead" — the hinge's axis would then sit at the turning circle's centre, above
  or below the sheet, as `curl`'s pivot does. "Ok leave it as is for now and we'll see how
  it looks": `crease` keeps its signed `radius` and optional `angle` until the fold renders
  (the paper aeroplane) have been seen.

## Implemented so far

`01ce0377` parameter removals and the migration strip · `bb18ea29` scalable check
sampling · `805756cc` `sample_path` · `a09998ba` `axial` with rotate and translate ·
`e7c6ef94` `axial` scale · `f0bfce9a` `extrude`, `crease` and `closed` · `7b68b8fe` `bend`
and its `along` · `44f5253b` `crease` with an axis, either hand and a signed radius ·
`5e606444` `curl` · `749667f3` `expression_map` boxes every entry · `65e2514c` `along`'s run-on. Full `skein.sh` green on
each. The scene syntax is renamed throughout; the C++ names still say the old words.
