# Buried veins

`buried-veins.pov` is a hand lit from inside its own skin. One buried volume light fills a layer 0.3 to 1.2 mm under
the skin of the back of the hand and the fingers' first joints, and one procedural pigment decides where in that layer
light is given off: a vein network of thin lines on black. Five trunks run from the wrist to the gaps between the
knuckles with an arch across the middle, branches leave them and capillaries leave the branches, each level finer and
more crowded than the last. The hand is a skin-like method 2 subsurface solid, warm skin over paler flesh, in which red
light spreads farthest and blue least. Units are metres; subsurface distances are millimetres (`mm_per_unit 1000`).

The scene shows how the two quality knobs of a light buried in a subsurface body trade against each other; see "Lights
Inside Subsurface Materials" in the reference.

Preview, from this directory:

```sh
povray +Iburied-veins.pov +W800 +H450 -A
povray +Iburied-veins.pov +W480 +H300 -A Declare=View=1 Declare=Cells=400000 Declare=Samples=16
```

## Controls

| Declare | Default | Meaning |
| --- | --- | --- |
| `View` | 0 | 0 the whole hand; 1 a close-up of the knuckles and the back of the hand |
| `Cells` | 400000 | the hand's `subsurface { domain_cells }`: map points over its 49,000 mm² of surface, about sqrt(49000 / Cells) mm apart |
| `Samples` | 16 | the vein light's `samples`: paths each map point walks |
| `LightCells` | 16000000 | the vein light's own `domain_cells`: its source cells, 0.48 mm across at the default |
| `Seed` | 7 | places the vein pattern |
| `VeinNits` | 1 | the vein light's brightness |
| `BranchGain`, `CapillaryGain` | 2, 5 | emission of branches and capillaries against trunks |
| `Fill` | 1 | the room light; 0 leaves only the veins |
| `Normals` | 1 | 0 removes the skin's normals |

## The vein light's own cells

An interior light reads its pigment from a lattice of source cells across its container's bounding box, filled once
before the render, not from the pigment at each point. A line narrower than about two cells comes out broken or is
missed. The layer is cut to the back of the hand so that its box is small, every vessel is at least 0.8 mm across, and
`LightCells` gives 0.48 mm cells (a 407 x 51 x 244 grid, about 300 MB). Finer lines need more cells: the count per
axis is capped at 128 x cbrt(LightCells / 500000), so halving the cell size takes eight times the cells.

## Cost

Each map point walks `Samples` paths once, when the render first needs its part of the cloud, so the cloud costs about
points times samples whatever the image size, over the whole hand and not only what the camera sees. The render
statistics give that time on their "Method 2 cloud lighting" line.
