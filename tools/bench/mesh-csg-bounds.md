# Mesh seams, interior certification, and CSG bounds

`mesh-csg-bounds.cpp` reads triangle-only OBJ records and prints the mesh,
positive merge, and difference bounds. Build it using the same flags and libraries
as `tests/source/finite_geometry_bounds.cpp`; the `check-prepared-sets.mk` target
accepts `PREPARED_SOURCE`, `PREPARED_OBJECT`, and `PREPARED_BINARY`. Then run:

```
mesh-csg-bounds tests/scenes/mesh-pole-gap.obj
```

The fixture separates two copies of a tetrahedron's pole by `sin(pi)`. That gap
survives conversion to float near zero. On `8554844ac` the mesh and merge have
`finite=1, finiteInterior=0`; the difference has `finite=0, finiteInterior=0`.
All three have `INFINITE_FLAG=0`. Replacing the fifth vertex with `0 0 1` restores
interior certification throughout. This is a diagnostic fixture, not an engine fix.

These flags answer different questions. `Mesh::Compute_BBox` requires closed
edge topology for an inside-vector mesh to certify its interior. `FiniteCSGBounds`
requires that certification for a difference's first child. Its failure falls
through to `CSG::Compute_BBox`'s legacy numerical box, which is marked uncertified.
`Update_Infinite_Flag` tests numerical box volume, not either certification flag.
The renderer's finite/infinite partition uses `INFINITE_FLAG`. Camera framing
separately inspects `finiteInterior`; its unbounded-part warning therefore does
not establish that every scene ray tests the object.

Proposed resolution: mesh generators should emit one canonical vertex at each
pole and triangulate a fan, avoiding both almost-collapsed rings and zero-area
triangles. Keep interior certification conservative. A relative tolerance in
certification alone would declare a genuinely open mesh closed without changing
its intersection geometry. If approximate welding is desired, it needs an explicit
geometry canonicalization design and tests for intentionally separate close shells,
large translations, scale, degenerate faces, and shared edges.

A camera-only policy could optionally frame an uncertified difference from the
positive operand's surface box. Do not generalize that to intersection culling or
interior transport: an open mesh's parity interior can extend outside its surface
box, and a cutter there can contribute a difference surface. Framing semantics
need a separate decision and regression tests; this diagnostic changes none.
