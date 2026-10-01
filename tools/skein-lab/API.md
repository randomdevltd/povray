# skein-lab API

One entry point: `import { ... } from './src/index.ts'`. A model is a list of **ops** applied in order to
the unit quad `(u, v) -> [u, v, 0]`. Every op maps the current surface to a new one.

```ts
import { shape, scale, fold } from './src/index.ts';

export default shape({ wrap: 'u', ends: 'flat' },   // topology of the result
  scale([1, 2, 1]),                                  // quad becomes 1 x 2
  fold({ radius: ({ v }) => 1 - 0.75 * v }),         // roll it into a tapered tube
);
```

## Values: constants, functions, series

Most op arguments accept a `Value<T>`: a constant, or a function of the per-sample inputs.

| input | meaning |
|---|---|
| `u`, `v` | surface parameters in [0, 1] |
| `x`, `y`, `z`, `p` | the incoming point (before this op) |
| `n` | the incoming surface normal (finite differences, only computed if read) |
| `theta`, `s`, `t`, `w` | inside `fold` only: angle, fold-direction, axial and offset coordinates |

Functions bind what they use: `({ v }) => 1 - v`, `({ n }) => n[2]`, `({ x, y, z }) => ...`.

A **series** (`path`, `spline`, `series`) is a function of one number. Used directly as a value it is
evaluated at `v`. Use `by` to drive it from anything else:

```ts
fold({ radius: taper })                         // taper(v)
fold({ radius: by(taper, 'u') })                // taper(u)
scale(by(flare, ({ y }) => y / 2))              // flare(y / 2)
displace(by(ridges, ({ u, v }) => u + 0.3 * v)) // any expression of the inputs
```

`by(f, key | (sample) => number)`: keys are `'u' 'v' 'x' 'y' 'z' 's' 't' 'w' 'theta'` (`'theta'` is
normalised to turns, theta / 2π).

## Model

### `shape(topology, ...ops)`
`topology`: `wrap: 'none' | 'u' | 'v' | 'uv'`, `ends: End | [End, End]` with `End = 'open' | 'flat' | 'pole'`
(at v = 0 and v = 1, only when u wraps), `grid: [nu, nv]` (default 160 × 160), and for the v seam
`shift` (u offset as a fraction, e.g. a quarter-twisted square torus) and `flip` (u → 1 − u, Möbius, Klein).
A model may also be an array of shapes: `export default [body, stem]`.

### `ops(...ops)`
Groups ops into one op. `const tube = ops(fold({ radius: 0.2 }), scale([1, 3, 1]))`.

## Transform primitives

```ts
scale(2)                                   // uniform, about the origin
scale([1, 3, 1])                           // per axis
scale([2, 2, 2], [0, 1, 0])                // about a point
scale(({ v }) => [1 + v, 1, 1 + v])        // function-valued
translate([0, 0.75, 0])                    // absolute
translate(sway)                            // a Vec3 path, driven by v
rotate(Y, Math.PI / 4)                     // axis + angle, about the origin
rotate(Y, ({ y }) => 1.2 * Math.PI * y, [0.5, 0, 0])  // angle as a function, about a point
matrix([[1, 0.3, 0], [0, 1, 0], [0, 0, 1]])           // 3x3 linear
matrix([[0, -1, 0, 2], [1, 0, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])  // 4x4 affine/projective, absolute
```

Constant arguments take a fast path that never builds a sample.

### `bend({ origin, side, toward, radius, angle })`
Hinge deformer: points past the plane through `origin` facing `side` are rolled onto a cylinder of
`radius` that touches the sheet along the hinge, curling `toward`; arc length is kept, and past
`angle` (default unlimited) the sheet continues straight. Points behind the hinge are untouched.
`radius` and `angle` are values. Thickness is kept, so it bends closed thin slabs too.

```ts
bend({ origin: [0.6, 0.45, 0], side: [1, 1, 0], radius: 0.07 })                 // corner curl
bend({ origin: [-0.425, 0, 0], side: X, radius: ({ v }) => 0.2 + 0.25 * v, angle: 2.1 })  // page turning
bend({ origin: [0, 1.5, 0], side: Y, toward: X, radius: 0.35, angle: 1.7 })     // droop the tip of a hat
```

## Surface ops

### `fold(options)`
Bends the current surface around an axis. Reads the incoming point as `s` (along `along`), `t` (along
`axis`) and `w` (along `along × axis`); maps `theta = start + s · range`; places the point at radius
`radius + w`, angle `theta + twist`, axial `t + axial`.

| option | default | |
|---|---|---|
| `axis` | `Y` | a direction, or a path (Vec3 series or `(t) => Vec3`): a sweep |
| `origin` | `[0, 0, 0]` | where a straight axis passes |
| `along` | `X` | fold direction; also the zero-angle direction for a straight axis |
| `radius` | `1` | number, function, or series; may return `[r, angle, axial]` or `{ radius, angle, axial }` |
| `curve` | — | section as a 2D curve `(t) => [a, b]` or a 2D series, instead of `radius` |
| `twist` | `0` | angle offset (works for `radius` and `curve`) |
| `axial` | `0` | axial offset (a coil when it grows with theta) |
| `range`, `start` | `2π`, `0` | angle range; partial ranges curl a sheet |
| `up`, `roll`, `correct` | `Z`, `0`, `true` | path axis only: zero-angle up vector, extra roll, holonomy correction |

```ts
fold({ radius: 0.5 })                                              // tube
fold({ axis: Z, along: Y, radius: 2 })                             // bend that tube into a torus
fold({ radius: ngon(6, 0.5), twist: ({ v }) => Math.PI * v })      // twisted hexagon
fold({ curve: star(5, 0.6, 0.3) })                                 // non-polar section
fold({ axis: torusKnot(2, 3), radius: 0.05 })                      // sweep along a path
fold({ axis: Z, along: Y, range: 12 * Math.PI, radius: 0.6, axial: ({ theta }) => 0.05 * theta })  // coil
```

### `lathe(profile, options?)`
A fold whose section is a profile `[radius, height]` per v: `lathe(spline(...))` or
`lathe(({ v, theta }) => [r, h])`. Accepts the other fold options (`range`, `twist`, ...).

### `displace(amount)`
Moves each point along the incoming normal. `displace(({ u, v }) => -0.04 * crackle(tubeUV(u, v, 0.45, 10)))`.

### `fn(f)` / `fn(fx, fy, fz)`
Raw escape hatch: `fn(({ u, v }) => [..., ..., ...])`.

### `spherical(radius = 1)`
Maps (u, v) to a sphere, radius as a value: `spherical(({ u }) => 1 + 0.1 * Math.cos(12 * Math.PI * u))`.

### `morph(keys, t)`
Blends whole pipelines piecewise-linearly: `morph([fold({ radius: 0.5 }), fold({ radius: ngon(4, 0.5) }), fold({ curve: star(5) })], ({ v }) => v)`.

## Series

```ts
path([0, 0, 0], [1, 1, 0], [2, 0, 1])                         // Vec3 polyline, uniform parameter
path(0.35, 0.55, 0.6, 0.42, { interp: 'cubic' })              // scalar series
path(...corners, { closed: true, interp: 'catmull', arclength: true })
spline([0, [0, 0]], [0.12, [0.55, 0]], [1, [0.52, 2.1]])      // knots: [parameter, value], Catmull-Rom
series((t): Vec3 => [Math.cos(t), t, Math.sin(t)])           // any function, made usable as a value
```

Options: `closed` (wraps t; closing knot at 1), `interp: 'linear' | 'catmull' | 'cubic'` (cubic is a
natural / periodic C2 spline), `arclength` (reparameterise by length). Points may be numbers, 2D or 3D.

## Helpers

Sections: `circle(r)`, `ngon(n, r)`, `superellipse(p, a, b)` (polar, for `radius`), `star(points, outer, inner)`
(a closed 2D path, for `curve`). Paths: `helix(radius, height, turns)`, `torusKnot(p, q, R, r)`.
Noise: `cells(p)` → `{ f1, f2 }`, `crackle(p, width)`, `noise(p)` (smooth value noise in [-1, 1]),
`fbm(p, octaves)`, `tubeUV(u, v, length, scale)` (seamless in u). All deterministic.
Constants: `X`, `Y`, `Z`. Vectors: `add sub dot cross norm dist normalize lerp`.

## Metrics

`measure(model)` → `{ area, volume, union, seamGap, selfIntersections, contacts, orientable, open }`.

- `volume`: divergence-theorem volume (counts overlapping regions once per covering, so twice where a
  surface overlaps itself).
- `union`: volume of the solid under the **nonzero winding rule**, by exact crossing integration along a
  256² grid of rays (`unionVolume(model, rays)`); `null` for open or non-orientable results.
- `selfIntersections`: informational count of intersecting triangle pairs within each part on a 40² mesh
  ("self-overlaps (handled)"); `contacts`: pairs between different parts (union joints).
- `orientable` / `open`: from edge-consistency propagation over the welded mesh (`orientation(mesh)`).
  A closed non-orientable result (Klein) is the one error: it has no inside.
- `windingSlice(model, z, n)`: winding numbers on a plane, for cutaways.

`buildMesh(shape, nu?, nv?)`, `area`, `volume`, `seamGap`, `intersections(model, n)`, `orientationOf(model)`.
