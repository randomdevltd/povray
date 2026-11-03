# Structured subsurface materials

`material-exploration.pov` uses metres, with `mm_per_unit 1000` for subsurface distances. The eight objects are a blue-veined hand, skin ear, wax candle, lamp membrane, fruit cross-section, ribbed leaf, alien control panel and rune scroll.

The default view contains all eight objects. `Declare=Artifact=N` selects one object, numbered 0–7. `Declare=Sheet=1` places eight rows against five treatments. Use a portrait image for the sheet, such as `+W800 +H1200 +PR +WT2`.

| Treatment | Transport | Pattern sampling | Source | Surface |
| --- | --- | --- | --- | --- |
| 0 | 0.35 times path length | surface | external | fine pores |
| 1 | 0.7 times path length | 0.5 mm deep | external | fine pores |
| 2 | nominal scale | 1 mm deep | embedded | fine pores |
| 3 | 1.7 times path length | 2 mm deep | embedded | fine pores |
| 4 | 2.5 times path length | 3 mm deep | embedded | coarse pores |

The transport distances are illustrative material parameters, not measured coefficients. The five treatments are combined visual examples, rather than isolated numerical comparisons. The emissive hand, circuits and runes use subsurface emission pigments. The lamp and flame use interior volume lights. Skin normals remain separate from the geometric boundary.

`Declare=Case=N` selects one of 864 configurations. For an object index A, source mode S, transport strength L, sampling mode V, bounce B and normal mode N, the index is `A + 8*(S + 3*L + 9*V + 27*B + 54*N)`. A is 0–7; S, L and V are 0–2; B and N are 0–1. Source modes select external illumination, embedded emission and outward surface emission. Sampling modes select surface sampling or volume sampling at 0.6 and 1.2 mm. All selections remain individually renderable.

In the combined sheet, treatment 3 shows embedded sources at the 1.7 times transport scale. `SheetBounce=1` additionally enables explicit candle bounce. Isolated treatment 3 enables candle bounce by default, and indexed cases retain bounce comparisons for every object. `WaxBounce=1` enables an explicit bounce light on wax, and `WaxBouncePasses` selects its passes. `SourceOn=0` disables the flame and lamp's volume lights while retaining their visible shapes. These controls permit the same candle to be compared with and without secondary illumination. They do not change the wax's absorption or scattering distances. `WaxLength` defaults to 0.083 metres and `WaxRadius` to 0.0105 metres. `WaxLength=0.160` reproduces a 160 mm taper while retaining the same flame position and source power. Wax has RGB transport distances of 8, 5 and 3 mm before the selected treatment scale. A candle need not illuminate its full height: the bottom can remain dark when physically absorbed light does not reach it.

`Exposure` uniformly scales the illumination and emission inputs; it defaults to 1. The flame source has 12 lumens before exposure and the membrane lamp 0.3 lumens. Keep exposure and treatment fixed when comparing bounce or renderer changes. The raw output can clip bright flame-adjacent wax; use linear high-dynamic-range output for energy measurements.

`InternalSource=1` disables the legacy subsurface emission pigment and places a patterned, transparent volume emitter inside the hand and fingers, panel or parchment instead. Its source is 0.02 lumens before exposure. A white strip beside each object faces both the material and camera to test exported light. This mode is intended for matched renderer comparisons: a visible pattern produced by legacy subsurface emission does not establish internal-source transport.

For controlled comparisons, `TransportScale`, `LookupDepth` and `LookupSpread` override the combined treatment. `Fill=0` removes the two external studio lights. `SourceModeOverride` selects one source mode directly, and `SkinPattern=1` adds a cellular skin pigment over homogeneous flesh. Keep these controls fixed across renderer comparisons.

`Export=1` adds a surface bounce light to the selected material, including the panel and parchment. Compare this switch at fixed source power to test the surface light bridge. `SurfaceSource=1` adds an independent, outward patterned surface light while retaining the chosen embedded source mode; it supplies its own energy and is not an exported-flux measurement.

`EmissionExport=1` requests a surface `light { emission }` without legacy finish emission. On a renderer supporting primary internal-flux export, this publishes the physical buried source field at the material boundary. Use `Export=0/1` and `EmissionExport=0/1` for none, emission, bounce and combined comparisons. The independent `SurfaceSource` pigment input takes precedence over the emission input when both are selected.

`PanelRound=0` replaces the rounded panel with a closed box as a boundary-tracing diagnostic. Both versions enclose the same physical source. Keep this setting identical in comparisons; the box does not validate curved-boundary behaviour.

`SubsurfaceSamples`, `InternalSamples` and `EmitterSamples` expose the global surface, internal-walk and emitter budgets. Defaults are 12, 2 and 16 for quick shape proofs; `InternalSamples=32` or higher reduces physical internal-source noise. Use Radiance output (`+FH`) to retain bright flame and wax values without changing source power; HDR image viewers can tone-map previews.

`PanelHalfDepth` sets the real material thickness on either side of its buried source, defaulting to 0.006 metres. Shallower covers, such as 0.002 metres, expose finer circuit detail with shorter optical depth. Keep geometry fixed when comparing sampling quality or renderer changes.

`CameraZoom` enlarges an isolated object without changing physical dimensions. The physical hand source follows a boundary inset by 0.7 mm and occupies a 0.4 mm central slice through the palm and fingers. Its blue vein density remains separate from homogeneous flesh scattering.

The transparent embedded emitter has `ior off`: it supplies light within the host material without adding a separate air cavity or optical interface.

`VeinLayer=1` puts the hand emission inside a vascular layer bounded by the 0.7 mm and 1.5 mm inset hand surfaces, instead of the central slice. `VeinScale=0.015` spaces the physical blue veins farther apart than the default 5 mm pattern so their diffusion remains distinct. These controls change real source placement and pattern scale, not material attenuation.

`VeinPattern=1` replaces the turbulent marble source density with a branched vascular network following the palm and fingers. The source remains bounded by the selected real interior layer and uses the same total luminous flux.
