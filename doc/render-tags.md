# Render tags

Render tags describe objects and cameras. Boolean filters select different views of one scene without copying geometry. A separate command-line filter limits which entities participate in preparation and rendering.

## Tags and expressions

```pov
sphere { 0, 1 tags { "draft", "room-a" } }
camera { location -5*z look_at 0 filter_tags { "room-*" & !"draft" } }
```

`tags` is inert metadata: tagging a camera does not change what it sees. Names are case-sensitive strings, sorted and deduplicated; repeating a name is harmless. Assigned names cannot contain `*`.

## Inheritance

Tags are inherited down the object tree, as CSS properties are. What an object writes decides its effective tags:

| Written | Effective tags |
| --- | --- |
| nothing (the default) | its parent's effective tags |
| `tags { none }` or `tags {}` | none, and nothing inherited |
| `tags { "a", "b" }` | exactly that list; the parent's are not inherited |
| `tags { "a", inherit }` | that list plus the parent's effective tags |

The tree is every container: `union`, `merge`, `light_group`, `intersection` and `difference`. A top-level object has no parent, so writing nothing there means no tags. `none` stands alone in a list. A list built from a spread that may be empty and should still inherit is written `tags { ...Tags, inherit }`. `inherit` is matched by its text inside `tags { }` and stays free as a name elsewhere. A later `tags` block on the same object replaces earlier ones entirely, list and `inherit` alike: `object { Thing tags { "b" } }` drops the tags `Thing` was declared with, and `tags { "a", inherit } tags { "b" }` gives exactly `"b"`, without inheriting. A camera takes the same forms but has no parent, so `inherit` adds nothing to its tags; a `tags` block in `camera { Eye tags { ... } }` replaces those the declared camera brings, and `none` or `{}` clears them.

```pov
union {
  sphere { 0, 1 }                         // "a"
  box { 1, 2 tags { "c", inherit } }      // "a" and "c"
  box { 3, 4 tags { "c" } }               // "c" only
  tags { "a" }
}
// filter "a": the sphere and the first box; "a" & !"c": the sphere; "c": both boxes
```

`filter_tags` takes one expression, with these operators in descending precedence:

| Expression | Matches |
| --- | --- |
| `"room-a"` | That exact tag |
| `"room-*"` | A whole tag matching the wildcard; `*` matches zero or more characters |
| `any` | At least one tag |
| `none` | No tags |
| `!expression` | Complement |
| `left & right` | Both expressions |
| `left \| right` | Either expression |
| `(expression)` | Explicit grouping |

Thus `"a" | "b" & !"c"` means `"a" | ("b" & (!"c"))`. `any | none` selects everything, and `any & none` selects nothing. Omitting a filter selects everything when no applicable default exists. An empty expression is an error; use an explicit expression for an empty selection.

## Tagged selections

`filter_tags` and `tagged` share the expression grammar and do different jobs:

- `filter_tags { EXPRESSION }` is a property of a camera, a portal, `global_settings` or the command line. It decides what a view shows when the scene renders, and it sees every object, wherever it was written.
- `tagged { EXPRESSION }` is an object. It stands for the objects already placed in the scene that the expression selects, and can be declared, framed with `look_at`, placed with `object { }`, transformed and combined in CSG.

```pov
camera { location -5*z look_at 0 filter_tags { "room-a" & !"draft" } }   // what this view shows
sphere { 0, 1 tags { "statue" } }
camera { location -5*z look_at tagged { "statue" } }                     // what this camera frames
object { tagged { "statue" } translate 3*x }                             // a copy beside the original
```

A selection expands once, when its expression is evaluated: at the `#declare` for a declared one, or where it is written. It holds the objects placed so far, in order, including those placed by macros, loops and `.pov4` programs. Objects written later are not in it, and neither is the tree it is written in: a container is placed only once every `tagged` inside it has expanded, so its earlier children are not in the scene yet. A `look_at` target must come before its camera. A declaration that is never placed is not in the scene, and placing a declared selection again places the same copies without selecting again. The command-line filter has already removed what it rejects; camera, portal and global filters do not affect a selection, so a camera can frame an object it does not show.

The expression is evaluated once per object, on its effective tags, and the copy keeps the structure around what it selects: a selected object inside a container is copied as that container, with its own tags and settings, holding only the selected parts. A `light_group` is copied as a light group, so a copied member is lit by those of the group's lights that are selected and copied with it, as in a hand-written group holding them. Everything the expression selects is copied with all its settings, lights and portals included. A copied portal makes its own far mouth when placed, and the far mouths that placed portals make are not selected themselves.

Using a selection as a `look_at` target or in a declaration adds nothing to the scene. Placing it adds copies of the selected objects as placed; the originals stay. The scene keeps its unions and light groups whole while it is parsed, so a selection sees them as written. Copies keep their `tags` as written, so inheritance applies in their new place: a copy that inherits takes the tags of the container it is placed in, and a later filter or selection must match those too. They share mesh data as `object { }` copies do, and a deferred mesh among them is built once for the original and its copies; neither selecting nor framing builds it while parsing, and framing measures it once the scene's deferred meshes are built. A selection of nothing is an empty union, with a warning; placing it adds nothing, and framing it is an error.

`tagged` is matched by its text: it is the selector only when `{` follows it in a 4.0 scene, and remains an ordinary identifier elsewhere.

## Cameras and defaults

A camera uses its own `filter_tags`, then the final `global_settings { filter_tags { ... } }`, then all objects. A global default declared after the camera still applies. The main camera remains the last accepted scene camera. When the command-line filter rejects every scene camera, rendering uses the built-in camera and reports that fallback.

```pov
camera { location -5*z look_at 0 }
sphere { 0, 1 tags { "preview" } }
global_settings { filter_tags { "preview" | none } }
```

A `screen` uses its camera's filter, the global default, or all objects. It does not inherit the observer's filter. A screen with no explicit camera captures an independent copy of the parser's default camera, including settings from `#default { camera { ... } }`; selecting a main scene camera does not change that copy.

```pov
#declare Monitor = camera {
  location <20, 0, -5> look_at <20, 0, 0>
  filter_tags { "monitor" }
}
pigment { screen { camera { Monitor } } }
```

## Portals

An entered portal side resolves its filter in this order: side, mouth, portal, global default, all objects. It never inherits the observer's selection. `front_filter_tags` and `back_filter_tags` select individual sides; `filter_tags` inside `near` or `far` supplies a mouth default. A portal-wide `filter_tags` supplies the common default. The portal object's `tags` determine which views contain its geometry.

```pov
portal {
  Door
  to { translate 20*x }
  filter_tags { "rooms" }
  near { filter_tags { "lobby" } front_filter_tags { "service" } }
  far { back_filter_tags { "outside" } }
  tags { "doors" }
}
```

## Command-line scene filtering

The INI option `Filter_Tags` uses the same Boolean grammar:

```sh
povray +Iscene.pov 'Filter_Tags="preview" | none'
```

This is a hard limit on the prepared scene: camera or portal filters cannot bring back excluded objects. It also filters tagged scene-camera candidates. `none` is often useful to retain untagged shared geometry and cameras.

`Filter_Tags` is a planning option applied after entity syntax and tags are known. It is never exposed as an SDL variable or a value readable by macro logic. Tags belong to individual entities; they do not control preprocessing. Macros and includes execute normally regardless of the command-line filter. Given more than once, the first `Filter_Tags` wins, as with `Declare=`. A malformed expression is reported where it goes wrong, for example `unclosed quote at character 2 of '!"a'`.

A macro can generate ordinary tagged objects without seeing the filter:

```pov
#macro PreviewSphere(Place)
  sphere { Place, 1 tags { "preview" } }
#end
PreviewSphere(0)
```

## Phases and deferred meshes

The language has two phases: parse turns the source into a program, and execute runs it, declaring values, expanding `tagged` and building and placing objects; only execute creates or copies objects. A `.pov4` scene is parsed into a tree first; classic SDL executes each statement as it is read. Scene preparation follows execute and never runs between statements. A scene is prepared in four phases:

1. **Parse.** The SDL runs, and generated or loaded meshes (`isosurface_mesh`, `skein_mesh` and `mesh2 { povm ... }`) are recorded without being built on the parser thread. With [eager preparation](html/r3_2.html#r3_2_8_1) (on by default; `-WE` turns it off) each starts building on one of `+WT` - 1 spare threads as soon as it is recorded, or, when `Filter_Tags` is given on the command line or in an INI file, once a kept object places it. The command-line filter removes what it rejects as each top-level object is completed.
2. **Scene sets.** Every filter is now known: the command line, each camera, `global_settings`, each screen camera and each portal side. One prepared set is made per distinct selection.
3. **Build.** Only meshes that some prepared set keeps are built, or finished if eager preparation started them, in parallel on up to `+WT` threads, and each once however many objects share its `#declare`; eager builds no set keeps are discarded. A mesh used only by objects every view rejects is never built (with eager preparation, one started before the scene's own filters were known is thrown away instead), so a camera or `global_settings` filter can switch whole profiles of a scene without command-line options.
4. **Render.** Bounding, photons, radiosity and tracing follow for each set.

Anything that needs a mesh's geometry while parsing builds it there and then, serially on the parser thread instead of in parallel after parsing (with eager preparation it uses a build that has finished or waits for one that is running): `min_extent`, `max_extent`, `trace`, `inside`, a mesh camera, a portal body, and an object used by `bounded_by`, `clipped_by`, an `object` or `potential` pattern or `projected_through`. Such a mesh is built even when no view uses it afterwards. The render reports the counts, and Parse Time includes the build phase, which a `Mesh build phase` line also times on its own; with eager preparation an `Eager preparation` line says what was built ahead, discarded or redone:

```
Deferred meshes: 6 recorded, 1 built for parse-time queries, 3 built for the scene, 2 not needed.
```

## Geometry and transport

A filter is evaluated once per object, against its [effective tags](#inheritance). Containers are not judged themselves: a container holds what remains of its children and disappears when none of them remain, so tagging a union tags everything in it that inherits, and a child with a list of its own is judged on that list alone.

In a `union`, `merge` or `light_group`, a rejected child is left out and the rest stay; a rejected light in a light group lights nothing. Splitting a union for acceleration keeps these decisions, since each piece keeps the tags it inherited.

**Changed from the first render-tag release:** each tagged container used to be checked against the filter as well, so a rejected container took all its contents with it, and a child's own tags were added to its containers' instead of replacing them. Filter `"u"` used to hide a sphere tagged `"u"` inside a union tagged `"uu"`; it now shows it, and the sphere is no longer `"uu"`. To keep a child in its container's tag set, write `inherit` in its list. `tags {}` used to keep the parent's tags and now clears them, so `tags { ...Tags }` with an empty `Tags` loses the inherited tags; write `tags { ...Tags, inherit }` to keep them. Repeated `tags` blocks on one object used to add their names together; the last one now replaces the others, so a copy that should keep a declared object's tags writes them again.

`intersection` and `difference` treat a rejected operand as empty space. An intersection with a rejected operand is empty and renders nothing. A difference whose first operand is rejected is empty; a rejected cutter cuts nothing, leaving the shape it would have cut. `inverse` complements empty space into everything, which an intersection ignores; a union holding everything has no surface and renders nothing.

```pov
difference {
  box { -1, 1 }
  cylinder { -2*z, 2*z, 0.5 tags { "hole" } }
}
// filter_tags { !"hole" } shows the box uncut
```

A view that keeps only some of a container's children gets its own copy of the container, holding the same children.

Each distinct resolved membership gets one prepared object list and bounds tree. Different expressions that select the same objects share preparation. Both classic slabs and BSP use these lists. Geometry is owned once; the prepared sets reference it.

Only the prepared-set identifier travels with a ray. Reflection, refraction, shadow and media rays preserve it; screens and portal crossings choose their resolved sets. Photon maps are isolated per prepared set. Radiosity caches are isolated per prepared set within each render view, with no mutable cache shared between concurrent views. Transport from an excluded light or object cannot leak into another selection.

Saved radiosity caches use `.set-<stable membership hash>` sidecars when filters are specified or multiple sets exist. The ordinary filename remains compatible for a single unfiltered scene. Saved photon maps use a keyed container for filtered or multiple sets; legacy photon files are accepted only for a single unfiltered scene.

## Grammar and measurement

The current Tree-sitter grammar covers Boolean tag expressions only and adds no bulk-scene syntax tree. Its generated C parser and runtime form the foundation for subsequent full SDL parsing; this change does not replace the SDL parser.

The tag keywords require `#version 4.0`; earlier scene versions retain these names as ordinary identifiers. `tagged` is not reserved at any version: a 4.0 scene treats it as the selector only before `{`. The `Filter_Tags` command-line and INI option is available independently of the SDL version. The Windows build now deliberately requires Visual Studio 2019 16.8 or later with v142 and a Windows 10 SDK for the C11 runtime; see [Windows build requirements](../windows/README.md#compilers).

Full SDL work must benchmark opaque or streamed bulk ranges, including concrete mesh construction and semantic checks. Generated meshes already wait for the scene sets; other entities are still constructed before their trailing tags are read. Full SDL parsing should parse the source with bounded bulk syntax, then skip excluded entity construction once tags are known. Macro execution should construct concrete geometry directly, with the master filter remaining outside SDL and macro logic.

`tests/render/render_tags.sh` checks selection pixels, invalid syntax, ordinary macro construction, nested views, acceleration equivalence, filtering inside light groups, unions, merges and CSG, and which deferred meshes are built for command-line, camera and global filters and for parse-time queries. `tools/bench/render-tags.pov` supplies three camera selections. `tools/bench/render-tags-many.pov` measures prepared-set scaling: `Declare=Count=N` varies screen count, and `Declare=Unique=0` reuses four memberships. Include parsing and preparation time and peak memory when comparing runs, in addition to trace time. Use `+PR` and compare decoded pixels.
