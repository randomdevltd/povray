# Skein: design notes

Skein is a beta feature: its syntax, parameter names and behaviour are subject to significant change and it probably contains more bugs than are listed here.

The design record for the `skein` primitive: a surface or solid defined by mapping the unit square through a list
of composable expressions. It states the model, the syntax as built, the decisions behind it, and the measurements
that validate them. A throwaway TypeScript and WebGL previewer (branch `spike/skein-lab`, `tools/skein-lab/`)
checked the model on meshes before the renderer existed: `API.md` there is its library reference and `NOTES.md`
records its decisions and friction.

How to read it: *Model* through *Open syntax questions* describe the primitive as it is now, with design-pass
text that was not built marked as such. From *POC decisions* to the end the sections are the build record in the
order built, kept with their measurements; syntax in an earlier one may be superseded by a later one, and is
marked where it could be mistaken for the present. The current syntax is `doc/skein-reference.md` and the manual
chapter `doc/html/r3_9.html`.

## Model

- **Default shape.** With no expressions the skein is the unit quad, `(u, v)` mapped straight onto `(x, y, 0)`.
  Every step starts from the current surface, so the xyz input always exists.
- **Every step is a map on the current surface.** Steps compose in order, any number, repeatable and nestable: a
  declared group is itself a step.
- **`extrude`** sweeps the surface around a straight axis, `x` or `y`. Its `radius` is the distance from the axis at
  each point, so a cylinder is `1`, and it may vary with `(u, v)`. A full `arc` closes into a tube; a partial one
  gives curled paper, which can be extruded again, and `arc` may itself vary, which fans the shape out. A section
  is a polar radius, star-shaped only, whether a function, a map or a scalar `path` gives it (the n-gon is
  cos(π/n) / cos(θ mod 2π/n − π/n)). A section that is not star-shaped has no direct form as built.
- **A curved axis is `bend`'s.** `extrude` builds about a straight axis and `bend { along <curve> }` carries the
  result onto a curve, matching arc length; the extrude's own curved axis was removed when `along` arrived (see
  *`bend`: the rename*). Sweeping needs no separate `sweep` step, and `lathe` and `spherical` are recipes for
  `extrude` with `bend`.
- **`bend`** moves material about an axis (a direction, a `path`, a `sample_path` or three functions): `rotate`,
  `translate` and `scale` about it, and `along` to follow a path.
- **`crease`** folds the sheet about a hinge over a turning circle of `radius`, keeping arc length, through an
  optional `angle`, then runs straight on.
- **`curl`** rolls the sheet about a straight `pivot` whose height is the roll's radius, the pivot moving along a
  `travel` path as the roll goes on.
- **`displace`** moves the point along the surface normal by the value of its expression.
- **`envelope`** turns a sheet into a solid in one pass, by doubling one axis: out along the front face, back
  along the rear.
- **Extrude of an extrude is allowed, and self-intersection is handled, not forbidden.** A surface that passes through
  itself is a solid by the nonzero winding rule, and interior sheets are clipped: only transitions between
  outside and inside count as hits. A non-orientable result (a Klein bottle) has no consistent inside, and is the
  case that fails loudly.
- **The affine steps are POV's own:** `scale`, `rotate`, `translate` and `matrix`, with an optional `about` origin
  and function-valued parameters.
- **A single sheet** cannot do genus above one, or branching; branching shapes are unions of skeins.
- **Beyond the skein** (a note, not scope). The expression system's output is a vector, so it could also build
  custom function patterns and optimised isosurface definitions. The compiled expression form is kept
  independent of the skein so those can share it (the isosurface fast mode, SIMD function evaluation).

## Design principles

The grammar was the design pass's deliverable: simple to write, maximally expressive.

1. **Reads like POV-Ray.** A `skein { }` block of keyword blocks in declared order, using the existing
   expression, `function`, `spline` and `pattern` syntax. One meaning per keyword, as the 4.0 grammar work needs.
2. **The common case is short.** The design pass wanted topology inferred where it could be (a closed section
   wraps u, a sweep end caps flat), the author stating only the exceptions. **Not built:** topology is declared
   and checked (*POC decisions*), so `closed` and `ends` are stated on every skein that needs them; without them
   a skein is not closed and its ends are open. Inference has not been decided either way.
3. **One value syntax everywhere.** Any parameter takes a constant, a `function` with named parameters, a `path`,
   a `map` chain, or a `sum`, `product`, `min` or `max` of those. No block needs its own form. Vector values are
   three scalar fields, as in `parametric`, or an `< >` constant.
4. **Universal inputs only.** Every expression, wherever it is written, sees the same three: the surface
   coordinates `uv`, the point mapped so far `pos`, and the normal there `norm`. A step's own coordinates (a
   extrude's angle, its offsets along and out from the axis) are deliberately never inputs: that would leak a parent's details
   into its children and make an expression work only inside one kind of step.
5. **Shorthand is sugar, not a second language.** A structured step has a documented expansion into the atomic
   ones where one exists, so the escape hatch uses the same syntax and a shorthand can be opened up.
6. **Reusable.** Sections, chains and fields are `#declare`-able and usable inside other skeins.
7. **Errors name the block and the parameter**, with the seam or pole that failed to close.
8. **A pipeline, not properties.** The mapping is an `expressions` list: any number of steps, repeatable, in any
   order, each taking the previous output state and returning the next, so the list progressively maps the unit
   square onto the surface. A group is itself a step, so groups nest and are reusable. Composition is
   associative, so nesting changes no result, only structure. Topology (`closed`, `ends`) describes the result and
   stays outside the list.

## Solver expectations from the design pass

Starting suggestions from the design pass, kept as history. **None of them is a setting**: a skein takes no
solver parameters at all. *POC decisions* below records what was built; each item says how it came out.
- **Bounds without a parameter.** Derive patch bounds automatically (spline hulls, interval arithmetic through
  the function VM) and require nothing from the author; `max_gradient` shows the cost of a bound parameter that
  is hard to choose. A block with no derivable bound is never culled early. *Built*, with sampled bounds where
  intervals do not reach (each **Flag**ged).
- **Derivatives.** Forward-mode automatic differentiation in the function VM for ∂S/∂u and ∂S/∂v of user
  functions; normals not from finite differences, which are noisy at poles. *Not built for user functions*:
  their slopes are central differences; steps, maps and paths are differentiated exactly.
- **Closed sweeps.** A rotation-minimizing frame around a wrapped v loop does not return to its start angle.
  Distribute the leftover twist evenly along v, with a manual override, so the torus knot closes without a seam.
  *Built* for a closed curved axis, with no override.
- **Root tolerance.** Relative to the bounding-box diagonal (around 1e-6), settable per skein. *Not settable*:
  fixed at 1e-10 of the diagonal.
- **Solve failure.** Cap Newton iterations (around 16) with bisection inside the leaf as the fallback. A ray that
  still fails to converge counts as a miss and is tallied in the render statistics, never reported as a hit.
  *Built* as 16 steps with a split to depth 6, the unresolved patches counted as misses in the statistics.
- **Hint memory.** A per-skein memory cap (around 128 MB) where leaves stop refining and the rest bisects at
  trace time, reported in the statistics. *Not built.*

## Keyword decisions

- **The list is `expressions`.** `ops` was rejected as terse and unlike POV-Ray; `map` stays free for the
  named-parameter router (an expression that maps N named parameters onto N named parameters).
- **Reserved-word check** (`source/parser/reservedwords.cpp`):
  - Free: `expressions`, `extrude`, `skein`, `displace`, `crease`, `path`, `clamp`, `mirror`, `project`, and bare `map`
    (only the `*_map` family and `map_type` are reserved).
  - Already reserved: `scale`, `rotate`, `translate`, `matrix`, `spline`, `repeat` (a warp type), `range`,
    `angle`, `offset`, `up`, `lathe`, `spherical`.
  - `scale`, `rotate`, `translate` and `matrix` already mean an affine transform of points, which is exactly
    what they do as steps. Reusing them is consistent; the new parts are the optional origin and function values.
  - **`path` absorbs `spline`.** A path is a series of plain points or spline points (a point with handles), mixed
    freely; a plain point is a spline point with no handles. The skein uses `path`; the existing `spline`
    keyword is untouched.
  - **`range` replaces `clamp`, `mirror` and `repeat`.** One `map` modifier maps a value into a range by one of
    three methods (`clamp`, `mirror`, `repeat`; see below). `range` is a reserved token, but only as the
    `#range` directive inside `#switch`, and it works as a bare keyword inside `map` (see the build-out
    decisions).
  - `lathe` as a step would collide with the `lathe` object, and `spherical` with the camera and `map_type` word.
    Both are recipes for `extrude` instead, so neither became a step.
  - **Extrude's total angle is `arc`**, not `range` or `angle` (`arc` is free; `angle` stays with `rotate`), so
    `range` is free for the expression above.
- **Named parameters.** An inline `function(v) { ... }` binds by name: the compiled function keeps its parameter
  names (`FunctionCode.parameter[]`, up to 56), so the skein can feed exactly the inputs a function declares
  (`u v x y z nx ny nz`), in any order. Pattern functions (`function { pattern { ... } }`) take no parameter
  list and are always `(x, y, z)`.

## Syntax by example

The list is `expressions { ... }`; a declared group is `#declare Name = expressions { ... }` and can be used as a
step; topology is `closed u|v|uv` and `ends flat|pole|open|sealed[, ...]` beside the list; angles are degrees, as in
`rotate`; the optional origin of `scale` and `rotate` is `about`; `extrude` takes `axis` (`x` or `y`), `radius` and
`arc` (the total angle, default 360), both any value. A twist is a `rotate` after the
extrude, a travel along its axis is a `bend` step after it, and a curve to follow is that step's `along`.

A tapered frustum:
```
skein {
  expressions {
    scale <1, 2, 1>
    extrude { radius function(v) { 1 - 0.75*v } }
  }
  closed u
  ends flat
}
```
A torus is an extrude of an extrude, tube then ring; the ring turns about `x`, so it is `torus { 2, 0.5 rotate z*90 }`:
```
skein {
  expressions {
    extrude { radius 0.5 }
    extrude { axis x  radius 2 }
  }
  closed uv
}
```
A donut with icing and sprinkles; `displace` moves each point along its normal, and the function reads the
normal as `nx` and the point as `x y z`:
```
#declare Cells = function { pattern { cells } }
skein {
  expressions {
    extrude { radius 0.45 }
    extrude { axis x  radius 1.2 }
    displace function(nx, x, y, z) {
      min(1, max(0, (nx - 0.1)*5)) * (0.04 + select(Cells(8*x, 8*y, 8*z) - 0.16, 0.025, 0))
    }
  }
  closed uv
}
```
A twisty tapered log with bark cracks; the circle embedding keeps the pattern seamless where `u` wraps:
```
#declare Cracks = function { pattern { crackle } }
skein {
  expressions {
    scale <1, 4, 1>
    extrude { radius function(v) { 0.5 - 0.15*v } }
    rotate { axis y  angle function(v) { 270*v } }
    displace function(u, v) { -0.04 * Cracks(3*cos(2*pi*u), 3*sin(2*pi*u), 9*v) }
  }
  closed u
  ends flat
}
```
A vase from a scalar profile, and a coil from a bend's shift that grows with the angle:
```
skein {
  expressions {
    scale <1, 2.2, 1>
    extrude { radius path { cubic_spline  0.35, 0.55, 0.6, 0.42, 0.22, 0.26, 0.38 } }
  }
  closed u
  ends flat, open
}

skein {
  expressions {
    extrude { radius 0.08 }
    extrude { axis x  radius 0.6  arc 6*360 }
    bend { axis x  translate function(v) { 0.05*12*pi*v } }
  }
  closed u
  ends flat
}
```
A twisted hexagonal column; the n-gon is written out, since section helpers are a deferred follow-up, and the twist
is a phase of the section along v, so no second step is needed (the `+ 1` keeps the argument of `mod` positive):
```
skein {
  expressions {
    scale <1, 3, 1>
    extrude { radius function(u, v) { 0.5 * cos(pi/6) / cos(mod(2*pi*(u - v/6 + 1), pi/3) - pi/6) } }
  }
  closed u
  ends flat
}
```
A trefoil knot is a straight tube bent along a path; a vector-valued axis is three scalar functions of the parameter,
and the tube is as long as the knot because a bend keeps the material's length:
```
#declare A = function(t) { 2*pi*t }
#declare K = function(t) { 2 + cos(3*A(t)) }
skein {
  expressions {
    scale <1, Length, 1>
    extrude { radius 0.05 }
    bend {
      axis y
      along function(t) { K(t)*cos(2*A(t)) },
            function(t) { K(t)*sin(2*A(t)) },
            function(t) { -sin(3*A(t)) }
    }
  }
  closed uv
}
```
A page with its corner curled, using a declared group for the sheet:
```
#declare Sheet = expressions { translate <-0.5, 0, 0>  scale <0.85, 1.1, 1> }
skein {
  expressions {
    Sheet
    curl { pivot path { <0.3, 0.6, -0.05>, <-0.7, 1.6, -0.05> }  travel path { 0, <0, 0, 0>, 2, <0, 0, -0.06> } }
  }
}
```
A double helix is two strands bent along helical paths and a loop of rungs; a list of shapes is a `union`:
```
#declare R = 0.6;  #declare H = 4;  #declare Turns = 1.5;
#declare Climb = sqrt(pow(2*pi*Turns*R, 2) + H*H);
#macro Strand(Phase)
  skein {
    expressions {
      scale <1, Climb, 1>
      extrude { radius 0.08 }
      bend {
        axis y
        along function(t) { R*cos(2*pi*Turns*t) }, function(t) { H*t }, function(t) { R*sin(2*pi*Turns*t) }
      }
      rotate y*Phase
    }
    closed u  ends flat
  }
#end
#macro Rung(Y)
  skein {
    expressions {
      extrude { radius 0.035 }
      translate <0, -0.5, 0>  scale <1, 2*(R - 0.1), 1>
      rotate z*-90  rotate y*(360*Turns*Y/H)  translate <0, Y, 0>
    }
    closed u  ends flat
  }
#end
union {
  Strand(0)  Strand(180)
  #declare Y = 0.15;
  #while (Y < 4)  Rung(Y)  #declare Y = Y + 0.27;  #end
}
```
The transform steps with their optional origin:
```
scale <2, 2, 2> about <0, 1, 0>
rotate y*45 about <0.5, 0, 0>
rotate { axis y  angle function(y) { 216*y }  about <0.5, 0, 0> }
translate <0, 0.75, 0>
matrix <1,0,0, 0.3,1,0, 0,0,1, 0,0,0>
```

## Grouped inputs and `map`

Why the grouped inputs: the function VM wires its built-in coordinate words to argument slots (`x` is the first
argument, `u` is the same slot under another spelling, `y` and `v` likewise), so a function cannot name both `u`
and `x`, and a bare `function { 1 - v }` silently reads `y`. A skein function therefore declares input groups,
and the skein fills a table with one slot per value, so nothing aliases and legacy functions are untouched:
```
displace function(uv)         { -0.04 * Cracks(3*cos(2*pi*uv.u), 3*sin(2*pi*uv.u), 9*uv.v) }
displace function(norm, pos) { min(1, max(0, (norm.z - 0.1)*5)) * Sprinkles(pos.x, pos.y, pos.z) }
displace function(uv, pos)  { Cracks(pos.x, pos.y, uv.v) * (1 - uv.u) }
```
- `uv` has `.u .v`; `pos` is the incoming point, `.x .y .z`; `norm` is the incoming surface normal, `.x .y .z`. (`local` and `normal` are reserved words, so they cannot be parameter names.)
- Only declared groups are filled; the normal is computed only when some function declares it.
- The bare names (`function(v)`, `nz`) stay as shorthand where they do not collide.
- Dotted access on a parameter is a small extension to the function compiler, on for skein functions only.
- **Only universal inputs.** A function takes `uv`, `pos` and `norm`, which mean the same wherever it is written. The
  enclosing step's own coordinates (an extrude's angle `theta`, its `s`, `t`, `w`) are deliberately not inputs: that would
  leak a parent expression's details into children and make a function work only inside one kind of step. The angle
  of the incoming point is `atan2` of its `pos` coordinates, or `u` scaled by the arc on a first extrude, and `range`
  adjusts either.

`map` binds named inputs into the parameters of a built-in, so common cases need no custom function and the
built-in stays recognisable to the renderer (fast paths, SIMD). Inputs pass through optional built-in modifiers,
chained left to right:
```
translate {
  x  map { uv.v  sin { frequency 8  amplitude 0.1 } }
  z  map { uv.v  cos { frequency 8  amplitude 0.1 } }
}
```
A modifier has an arity: one input (`sin`, `linear`), two (an image or 2D pattern sample, 2D `cells`, `length`
of a pair, `atan2`), or three (noise of `pos`). `map` lists as many inputs as the first modifier needs, and a
mismatch is a parse error. Two maps added along one normal are a `sum` (the braces form of `displace` this
sketch first used was not built):
```
displace sum {
  map {
    uv.u  linear { scale 3 }  range { repeat }      // each input has its own chain: tile an image 3 times around
    uv.v  linear { scale 2 }  range { mirror }      // and back and forth along the length
    image { png "bark.png" }                        // a modifier expecting 0..1 consumes the prepared pair
    linear { scale 0.05 }
  }
  map { pos.x, pos.z  length  sin { frequency 6 } }    // ripples from the axis
}
```
`range` is a one-input modifier: it remaps a value from an input range to an output range (both 0..1 by default)
and can wrap the input first by `repeat`
(modulo), `mirror` or `clamp`. `u` and `v` never leave 0..1, so it is only needed when a value that can (a scaled
`uv`, or a value from `pos`) feeds a modifier that expects 0..1: an image, or a path or gradient lookup. On a
wrapped axis, `repeat` with an integer count closes the seam, because the value returns to its start at the wrap
(the joins between copies are seamless only if the image is tileable); `mirror` is seamless at every join and
closes the wrap seam when the count is even.

An infinite pattern such as `cells` or noise needs no `range`, but it does not close the seam at a wrapped `u` by
itself: that needs a periodic variant of the pattern or the circle embedding. Open.

A pair is named as a group (`uv`) or a list (`pos.x, pos.z`), both built. Open: periodic infinite patterns on a
wrapped axis. The same inputs-to-modifiers idea could serve isosurface functions, where the inputs are `x, y, z`.
Vector-valued function outputs stay deferred: three scalar functions until the arrow-function work.

## Textures under the surface, and printed paper

At a hit the skein supplies the exact `(u, v)` of the surface point, so `uv_mapping` follows the shape. Nothing is
defined yet for points under the surface (SSLT volume sampling, interiors): a 2D uv texture exists only on the
surface. The natural extension is to treat the skein as a 3D map `(u, v, w)`, where `w` is depth along the normal and
`w = 0` is the surface. An extrude already works this way (an incoming offset adds to the radius). An interior point is
then found as `(u, v, w)` by a Newton solve, like the ray solve, and a texture written in `(u, v, w)` follows the
surface as it curls and twists. Points deeper than the local radius of curvature have no unique inverse.

A thin closed solid (a folded slab) has a seam inside: its two faces are different ranges of `u`, so the nearest-surface
`(u, v, w)` flips at the mid-plane, and a back print would switch over there instead of bleeding through. Printed
paper with a different print on each side, seen with SSLT under backlight, needs width mode: one shared `(u, v)`
and a signed depth `w` from `-t/2` to `+t/2`, continuous through the thickness. The front print sits at `+t/2` and
the back print at `-t/2` (seen mirrored, so `u` reversed or a front/back flag). A screen or portal capture from
elsewhere in the scene can supply either print through the same `(u, v)`. To check: how SSLT volume sampling queries a
pigment at an interior point, how a screen texture is sampled between the faces, and footprint filtering for small
text at grazing angles. Width mode moves from stretch goal to the likely route for this use.

Width mode is better as an expression than as a separate setting: `envelope`. It doubles the domain along one axis and
traverses it there and back. The first half offsets the surface by `+thickness/2` along the normal; the return half
runs the same surface backwards, so its normal flips and the same offset lands on the opposite face. `thickness` is a
constant or any expression of `uv`, `pos` or `norm`:
```
envelope { thickness 0.02 }
envelope { thickness function(uv) { 0.05 * pow(sin(pi*uv.u), 0.5) } }   // tapers to zero at the edges
```
Where `thickness` reaches zero at an edge the two faces meet and the solid closes by itself; otherwise the rim is a slit
and needs an `edge` option (`round`, a half-circle of radius `thickness/2`, or `flat`). A thickness larger than the local
radius of curvature folds the inner offset through itself, which the winding rule handles. It cannot be built from the
existing steps, because the doubling remaps the surface's `(u, v)` domain before evaluation, while `range { mirror }`
acts on values inside `map`; so it is a built-in step. Built under these names, with `axis u|v` and
`edge round|flat` (*Batch two*). Width mode and textures in `(u, v, w)` are not built.

What `envelope` needs is to sample the surface so far at another `(u, v)` than the current one. That is a step in its
own right: `sample { at <u', v'> }` evaluates the chain so far at coordinates given by expressions and carries on from
that point, with its normal. It composes with `map`, `range` and the transforms, so mirror, shear (`u' = u + k v`) and
`envelope` (sample at the mirrored `u`, then displace by `thickness/2`) are all buildable from it. `sample` is built
(*Batch two*); the callable form below is not. A callable form
for user functions is possible later: POV already has vector-valued functions as a type for spline, transform and
pigment sources (a function whose body traps into C++ with private data), so the skein could register one over
the chain so far. Bodies are scalar-only, so it would take a component argument (`sample(u, v, 0)` for x) until a vector
syntax exists. Each call is a full chain evaluation, derivatives through it are finite differences or nested automatic
differentiation, and its range over the whole domain is the bounding box of the surface so far, which is conservative.

## Paths and ranges

A path is a series through space or through values: plain points, spline points, or a mix. A plain point is a
spline point with no handles. It is a function of one number, driven by `v` where a value is expected.
```
path {
  cubic_spline                               // or linear_spline, quadratic_spline, natural_spline
  <0, 0, 0>                                  // plain point
  <1, 1, 0>  handle <0, 1, 0>                // spline point, one handle
  0.8, <2, 0, 1>  handles <-1, 0, 0>, <1, 0, 0>   // optional parameter value first; in and out handles
  closed  arclength
}
```
`range` is a one-input modifier in a `map` chain (see above), replacing separate `clamp`, `mirror` and `repeat`
steps. It maps any input range onto any output range. The input range (`from`) and output range (`to`) both
default to 0..1, and an optional method (`repeat`, `mirror` or `clamp`) says what happens to an input outside the
input range before it is mapped. With no method it is a plain linear remap.
```
range { repeat }                        // modulo: 1.25 becomes 0.25
range { mirror }                        // reflect back and forth: 1.25 becomes 0.75
range { clamp }                         // pin at the ends of the input range
range { from -3, 3  to 0, 1  clamp }    // any range onto any range: a distribution's output onto 0..1
range { to 1, 0 }                       // a reversed output range: flips a value (1 - x for 0..1)
range { from 1, 0 }                     // a reversed input range does the same
```
A value scaled to three times its range with `range { mirror }` runs there and back; `repeat` tiles it three times.

## Findings so far

From the prototype (full lists in its `NOTES.md`):
- An extrude assumes its input is 1 wide in the wrapped direction, so most tubes open with a `scale`. `scale` is now a
  primitive for this.
- Detail on both faces of a thin solid pushes the faces through each other; use a `z` offset instead.
- A function-valued uniform `scale` also scales `y`; per-axis forms avoid it.
- Each stage that reads the normal evaluates the previous stage five times in the prototype, so a chain of
  normal-reading stages grows exponentially. The renderer needs automatic differentiation, not nested finite
  differences.
- A clockwise section silently gives an inside-out solid; curve orientation needs a check.
- A single sheet cannot do genus above one or branching. Branching shapes are unions of skeins.
- Self-overlap is handled by the nonzero winding rule; only a non-orientable result (a Klein bottle) fails.
- Fine octave detail needs about four to five mesh samples per wavelength; exact surfaces avoid the limit.

From writing the examples above:
- A vector-valued function is three scalar functions today (the knot axis, the double helix). POV functions
  return one float, so a vector form needs a new syntax; that is deferred (see the open questions).
- Angles: `rotate` takes degrees but `sin` and `cos` take radians; mixing them in one function is error prone
  (note `radians()` in the coil, which also assumes a function that does not exist yet).
- `cells` returns a per-cell value, not the distance to the nearest feature point the bark, boulder and
  donut examples need; a distance (Worley) pattern is missing.
- The existing `spline` has no scalar series, closed paths or arc-length reparameterisation; `path` adds them and
  absorbs `spline`.
- Section helpers (`ngon`, `circle`, `superellipse`, `star`) and the noise helpers (`fbm`, `noise`) have to be
  built in or written as functions; the spike has them as library helpers.
- `repeat` is a reserved warp type and `range` a directive word; inside `map` both turned out to work as plain
  keywords, since the directive handlers run only after a `#` (checked in `skein_keywords.pov`).

From the renderer proof of concept (decisions and numbers below):
- The function VM loads `u` into the `x` register and `v` into `y`. A parameterless `function { 1 - v }` reads
  `y` without a word, and no function can take both `u` and `x` (or `v` and `y`), so the design's
  `function(u, v, x, y, z, nx, ny, nz)` cannot compile as written. The VM needs distinct registers, or stack
  parameters looked up by name.
- An inline `function` takes no parameter list in POV; only `#declare F = function(v)` does. The skein compiles its
  own inline functions through the same path as `#declare`. A declared function as a value (`radius F`) is not
  accepted yet: telling `F` from the float expression `F(0.5)` needs two tokens of lookahead.
- Every new reserved word breaks scenes that use it as an identifier; a distributed scene declares `pole`. The
  sub-keywords therefore match by spelling inside the block.
- Sections with corners (an n-gon from a radius function) break the smooth-patch assumption that a ray steep to a
  patch meets it once: Newton steps across the corner onto the next face and the near face's root is lost. Creased
  patches need their own handling, and normals within one finite-difference step of a corner blend the two faces.
- The VM's range plan bounds `+ - * / min max abs` only. `cos`, `mod` and `pow` are not boundable, so an n-gon or a
  sinusoidal radius falls back to sampled bounds, which are neither conservative nor cheap (about six chain
  evaluations per Newton step on the hexagon, from re-sampling every split patch).
- Boxes alone leave rays that skim the surface splitting to the depth limit; about half the Newton solves on the
  torus were such near misses. A tangent-slab bound from interval derivatives removes them, but only where the
  slopes are closed form; extending it to user functions needs interval derivatives from the VM (the forward-mode
  differentiation this note already asks for).
- The existing `torus` primitive is the less accurate of the two: on a ray grazing the equator its hit lies 2.6e-6
  inside the exact surface, where the skein's lies within 1e-9.

## Recipes, not built-ins

The previewer has `lathe` and `spherical`, but they are sugar for what an extrude already does. Helpers for very common
things are welcome; the rule is to write the recipes from the minimal set first and add a helper only once a recipe
repeats enough to be worth one (a lathe may yet earn it):
- **A lathe profile** `(radius(v), height(v))`, including one that turns back on itself, is
  `extrude { radius function(v) { r(v) } }` with `bend { axis y  translate function(v) { h(v) - v } }`
  after it. A straight axis has no frame to break; a curved axis is for sweeps.
- **A sphere** is the same with `radius R*sin(pi*v)`, the bend's shift giving height `-R*cos(pi*v)`, and `ends pole`.
- **A bowl or wine glass** is an open revolved profile (`ends open`) plus `envelope { thickness ... edge round }`.
  A glass is two skeins in a union: a solid stem, and an open cup with an envelope for its wall. As built this is
  blocked: an envelope along v on a curved profile stalls the solver (`doc/skein-todo.md`, **Defects**).
- **`envelope` stays a built-in** even though `sample` and the other steps could express it: turning a flat into a
  solid in one pass is too useful to leave as a hard set-up each time.
- **`morph`** is a first-class construct, named `expression_map` and written exactly like `pigment_map` and
  `texture_map`. In this fork those parse as entries `[value item]`, one position each, sorted by value; the lookup
  (`BlendMap::Search`) blends the two neighbouring entries linearly by position, holds the first entry below the first
  stop and the last above the last stop, and two stops at the same value make a hard step. Pigment maps also take
  `blend_mode` and `blend_gamma`, and a map can be declared and reused by identifier. `expression_map` copies the
  entries and the lookup (`blend_mode` and `blend_gamma` do not apply), with each item a step or a declared group
  (a whole pipeline), driven by `v` unless an input is named:
```
expression_map {
  [0.0 CircleSection]
  [0.4 CircleSection]               // hold the circle to 0.4, a plateau as in any POV map
  [0.5 SquareSection]               // blend to the square across 0.4 to 0.5
  [0.7 SquareSection]
  [0.8 StarSection]
  [1.0 StarSection]
}
```
  The blended results are points; normals come from the derivative of the blend, which includes the weight change
  between neighbours. At most two entries are evaluated per sample, and two neighbours that are the same item are
  evaluated once. The hull of the two neighbours' bounds is conservative.

## Stacked displacement

Several `displace` steps in a row are kept as they are. Each step follows the normal of the surface the previous
one produced, which is not the same as one displacement summing the same values along a single normal, and it can
look better for fractal detail. The cost of the repeated normals is accepted.

A sum along one normal is a `sum` expression, not a special `displace`: it evaluates its entries at the same inputs
and adds them, and works wherever a value is expected (a radius, a thickness, a translate parameter). A custom function
can already do it, at the cost of the function VM; `sum` of built-in `map` chains does not.
```
displace sum {
  map { pos.x, pos.z  length  sin { frequency 6  amplitude 0.05 } }
  map { pos  noise { frequency 4 }   linear { scale 0.03 } }
  map { pos  noise { frequency 16 }  linear { scale 0.008 } }
}
```
It is evaluated once per sample and reads the normal once. `sum`, `product`, `min` and `max` are built as values
(*Batch two*), and the `fbm` map modifier sums noise octaves; an `octaves` expression was sketched here and is not
built.

## Dropped: `twist` and `roll`

`extrude` has no `twist` option and `path` no `roll`; both were redundant. The three behaviours they covered are
expressible with other steps: a shape twisted with the texture following is a `rotate` with a function-valued angle
after the extrude (the points carry their `uv`); a shape twisted with the texture straight is an angle shift inside
`radius`; a texture twisted over a straight shape is `sample { at <u + k*v, v> }`, with `range { repeat }` wrapping `u'`
on a wrapped axis. On a curved axis, bake the angle shift into `radius` and shear the `uv` with `sample` if the
texture should follow. The scenes that used `twist` were converted when `sample` landed (see *Batch two*).

## Helper library (deferred follow-up)

The atomic syntax stays small. Things that are simple to write with it, but easy not to know how, belong in a
standard include of prewritten expressions and macros, as a follow-up piece of work that starts after the syntax has
settled (so nothing is built on syntax that may still change). Candidates collected so far:
- `UvTwistExpression`, a `sample` that shears `uv` by a function of the other axis;
- a lathe profile (an extrude with `bend`), a sphere (an extrude with pole ends), a bowl and a glass (an open profile plus
  `envelope`), a thick printed page;
- section helpers (`circle`, `ngon`, `superellipse`, `star`) and path helpers (a helix, a torus knot);
- `octaves` as a `sum` of noise layers, and `product`, `min` and `max` siblings of `sum`;
- mirror, tile and shear helpers built on `sample` and `range`.

## Open syntax questions

Settled during the build-out, and recorded with the decisions below: degrees for object angles, radians inside
function bodies and cycles for pattern parameters; `about` as the origin word, and the braces form of `rotate`;
`closed` and `ends` beside the list; `crease` in the core vocabulary, with `lathe` and `spherical` as recipes and
`morph` built as `expression_map`; the `by` helper as a `path` modifier inside `map`; a plain point meaning "no
handle information, the interpolation keyword decides"; the spelling of `handle` and `handles`; and `range` as a
bare keyword.

Settled since: **`extrude` takes no `origin`.** It only ever added a constant to the output, so it was exactly a
`translate` after the extrude, and the curved branch ignored it outright because a path axis carries its own
position. `axis` places the extrude and a `bend` step travels along it, which is why an `origin` was never
written in a single scene. The word is simply gone; the syntax has never shipped, so there is nothing to migrate.

Settled since: **the names are `extrude`, `crease` and `closed`**, and `extrude` keeps only `axis`, `radius` and
`arc` (`doc/skein-decisions.md` records the owner's wording). See *Renamed* below.

Still open:
- **Vector-valued expressions are deferred.** Functions cannot return a vector, which points at an arrow-function
  syntax in the scene language itself. That is a separate piece of work that follows the formal-grammar rewrite of
  the language, not part of this feature. Until then the skein uses three scalar functions, a `path`, or
  structured steps such as `extrude` and `translate`.
- **Parameter values in a scalar path**, which `0.2, 0.5` cannot express unambiguously.
- **Promoting the sub-keywords to reserved words** is a 4.0-grammar matter. Today they match by spelling inside a
  skein, so a scene that uses one as an identifier keeps parsing.
- **A pattern periodic in `u`** for a wrapped axis: no circle embedding keeps strong grain on steep flanks
  (measured in *Bark cracks on a taper*).
- **Envelope topology**: classifying a sheet's ends geometrically rather than from the declared `closed`, so that a
  pole, a periodic axis and a nested envelope each behave (measured in *Batch two*).
- **`crease`'s `radius`**: `doc/skein-decisions.md`, **Not decided**.

## POC decisions

The renderer proof of concept (`source/core/shape/skein.cpp`, `source/parser/parser_skein.cpp`) covers
`skein { expressions { ... } closed ... ends ... }` with `scale`, `rotate`, `translate`, `matrix`, `transform` and
the extrude, then spelt `fold`, with `axis along origin radius twist arc start`. Where the design was silent it takes the simplest consistent
answer; **Flag** marks the ones where the grammar or the maths felt wrong.

- **Only `skein` is reserved.** `expressions`, `extrude`, `closed`, `ends`, `flat`, `pole`, `axis`, `arc` and `uv`
  match by spelling inside a skein block, so existing scenes keep parsing; `closed` is also a `path` word, in
  another block, which spelling alone separates. (The POC also took `origin`, `twist`, `along` and `start`, all
  since removed.)
- **Affine steps are POV's own transforms** (degrees, `rotate` x then y then z); neighbouring ones merge into one
  matrix. `about` follows a plain `scale` or `rotate` (build-out).
- **Extrude reads the point as cylindrical coordinates in its own frame and emits it as Cartesian about the axis.**
  The three incoming components each become one coordinate: p·along is the angle (p·along × arc), p·axis the
  travel along the axis, and p·(along × axis) adds to the radius. It places
  axis·t + along·ρ cos + (axis × along)·ρ sin with ρ = radius + w. `arc` is in degrees, 360 by default. `along`
  is not along the axis: it is the direction whose coordinate is wrapped into the angle, and where angle zero
  points. It is derived rather than given — y for an axis along x, x for one along y — since those are the only
  straight axes the step allows. (The POC also took a `twist` angle, an `origin`, a given `along`, a `start` angle
  and a travel along the axis, all since removed; the travel is now a `bend` step.)
- **A function value names its inputs:** `function(v) { ... }` or `function(u, v) { ... }`. Parameters bind by name
  to `u`, `v` and `x`, `y`, `z` (the incoming point); a bare `function { }` is refused, as is one taking both `u`
  and `x`, or `v` and `y`. **Flag:** both rules exist only because of the VM's register aliasing (see the findings).
  Superseded by grouped inputs (build-out); the extrude's own coordinates (`theta`, `s`, `t`, `w`) are deliberately not inputs (see Grouped inputs);
  at most 16 function values per skein in the POC. The build now allows 32, and a 33rd is a parse error.
- **Derivatives** are forward-mode through affine and extrude steps, exact. A function value's slope is a central
  difference in each declared argument (step 1e-7, taken across the wrap when `u` or `v` wraps), chained with that
  argument's own derivative: a known limit, noisy only at corners.
- **Topology is declared, then checked.** `closed u|v|uv`; `ends` takes one value for both ends or two (v = 0, v = 1)
  and applies only when u wraps and v does not. A wrapped seam open by more than 1e-6 of the size is a parse error
  naming the seam, the gap and where; `ends pole` must close to a point; `ends flat` needs a planar end ring that
  winds once around its centroid (a star-shaped section).
- **Orientation is found, not assumed.** Outward is the sign of the wall's flux about the bounding-box centre, and
  caps face away from the wall, so a clockwise section is not inside out.
- **A closed solid** means `closed uv`, or `closed u` with both ends flat or pole; anything else is a patch object with no
  inside. Inside is the nonzero winding number of the crossings of one fixed ray. A closed skein reports only the
  crossings where inside changes, so self-overlap is clipped; an open one reports every crossing.
- **Caps** lie in the end ring's plane. A cap hit is inside when it is no further from the centroid than the ring
  where the ring passes the same angle (bracketed from 64 samples, refined by Newton). Its uv is that ring u and
  the end's v.
- **Intersection is exact per patch.** A uv grid (patch count chosen so the sampled normal turns at most 11.25° per
  patch along each parameter, 2 to 64 a side (256 since the build-out, for the knot), at least 4 where it wraps) under a bounding tree. In each patch the ray meets, Newton solves
  S(u, v) = O + tD from the centre (at most 16 steps, residual below 1e-10 of the bounding diagonal). A patch splits
  in four, to depth 6, where Newton fails or the ray grazes; a root met more steeply than the patch's flatness
  allows is its only one. Hits within 1e-7 of the diagonal with the same sign merge. A patch still unsolved at depth
  6 is counted as unresolved and treated as a miss.
- **Creases**: a patch whose sampled normals spread more than 22.5° is creased. It always splits, at the deepest
  level Newton also starts from its corners, and a root reached across a turn of the normal settles no patch.
  Within one slope step (1e-5) of a crease, differenced slopes straddle it and Newton converges only linearly; one
  that stalls within 1e-4 of the diagonal of a root takes its slopes again at a 1e-8 step and goes on. Every
  crossing counts: inside is counted back from the ray's far end, so one unpaired crossing flips every hit before it.
- **Bounds.** Patch boxes come from interval arithmetic through the chain: exact intervals for the affine steps
  and for an extrude's cos and sin, conservative up to rounding (padded by 1e-9 of the diagonal). A function value of
  `u` and `v` only that the VM's range plan covers is enclosed by `EvaluateRange`, conservatively. Any other function
  value is sampled (5 × 5 per grid patch, 3 × 3 per split patch) and padded by 25% of its spread plus 1e-6 of its size: **not
  conservative**. Where no extrude reads a function, the patch is also bounded by the slab about its centre tangent
  plane, half-thickness Σ (half width × largest slope across the plane) from interval derivatives: conservative by
  the mean value theorem.
- **Statistics**: `Skein`, `Skein Bound` and `Skein Newton` rows, plus Newton steps, chain evaluations and
  unresolved patches; user functions show in `Function VM calls`.
- **Deferred** in the POC and since built (below): the extrude around a path axis, declared `expressions` groups,
  `displace`, `crease`, `path`, `range`, normals as inputs and `about`.

### Syntax build-out

Grouped inputs, `displace`, `map`, function-valued transforms, `path` and declared groups, in
`skein.cpp`, `parser_skein.cpp` and a switch in the function compiler (`fncode.cpp`, `parser_functions.cpp`).

- **Grouped inputs.** A skein function compiles through `FNCode::GroupedParameter`: `uv` becomes the stack parameters
  `uv.u uv.v`, `pos` becomes `pos.x pos.y pos.z`, `norm` becomes `norm.x norm.y norm.z`. Every parameter, the bare
  shorthands `u v x y z nx ny nz` included, is read from the stack, so `function(v, y)` no longer aliases. The
  expression parser reads `group.member` only while a skein function is parsed (`Parser::mFunctionGroups`), and `t` is
  a plain name there. Other functions and scenes take the old path.
- **The normal input** is the unit ∂S/∂u × ∂S/∂v of the chain before the step, the previewer's convention (outward for
  a counter-clockwise extrude), nudged 1e-6 inward in v where the surface pinches. It is computed only for a step that
  reads it. **Flag:** a value reading `norm`, and every `displace`, take their u and v slopes from second-order
  differences of the chain before the step (step 1e-5, central inside, one-sided at an open edge): four extra prefix
  evaluations, multiplied again by each earlier normal reader. Other function values keep the POC's per-argument
  central differences.
- **`displace <value>`** moves the point along that normal by one value (constant, function, map or path). Its box is
  the incoming box plus an enclosure of the normal times the value's range. The normal enclosure comes from interval
  slopes when every earlier step is affine or a constant straight extrude, and is then conservative; otherwise it is
  3 × 3 sampled normals padded by half their spread plus 1e-3: **not conservative**. A braces form summing several
  values (the two-map `displace` sketched above) is not implemented.
- **`map { ... }`** is a postfix program: an input pushes a value, a modifier pops its arity and pushes one. Each
  input may carry its own chain before a combining modifier; a modifier with more inputs than are waiting, or a map
  left with other than one value, is a parse error. Inputs are `uv.u` to `norm.z` or the bare shorthands.
  Modifiers: `linear { scale offset }`; `sin` and `cos { frequency amplitude phase }`, amplitude ·
  sin(2π(frequency · x + phase)), cycles as in POV's pattern `frequency` and `phase`; `length` and `atan2` of two
  inputs (radians, first input the y); `range`; and `path { ... }`, a scalar path looked up at the value (the
  missing `by`). There is no three-input modifier yet. Derivatives are exact (dual numbers) and bounds are interval
  arithmetic over the patch, conservative up to rounding.
- **`range { from a, b  to c, d  repeat | mirror | clamp }`** as specified above, both ranges 0..1 by default, a
  zero-width input range a parse error. **`range` and `repeat` work as keywords inside `map`:** without a `#` they
  are plain `RANGE_TOKEN` and `REPEAT_TOKEN` tokens. The handlers at `parser_tokenizer.cpp` 1175 and 1218 run only
  after a `#`, and a skipped branch fast-forwards to the next `#`. `tests/render/skein_keywords.pov` uses both in
  taken and skipped `#switch`/`#range`, `#if` and `#while` branches. No workaround was needed. The one limit is
  POV's own: a reserved word cannot be a macro argument, so a macro cannot pass `repeat` in as a parameter.
- **Function-valued transforms.** `translate { x V  y V  z V }`, `scale { x V  y V  z V  about <c> }` and
  `rotate { axis <a>  angle V  about <c> }` (degrees) take any value per parameter; an all-constant braces form becomes an
  ordinary affine step. `about` also follows a plain `scale` or `rotate`. Their bounds are intervals (conservative);
  like any non-constant step they end the tangent-slab cull.

- **`path { ... }`** takes an optional interpolation word, points, and `closed` or `arclength` anywhere. A point is a
  number or a vector, a vector optionally preceded by its parameter value (`0.8, <2, 0, 1>`) and followed by
  `handle <h>` (out handle h, in handle −h) or `handles <in>, <out>`. Handles are offsets from the point, as in a
  drawing program. Every segment is a cubic Bézier. A handle sets its side; otherwise the interpolation word
  decides. `linear_spline` (the default) gives exact linear interpolation, which settles the plain-point question
  above: a plain point means "no handle information, the keyword decides". `cubic_spline` is Catmull-Rom with
  non-uniform spacing and reflected ends. `natural_spline` is the C2 natural spline, periodic when closed: the
  previewer's `cubic`, and the vase uses it. `quadratic_spline` follows POV's `spline`, a segment taking the
  parabola through the previous, this and the next point.
- Parameters run 0..1 uniformly unless every point gives one. Given ones must increase, and a closed path's start at
  0 and stay below 1, where it closes. Outside its range an open path clamps (slope 0) and a closed one wraps.
  **Flag:** a scalar path takes no parameter values, since `0.2, 0.5` cannot say whether 0.2 is a parameter or a
  value. `arclength` reparameterises a vector path by length over the same range (Simpson per table cell, a Hermite
  inverse with one-sided speeds at the knots); on a scalar path it is a parse error. A scalar path as a value is
  read at v; a `path` modifier in a `map` reads it at anything. Its bound is the control polygon of the piece of each
  segment in the range (blossoming), conservative; with `arclength`, widened by a table cell.
- **Curved extrude axis** (history: removed from `extrude` when `bend`'s `along` took it over, see *Renamed* and
  *`bend`: the rename*; the frame machinery here is what `bend`, `crease` and `curl` use now):
  `axis path { ... }` (vector points) or `axis function(t) { }, function(t) { }, function(t) { }`.
  As in the previewer the extrude reads the incoming point against y and `along`; the path parameter is
  t = p·y, and `origin` does not apply. The frame is rotation-minimising (double reflection over 2048
  samples, interpolated and re-orthogonalised against the live tangent), its zero angle `up` (default +z) projected
  off the start tangent. A closed axis (ends within 1e-9 of its size) spreads the leftover turn evenly along t, so
  the seam closes. The frame's own derivative is ω × (frame), ω = T × T' + the correction rate: the table
  interpolation makes it approximate, which Newton tolerates. **Flag:** a function axis takes its first two
  derivatives from central differences (step 1e-5). **Flag, not conservative:** a curved extrude's box is the axis hull
  over the patch's t range plus the section swept by the frames at both ends and the middle, padded by the
  second-order drift (Ω·half range)²/8 × reach, Ω being 1.5 × the largest turning rate sampled at the table's
  points; a function axis's hull is 9 samples padded by a quarter of the largest sampled |P''| × the spacing².
- **Trefoil** (2, 3), radius 0.05, both axes wrapped: no seam (the parse-time seam check passes at 1e-6 of the size),
  and rays from 401 centre-line points, including the seam at t = 0, 1/3, 2/3 and 1, leave the tube at 0.05 within
  0.77e-9. The previewer's trefoil is 31.8986 long, so π r² L = 0.25053; its 48 × 960 mesh holds 0.24982. Crossings of
  300 × 300 rays along z (`skein_knot.pov`, `Volume=300`) give 0.25038, 0.06% under π r² L.
- **`#declare G = expressions { ... }`** is a new identifier type (`EXPRESSIONS_ID_TOKEN`, an `Assignable`). Used as
  a step, its steps are spliced in, neighbouring affine steps merging; groups nest, copy with `#declare H = G;` and pass
  to macros. Function slots are assigned per skein when it is prepared, so one group can serve several skeins. The only
  change outside the skein: `#declare X = expressions` with an undeclared `expressions` now parses a group where it
  used to be an error.
- **The hinge step, then `bend { origin side toward radius angle }`** (history: renamed `crease`, and its `origin`,
  `side` and `toward` replaced by `axis`, see *`crease`: the axis*) follows the previewer: points past the plane through `origin` facing
  `side` roll onto a cylinder of `radius` touching the sheet along the hinge, curling `toward` (default +z), keeping
  arc length; past `angle` (degrees, unlimited by default) they run straight. `radius` and `angle` take values. Its box
  is interval arithmetic over the roll, conservative.

### Build-out checks, cost and shapes

`tests/render/skein.sh` now also runs `skein_keywords.pov` and `skein_syntax.pov` and 16 more pixel pairs. Against
primitives, with the POC's `Check` (hits, normals and `inside()` within 1e-6): the frustum from `function(uv)`,
`function(pos)`, `function(v, y)`, a `linear` map, a reversed `range`, a `scale` map after the extrude, a scalar path,
an `arclength` path axis and a group used twice; the tube from `function(norm)`, a straight path axis with handles and
a declared group; the torus displaced by 0.1 (0.73e-9 off the exact surface, where the `torus` primitive is 109e-9
off); the tube displaced by a `length` map and by a nested group passed through a macro. Against implicit surfaces
(`SurfaceCheck`, hits within 1e-6 of the surface and normals within 1e-6 of its gradient): a tube displaced by
`function(norm, pos)`, swayed by `sin` and `cos` maps, ridged by an `atan2` map, lobed by a repeated u, and a bicone
from a mirrored v, all within 0.3e-9 and normals within 2.2e-9. The shapes with a primitive also render
pixel-identical to it at 96 × 72 (the 16 new pairs, one a cutaway of the displaced torus). The vase's natural spline matches the previewer's radius at seven heights within 0.15e-9; the
bent square's flat, arc and straight probes are within 0.14e-9.

Cost: on the POC's nine counted renders (320 × 240, one thread, `-A`) every counter is unchanged, rays, bound tests,
Newton solves and steps, chain evaluations and VM calls alike. Trace time over the nine, two interleaved runs of each
build through the queue on the POC's own scenes: 8.82 and 8.63 s for the POC, 8.69 and 8.77 s now. Seven scenes
without a skein (biscuit anti-aliased, chess2, isocacti, primitiv, torus2, isosurfaces, random_effects; one thread,
16-bit) render bit-identical between the POC build and this one, and the POC build was bit-identical to the base.

Shapes, each a short skein source in `tests/render`, rendered 400 × 300 at `+A0.3` with a build
at commit `c63addf5` (four threads):

| scene | what it uses | trace |
|---|---|---|
| `skein_donut.pov` | extrude of an extrude; `displace function(norm, pos)`: icing from `norm.x`, domed sprinkles from a crackle F1 of `pos` | 52.1 s |
| `skein_log.pov` | a radius function of v and a twist as a phase of u along v (an extrude `twist` when measured, then a `rotate`); `displace function(uv)`, a crackle of uv on a circle embedding, faded at the flat ends | 104.6 s |
| `skein_vase.pov` | a natural-spline `path` radius, `ends flat, open` | 0.2 s |
| `skein_coil.pov` | extrude of an extrude with `arc 6*360` and a `bend` step climbing with v | 2.0 s |
| `skein_helix.pov` | two extrudes around helical function axes and 14 rungs, unioned | 19.4 s |
| `skein_column.pov` | a `sin` function of u for flutes with a quarter-turn twist as a phase of u along v (a `rotate` since batch two), a hexagonal plinth | 1.0 s |
| `skein_knot.pov` | the trefoil, an extrude around three functions of t, `closed uv` | 252.6 s |
| `skein_page.pov` | a declared sheet group and a corner `crease` | 0.9 s |

The table is as measured then. Since, the knot and the helix are straight extrudes carried by `bend { along }`,
and the page is a `curl`.

Volumes from ray crossings (`Volume=N`), against the previewer: the knot as above. The coil from 300 × 300 rays holds
0.45476; a disk swept by a screw motion keeps its area times its centroid's circle, π · 0.08² · 2π · 0.6 · 6 = 0.45482
(−0.013%), and the previewer's spring mesh holds 0.45178 because its 32-gon section covers 0.9936 of the circle
(0.45470 corrected). The donut without sprinkles (`Sprinkle=0`) holds 5.18447 from 150 × 150 rays against the
previewer's 5.18310, 5.18402 corrected for its 200 × 320 polygons.

Findings from the build-out:
- Steps without interval slopes (function values, `displace`, curved extrudes) have no tangent-slab cull, so near misses
  split to depth 6. A displacement padded by its full amount in every direction never shrank with the split (47.9 s
  for the displaced torus at 96 × 72); bounding the normal over the patch brought it to 0.27 s. The knot's function
  axis costs 9 VM calls a point and dominates its 253 s; a `path` axis evaluates without the VM.
- `ends flat` needs a planar ring, so a displacement must fade to zero at a flat end unless the end wall's normal
  lies in the end plane; the log fades over the last 5% of v. A step-valued displacement (the previewer's
  sprinkles) tears a closed surface; the donut's sprinkles are domes.
- Sampled bounds for a function of `pos` or `norm` can miss features smaller than the sample spacing (5 × 5 a grid
  patch, 3 × 3 a split patch); the donut's sprinkles survive because the bumps' own normal turns refine the grid.
- **The log's seam (open).** Feeding crackle the circle embedding (3 cos 2πu, 3 sin 2πu, 9v) closes the u seam, but
  the embedding is a cylinder of fixed radius while the log tapers by 30%, so the cracks stretch toward the thin
  end, and their aspect depends on the 3 against 9 chosen by hand. A periodic pattern on a wrapped axis, or a
  `sample uv` form that does the embedding at the surface's own scale, is still open. Measured in *Bark cracks on a
  taper* below.

Known limits after the build-out:
- **Not conservative** (each flagged above): sampled ranges of function values outside the VM's range plan (as in the
  POC), the sampled normal enclosure of a `displace` after a function-valued or curved step, the frame drift of a
  curved extrude, and a function axis's hull.
- **Differences, not derivatives:** steps that read the normal (four extra chain-prefix evaluations each, compounding
  when such steps nest), function axes, and function values (the POC's per-argument differences).
- **Not built:** vector-valued functions, `lathe`, `spherical`, `morph`, three-input modifiers (noise, 2D patterns,
  images), a `sample` form, a summed `displace`, the extrude's own coordinates as inputs (dropped on purpose; the
  coil writes its climb as a map of v), parameter values in a scalar path, and a declared function as a
  value (`radius F`). Batch two built `morph` as `expression_map`, the samplers, `sample`, `sum` and declared values;
  `lathe` and `spherical` stay recipes; vector-valued functions and scalar-path parameters remain.
- Next, by expected gain: interval derivatives through maps and paths (dual intervals), so those steps keep the
  tangent-slab cull; a cheaper frame for function axes (or converting them to a path at parse time); then the POC's
  list (interval derivatives for VM functions, range plans for `cos`, `mod` and `pow`).

### Torus cost comparison

`tests/render/skein_torus.pov` renders one torus (major radius 2, minor 0.5, about the axis the extrude now takes)
three ways with the same camera, lights and finish: the skein (`Object=1`), the `torus` primitive (2) and an
`isosurface` (3). The isosurface is tuned as a fair opponent: the squared distance to the tube's centre circle,
`contained_by` the torus's own bounding box, `max_gradient 4.2` and `accuracy 1e-4`. The gradient of that function is twice the distance to the tube's
centre circle, at most 2·√(2² + 0.5²) = 4.12 inside the box (on the axis), so 4.2 is safe with 2% to spare; POV's
own report of 2.88 is only the largest it met on these rays. An `accuracy` of 1e-4 puts each hit within 1e-4 of the
surface along the ray, about a fortieth of a pixel at this size, and the finite-difference normal steps the same
distance. 1280 × 960, one thread, one release build (`tools/povray-image`) through the render queue, wall clock the
minimum of three runs beside other work in the queue's second slot:

| | skein | `torus` | isosurface (4.2, 1e-4) | isosurface, `max_gradient 6` | isosurface, `accuracy 1e-3` |
|---|---|---|---|---|---|
| object tests, `-A` | 1633246 | 1633246 | 1615257 | 1615257 | 1548517 |
| work per test, `-A` | 9.5 Newton steps, 11.2 chain evaluations, 61 bound tests | one quartic | 33.9 function calls | 44.2 function calls | 23.2 function calls |
| wall clock, `-A` | 5.17 s | 0.52 s | 3.76 s | 5.01 s | 2.62 s |
| pixels differing from `torus`, `-A` | 0 | — | 2.67%, 0.46% by over 5%, max 81/255 | 2.67% | 7.08%, 0.27% by over 5% |
| object tests, `+A0.3` | 1848866 | 1848868 | 2003868 | 2003868 | 1880941 |
| supersamples, `+A0.3` | 73944 | 73944 | 153648 | 153648 | 131247 |
| wall clock, `+A0.3` | 7.74 s | 0.70 s | 6.57 s | 8.47 s | 4.20 s |
| pixels differing from `torus`, `+A0.3` | 1 (10/255) | — | 2.98%, 0.39% by over 5%, max 47/255 | 2.98% | 7.27% |

The skein matches the primitive pixel for pixel at this size (one anti-aliased pixel apart) and costs about ten
times as much; against the isosurface it takes 1.4 times as long without anti-aliasing and 1.2 times with it, at
a third of the function evaluations but sixty bound tests a ray. The isosurface differs along the shadow
terminators, where hits up to `accuracy` inside the surface shadow themselves into speckle, and along the
silhouettes; its noise also draws twice the supersamples. The bound tests are the skein's next cost to cut (an
ordered descent of the patch tree, or fewer, tighter patches).

### Bark cracks on a taper

The log's open seam question, measured. `tests/render/skein_bark.inc` cuts one crackle into a groove (`F2 − F1` under
0.12, depth 0.07 R, faded over the end 5% of v so the flat caps stay planar) on two shapes of length 4: a log tapering
linearly from radius 0.6 to a 0.04 needle (`skein_bark_taper.pov`) and a rod R = 0.3 + 0.25 sin 5πv, three bulges of
0.55 and two necks of 0.05 with 45° flanks (`skein_bark_rod.pov`; `Bent=1` extrudes it round a 45° `path` arc instead).
A 240° twist follows the displace. Only the point fed to the pattern changes (K = 5 texture units per unit, grain
G = 2.5, so cracks run 2.5 times longer along the axis):

1. **fixed circle**, the present log: (K R₀ cos 2πu, K R₀ sin 2πu, K L v / G), R₀ the mean radius;
2. **tracking circle**: (K R(v) cos 2πu, K R(v) sin 2πu, z), z = K L v / G (2a) or K s(v) / G with s the meridian
   arclength, a Simpson `sum` in the function (2b);
3. **`pos`**: (K x, −K z, K y / G) of the point entering the displace.

`skein_bark_check.pov` differentiates each mapping T against the surface S at the displace (before the twist):
u scale = |T_u| / |S_u| / K and v scale = |T_v| / |S_v| · G / K, both 1 as designed; grain is their ratio, 1 when the
cracks keep their designed elongation. Over v = 0.1 to 0.9:

| method | log u scale | log v scale | rod u scale | rod v scale | rod grain |
|---|---|---|---|---|---|
| 1 fixed circle | 0.59 to 3.33 (8.0 at the tip) | 0.99 | 0.55 to 6.00 | 0.71 to 1.00 | 0.55 to 6.00 |
| 2a tracking, linear z | 1.00 | 1.05 | 1.00 | 1.00 to 1.89 | 0.53 to 1.00 |
| 2b tracking, arclength z | 1.00 | 1.06 | 1.00 | 1.00 to 2.02 | 0.50 to 1.00 |
| 3 `pos` | 1.00 | 1.05 | 1.00 | 1.00 to 1.89 | 0.53 to 1.00 |
| fixed circle, arclength z (not rendered) | 0.59 to 3.33 | 1.00 | 0.55 to 6.00 | 1.00 | 0.55 to 6.00 |

With round cracks (G = 1) 2a and 3 read 1.000 everywhere on both shapes, and 2b reaches 1.22 on the rod's flanks.

- **2a is `pos` in disguise.** On a straight axis the tracking circle with linear z is the incoming point scaled by
  K(1, 1, 1/G), axes swapped: the two render pixel-identical (0 of 120000 differ, log, rod and seam views alike).
- **Arclength z counts the flank twice.** The tracking circle already moves radially by K R′ per unit v, so arclength
  in z gives |T_v|² = K²R′² + (K/G)²(L² + R′²) where the surface wants (K/G)²(L² + R′²). Linear z is already the
  arclength-correct choice. 2b is worse than 2a everywhere: 7% on the rod's flanks (22% at G = 1), 1% on the log. It
  is also the slowest by far, the 33-term sum running in every displacement call. Arclength z suits the fixed
  circle, which has no radial motion (v scale exactly 1), but that one gets u wrong.
- **Grain has a ceiling on steep flanks.** A seamless circle in an isotropic 3D pattern moves at least K|R′| per unit
  v, so an elongation G survives only where |R′|/L < 1/√(G² − 1), 0.44 for G = 2.5. On the rod's 45° flanks 2a, 2b
  and 3 all give round cracks (grain 0.50 to 0.53). Keeping grain there needs a pattern periodic in u.
- **The fixed circle** is right only at R₀: cells 1.7 times too wide at the log's base and 8 times too narrow at the
  needle, below a pixel, and 11 times apart between the rod's bulges and necks.
- **No seam.** Over 1001 values of v, the texture point and the pattern change by at most 1e-15 across u = 0/1 for
  every method on all three shapes. The control, u fed straight in, jumps 10.05 in texture space and 0.54 in the
  pattern, and the skein refuses it at parse ("open by 0.0231652 at v = 0.375"). In close-ups with u = 0 facing the
  camera (`Seam=1`) the step across the seam row is ordinary: it ranks 28th to 32nd of 41 neighbouring rows.
- **Twist.** For the uv methods the extrude's own twist and a twist step after the displace give the same image (3 pixels
  differ, by at most 4/255), but the later step costs 2.6 times the chain evaluations (1.39e9 against 0.53e9 on the
  log). `pos` follows only the later step: under the extrude's twist the round tube is unchanged, and `pos` renders 40
  pixels (anti-aliasing) from `pos` without any twist.
- **Bent axis.** The uv methods follow the material: their v scale runs from 0.90 outside to 1.12 inside the bend at
  the bulges, wood compressed on the inside. `pos` is world space: exact in scale through the bend, but its grain
  stays on world y, 18° off the axis at v = 0.1 and 0.9 and down to 0.82 inside and outside.
- **Cost**, trace time at 400 × 300 `+A0.3`, two jobs side by side: log 228 s (1), 314 s (2a), 817 s (2b), 290 s (3);
  rod 520 s, 915 s, 5429 s, 813 s; the log with the extrude's twist 131 s (2a), without twist 63 s (3). The bent rod at
  `-A`: 398 s (1), 667 s (2a), 554 s (3), with twenty times the straight rod's unresolved patches.

**Recommendation: the tracking circle with linear z (2a), twisted by the extrude.** It is seamless, scale-correct in u
and v for round cracks, follows the material through an extrude twist and a curved axis, and renders the same image as
`pos` before a later twist at well under half the cost. `pos` is the easy equal on a straight axis when no twist is
needed or the radius is not at hand. Never arclength z with a tracking circle. Strong grain on steep flanks needs a
u-periodic pattern; no circle embedding gives it.

What felt wrong in the syntax:
- The tracking circle restates the extrude by hand (cos 2πu, sin 2πu and R(v)), as the universal-inputs rule intends,
  so R must exist as a declared function: a `path` radius cannot be called from a function. On a straight axis
  `sqrt(pos.x*pos.x + pos.z*pos.z)` reads it back, but that is `pos` again.
- A profile's arclength has no expression (`arclength` applies to vector paths), so 2b integrates by hand in a `sum`,
  the slowest thing here (5429 s against 915 s on the rod).
- A twist after a curved extrude has no step (`rotate` takes a fixed axis), so on a bent rod `pos` cannot be twisted.
- The seam check samples 9 values of v, and a crack displacement is zero over most of the surface, so a seamed
  pattern can slip through; this one did not.
- Outside the skein: re-including a file of function declarations needs `#undef` first, and SDL's `? :` evaluates
  both branches.

### Batch two: value expressions, samplers, envelope and expression_map

Checks are in `skein_values.pov` (run by `skein.sh`); **Flag** marks what felt wrong or is not conservative.

- **`sum`, `product`, `min`, `max`** are values: `sum { entry ... }`, each entry any value (constant, function, map,
  path, a nested sum), all evaluated at the same inputs. Slopes follow the sum and product rules; `min` and `max` take
  the winner's slopes, so they crease where entries tie. Bounds are the interval sum, product, min or max of the
  entries' ranges, conservative where each entry's is. A copy of a sum is deep, so a group used by two skeins numbers
  its functions per skein.
- **Lookahead for value words.** `sum`, `product`, `min`, `max`, `map` and `path` start a value only when `{` follows;
  otherwise the word and the token after it both go back to the tokenizer (the second as a raw token, the word's
  parsed token restored), so `min(0.1, 0.2)` and a float declared as `product` still read as floats. `sum`, `min` and
  `max` are reserved (`sum` only inside function bodies); `product` is free. **Flag:** the token after the word cannot
  be an array or dictionary element, whose own lookahead holds the tokenizer's one slot; that is a parse error naming
  the word.
- Check: a radius `sum` of a constant, a `sin` map, a function and a path is off the exact sum by at most 0.20e-9 (one
  function: 0.25e-9); `product`, `min`, `max` and a nested sum by at most 0.06e-9. A torus displaced by a `sum` of two
  maps and a function against one function computing it: 2520 rays, hits within 1.01e-9 and normals within 1.26e-9,
  `inside()` equal at 1728 points; a tube translated by a `sum`, hits within 0.49e-9.
- **Samplers as map modifiers.** A bare group in a map pushes all its members (`uv` two, `pos` and `norm` three).
  - `noise { frequency amplitude }` (three inputs) is POV's own lattice noise, the one `bozo` and `f_noise3d` use
    (`PortableNoise`, the range-corrected generator 2: the `hashTable` lattice hash `Hash2d`/`Hash1dRTableIndex` into
    `RTable`, s-curve weights), centred as amplitude · (2N(frequency · p) − 1). It is re-derived in the skein with its
    exact gradient. **Flag:** it always uses generator 2, whatever `global_settings { noise_generator }` says.
  - `fbm { octaves lacunarity gain frequency amplitude }` (defaults 4, 2, 0.5, 1, 1) is the sum of that noise over
    octaves, frequency times lacunarity and amplitude times gain per octave, on the same core.
  - `cells { frequency amplitude form <a, b> }` (two inputs) is amplitude · (a F1 + b F2), F1 and F2 the distances to
    the nearest and second-nearest nucleus, `form` as in `crackle` (default `<1, 0>`, F1; `<-1, 1>` is the crack
    width). The nuclei are POV's crackle nuclei (`PickInCube`: `Hash3d` into the pattern random table) of the cells at
    z = 0, so the plane sees one point per unit square. Slopes are exact (the unit vectors from the nuclei) except on
    the cell edges, which are creases.
  - `image { <file or function image> interpolate 2|3 }` (two inputs) reads the image through `Parse_Image` and
    samples it as `image_pattern` does: greyscale, x across and y up, one tile per unit, repeating outside 0..1 (so
    `range` mirrors or clamps but is not needed to tile), clamped to 0..1. Bilinear (2, the default here; POV's default
    of none would tear a displacement) is creased at texel edges; bicubic (3) is smooth. Slopes are exact within a
    texel.
  - Bounds, all conservative: noise and fbm by the centred form, the value and gradient at the input box's centre
    plus H r²/2, H a bound on the gradient's Lipschitz constant (Gershgorin over the Hessian from the s-curve's
    s'' ≤ 6, the table's full entries ≤ 1 and half entries ≤ 0.5: about 60 per unit), or a slope bound times the half
    diagonal r (about 9.8 per unit), whichever is smaller, clipped to 0..1 per octave; cells from the value at the
    centre with slope |a| + |b|, clipped to its largest possible value; images from min and max pyramids over the
    texels the interpolation touches (bicubic widened by its 0.28 overshoot). The slope bound alone left a boulder (a
    sphere displaced by three octaves, 100 × 75) at 4167 Newton steps a ray, 3.2M unresolved patches and 413 s; the
    centred form brings it to 269 steps, 29k unresolved and 24.8 s.
- Check: a tube displaced by `noise` of `pos` is within 0.25e-9 of the radius `f_noise_generator(..., 2)` gives, and
  against the same displacement as a function of `pos` its hits are within 1.7e-9 and normals 3.3e-9 (the function's
  slopes are differences). `fbm` with three octaves, and a `sum` of three `noise` maps, both within 0.25e-9 of three
  `f_noise_generator` octaves. A sphere from an extrude with a `bend` travel and pole ends, displaced by a `sum` of three noise octaves, is
  closed: `inside()` matches the crossing parity along two directions at 1000 of 1000 points. A sheet displaced by
  `image` of `Mount1.png` lies on 0.2 · `image_pattern` within 6.0e-9 (bilinear) and 6.0e-9 (bicubic), POV's float
  colours; its normals match differences of the hits at 1000 of 1000 points. The same displacement written as a
  function of `image_pattern` misses 580 of 4000 rays: its range is sampled, the known non-conservative case, which
  the pyramid bounds avoid. POV has no 2D Worley function to compare `cells` with; F1 heights stay in 0 to
  amplitude · √2 and the normals match differences of the hits at 1000 of 1000 points.
- **Nested evaluation.** The evaluator now works on scopes: the top chain, or the item list of an `expression_map`
  step inside its parent. "The surface entering step k" of any scope can be evaluated at any `(u, v)`, which is what
  `sample`, `envelope` and nested normal readers need. Each step records the wrap topology of the surface entering and
  leaving it (an envelope's sheet does not wrap along the doubled axis even where the result does), and differences
  and pole nudges use those, not the skein's. Every counter of the existing scenes is unchanged by the restructure.
- **`sample { at <u', v'> }`** evaluates the chain so far at `(u', v')`, each a value of `uv`, `pos` and `norm` at the
  incoming point, and carries on from that point; slopes by the chain rule. Later steps still read the skein's own
  `uv`. The coordinates are used as given: wrap or clamp them with `range`. Its box is the chain so far over the
  interval of `(u', v')` (conservative where those intervals are).
- **`envelope { thickness T  axis u|v  edge round|flat }`** (axis `u` by default, no edge by default) doubles the
  axis: the front face is the sheet moved by +T/2 along its normal, then the back face runs the sheet backwards moved
  by −T/2, so the outward normal is +n on the front and −n on the back. `thickness` is any value; its `uv`, `pos` and
  `norm` are the sheet's, so `sin(pi*uv.u)` tapers to zero at both sheet edges. With an edge, each rim takes 1/32 of the
  doubled axis: `round` is a half circle of radius T/2 about the sheet's edge, `flat` the straight segment between the
  faces. With `closed` on the doubled axis the near edge (sheet coordinate 0) gets a rim too and the result is a loop:
  front from 1/64 to 31/64, far rim to 33/64, back to 63/64, near rim across the seam; without an edge, front 0 to 0.5
  and back 0.5 to 1. Without `closed` the ends of the doubled axis stay apart, as for a bowl along `v` whose sheet starts
  at a pole: its two ends are the outer and inner poles. With no edge, a thickness above 1e-6 of the size where the
  faces must meet is a parse error naming the gap. The faces' slopes are the displacement's second-order differences at
  the sheet's coordinates (one-sided at the sheet's own edges); a rim's are exact across it and differenced along it.
  Box: a face is the sheet's box moved by ±T/2 along the enclosure of its normal (as `displace` does); a rim is the
  edge's box moved by (T/2) cos φ along the normal and (T/2) sin φ along the outward edge direction, over the angle
  interval φ the patch covers. A function of `uv` only is bounded by the VM's range plan over the sheet coordinates it
  actually reads. **Flag, not conservative** where the sheet is not affine or a straight extrude: the normal enclosure is
  sampled (as for `displace`), and the edge direction always is (3 samples, padded by half their spread plus 1e-3).
  A first box padded by T/2 in every direction never shrank below the thickness as patches split, and a rim box
  covering the whole half disc never shrank across the rim: a round-edged slab took 18.7M Newton steps for 3238 rays
  at 80 × 60 (6.4 s); with these boxes it takes 2428 (0.007 s).
- **Grid lines** for a skein with `sample` or `envelope` are placed three quarters by normal turn and one quarter by
  parameter (512 bins on four iso-lines), so a rim's half turn in 1/32 of the axis gets a dozen patches instead of one
  creased one. Other skeins keep the uniform grid, so their patches and counters are unchanged.
- **`ends sealed`** closes an end whose ring folds onto itself, front meeting back (an envelope whose thickness reaches
  zero along that edge): no cap, and Prepare checks the ring at 17 points within 1e-6 of the size. A skein with
  `closed u` and both ends flat, pole or sealed is closed. **Flag:** the word is new; a thickness that is zero only up to
  rounding fails the check when a root magnifies it (`pow(sin(pi*v), 0.25)` leaves 7e-6 at v = 1), so tapers should
  reach zero exactly.
- Without edges an envelope equals `sample { at <map { uv.u  linear { scale 2 }  range { mirror } }, map { uv.v }> }`
  then `displace` by T/2 (the mirrored half runs backwards, so its normal flips), except that T then reads the doubled
  `u`; the rims are what `sample` and `range` cannot place.
- Check (`skein_envelope.pov`): the half tube from `sample at <u/2, v>` matches a half extrude (2887 rays, 1.4e-9), and
  `sample at <u, v>` then `displace` matches the plain displace (0.7e-9). A 2 × 1 sheet with thickness 0.1 and flat edges
  equals the box: 0 off the exact surface, normals and `inside()` identical, volume 0.2 exactly from 1600 rays. With round
  edges it lies on the rounded slab within 0.20e-9, normals within 5.0e-9 of the implicit gradient. A thickness
  0.1 sin(πu) sin(πv) closes the solid with `ends sealed`; its volume is within 0.007% of 2 · 0.1 · (2/π)². A sheet
  curled 286° by `crease` and thickened is closed (1000 of 1000 points) and holds the uncurled volume within 0.06%; one
  thickened to 0.3 past the crease radius 0.12, so its inner face folds through itself, is closed too (1000 of 1000).
- **Finding: grazing rays on thin solids.** The lattice check of `inside()` against the crossing parity along two
  directions disagreed at up to 6 of 1000 points on the tapered envelope, each along one direction only: the points lie
  within 1e-5 of the face and the ray meets it at 3°, or crosses the zero-thickness corner wedge, and the solver misses
  the near-tangent root. A squashed sphere from exact steps misses 3 of 1000 the same way, so it is the solver's grazing
  limit, not the envelope. The check fails on a point that disagrees along both directions (a leak) or on more than 1%.
  One such point did disagree along both: 0.006 from a sealed end whose taper `sqrt(sin(πv))` has an infinite slope,
  where the `inside()` ray grazed. Tapers that reach zero with a finite slope avoid it.
- **`twist` removed** (owner decision, see Dropped above). The option is simply gone. Converted: the twisted tube
  and the twisted tapered hexagon in `skein_shapes.inc`, `skein_column.pov`,
  `skein_log.pov` and the bark include's `TwistInFold`, each to a `rotate { axis y  angle ... }` right after the extrude
  (`skein_column.pov` and `skein_log.pov` later became a phase of u along v inside the extrude, which needs no second step).
  The extrude turned +x toward −z by +angle, and so does the rotate step, so the surface and its `uv` are the same; the
  twisted tube still renders identical to the plain tube, and its uv checker still turns. Against the pre-batch build
  on the originals, all five views render pixel-identical (the twisted tube plain and with its checker and the tapered
  hexagon at 320 × 240, the column and the log at 200 × 150). The rotate step costs more: chain evaluations 2.8 times
  (tube), 3.6 (hexagon), 2.7 (column) and 2.1 (log), because a function-valued rotate ends the tangent-slab cull that a
  extrude with a twist kept, and the looser object box adds ray tests (21% on the tube).
- **`expression_map { [driver] [value item] ... }`** reads like `pigment_map`: entries `[value item]` with an optional
  comma, searched as `BlendMap::Search` does: below the first stop the first item, above the last the last, equal stops a
  hard step, otherwise the two neighbours blended linearly by position. An item is one step, an inline `expressions { }`
  or a declared group; entries made from one declared group are one item, so a plateau (`[0 A] [0.3 A]`) evaluates it
  once. Each neighbour's pipeline maps the incoming point, the results blend as points, and the slopes include the
  weight's change, (B − A) dλ/du. The driver is `v` unless an input is named first as in `map` (`uv.u`, `pos.y`) or any
  value (a map, function, path or sum) is given. Box: the hull of the boxes of the items the driver's range can reach,
  conservative where theirs are; when ranges are sampled every item runs, so each function gets one. An inline item and
  a declared group are boxed alike: each entry is its own copy with its own function values, and sampling runs every
  entry, a group repeated at two stops included, so no value is left unsampled at ±1e10. **Flag:** entries
  must not decrease (a parse error naming the pair); POV's own maps accept them silently and misbehave. `blend_mode` and
  `blend_gamma` are colour options and do not apply.
- **A declared function as a value:** `radius F`, `displace F`. A function identifier followed by `(` is a call inside a
  float expression, as before; otherwise it is the function itself. That takes two tokens of lookahead: the identifier's
  parsed token is restored and the token after it goes back to the tokenizer as a raw token. Its parameters bind by name
  to `u v x y z nx ny nz` (`x y z` when it has no list, so a `function { pattern { ... } }` reads `pos`). The groups `uv`,
  `pos` and `norm` need the function inline, where dotted access is compiled, and a declared function naming both `u` and
  `x`, or `v` and `y`, is refused: POV's legacy compiler loads each pair into one register.
- Check (`skein_blend.pov`): a tube from circle (held to 0.3) to a squircle (0.5 to 0.7) to a five-lobed star (from
  0.9, an inline step) is off the items at the stops by at most 0.29e-9 and off the linear blend between them by 0.31e-9.
  Against one extrude whose radius blends the three, 524 of 700 rays hit both, none only one, points within 0.75e-9,
  normals 2.0e-9, `inside()` at 343 of 343; closure at 216 of 216; the equal-stop step off by 0.004e-9; the `pos.y` driver
  against `v`, points exact, normals 0.25e-9. Box, inline or declared star alike: x −0.4688 to 0.5020, y 0 to 3,
  z ±0.4915 (in `skein.sh`). A declared radius and displacement (`SkeinDeclared` in `skein_shapes.inc`)
  parse and trace: `skein_csg.pov` includes them and passes; their pair check against the same written inline and the
  call form `radius TaperRadius(0.25) + 0.1` (`skein_values.pov`) and the pixel pair (`skein.sh` shape 29) have not run.
- **CSG** (`skein_csg.inc`, `skein_csg.pov` in `skein.sh`, renders from `skein_csg_render.pov`). Constructions 1 to 9 are
  built from skeins and, with `Prim=1`, from the primitives they equal: union and merge of two crossing tori, a frustum
  and a sphere intersected, a torus minus a tube, a box minus a tube, a box with an `inverse` torus inside, a torus
  `clipped_by` a plane and `bounded_by` a box, and a three-level nest (a merged torus and rod minus a frustum and
  sphere intersected). Against the primitive compositions, over 4000 random rays: no ray hits only one, hits within
  0.23e-6 (union), 4.4e-9 (frustum and sphere), 1.0e-9 (box minus tube) and 2.7e-6 where a `torus` primitive takes part
  (its own error, the POC's 2.6e-6), 5.0e-6 for the clipped torus; `inside()` agrees at 3375 of 3375 lattice points for
  every case. Every crossing traced along 300 rays flips `inside()` (no hit on an interior sheet; a plain `union` keeps
  interior surfaces by design, so it is checked against the primitives only). Constructions 10 to 13 exist only as
  skeins: the thick curled envelope minus a sphere, minus a skein tube, intersected with a box, and the self-overlapping
  curl merged with a sphere: each closed (1000 of 1000) with no hit on an interior sheet. An open skein in an
  intersection gets POV's warning for patch objects and acts as one: no inside, clipped by the other operand.
- **Bug found by the CSG check, fixed:** `Skein::Inside` compared a `bool` with `Test_Flag(this, INVERTED_FLAG)`, which
  is the flag mask (4), so an inverted skein was inside everywhere: a skein subtracted in a `difference` or under
  `inverse` cut nothing (a box minus a skein tube kept its top face; 0.9% to 8% of pixels differed). It now compares
  booleans; with it the opaque and see-through renders of cases 1 to 9 are compared again (a scratch contact sheet).
- **Two envelopes, and an envelope on a closed sphere** (a scratch experiment): a sheet with
  0.12 along `u` then 0.03 along `v` (`closed uv`) is a closed hollow pipe whose wall holds 0.1346 against 0.1344 analytic
  (stadium perimeter × length × 0.03 plus the round end rims); with the second thickness 0.2, past the slab's 0.12, the
  inner wall folds through the slab and the winding rule fills the bore: one solid pipe, closed, 0.842. A sphere with
  one envelope 0.1 along `v` and `edge flat` is a closed hollow shell (0.999 of the lattice; 1.2594 against
  4/3 π (1.05³ − 0.95³) = 1.2577), the rim at the pole a zero-area segment. Refused, and why: 0.12 then 0.03 both along
  `u` with `ends flat` (the end ring is two nested loops, not star-shaped, so no flat cap); a sphere along `v` without
  an edge, and a sphere along `u` ("thickness 0.1 where its faces meet"). Both refusals are the slit check being too
  strict: at a pole the edge has no length, and along a periodic axis the sheet has no edge. Smallest fix: classify each
  envelope's sheet ends geometrically (edge, pole or periodic) instead of from the declared wrap; skip the slit check at
  a pole; for a periodic axis make the two passes two closed shells (no rims) with a grid line at 0.5 and check the
  seam per shell. The same classification would give a nested envelope its own loop: today the inner envelope of two
  along one axis does not loop, because the loop is read from the skein's final `closed`.

### `bend`: moving material about an axis, and along a path

```
bend {
  axis y                 // a direction, a path, a sample_path, or three function(t)s
  rotate { angle V }     // turn about the axis, degrees
  translate V            // move along the axis
  scale V                // scale the distance out from the axis
  along <axis>           // follow that path: to its point and orientation at the same arc length
}
```

A step that moves the material it is handed instead of building anything from it. The axis says only where the
transform's origin and direction lie; the transforms themselves are values, so `angle`, the shift and the factor
take a constant, a function, a `map`, a scalar `path` or a `sum`, like every other parameter. `extrude` spends one
direction of the sheet, mapping its extent into an angle and replacing its distance from the axis with `radius`;
`bend` keeps the shape and transforms it. Four built-ins: the twist, the coil's climb, a taper and the path. Any of
them may be given, in any order. The turn and the scale commute, since the turn fixes the axis and the scale is the
same in every direction about it, and both act on the incoming distance before the shift is added. `translate` and
`scale` need no parameter word: inside a `bend` the axis is given, so each has one meaning available.

- **The axis** (`Parse_Skein_Axis`): a direction, which passes through the world origin, a `path` or `sample_path` of
  vectors, or three `function(t)`s. A curved axis forces the axis direction to y, and a `sample_path` is placed against
  the surface entering the step when the skein is prepared. `extrude` no longer takes any of the curved forms: a path
  is `along`'s job.
- **Straight axis.** The point turns about the axis line through the world origin by `angle` degrees, its distance out
  from the axis is multiplied by the scale, and it shifts by axis · the translate, each evaluated per point from the
  usual inputs. So about y, `bend { axis y  rotate { angle V } }` is the `rotate { axis y  angle V }` step,
  `bend { axis y  translate V }` is `translate { y V }` and `bend { axis y  scale V }` is `scale { x V  z V }`; the
  coil's climb is `bend { axis x  translate ... }` after the extrude.
- **The lengthwise scale is gone.** "Scaling along the axis actually makes no sense. The axis already has length."
  It was also inert in the common case: on a path axis parameterised by y the offset along the tangent is zero by
  construction, so it bit only on a bowed axis. Its value slot goes with it, leaving three.
- **Curved axis.** The station is t = p·y, the rule the extrude took for its curved axis, clamped to the axis's parameter
  range: past the end of an open path the path keeps no tangent, so the station stops there with slope zero, as the
  path's own values clamp. At that station the point turns about the axis's tangent, through the axis point, shifts
  along that tangent, and has its offset from the axis point scaled across the tangent. That is the twist that
  `rotate`, with its fixed axis, could not give a bent rod — the limitation recorded above. A turn about the tangent
  turns the rotation-minimising frame's two radial components and leaves the tangent one, so where the frame's zero
  angle points, and its closure correction, cancel out of the point and its slopes alike. (History: the lengthwise
  factor, `scale { along }`, since dropped, scaled the offset measured along the tangent *from the axis point*, which
  is zero by construction where the station rule already lands the axis point at the point's own tangent coordinate,
  as on a path along y parameterised by y; it bit only where the tangent leaves y.)
  **Flag:** the station is the point's y coordinate in space, not the axis parameter the material came from. The two
  agree where y is that parameter — the sheet before any extrude, a straight tube about y — and differ otherwise by the
  y component of the point's offset from the axis, at worst r |T × y| on a tube of radius r. So a constant turn about a
  curved axis deforms a tube built about that same axis rather than leaving it alone: each point takes its frame where
  its own y lands on the axis, not where the extrude placed it. Some deformation is what a transformation does, and the
  rule is the extrude's own, so it stands. A straight axis, and a path axis along y, place the frame where the material
  expects it, and the checks below use those where exactness is the point.
- **`along <axis>` follows a path.** At each station the material is carried to that path's point and turned to its
  orientation: the offset from the axis is read in the axis's frame and put down in the path's frame there, so a
  straight extrusion bent by `along` runs down the path with its cross section carried round, which is what the
  extrude's own path axis used to do. The two stations correspond **by arc length**: material at distance d along the
  axis lands at distance d along the path, measured along the curve. That is what makes this a bend rather than a
  stretch, and it is independent of how the path happens to be parameterised, which a path read from control points
  never is evenly. The consequence an author meets is that the material must be as long as the path it is to cover:
  the knot scales its sheet to the trefoil's arc length, the helix to sqrt((2 pi T R)^2 + H^2), the bark rod to its
  own `BarkL`. Material reaching past the end of an open path carries on along the tangent there, as a `crease` runs
  straight past its angle; a path that closes wraps instead. (History: as built in `7b68b8fe` part of that run-on was
  missing, a tube 3 long along a two-point path 1 long not hit at x = 2.5 and `inside(<2.5, 0, 0>)` 0; fixed in
  `65e2514c`, below.)
- **The frame `along` carries the cross section into** is the axis's own: the rotation-minimising frame for a curve,
  and for a straight axis the extrude's pair (the direction the axis leaves, and its cross product with the axis), so
  that bending a straight `extrude` reproduces the curved extrude exactly. A straight axis and a straight `along` are
  therefore the identity, while a straight *path* used as `along` comes out a quarter turn about the axis, because a
  path's frame is seeded from z and a direction's from x. That mismatch is `extrude`'s own, between its straight and
  its path axis, and is left as it was found.
- **Arc length** is tabulated with the frames, at the same 2049 samples, by Simpson over the speed; the inverse is a
  monotone cubic through the table with 1/speed for its end slopes, which is how a path's own `arclength` already
  inverts. The round trip is exact at the nodes, so `bend { axis P  along P }` is the identity to the table's own
  error.
- **Derivatives** are exact by the chain rule. The step is linear in the point about its frame, so a slope's part
  across the axis turns and takes the scale and its part along the axis is left alone, while the factor's, the angle's
  and the shift's own slopes multiply the point's parts: scale ∂(R p)⊥ + ∂scale (R p)⊥ +
  scale (T × (R p)⊥) ∂angle + T (∂t + ∂shift). On a curved axis the station moves the
  frame, so each slope first carries (P' + ω × the moved offset) ∂t, and the offset's own slope is taken in the moving
  frame, ∂p − (P' + ω × offset) ∂t, before the turn and the factor. ω × T is the tangent's turn, so an axis that bends
  contributes exactly once, and ω's component along T cancels, as it must for a turn whose angle is measured about the
  tangent and not from the frame.
- **`along`'s derivatives** take two more factors by the same rule. The offset's components are read in the axis's
  moving frame, as above, and the placed point is their sum in the target's frame, so the target's frame contributes
  ω × (the placed offset) ∂τ and its point P' ∂τ, where ∂τ is dτ/ds × ds/dt × ∂station: the arc-length inverse's
  rate and the axis's own speed. Where the material reaches past an open target's end dτ is zero and the tangent
  carries ∂(the overhang) = ds/dt × ∂station instead, which keeps the surface smooth across the end and keeps the
  Jacobian from going flat exactly at a cap ring, where a plain clamp to the end made the solver miss crossings.
  So past the end the run-on's station slope comes from `grow` alone: `Target` reports a zero rate there, and the
  target's swing carries nothing. **Fixed after this section was written** (`65e2514c`): as first built `Target`
  still reported the end's rate in the overhang, so the tangent entered the v-slope twice, through the swing and
  through `grow`; points and boxes were right, the Jacobian was not, and Newton converged slowly or not at all, so
  the residual cut-off dropped roots in the middle of the run-on.
- **Bounds** are interval arithmetic, and are written so that the identity encloses nothing extra. The whole step is
  linear in the point: scale × R(T, angle) is that linear part, and what is left along the axis enters as
  (1 − scale·cos angle), so at a scale of one and a zero angle every added term carries a zero
  coefficient and the enclosure is the incoming box. A curved axis adds the axis point as (I − scale·R) P over the
  patch's station range, which cancels the same way; written instead from the offset p − P it would stay three hulls
  wide at any angle, and did box a full turn about a bowed path at ±2.2 around a tube 1.2 across. Measured, an
  identity `bend` boxes the oval tube to the last bit about a straight axis and to 2.2e-12 of its size about a curved
  one. T is the tangent at the middle of the station range; using it there costs a pad of the turn the tangent can
  take over that range (the axis's turning rate × half the range) times
  2√2 sin(angle/2) · scale · the offset's reach + 2 |1 − scale·cos angle| · that reach + the shift's reach, which is
  zero at the identity. **Flag, not conservative:** that turning rate is sampled at the frame table's points (1.5 × the
  largest seen), and a function axis's hull is sampled, as a curved extrude's box is. An all-constant `bend` stays a step
  rather than merging into the affine matrix, so like any non-affine step it ends the tangent-slab cull.
- **`along`'s bounds** come in two shapes. About a straight axis the offset's two components are exact, since the axis
  point has none of either, so the box is the target's own hull over the station's arc-length range plus the swing of
  those components in three frames across it, padded by the midpoint bow (an eighth of the turn squared times the
  reach) exactly as a curved extrude's box is: that is tight, and `along x` boxes the oval tube to its own box with x
  and y swapped, to the last bit. About a curved axis the step is written as a displacement from the identity —
  p + (target point − axis point) + (M − I)(p − axis point) + T·shift — so following the same axis, where M is I and
  the displacement is zero, encloses the incoming box. **Flag, not conservative:** the displacement is sampled at
  three stations and padded by half the range times two sampled constants, the largest velocity and the largest spin
  by which the target parts from the axis (both 1.5 × the largest of 257 samples, and both zero on the same axis).
- Checks (`tests/render/skein_bend.pov`, run by `skein.sh`): twenty pairs at 900 rays and a 7³ `inside()` lattice,
  mostly on an oval tube (an extrude of radius 0.4, scaled 1.5 in x, 1.6 tall). No ray hits only one shape and `inside()`
  agrees at every point anywhere below. Against the steps `bend` must equal, point and normal errors are at the
  solver's root tolerance, which is 1e-10 of the bounding diagonal: a 37° turn 0.33e-9 / 0.33e-9, a turn of 140 v
  3.31e-9 / 10.7e-9, a 50° turn about <1, 2, 0.5> 0.63e-9 / 0.87e-9, a shift of 0.5 v along y bit-identical, and a
  shift of 0.4 along the skew axis 0.74e-9 / 0.62e-9; a scale of 1.4 against `scale { x 1.4  z 1.4 }`
  6.62e-9 / 7.35e-9 and of 1 − 0.4 v 0.37e-9 / 0.29e-9. The coil of `skein_coil.pov`, its climb given as
  `bend { axis x  translate map { uv.v  linear { scale 0.6*pi } } }`, is bit-identical to the same spring
  shifted by `translate { x ... }`. A scale of one is a no-op about a straight axis and about a bowed path (0.74e-9, the solver's own
  scatter against the untransformed tube), and its box matches the untransformed tube's exactly about the straight
  axis and to 2.15e-12 about the curved one.
  About a curved axis: a path axis along y places its frame on the y axis, so the same 140 v turn is the `rotate` step
  again (1.38e-9 / 1.93e-9), as it is for that axis written as three `function(t)`s (1.40e-9 / 1.95e-9), and the radial
  taper is the `scale` step again (0.37e-9 / 0.48e-9); a `sample_path` axis on the tube's own surface is bit-identical
  to the same path written absolutely. A constant 48° turn about the path axis leaves a round tube alone
  (1.72e-9 / 3.66e-9), and a full turn about a bowed path leaves the oval tube alone (0.74e-9 / 0.62e-9). A sheet of
  constant y has one station, so a turn there must be the rotation about the path's point and tangent at that station,
  and a scale of 0.7 with a 55° turn must be the matrix 0.7·I + 0.3 T Tᵀ about
  that point followed by that rotation: both are bit-identical. (A flat sheet's outward side is undecidable — the flux
  of p − centre through it is zero — so those two compare normals up to sign, through a `SheetCheck` in the scene.)
  A twist of 120 v and a taper of 1 − 0.5 v, both about the bowed path, are closed (216 of
  216 lattice points agree with the crossing parity along two directions), and the twist moves the material at 99 of
  400 probes.
  `along` the step's own axis is that no-op again (0.74e-9 / 0.62e-9) and boxes the oval tube to the last bit, as
  `along x` does to the same box with x and y swapped; `along` a straight `path` is the quarter turn its frame seed
  implies, matching `rotate y*-90` at 0.59e-9 / 0.67e-9 and boxing it to 1.8e-12. A tube 2.5 long bent along a helix
  of constant speed stands at radius 0.08 from the helix point at arc length y within 0.22e-9 over 201 stations, with
  none of 20 points past its end inside it, which is arc length kept; a tube bent round a handled path is a closed
  solid (216 of 216).
  The run-on (added with `65e2514c`, before → after the fix): a tube 3 long from y = −1 along a two-point path 1 long
  against the same tube rotated onto x, 209 → 467 of 900 rays hitting both and 258 → 0 hitting one, points
  0.75 → 2.30e-9 and normals 1.96 → 22.7e-9 apart, `inside()` agreeing at 303 → 343 of 343; the same tube along a
  two-segment polyline, rays from the run-on centre lines leaving at radius 0.1 within 1 (a miss) → 0.29e-9 with
  53 → 0 of those centre points outside it, and closed at 200 → 216 of 216.

### Renamed: `extrude`, `crease` and `closed`

The first phase of the expression set `doc/skein-decisions.md` settles. `fold` becomes **`extrude`**, the old `bend`
becomes **`crease`** (keyword only at this phase: its `origin`, `side`, `toward`, `radius` and `angle` were untouched;
the later `crease` phase replaced the first three with `axis`), and the topology word `wrap u|v|uv` becomes **`closed u|v|uv`**. All three
still match by spelling, so nothing new is reserved; `closed` is also a `path` word and the two do not collide,
being read in different blocks, which `skein_keywords.pov` now proves with a closed scalar path inside a skein
that is itself `closed u`.

- **`extrude` is a straight line only**, so its axis must be `x` or `y`; any other direction is a parse error naming
  the two. A `path`, `sample_path` or three-function axis was accepted one phase longer, until `bend`'s `along`
  arrived to carry the knot, the helix and the bark rod.
- **`along` is gone.** It was always derivable and never written: the wrapped direction is the one the axis leaves,
  y for an axis along x and x for an axis along y, which is the rule the default already used.
- **The travel along the axis is gone.** It is a translate along the axis, which the `bend` step does, so
  `extrude { ... axial V }` becomes `bend { axis <a>  translate V }` straight after the extrude. The two
  are bit-identical: the coil's climb and the noise sphere's profile both converted with no measured change.
- **`arc` is a value**, so it may vary with `(u, v)`; it was a float, converted to radians at parse time, and the
  conversion moves to evaluation. The angle is arc × p·along, so a varying arc contributes its own slope: ∂angle is
  arc ∂(p·along) + p·along ∂arc, and the patch box takes the interval product of the two. A varying arc is not a
  "constant straight extrude", so it drops the exact interval slopes, as a varying radius already did, and the
  normal enclosure falls back to sampling.
- **Scene conversion.** `extrude { axis z  along y  radius R }` — the second extrude of every torus and donut — is
  `extrude { axis x  radius R }`, which builds the same torus about x instead of z. The primitives beside it
  (`skein_shapes.inc`) and the exact surfaces it is measured against (`skein_check.pov`, `skein_syntax.pov`) are
  rotated to match, `torus { 2, 0.5 rotate x*90 }` becoming `torus { 2, 0.5 rotate z*90 }`, and the two torus
  cameras in `skein_shapes.pov` are the old ones turned by the same quarter turn. `skein_donut.pov` keeps its
  icing on the axis side by reading `norm.x`, with its `Lift` function, its `gradient` pigment and its standing
  rotation turned with it; `skein_coil.pov` climbs along x.
- **Checks.** Full `skein.sh` is green and every pair still renders pixel for pixel the same. The torus about x
  sits 0.70e-9 off the exact surface against the primitive's own 46.5e-9, so their hits part by 48.9e-9 and their
  normals by 30.0e-9, all of it the primitive's error, over 2513 of 4000 rays hitting both and none hitting one;
  `inside()` agrees at 8000 of 8000 lattice points. The coil's climb as a `bend` step is bit-identical to the
  same spring shifted by `translate { x ... }` (580 of 900 rays, `inside()` 343 of 343).
- **`arc` varying, measured** (`skein_values.pov`). A fan of radius 0.5 whose arc grows from 90° to 270° over v is
  bit-identical, points and normals, to the full-arc tube resampled over u by
  `sample { at <function(u, v) { u*FanArc(v)/360 }, function(v) { v }> }` (2251 of 4000 rays hit both, none only
  one). Rays leaving the axis outward meet it at radius 0.5 within 1e-12 at half and at 0.98 of its arc, and 18 of
  18 rays past the arc's end miss, so the angular extent is the arc value at each v. Thickened by an `envelope`
  that tapers to zero at both ends it is a closed solid: `inside()` matches the crossing parity along two
  directions at 1000 of 1000 lattice points.

### `bend`: the rename, the lengthwise scale dropped, and `along`

The second phase of the expression set. The built `axial` step is renamed **`bend`**, the word freed by the old
`bend` becoming `crease`: "Axial and bend are the SAME THING. Or rather, bend can be described by axial along with a
transform that describes a path to bend the axis along." Scene keywords only again, so the C++ keeps `kAxialStep` and
its own spellings.

- **The lengthwise scale goes**, with the parameter words of the two built-ins that no longer need them:
  `scale { along V  radial V }` is `scale V`, the factor out from the axis, and `translate { along V }` is
  `translate V`. That frees the fourth value slot the scale had added, and the step is back to three.
- **`along <axis>` is the path-following built-in**, the first to take an axis rather than a value and the first to
  produce a position and an orientation together. It is described above with the rest of the step: the station
  correspondence is arc length against arc length, the frame it carries the cross section into is the axis's own, and
  the material carries on along the tangent past an open path's end.
- **`extrude`'s path axis is gone.** `extrude { axis <curve>  radius R }` is
  `extrude { radius R }` with `bend { axis y  along <curve> }` after it, on a sheet scaled in v to the curve's own
  length. Each scene converts: the knot's sheet is scaled to the trefoil's arc length (31.898600666, by 32 Simpson
  panels on 2π√(9 + 4K²) in the scene), the helix strand's to √((2πTR)² + H²), the bark rod's to its own `BarkL`,
  and the two straight path axes of `skein_shapes.inc` to 2. That the author must know the length is the price of a
  bend that keeps it; remapping an axis's input, which is wanted and not built, is where a different distribution
  would go.
- **The decisive check** is the knot built both ways while `extrude` still read a path axis: an extrude around the
  trefoil, and a straight tube of the trefoil's length bent along it. Of 900 rays, 91 hit both and none hit one, their
  points parting by 2.57e-9 and their normals by 44.4e-9, with `inside()` agreeing at 343 of 343 lattice points; the
  bent form's box is the tighter by 5.6e-4 of 5.47 across. `along` is the extrude's path axis, written as a transform.
- **Scene checks after the conversion.** Full `skein.sh` is green, every pixel pair included. The tube bent along a
  handled path sits 0.244e-9 off the exact cylinder and the frustum along an `arclength` path 0.313e-9 off the exact
  cone, both against their primitives with no ray hitting one and `inside()` agreeing at 8000 of 8000; rays from the
  trefoil's centre line still leave the tube at radius 0.05 within 0.76e-9, the seam at t = 0, 1/3, 2/3 and 1
  included, so the sheet's length closes the wrap. `skein_sample_path.pov`'s four pairs still meet point for point
  (normals within 0.001e-9 on the coned two) with the path now placed against the tube the bend is handed rather than
  the flat sheet, at u = 0.5, where the inset the placement takes at an edge does not reach. The bark rod bent along its arc sits within 722e-9 of its own SDL surface formula over 336
  points, which is the cubic path's departure from the exact circle it approximates, and the 16 points it misses are
  the end ring, hit edge on.
- **The knot's volume** from 300 × 300 rays is 0.250469 against the previewer's 0.25053, which is πr²L for the arc
  length above; the straight extrude measured 0.25038 the same way, so the bent form is the nearer of the two.

### `crease`: the axis, either hand, and a signed radius

```
crease {
  axis <axis>            // a direction, a path, a sample_path, or three function(t)s
  radius V               // the turning circle; its sign is the way the sheet curls
  angle V                // how far round before running on straight, degrees; unlimited when left out
}
```

The third phase of the expression set: "axis is a path, and angle and radius can both be varied along that path."
`origin`, `side` and `toward`, which between them placed a straight hinge, are gone, and `axis` takes every form
`bend`'s does through `Parse_Skein_Axis`. The C++ keeps `kBend`, `Bend()` and `Parse_Skein_Bend`.

- **The frame.** At the hinge point c the crease works in (T, A, N): T the hinge's unit direction, N its curl
  normal, A = T × N. A point's offset d = p − c splits into a = d·T, carried along the hinge unchanged; ρ = d·A,
  its distance past the hinge, where ρ ≤ 0 returns the point completely unchanged; and h = d·N. Past the hinge the
  old arithmetic applies as it was, in that frame: φ = ρ/R up to the angle, the run-on ρ − φR, the arm R − h, and
  the result c + T a + A e + N n. A negative radius uses |R| with N and h negated, so the centre sits at c + N R
  either way and the sign alone says which way the sheet curls; a radius slope enters as sign × ∂R. **Chosen, not
  decided** (all in `doc/skein-decisions.md`): the half on the T × N side moves, so for a sheet in the xy plane
  with the hinge along +y the +x half creases (along −y, the −x half) and reversing the path moves the other; a
  positive radius turns it toward +z; N is z made square to T, x when T runs along z.
- **A straight hinge is exact anywhere in space.** A direction axis passes through the origin; a `path` that is
  open, linearly interpolated, without handles and through collinear points (within 1e-12 of its span) is
  read at parse time as the line through its first point toward its last, however far its points reach. The step
  then stores the frame as `side`, `along` and `axis` with the first point as `origin`, which is exactly what the
  old `origin`/`side`/`toward` arithmetic read, so the straight evaluator and its interval box are the old code with
  the sign added. N is seeded from z in the parser, as `BuildFrames` seeds a path axis, not from `extrude`'s straight
  pair, so a direction and a path give the same hinge. The page corner — `side <1, 1, 0>  toward -z` — is the
  diagonal path from its old origin with a radius of −0.07.
- **A curved hinge** is every other axis, a `sample_path` included even where it is straight once placed (it is then
  the same surface, only not exact in intervals). It is bend's machinery unchanged: the station is the point's y,
  clamped to an open path's ends; the frame comes from the axis's rotation-minimising table, with N = e1 and
  A = e2 = T × e1; a `sample_path` is placed against the surface entering the step at Prepare. Past an open end the
  station stops and the a term carries the point along the end tangent, so the hinge runs on straight.
  **Flag**, as for `bend`: ρ is the offset's component along A from the hinge point at the point's own y, not the
  distance to the nearest point of the hinge. The two agree to first order and part by about half the hinge's
  curvature times a², the square of the offset along T, so they agree wherever the hinge runs along y.
- **Derivatives** are exact by the chain rule, bend's form: on a curved hinge the station moves the frame, so each
  slope first sheds (c′ + ω × d) ∂t before it is split into its T, A and N parts, and the result gains
  (c′ + ω × (T a + A e + N n)) ∂t; ∂t is the slope of y, zero past an open end. The radius's and angle's own slopes
  enter as they did.
- **Bounds.** About a straight hinge the box is the old interval arithmetic, exact in intervals, run once for each
  sign the radius takes over the patch and hulled; a radius spanning zero turns up to the limit, as a sharp crease
  does. About a curved hinge the step is written as a displacement from the identity,
  p + A (e − ρ) + N (sign × n − h), in the frame at the middle of the patch's station range, with ρ and h widened by
  the frame's turn over half that range times the offset's reach and the displacement padded by the same turn times
  its own reach; a patch wholly on the near side boxes to the incoming box exactly. **Flag, not conservative:** the
  turning rate is the axis's sampled one (1.5 × the largest of the frame table's), and a function axis's hull is
  sampled, as for `bend`'s curved branch. A crease is never "straight" in the box bookkeeping, so the interval slopes
  stop at it and, like any non-affine step, it ends the tangent-slab cull, as before.
- **Varying along the hinge.** `radius` and `angle` are values at the incoming point, as they were, so a value of v
  varies along a hinge running along y; nothing new was needed beyond saying so.
- **Scene conversion** (`skein_page`, `skein_envelope`, `skein_envelope_uv`, `skein_csg.inc`, `skein_csg_render`,
  `skein_shapes.inc`'s `SkeinBent` for `skein_syntax`): `origin <o>  side x` becomes `axis path { <o>, <o> + y }`, and
  the page's `side <1, 1, 0>  toward -z` becomes the path from its origin along <−1, 1, 0> with the radius negated.
  Each was run in its old syntax on the reference build (commit `7b68b8fe`) and converted on the new one: every
  number `skein_syntax`, `skein_envelope` and `skein_csg` print is the same (the crease probes 0.136e-9 both), and
  `skein_page` at 480 × 360, `skein_csg_render` cases 14 and 10 (checker) at 320 × 240 and `skein_envelope_uv` at
  160 × 120 are pixel for pixel identical, with identical solver counters.
- Checks (`tests/render/skein_crease_axis.pov`, run by `skein.sh`; 900 rays and a 7³ lattice per pair). A straight
  path against a direction axis moved to it, both thickened by an envelope: 564 rays hit both, none one, points
  0.000e-9 and normals 0.005e-9 apart, `inside()` 343 of 343. Against three `function(t)`s on the same line, which
  take the curved branch: 0.378e-9 / 1.399e-9, 343 of 343. A `sample_path` on the sheet against the same line in
  space: 0.119e-9 / 0.551e-9. A reversed path against the forward crease mirrored in x: 0.537e-9 / 1.751e-9; a
  negative radius against the positive one flipped in z: bit-identical. Probes from the turning centre, and onto the
  flat half from above, are within 0.005e-9 of the exact roll and of the incoming sheet. The page corner, a diagonal
  hinge off the origin with a negative radius: 0.000e-9 on its near side and 0.060e-9 on the roll, points and
  normals. A radius of 0.1 + 0.15 v against a unit crease of the sheet scaled across the hinge by that radius, and
  scaled back after (which is the same roll, since the crease is homogeneous in ρ, h and R): 0.205e-9 / 0.895e-9;
  points at arc length on their own row's circle within 0.008e-9. An angle of 60° + 120° v: points and normals on the
  straight run past it within 0.257e-9, the normal including the angle's slope. About a bowed parabola
  x = 0.25 y² − 0.1, points land at their arc length on the turning circle at their station within 0.004e-9; the
  parabola as a path ending at the sheet's edges against one running past them, thickened: points 0.000e-9 apart and
  `inside()` 343 of 343, and closed (216 of 216 lattice points agree with the crossing parity along two directions).
- **Found:** a function axis's differenced second derivative makes its frame noisy enough that an envelope after the
  step, which differences the step's slopes again, scatters the normals: the same thickened parabola as functions
  is 6.1e-3 off the path in normals (2.0e-9 in points). Recorded in `doc/skein-todo.md`; the check uses two paths.
- **Errors**, as debug scenes: a constant negative `angle` stops with "angle is a magnitude of 0 or more". A varying
  angle is not checked; where it dips below zero the sheet jumps off the hinge (recorded in `doc/skein-todo.md`).

### `curl`: a roll whose pivot travels

```
curl {
  pivot <path>           // a straight path, or a sample_path straight once placed; its height is the roll's radius
  travel <path>          // vectors: the pivot's displacement, read in turns of the roll
}
```

The fourth expression: "An axis placed where you want it, whose distance from the sheet is how fat the roll is, and a
`travel` path for where that axis moves to as the roll proceeds." The travel is required: "There is no no-travel case
for curl. The travel is what produces the curl." A roll about a fixed centre is the crease. The C++ is `kCurl`,
`Parse_Skein_Curl`, `Evaluator::BuildRoll`, `Evaluator::Curl`, `Evaluator::CurlBox` and `SkeinRoll`; the straightness
test (`SkeinPath::Straight`) and the hinge frame (`SkeinStep::Hinge`) are shared with the crease, whose behaviour is
unchanged.

- **The frame** is the crease's: T the pivot's direction, N z made square to T, A = T × N, and the A side rolls (for
  a sheet in the xy plane and a pivot along +y, the +x half, toward +z when the pivot is above the sheet). The
  starting radius R0 is the pivot's signed height along N: for a `path`, above the plane through the world origin
  facing N; for a `sample_path`, placed at Prepare against the surface entering the step, the placed point less the
  surface point, which must agree all along the pivot (within 1e-9 of the reach). A negative R0 flips N, so the sheet
  rolls the other way; the foot is the pivot dropped onto the sheet.
- **The section.** With w(φ) the travel at φ/2π turns less its value at 0, in (T, A, N), the radius is
  r = |R0| + w·N and the section is the circle of radius r about the moved pivot:
  C(φ) = (w_T, w_A + r sin φ, r (1 − cos φ)), with C′ and C″ analytic from the path's own derivatives. Past an open
  travel's ends the path clamps, as every path does, so the pivot holds still and the roll carries on at its last
  radius; a closed travel repeats. Material at distance ρ past the foot lands at arc length ρ along C, carried along
  T by its own a; an offset h toward the pivot moves along C's in-plane normal, so the inner face of a thickened roll
  is the tighter.
- **The table.** At Prepare the step boxes the incoming sheet and takes its extent past the foot along A; it marches
  the speed |C′| by Simpson at 1024 cells a turn until the arc reaches that extent (an error past 256 turns), then
  tabulates the arc length at no fewer than `kFrameSamples` stations into a `SkeinAxis`, whose `Parameter()` inverts
  it per point as `bend`'s `along` does. Each station also keeps C and its unit normal for the box. Every station is
  checked: r > 0, and C′ keeps a component in the (A, N) plane and starts forward along A, so the sheet never doubles
  back. Past the table the roll runs straight on along its end tangent.
- **Derivatives** are the chain rule through φ(ρ), dφ/dρ = 1/|C′|: a slope's A part moves the point along C′ and
  turns the normal offset by the section's in-plane curvature, its T and N parts carry through as they are.
- **Bounds.** A patch's ρ range picks a run of table stations; the box hulls their section points and normals, pads
  the points by half a station's step times the speed bound and the normals by half a step times the turning bound,
  adds the straight run past the table's end, and maps that through the frame with the patch's own a and h. A patch
  wholly before the foot boxes to the incoming box exactly, and one straddling it hulls with it.
- **Flag, not conservative:** the speed and turning bounds are sampled, 1.5 × the largest seen over the table's
  stations and half-stations.
- **Flag: every turn of a scroll touches the sheet beneath the pivot**, because each turn's circle has the pivot's
  height for its radius. A travel straight away from the sheet gives turns that all meet along the foot, so a
  thickened scroll of more than about one turn overlaps itself there and is not a closed solid: a scroll of three turns (0.05 a turn, R0 0.1) thickened by 0.02 agrees with the crossing parity at 212 of 216 lattice points, the four misses along the skew ray through the foot. A
  travel along the sheet separates them only by crossing them over, since two circles tangent to one line at
  different points intersect. This is the owner's definition taken literally; a spiral about a fixed centre is not
  expressible, because no pivot height describes it.
- **Flag: a travel along the pivot kinks the sheet at the foot.** The section's start tangent then has a T part the
  flat sheet does not, so a coil leaves the sheet at an angle, a real crease in the surface rather than in its
  parameterisation. The same happens where an open travel path ends anywhere but a whole turn, or with a part along
  T or A: its derivative stops there, and the section's tangent jumps.
- **Flag: the table reaches the incoming box's extent**, which is an interval box and so reaches past the sheet;
  only where that box is sampled (a function axis upstream) could a point fall past the table, where the roll runs
  straight on.

- **The page** (`tests/render/skein_page.pov`), the owner's named example, is a curl now: the crease's diagonal hinge
  lifted 0.05 toward the reader as the pivot, with a travel of 0.03 a turn further toward the reader over two turns,
  so the corner, about 1.1 turns of sheet at the diagonal's middle, rolls up wider as it goes. The other crease scenes
  are the hinge step's fixtures and are unchanged.

Checks (`tests/render/skein_curl.pov`, run by `skein.sh`; 900 rays and a 7³ lattice per pair). The travels are written
as `path { 0, <0, 0, 0>, N, N*<step> }` by a scene macro, so a scroll of N turns spans N turns.

- **Length kept**, against the section integrated independently in the scene by Simpson at about 400 panels a turn:
  a scroll (0.03 a turn away from the sheet, R0 0.1) over 2.6 turns and 2.103 of sheet, points and normals at their
  arc length within 0.000e-9; a coil (0.25 a turn along the pivot) over 3.5 turns and 2.219, 0.061e-9; a slide (0.05
  a turn along the sheet) over 0.9 turns, 0.015e-9. Probes onto the flat sheet clear of the roll: 0.000e-9.
- **The path's own clamp**: a travel of one turn on a sheet of three against a path that holds the pivot still from
  one turn to four is bit-identical (545 rays).
- **Either hand and either side**: a reversed pivot against the forward roll mirrored in x, 543 rays, points
  0.286e-9, normals 0.901e-9; a pivot below the sheet with the travel reversed against the roll above it flipped in
  z, bit-identical.
- **Closed**: a scroll under a turn thickened (216 of 216 lattice points agree with the crossing parity, 36 inside)
  and a coil of three turns thickened (216 of 216, 5 inside). A scroll of three turns thickened, with its pivot given
  as a `sample_path`, against the same as a path: bit-identical, `inside()` 343 of 343.
- **Near the crease, as a sanity check and not an equivalence**: a travel of 1e-7 a turn away from the sheet against
  the crease of the same radius with an angle of 3600°. Under a turn thickened, 565 rays, points 3.95e-6, normals
  5.76e-5, `inside()` 343 of 343; the same with a `sample_path` pivot, the same numbers; a `sample_path` pivot below
  the sheet against the negative crease, 2.24e-6 and 1.98e-5, 343 of 343; three turns of bare sheet, 7.08e-6 and
  5.18e-5. Rays from the pivot outward meet the roll within 1.1e-7 of the crease's circle, which is the travel itself;
  the larger pair numbers are grazing rays, where that offset moves the hit along the ray.
- **Errors**, as debug scenes (the suite cannot expect a failure): no pivot, no travel, a curved path and three
  functions as the pivot, a curved `sample_path`, a pivot given as a direction (it passes through the origin, so it
  lies on the sheet), a pivot on the sheet as a path and as a `sample_path`, a `sample_path` whose height runs from
  0.0995 to 0.199, a scalar travel, a travel that brings the radius to zero at 1.43 turns, one along the sheet that
  outruns the roll at once, and a pivot 1e-4 off a sheet of 2 (past 256 turns): each stops with its own message,
  and a straight `sample_path` with a travel parses.
- **Renders** (400 × 300, no AA, scratch), each thickened by 0.012 with round edges and each read
  as a roll the travel makes. A scroll (`case1.png`): R0 0.08, 0.04 a turn away from the sheet over a travel of four
  turns, 2.5 of sheet in about 2.9 turns; the turns nest and all meet along the foot; 61.8 s, 4.69M unresolved
  patches. A coil (`case2.png`): a strip 0.12 wide, R0 0.15, 0.3 a turn along the pivot, 3.5 of strip in about 3.7
  turns, each loop touching the floor; 18.3 s. A near-constant roll (`case3.png`): R0 0.15, 0.001 a turn, 0.85 of
  sheet in 0.9 of a turn; 12.3 s. The page (`page_final.png`, end-on `pageA_end.png`) as committed; a roll narrowing
  toward the corner (R0 0.09, 0.04 a turn toward the sheet, `pageB.png`, `pageB_end.png`) reads much the same and was
  not taken.

### `fold`: a new normal per point

```
fold { from <U, V>  x V  y V  z V }                // the new normal in space
fold { from <U, V>  perturb { x V  y V  [z V] } }  // in the tangent frame (u direction, v direction, normal), z 1 by default
```

The owner's API (2026-10-03/04): the surface is distorted to conform to new normals given per point, keeping local
relative position as far as it can, solved from one uv origin. `perturb` is shorthand for a computed normal,
normalize(x Tu + y Tv + z N), as a normal map's channels; nothing else rotates. The C++ is `kConform`,
`Parse_Skein_Conform`, `Evaluator::BuildFold`, `FoldPoint`, `FoldPanel`, `Conform`, `ConformBox` and `SkeinFold`.

- **The turn.** A normal alone does not say how the tangent plane spins about it, and after two hinges that are not
  parallel the shortest turn from the incoming normal is far off (29 degrees after two creases, measured in the
  proposal). So the rotation Q of the tangent plane is carried along the solve: between two nearby samples the
  incoming and the new normals each move by their least rotation, Q' = R(m0 -> m1) Q R(n0 -> n1)^T. That is exact for
  a normal moving on a great circle (every straight hinge), and continuous through 180 degrees. For a normal moving on
  any other path the least rotations spin about the normal by the path's swept area; within a Gauss-Kronrod panel that
  spin is integrated from the polynomial through the panel's normals, d psi = (a x m) . dm / (1 + a . m) with a the
  panel's first normal, for the new normals less the incoming ones. With the spin added, a developable field (a cone,
  a crease stack) is solved to the panel tolerance whatever the path, and the identity stays exactly the identity.
- **The point.** S' = S'(from) + the integral of Q dS along a straight run in (u, v), by Gauss-Kronrod 15 panels
  (Kronrod against Gauss, and the spin from all 17 points against that from the Gauss 7 and the ends, both to 1e-11 of
  the sheet's size). The slopes are Q S_u and Q S_v: no second derivatives.
- **Kinks and jumps.** A panel that fails does not halve blindly. The field alone is bisected toward the half whose
  midpoint strays further from a straight step (the incoming point's bow, the turn of either normal off the step's,
  and the chord's error over the turn), with one sample a halving once the change across the bracket stops shrinking
  (a jump); the bracket is crossed by one least-turn step and the run carries on in panels either side. The step stops
  at 1e-6 of the panel tolerance for an exact fold, since its error moves with the point and an envelope differences
  the fold over 1e-8, and at 1e-3 for a blended one. A run spends at most 3000 samples. An incoming slope that is
  infinite or undefined (an outline like pow(sin(pi v), 0.8) at its tips) is taken from 1e-6 inside.
- **The table (Prepare).** 65 columns, u = i/64, each tabulated at v = k/128: solved from `from` along its row to each
  column, then up and down each column; every eighth row is solved again across each pair of columns to measure how far
  the loops fail to close. When none fails by more than 1e-9 of the size the field is developable and a point is solved
  from its nearest entry; otherwise from the entry nearest it on the column each side, along the row through it to the
  other column, the two blended linearly across, which keeps the surface whole (the columns are consistent by
  construction, and the blend matches each column at its own u), at a looser 1e-10. Prepare calls the parser's progress
  report between columns and patches, so a cancel stops it.
- **Enclosure.** Folding keeps lengths, so a patch lies within the longest path from its centre to its corners of the
  folded centre: the incoming slopes' interval reach (sampled at 3 by 3 with a 1.5 margin when they are not exact)
  times the half widths. That shrinks with the patch, which is the wizard-hat lesson; a blended field adds the slope of
  the largest loop gap among the cells the patch covers. The box is a cube, not oriented, so it is up to about 1.7 times the patch's own.
- **Checks.** `skein_fold.pov` (400 rays each): a 120 degree hinge by x, y, z and by perturb against `crease`, 0.058e-9 in
  points and 0.069e-9 in normals; thickened by an envelope, 0.196e-9 and 3.849e-9, inside() 216 of 216; a 180 degree
  hinge by perturb, 0.301e-9 and 0.700e-9; a torus folded to its own normals, 3.243e-9 and 4.340e-9, inside() 216 of 216.
  Box cost (`skein_fold_box.pov`, rows 12 and 13 of 24 by 24): 284 unresolved patches and 10742 bound tests in 0.08 s;
  with a box that does not shrink, 5582614 and 7445214 in 147 s. The whole 24 by 24 view: 2532 unresolved and 104644
  bound tests, against 142 and 16042 for the same view as a `crease`, whose box is exact.
- **Cost of discontinuities (2026-10-04).** Single pixels at 320 by 240, one thread, before and after: a crumpled sheet
  whose crackle facets tilt with sign jumps and eased kinks, Prepare 20.4 s to 2.4 s and 36 to 80 s a pixel to 0.09 to
  0.39 s; jumps only, 5.1 s to 0.8 s and 8 to 32 s to 0.05 to 0.19 s; kinks only, 9.3 s to 1.4 s and 1.8 to 2.5 s to
  0.04 to 0.07 s; a leaf whose outline has an infinite slope at its tips never finished parsing (over 9 minutes, deaf to
  a cancel) and now takes 1.1 s (curl) and 1.8 s (cup). A 90 degree hinge, 0.05 s to 0.09 s Prepare, the larger table;
  a smooth cup unchanged. Newton's success on the crumpled sheet rose from 0.7 to 2.4% to 13 to 19%: its failures were
  loose blended boxes (the worst cell's gap applied everywhere), not the creases. `skein_fold_crumple.pov` (rows 12 and
  13 of 24 by 24, in `skein.sh` with a ceiling of 5 million): 1591179 evaluations and 2.1 s, against 170226611 and 227 s
  halving the integral.
- **Values** of a fold take no function slots: nothing encloses them, so no patch samples the chain for them.

### Review fixes: the function context, copied axes and the check guards

- **A function axis inside an `expression_map` entry**, in a skein with no function values, left the evaluator
  without a function context, and the first axis evaluation crashed. The context now comes from the first function
  value or else the first function axis anywhere in the chain, blend entries included. Check (`skein_blend.pov`):
  that skein against the same map driven by `function(uv) { uv.v }`, 308 of 700 rays, 0.000e-9 in points and normals.
- **Copied steps shared their axes.** Prepare places a `sample_path` axis and builds its frame table in the step's
  `SkeinAxis`, which every copy of the step shared, so a declared group spliced twice took its last placement for
  both uses, and a group used by a second skein was re-placed under the first. A copied step now owns copies of its
  `curve` and `target`. Its blend and value sums were already deep; its values' functions, maps and paths are not
  written after parsing, and Prepare gives a curl a new roll rather than writing the parsed one. Checks
  (`skein_sample_path.pov`), each against the same written inline: the group spliced twice, 142 of 900 rays; in the
  first of two skeins, 447; in the second, 346; all 0.000e-9. Before the fix the first two had 163 and 184 rays
  hitting only one.
- **Check guards.** `Check` fails when no ray hits both and `InsideCheck` when no lattice point is inside, as the other
  checks already did; `Crossings` and `BoundaryCheck` stop a ray at 64 crossings and fail the check if any ray
  reaches it, so a self-touching surface cannot spin them. No existing check changed a number. The whole `skein.sh`
  takes 6 min 19 s through the queue.

## POC results

`tests/render/skein.sh` (in `make check`) runs `skein_check.pov` and renders `skein_shapes.pov`. The check traces
4000 random rays per shape at the skein and at the primitive it equals, measures both hits against the exact
implicit surface, and compares `inside()` on a 20³ lattice. Tolerance 1e-6:

| shape (primitive) | rays hitting | skein off exact surface | primitive off it | max point Δ | max normal Δ | inside agrees |
|---|---|---|---|---|---|---|
| torus 2, 0.5 (`torus`) | 2562 | 0.69e-9 | 2618e-9 | 2744e-9, 1 ray, primitive's error | 390e-9 | 8000/8000 |
| tube r 0.5, h 2 (`cylinder`) | 3201 | 0.24e-9 | 0 | 3.3e-9 | 3.1e-9 | 8000/8000 |
| frustum 1 to 0.25, h 2 (`cone`) | 2316 | 0.32e-9 | 0 | 4.4e-9 | 5.2e-9 | 8000/8000 |
| twisted tube, 90° (`cylinder`) | 3201 | 0.24e-9 | 0 | 3.1e-9 | 2.4e-9 | 8000/8000 |
| hexagonal column (`prism`) | 3237 | < 1e-12 | 0 | < 1e-12 | 0.32e-9 | 8000/8000 |

No ray hit one and missed the other, and no skein normal pointed inward. At 320 × 240 without anti-aliasing the
torus, tube, frustum and twisted tube render pixel-identical to their primitives, as do CSG cutaways (a box and a
sphere differenced out) of the torus, frustum and hexagonal column. The plain hexagonal column differs in one
pixel of 76800 (17/255), a silhouette ray landing within the finite-difference step of a corner. The twisted
tube renders identical to the untwisted one in a plain pigment, while a uv checker turns with the twist. Seven
scenes without a skein (biscuit, chess2, isocacti, primitiv, torus2, isosurfaces, random_effects; one thread,
16-bit) render bit-identical against the base build made with the same flags, and biscuit (anti-aliased) and
isocacti do too between release builds of the base and the branch installed in the render queue.

Cost at 320 × 240, one thread, two lights (camera and shadow rays), against one primitive test per ray:

| shape | rays tested | Newton solves | Newton steps | steps per ray | chain evaluations | function VM calls | unresolved |
|---|---|---|---|---|---|---|---|
| torus | 102031 | 210418 | 974351 | 9.5 | 1145985 | 0 | 44 |
| tube | 46762 | 65223 | 269349 | 5.8 | 295695 | 0 | 0 |
| frustum | 75462 | 437564 | 1332741 | 17.7 | 1358359 | 4075077 | 21661 |
| hexagonal column | 36146 | 295081 | 986499 | 27.3 | 6171851 | 8167497 | 2183 |
| twisted tube | 46762 | 216922 | 820551 | 17.5 | 827720 | 2483160 | 10465 |
| twisted tapered hexagon | 33082 | 538045 | 1618724 | 48.9 | 12375956 | 34494064 | 30875 |

The tangent slab is what keeps the torus and tube near five to ten Newton steps per ray; the shapes with function
values fall back to boxes alone and pay for near misses (the unresolved column), and the hexagon also for
re-sampling its unboundable radius. Next steps, in order of gain: interval derivatives for user functions (slab
for every shape), range plans for `cos`, `mod` and `pow`, then the extrude around a path axis.
