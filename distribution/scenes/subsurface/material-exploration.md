# Structured subsurface materials

`material-exploration.pov` uses metres, with `mm_per_unit 1000` for subsurface distances. The eight objects are a blue-veined hand, skin ear, wax candle, lamp membrane, fruit cross-section, ribbed leaf, alien control panel and rune scroll.

The 864 selectors and eight-by-five sheet are an exploration scaffold, not a final curated showcase. Legacy subsurface emission remains the default reference path while its physical replacement is being verified. The default view contains all eight objects. `Declare=Artifact=N` selects one object, numbered 0–7. `Declare=Sheet=1` places eight rows against five treatments. Use a portrait image for the sheet, such as `+W800 +H1200 +PR +WT2`.

| Treatment | Transport | Pattern sampling | Source | Surface |
| --- | --- | --- | --- | --- |
| 0 | 0.35 times path length | surface | external | fine pores |
| 1 | 0.7 times path length | 0.5 mm deep | external | fine pores |
| 2 | nominal scale | 1 mm deep | embedded | fine pores |
| 3 | 1.7 times path length | 2 mm deep | embedded | fine pores |
| 4 | 2.5 times path length | 3 mm deep | embedded | coarse pores |

The transport distances are illustrative material parameters, not measured coefficients. The five treatments are combined visual examples, rather than isolated numerical comparisons. The emissive hand, circuits and runes use subsurface emission pigments. The lamp and flame use interior volume lights. Skin normals remain separate from the geometric boundary.

`Declare=Case=N` selects one of 864 configurations. For an object index A, source mode S, transport strength L, sampling mode V, bounce B and normal mode N, the index is `A + 8*(S + 3*L + 9*V + 27*B + 54*N)`. A is 0–7; S, L and V are 0–2; B and N are 0–1. Source modes select external illumination, embedded emission and outward surface emission. Sampling modes select surface sampling or volume sampling at 0.6 and 1.2 mm. All selections remain individually renderable through the default reference path; the full combination matrix has not been validated with the physical internal-source solver.

In the combined sheet, treatment 3 shows embedded sources at the 1.7 times transport scale. `SheetBounce=1` additionally enables explicit candle bounce. Isolated treatment 3 enables candle bounce by default, and indexed cases retain bounce comparisons for every object. `WaxBounce=1` enables an explicit bounce light on wax, and `WaxBouncePasses` selects its passes. `SourceOn=0` disables the flame and lamp's volume lights while retaining their visible shapes. These controls permit the same candle to be compared with and without secondary illumination. They do not change the wax's absorption or scattering distances. `WaxLength` defaults to 0.083 metres and `WaxRadius` to 0.0105 metres. `WaxLength=0.160` reproduces a 160 mm taper while retaining the same flame position and source power. Wax has RGB transport distances of 8, 5 and 3 mm before the selected treatment scale. A candle need not illuminate its full height: the bottom can remain dark when physically absorbed light does not reach it.

`Exposure` uniformly scales the illumination and emission inputs; it defaults to 1. The flame source has 12 lumens before exposure and the membrane lamp 0.3 lumens. Keep exposure and treatment fixed when comparing bounce or renderer changes. The raw output can clip bright flame-adjacent wax; use linear high-dynamic-range output for energy measurements.

`InternalSource=1` disables the legacy subsurface emission pigment and places a patterned, transparent volume emitter inside the hand and fingers, panel or parchment instead. Its source is 0.02 lumens before exposure. A white strip beside each object faces both the material and camera to test exported light. This mode is intended for matched renderer comparisons: a visible pattern produced by legacy subsurface emission does not establish internal-source transport.

For controlled comparisons, `TransportScale`, `LookupDepth` and `LookupSpread` override the combined treatment. `Fill=0` removes the two external studio lights. `SourceModeOverride` selects one source mode directly, and `SkinPattern=1` adds a cellular skin pigment over homogeneous flesh. Keep these controls fixed across renderer comparisons.

`Export=1` adds a surface bounce light to the selected material, including the panel and parchment. Compare this switch at fixed source power to test the surface light bridge. `SurfaceSource=1` adds an independent, outward patterned surface light while retaining the chosen embedded source mode; it supplies its own energy and is not an exported-flux measurement.

`EmissionExport=1` requests a surface `light { emission }` without legacy finish emission. On a renderer supporting primary internal-flux export, this publishes the physical buried source field at the material boundary. Use `Export=0/1` and `EmissionExport=0/1` for none, emission, bounce and combined comparisons. The independent `SurfaceSource` pigment input takes precedence over the emission input when both are selected.

`PanelRound=0` replaces the rounded panel with a closed box as a boundary-tracing diagnostic. Both versions enclose the same physical source. Keep this setting identical in comparisons; the box does not validate curved-boundary behaviour.

`SubsurfaceSamples`, `InternalSamples` and `EmitterSamples` expose the global subsurface diffuse, internal-walk and emitter budgets. Defaults are 64, 128 and 96. The internal budget controls the per-channel random walks at each shaded point; emitter samples also apply to the flame, lamp, embedded patterned media and exported surface lights. `AreaSamples=9` sets both dimensions of the studio area light. Override these values explicitly for quick shape proofs; image resolution alone does not reduce transport noise. Use Radiance output (`+FH`) to retain bright flame and wax values without changing source power; HDR image viewers can tone-map previews.

`PanelHalfDepth` sets the real material thickness on either side of its buried source, defaulting to 0.006 metres. Shallower covers, such as 0.002 metres, expose finer circuit detail with shorter optical depth. Keep geometry fixed when comparing sampling quality or renderer changes.

`CameraZoom` enlarges an isolated object without changing physical dimensions. The physical hand source follows a boundary inset by 0.7 mm and occupies a 0.4 mm central slice through the palm and fingers. Its blue vein density remains separate from homogeneous flesh scattering.

The transparent embedded emitter has `ior off`: it supplies light within the host material without adding a separate air cavity or optical interface.

`VeinLayer=1` puts the hand emission inside a vascular layer bounded by the 0.7 mm and 1.5 mm inset hand surfaces, instead of the central slice. `VeinScale=0.015` spaces the physical blue veins farther apart than the default 5 mm pattern so their diffusion remains distinct. These controls change real source placement and pattern scale, not material attenuation.

`VeinPattern=1` replaces the turbulent marble source density with a branched vascular network following the palm and fingers. The source remains bounded by the selected real interior layer and uses the same total luminous flux.

## Physical source limits

`InternalSource=1` uses homogeneous flesh coefficients. Arbitrary source density and a patterned outer skin are supported. Spatially varying flesh absorption or scattering is not supported by the current internal radiative-transfer solver and reports a diagnostic; the reference-path selectors do not establish support for that combination. The physical mode therefore disables the reference flesh lookup beneath the surface while retaining the independent source pattern.

The validation so far covers selected hand, panel, parchment, candle and skin examples, along with separate engine regressions. It does not establish every source, pigment, bounce and normal combination in the 864-case matrix. Legacy emission remains available for comparisons and is not removed by these examples.

## Selected proof controls

Run these from the scene directory with a renderer supporting the physical internal-source and surface-export features. `+FH` retains linear high-dynamic-range values; use the same display exposure for comparisons.

```sh
povray +Imaterial-exploration.pov +W400 +H230 +PR +WT2 +FH Declare=Artifact=0 Declare=InternalSource=1 Declare=Fill=0.15 Declare=Export=0 Declare=EmissionExport=1 Declare=InternalSamples=32 Declare=CameraZoom=1.5 Declare=VeinLayer=1 Declare=VeinPattern=1
povray +Imaterial-exploration.pov +W400 +H230 +PR +WT2 +FH Declare=Artifact=7 Declare=InternalSource=1 Declare=Fill=0 Declare=Export=0 Declare=EmissionExport=1 Declare=InternalSamples=32 Declare=CameraZoom=1.5
povray +Imaterial-exploration.pov +W400 +H230 +PR +WT2 +FH Declare=Artifact=2 Declare=WaxLength=0.160 Declare=TransportScale=1 Declare=WaxBounce=0 Declare=Treatment=2 Declare=Fill=0 Declare=InternalSamples=32
```

For the candle bounce pair, change only `WaxBounce` to 1. For the optical-depth comparison, retain `WaxBounce=0` and change only `TransportScale` from 1 to 4. The latter uses illustrative RGB transport distances of 32, 20 and 12 mm; it is a different material, not a change to source power or attenuation outside the material.

`skin-closeup.pov` provides an independent normal detail view. Its defaults are a grazing key, cool fill, `Strength=0.35` and visible highlights. Use `Highlights=0` to isolate the subsurface response and `Flat=1` for the same geometry without perturbed normals.

## Detailed objects and transparent coatings

The closed hand uses overlapping tapered finger segments with rounded joints, nails and knuckle creases. The ear adds a helix, antihelix, concha, tragus and lobe. The wax includes solid drips and a metal holder, the membrane lamp has a rib cage and pedestal, the fruit has a rind and radial membranes, the leaf has bilateral secondary veins, the panel has a rounded metal frame and slotted screws, and the parchment has rolled ends and end fittings. `Detail=0` removes added decorative geometry where applicable. Material normals use pores, wax folds, membrane fibres, fruit granules, leaf grain, enamel stipple and parchment fibres at physical scales.

`Coating=1` adds a second ordinary texture over the homogeneous subsurface texture of the hand, panel and parchment. Its `rgbt` pigment has opaque painted or printed regions and fully transparent openings; it has no subsurface finish or emission. The panel carries teal enamel with bare circuit tracks and small worn chips. The parchment carries opaque printed bands with transparent rune strokes and unprinted margins. The hand carries dark cellular markings over the blue vascular source. `Coating=0` removes only this outer texture, keeping host geometry, homogeneous scattering, buried source, illumination and luminous power fixed. Transparency belongs to the coating; the underlying material remains a closed opaque subsurface solid.

The coating attenuates the visible exiting subsurface field and the exported surface field. It does not introduce a separate scattering volume or model absorption by a physical coating at every intermediate internal reflection. A transparent coating region reveals subsurface light; it does not cut a hole through the solid or make the whole host transparent. The normal pattern changes surface shading and supported diffusion entries; it does not bend internal random walks away from the geometric boundary.

For detailed physical proofs use `InternalSource=1`, `Treatment=2`, `SubsurfaceSamples=64`, `InternalSamples=128` and `EmitterSamples=96`, with `+PR +AM4 +WT2 +FH`. Begin at 400 by 230 pixels, inspect, then use 800 by 460 or larger with the same source power and material. `EmissionExport=1` includes the adjacent receiver in the panel, hand and parchment proof. `Fill=1` retains the studio light for readable form; `Fill=0.15` is a separate low-fill transport demonstration and should not be compared as a sampling improvement.

Run these from this directory for the detailed panel, parchment and hand. They preserve the 0.02 lumen source and exposure 1; the hand uses the vascular layer and branched source pattern.

```sh
povray +Imaterial-exploration.pov +W800 +H460 +PR +AM4 +A0.1 +WT2 +FH Declare=Artifact=6 Declare=InternalSource=1 Declare=Fill=1 Declare=Export=0 Declare=EmissionExport=1 Declare=SubsurfaceSamples=64 Declare=InternalSamples=128 Declare=EmitterSamples=96 Declare=CameraZoom=1.5
povray +Imaterial-exploration.pov +W800 +H460 +PR +AM4 +A0.15 +R2 +WT2 +FH Declare=Artifact=7 Declare=InternalSource=1 Declare=Fill=1 Declare=Export=0 Declare=EmissionExport=1 Declare=SubsurfaceSamples=64 Declare=InternalSamples=128 Declare=EmitterSamples=96 Declare=CameraZoom=1.5
povray +Imaterial-exploration.pov +W800 +H460 +PR +AM4 +A0.1 +WT2 +FH Declare=Artifact=0 Declare=InternalSource=1 Declare=Fill=1 Declare=Export=0 Declare=EmissionExport=1 Declare=SubsurfaceSamples=64 Declare=InternalSamples=128 Declare=EmitterSamples=96 Declare=CameraZoom=1.5 Declare=VeinLayer=1 Declare=VeinPattern=1
```

For an overview of the eight detailed physical examples, omit `Artifact` and `CameraZoom`, keep `VeinLayer=1` and `VeinPattern=1`, use `EmissionExport=0`, and render at 800 by 560 pixels with the same three sample budgets and `+AM4 +A0.15 +R2`. The explicit antialiasing depth bounds pixel sampling around stochastic transport without reducing its internal-walk or emitter budgets. This overview demonstrates the selected treatment and source configuration; it does not validate the entire selector matrix.
