# Subsurface showcase

`showcase.pov` is one composed night-time desk lit mostly by its own glowing
objects: a scaly dragon egg with fire inside, a hand with glowing capillaries,
a backlit ear, a wax candle, a membrane lantern, grapes, a circuit tablet and a
paper scroll with runes. Units are metres; subsurface distances are millimetres
(`mm_per_unit 1000`).

Preview (no antialiasing), from this directory:

```sh
povray +Ishowcase.pov +W1200 +H800 -A +WT8
```

## Controls

| Declare | Default | Meaning |
| --- | --- | --- |
| `View` | 0 | 0 the whole desk; 1 egg, 2 hand, 3 ear, 4 candle, 5 lantern, 6 grapes, 7 tablet, 8 scroll close-ups |
| `Exposure` | 0.075 | camera exposure: multiplies every physical source |
| `Fill` | 1 | dim warm room light from off-frame |
| `Sources` | 1 | 0 switches every flame and buried emitter off |
| `Export` | 1 | 0 removes the surface lights that carry light out of glowing objects onto neighbours |
| `Pigments` | 1 | 0 removes the opaque layers over glowing material (egg scales, lantern lacquer, tablet pads, ink, grape bloom) |
| `Normals` | 1 | 0 removes all texture normals |
| `SssSamples`, `SssInternal`, `LightSamples` | 32, 64, 32 | noise budgets only |

Source fluxes are real-world lumens before exposure: `CandleLumens` 12,
`LanternLumens` 6, `EggLumens` 3, `PanelLumens` 0.3, `RuneLumens` 0.1,
`HandLumens` 0.1.

Every object carries a tag (`egg`, `hand`, `ear`, `candle`, `lantern`,
`grapes`, `panel`, `scroll`, plus `room` and `fill`), so a study keeps one
object and the lights it needs:

```sh
povray +Ishowcase.pov +W480 +H320 -A Declare=View=1 'Filter_Tags="egg" | "candle" | "room" | "fill" | none'
```

## Geometry limits

A buried emitter needs its host to answer the renderer's enclosing-boundary
query. Spheres, boxes, cylinders, linear sphere sweeps, `sor` and CSG of these
work. Blob, mesh2 and isosurface hosts, and a superellipsoid merged with other
shapes, stop with `Internal subsurface light requires an enclosing material
boundary`, so the hand and ear are built from linear sphere sweeps and the
scroll's rune area is a box between two mesh ends that carry no source.
