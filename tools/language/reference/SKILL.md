# POV-Ray 4.0 scene language (.pov4): agent reference

Generated index into the scene language. For anything not here, run
`node tools/language/reference.mjs <term>` from the repo root (exact name first,
then a unique near-miss, e.g. `phongg` → `phong`); `--list` prints the block
index below. Data files sit beside this file; the tables are generated from the
parser's own word lists, never hand-copied.

## Execution model

- A `.pov4` file is a program that runs once to build the scene. Nothing re-runs per frame.
- Code emits *items* into an *output*: top level → the scene; function body → the function's result (a *fragment*); block body → the block's contents; array literal → the array's elements.
- An item is anything legal in a classic block body: a keyword (`translate`, `rgb`), a value (`<1, 0, 0>`, `2.5`), a block (`pigment { ... }`), a bracket group (`[0 color Red]`).
- Statements (`let`, `if`, `for`, `while`, `return`, `include`) emit nothing; their bodies emit into the enclosing output. `scale if (Big) { 2 } else { 1 }` supplies the operand.
- A statement manifests its items only when its output is the scene: `let Ball = sphere { 0, 1 };` adds nothing; the line `Ball` adds the sphere.
- Top level accepts only manifestable items: objects, `camera`, `light_source`, `background`, `fog`, `sky_sphere`, `rainbow`, `media`, `global_settings`, `default`, `photons`, `radiosity`.
- Functions are components: `fn Tree(H) { cylinder { ... } sphere { ... } }` — `Tree(2)` at top level manifests both; `let T = Tree(3);` binds a fragment; `...T` splices it.
- `return E;` ends a function with `E`; an error after items were emitted. A function that emits nothing and does not return yields `null`.
- Lambdas: `(S) => S * 2`; braces make a body whose last items are the result.
- `let X = E;` binds in the current function scope; `X = E;` assigns to the nearest existing binding; `global X = E;` binds at file scope. Loop and branch bodies do not open scopes.
- `.inc4` includes run in the including file's scope.
- Objects and blocks are still built by the classic parser: the evaluator lowers every manifested item to classic SDL with all expressions evaluated.
- Block item words are statically checked against generated tables; a typo fails at parse time with a located error, also in branches that never run.
- There is no `#` meta-language. A 4.0 file may `include` classic files and call their macros.

## Statement and expression forms

```pov
let X = 1;   X = X + 1;   global Seed = seed(42);   fn F(A, B = 2) { A * B }
input Quality = 3;   input Name;   // Declare=Quality=5 Declare=Name="sky"; no default = required
if (X > 1) { ... } else if (X < 0) { ... } else { ... }   while (X < 10) { ... }
for (I = 0 to 9) { ... }        // inclusive; for (V in A) takes arrays, dict keys, fragments
[for (I = 0 to 9) { I * I }]     // array comprehension;  { A: 1, "b c": 2, [K]: 3 } dictionary
```

Precedence high→low: call/index/member; unary `- + !`; `* /`; `+ -`; `< <= > >=`; `== !=`; `&&`; `||`; `C ? A : B`; lambda. `==`, `&&`, `||` replace classic `=`, `&`, `|`; comparisons and logic need no parentheses except inside a vector `<1, (A > 0), 3>` and for a bare `<` after an item in any item list (block and function bodies, top level, arrays, comprehensions), which starts a vector: `fn Lt(A, B) { (A < B) }`.

## Blocks

`keyword { body }`; the body is an output, so statements are legal inside it. One-liners:

<!-- generated:begin block table -->
| block | items | one-liner |
|---|---|---|
| `background` | — | The colour or pigment shown behind every object. |
| `bicubic_patch` | 38 | Bezier patch surface from a 4x4 control-point grid. |
| `blob` | 37 | Metaball surface from sphere and cylinder strength components. |
| `bounce` | 1 | Scattering orders of a light's own emission field: bare `bounce` is one pass, `bounce { passes N }` runs N. |
| `box` | 32 | Box from two opposite corner vectors. |
| `bump_map` | 33 | Normal perturbation pattern wrapping a 2-D image file. |
| `camera` | 35 | The eye: location, direction, projection type, lens effects; an output item renders the camera as its own view, one image per such camera from a single parse. |
| `composite` | 75 | Legacy CSG grouping from before union; still parsed. |
| `cone` | 33 | Cone or frustum from two end points and radii; open removes the caps. |
| `cubic` | 32 | Third-degree polynomial shape; a special case of poly. |
| `cylinder` | 33 | Cylinder from two end points and one radius; open removes the caps. |
| `default` | — | Item values every later object begins from: default { texture { ... } }. |
| `density` | 102 | Pattern block scaling media density across its container. |
| `difference` | 75 | CSG: first child minus every following child. |
| `disc` | 32 | Infinitely thin flat disc: center, normal, outer radius, optional inner radius. |
| `finish` | 21 | How the surface reflects: highlights, reflection, ambient, brilliance. |
| `fog` | — | Fades distant objects toward a colour; fog_type picks flat fog or ground mist. |
| `global_settings` | 25 | Renderer-wide scene options: radiosity, photons, gamma, tracing depth. |
| `height_field` | 58 | Triangle mesh whose heights come from an image's brightness. |
| `image_map` | 36 | Pigment pattern wrapping a 2-D image file around the object. |
| `interior` | 15 | Inside-the-surface properties: ior, caustics, fading, media, and the volume light of a light block. |
| `intersection` | 75 | CSG: points inside every child. |
| `isosurface` | 44 | Implicit surface where f(x,y,z) is zero, solved while rendering. |
| `isosurface_mesh` | 44 | Isosurface tessellated into a mesh2 at parse time, sharp edges exact. |
| `julia_fractal` | 55 | 3-D slice of a 4-D quaternion Julia set. |
| `lathe` | 36 | Solid of revolution: a 2-D profile curve spun about y. |
| `lemon` | 33 | Cone-like shape whose side is an arc of Inner_Radius; end radii may be zero. |
| `light` | 12 | A light on its object's surface, or its interior's volume: pigment or own-emission input, brightness in nits or lumens, bounce passes, sampling. |
| `light_group` | 75 | Objects lit only by the lights named inside it. |
| `light_source` | 54 | A light: position, colour, area, shadowing and photon options. |
| `material` | 8 | texture with interior and interior_texture grouped together. |
| `media` | 22 | Participating medium: scattering, emission, absorption, intervals. |
| `merge` | 75 | CSG union that removes inner surfaces, for transparent children. |
| `mesh` | 35 | One object collecting explicit triangle components. |
| `mesh2` | 40 | Mesh in structured lists: vertex_vectors, normal_vectors, face_indices. |
| `normal` | 102 | Perturbed surface normals: bump pattern, bump map, slope. |
| `object` | 75 | Copies or wraps an already-declared object with new modifiers. |
| `output` | 3 | Camera item that renders this camera as its own view: the scene parses once, each camera with one renders its own image in order, cameras without one are not rendered. Bare output keeps the command line's size, window and file; the block's parts are optional, and a later output replaces an earlier one. Refused with animation, STDOUT output or photon save_file/load_file. |
| `ovus` | 35 | Egg shape from bottom and top radius, with radius, distance and precision controls. |
| `parametric` | 39 | Surface from three functions of u and v; precompute caches its mesh. |
| `pigment` | 102 | Surface colour: a pattern, colour map or image map. |
| `plane` | 32 | Infinite solid half-space from a surface normal and a distance. |
| `poly` | 32 | Polynomial surface up to seventh degree from numbered coefficients. |
| `polygon` | 32 | Flat shape from a loop of 2-D points, with optional hole loops. |
| `polynomial` | 32 | Poly with named coefficients in any order; omitted coefficients default to 0. |
| `portal` | 45 | Rays entering its body continue from the mapped point elsewhere. |
| `prism` | 39 | Solid swept from closed 2-D curves in the x-z plane along y. |
| `quadric` | 32 | Second-degree polynomial surface from ten coefficients. |
| `quartic` | 32 | Fourth-degree polynomial surface from thirty-five coefficients. |
| `rainbow` | — | An arc of colour bands across the sky: direction, angle, width, distance. |
| `refraction` | 3 | Independent interior index contributions: one or more raw scalar functions and scene-unit detail, or off. |
| `sky_sphere` | — | A pigment wrapped on a huge sphere around the scene to suggest a sky. |
| `smooth_triangle` | 33 | Triangle carrying per-vertex normals for smooth shading. |
| `sor` | 33 | Surface of revolution: a curve of points spun about the y axis. |
| `sphere` | 32 | Sphere from a center and a radius. |
| `sphere_sweep` | 36 | Tube of varying radius sweeping a spline of sphere centers. |
| `superellipsoid` | 32 | Superquadric shape, rounded box to pinched cylinder, from two exponents. |
| `tagged` | — | Object value: copies of the objects placed so far that a tag expression selects, with the containers around them, as in look_at tagged { "subject" } or object { tagged { "statue" } translate x }. Expands once; matched by text before a block, so tagged stays free as a name. |
| `tags` | 2 | Names a filter matches: omitted inherits the parent's, a list replaces them, inherit adds them, none clears them; a later tags block on the same object replaces earlier ones. |
| `text` | 36 | TrueType font rendered as geometry, with thickness and offset. |
| `texture` | 105 | One material layer: pigment, normal, finish and transformations. |
| `torus` | 32 | Ring in the x-z plane from major and minor radii. |
| `triangle` | 32 | Flat triangle from three corner points. |
| `union` | 75 | CSG: children grouped as one object. |
<!-- generated:end block table -->

## Rules of thumb

- Reserved words are positional: any word still works as a dictionary key (`{ step: 1 }`) or as a member (`D.step`).
- Objects and blocks always close with braces; statement bodies always take braces (no `#end`).
- No scene-state inspection at build time: built-ins that need the scene (`trace`, `inside`, `min_extent`, `str`) are handed to the classic parser; the program cannot query the scene while it runs.
- A classic twin of a `.pov4` scene is `#version 4.0;` classic syntax — same semantics, two spellings. A `#version 3.7` twin does not match: 4.0 changed defaults (gamma among them).

## Canonical examples

`tests/pov4/` holds twin fixtures — `foo.pov` classic beside `foo.pov4` (`basics`, `types`, `control`, `loop`, `components`, `layered`, `mixed`, `interop`, `points`, `review`). Paired examples are the fastest way to learn the mapping; `tests/pov4/identical.sh` checks a pair renders identically.

## Authoring via the codemod

Models know classic POV-Ray well: write classic, then
`node tools/language/codemod/codemod.mjs in.pov -o out/` and read the diff. A legitimate authoring route that also exercises the codemod.

## What did my scene actually become?

Render with `+GLout.pov`: the classic SDL the scene was lowered to is written to `out.pov`; rendering that file gives the same image.

## Lookup

Run `node tools/language/reference.mjs <term>` for any block, item or 4.0 word not in the table above.
