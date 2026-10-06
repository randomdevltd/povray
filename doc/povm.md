# The `.povm` mesh format

A `.povm` file holds the geometry of one triangle mesh as little-endian binary arrays, so a scene loads a large mesh
by reading it instead of parsing it as SDL text. Textures, `inside_vector` and transforms stay in the scene.

## Scene syntax

    mesh2 {
      povm "trunk.povm"
      [inside_vector <direction>]
      [OBJECT_MODIFIERS...]
    }

The file is found like an include file, in the scene's directory or a library path. A name that does not end in
`.povm` is tried with `.povm` appended first, then as given. `povm` replaces the `vertex_vectors`, `normal_vectors`, `uv_vectors`, `texture_list` and
index sections of `mesh2`; the mesh takes one texture, given in the scene like any object's.

The file read, triangle preparation and cached-tree load or build run as one deferred mesh task. The parser may
continue with later declarations, instances and transforms; a geometry query waits for that task, and otherwise all
pending mesh tasks finish after the last scene token. Up to `+WT` tasks run concurrently;
`+WT1` loads them serially.

## Layout

A 28-byte header, then the sections in this order, with no padding and nothing after the last:

| Offset | Type | Field |
|---|---|---|
| 0 | 4 bytes | magic `POVM` (`50 4F 56 4D`) |
| 4 | uint32 | version, `1` |
| 8 | uint32 | flags: bit 0 set if normal indices follow, bit 1 if uv indices follow; other bits 0 |
| 12 | uint32 | vertex count V, 1 to 2^30 − 1 |
| 16 | uint32 | normal count N, 0 to 2^31 − 1 |
| 20 | uint32 | uv count U, 0 to 2^31 − 1 |
| 24 | uint32 | face count F, 1 to 715,827,882 ((2^31 − 1) / 3) |

| Section | Type | Length |
|---|---|---|
| vertices | float32 x, y, z | V × 3 |
| normals | float32 x, y, z | N × 3 |
| uvs | float32 u, v | U × 2 |
| faces | uint32 vertex indices a, b, c | F × 3 |
| normal indices, if flag bit 0 | uint32, one per face corner | F × 3 |
| uv indices, if flag bit 1 | uint32, one per face corner | F × 3 |

All values are little-endian; indices count from 0. The file's size is therefore exactly
`28 + 12V + 12N + 8U + 12F × (1 + number of index flags set)`.

- **Normals** are per vertex when N = V and there are no normal indices: corner k of a face uses the normal with the
  same index as its vertex. Otherwise normal indices are required. A face is smooth when its three corner normals
  differ; point all three corners at the same normal for a flat face in a mesh that has smooth ones. A normal need
  not be unit length: one whose length differs from 1 by more than 10^-4 is normalised, and a zero normal gets
  x = 1, as in `mesh2`, with a warning.
- **UVs** follow the same rule: per vertex when U = V without uv indices, otherwise indexed. With no UVs every corner
  has UV <0,0>.
- **Faces** keep their order, which is the triangle order a mesh camera sees. Degenerate faces are kept, as in `mesh2`.

POV-Ray stops with a parse error on a wrong magic, an unknown version or flag, a size that does not match the header,
a count out of range, an index out of range, a vertex, normal or UV that is infinite or not a number, or normals or
UVs that need indices and have none.

A writer needs nothing else. In Python:

    import struct
    def write_povm(path, vertices, faces, normals=()):   # lists of tuples; normals per vertex or none
        with open(path, 'wb') as f:
            f.write(b'POVM' + struct.pack('<6I', 1, 0, len(vertices), len(normals), 0, len(faces)))
            for p in list(vertices) + list(normals): f.write(struct.pack('<3f', *p))
            for t in faces: f.write(struct.pack('<3I', *t))

## The bounding-tree cache

The first render that loads `trunk.povm` builds the mesh's bounding tree and writes it beside the file as
`trunk.povt` (the `.povm` path with its extension replaced by `.povt`). Later renders read the tree instead of
building it. The cache records the `.povm` file's size and a hash of its whole content, the tree format's version,
and an identifier of the POV-Ray build: on Linux the executable's GNU build ID, elsewhere the version and the time
the loader was compiled. If any of these differ the tree is rebuilt and the cache rewritten, so editing the `.povm`
file, even keeping its size and time stamp, or running a different build never uses a stale tree. A cache that fails
its own checks is rebuilt with a warning.

The cache is written to `trunk.povt.tmp`, created exclusively, and renamed into place only while that file is still
the writer's own, so renders running in parallel never read or install a partial tree. A render that finds a `.tmp`
leaves it to its writer, with a warning naming it if it is more than two seconds old; a `.tmp` more than ten minutes
old, or dated in the future (as a file server's clock can make it), is taken to be left by a crash and replaced. On
Windows the check that the `.tmp` is still the writer's own only tests that it exists, so a render overlapping a
reclaim on a network share could install a partial tree, which the next render finds corrupt and rebuilds. If the directory cannot be written, or I/O restrictions forbid it,
the tree is built for that render only, with a warning. Under `restricted` file I/O security in `povray.conf`,
list the mesh's directory in `[Permitted Paths]` as `read+write = /path/to/meshes` (or `read+write*` for a tree
of directories) to let POV-Ray keep its cache there. The `.povt` format is internal to the build that wrote it; deleting a
`.povt` file is always safe.

## Writing `.povm` from Node

`tools/povm/povm.mjs` is a dependency-free ES module for Node 24:

    import { writePovm } from './tools/povm/povm.mjs';
    await writePovm('trunk.povm', {
      vertices,        // Float32Array, x y z per vertex
      faces,           // Uint32Array, three vertex indices per face
      normals,         // optional Float32Array
      normalIndices,   // optional Uint32Array, three per face
      uvs,             // optional Float32Array, u v per uv
      uvIndices,       // optional Uint32Array, three per face
    });

It checks the counts and index ranges and that vertices are finite, rejecting what POV-Ray would, then streams the
sections to a temporary file renamed into place, so a large mesh is never copied into one buffer. `readPovm(path)`
reads a file back into the same shape; it is a helper for tests and tools and checks only the file's size.

## Converting existing meshes

    node tools/povm/convert.mjs trunk.obj trunk.povm
    node tools/povm/convert.mjs trunk.inc trunk.povm

`convert.mjs` uses the same writer.

- **Wavefront OBJ:** `v`, `vt` and `vn`; `f` with 1-based or negative indices, as `v`, `v/vt`, `v//vn` or `v/vt/vn`.
  Polygons become fans from their first vertex, and normals are normalised as POV-Ray's OBJ import does. Faces keep
  their file order, where the import moves faces without normals after the rest. Groups, smoothing groups and
  materials are ignored with a warning.
- **POV-Ray SDL:** a file holding one `mesh` or `mesh2`, optionally as `#declare Name = ...` after `#version`.
  `mesh` triangles have their corners and normals welded where equal, as POV-Ray's `mesh` parser does, and degenerate
  triangles are dropped as it drops them; `mesh2` normals are normalised as `mesh2` does. Only literal numbers are
  read: an expression, identifier, macro or other directive stops the conversion with an error. Textures, texture
  indices and `inside_vector` are ignored with a warning; any other modifier inside the mesh is an error, since it
  belongs on the object in the scene.

The converter's output renders identically to its source, pixel for pixel, except that UVs are stored as float32
where SDL keeps them in double precision.

`node --test tools/povm/test/povm.test.mjs` tests the writer and the readers. `tools/povm/test/test.sh <povray>
[<povray built with POV_PARSER_EXPERIMENTAL_OBJ_IMPORT=1>]` renders each kind of source against its conversion and
exercises the loader's errors and the tree cache.
