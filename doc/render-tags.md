# Render tags

Render tags describe objects and cameras. Boolean filters select different views of one scene without copying geometry. A separate command-line filter limits which entities participate in preparation and rendering.

## Tags and expressions

```pov
sphere { 0, 1 tags { "draft", "room-a" } }
camera { location -5*z look_at 0 filter_tags { "room-*" & !"draft" } }
```

`tags` is inert metadata: tagging a camera does not change what it sees. Names are case-sensitive strings, sorted and deduplicated; repeating a name is harmless. Assigned names cannot contain `*`. An empty `tags {}` means no local tags.

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

The current SDL parser constructs an object, and may load or construct its mesh, before discovering its trailing tags. Filtering therefore avoids subsequent object postprocessing, bounds preparation, radiosity and photon work, but does not yet avoid this initial construction. A macro can generate ordinary tagged objects without seeing the filter:

```pov
#macro PreviewSphere(Place)
  sphere { Place, 1 tags { "preview" } }
#end
PreviewSphere(0)
```

## Geometry and transport

Every object checks the filter, at every level. Its effective tags are its own and those of everything that contains it, so tagging a union tags all of it. A tagged container that the filter rejects takes all of its contents with it. An untagged container only groups: its children decide, and it disappears when none of them remain.

In a `union`, `merge` or `light_group`, a rejected child is left out and the rest stay; a rejected light in a light group lights nothing. Splitting a union for acceleration keeps these decisions, since each piece still answers to the union's tags.

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

The tag keywords require `#version 4.0`; earlier scene versions retain these names as ordinary identifiers. The `Filter_Tags` command-line and INI option is available independently of the SDL version. The Windows build now deliberately requires Visual Studio 2019 16.8 or later with v142 and a Windows 10 SDK for the C11 runtime; see [Windows build requirements](../windows/README.md#compilers).

Full SDL work must benchmark opaque or streamed bulk ranges, including concrete mesh construction and semantic checks. It should parse the full source with bounded bulk syntax, then skip excluded entity construction once tags are known. Macro execution should construct concrete geometry directly, with the master filter remaining outside SDL and macro logic.

`tests/render/render_tags.sh` checks selection pixels, invalid syntax, ordinary macro construction, nested views, acceleration equivalence, and filtering inside light groups, unions, merges and CSG. `tools/bench/render-tags.pov` supplies three camera selections. `tools/bench/render-tags-many.pov` measures prepared-set scaling: `Declare=Count=N` varies screen count, and `Declare=Unique=0` reuses four memberships. Include parsing and preparation time and peak memory when comparing runs, in addition to trace time. Use `+PR` and compare decoded pixels.
