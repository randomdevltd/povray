# Progressive photons

The experimental photon estimator is selected independently of its work budget:

```pov
global_settings {
    photons { method 2 quality 1 }
}
```

Render with `+PR -A`. Photon quality includes stochastic pixel sampling. It is a
positive work multiplier: `ceil(4 * quality)` image passes, each with 65,536
emission samples per prepared scene set, preceded by one independent pilot batch.
The current maximum quality is 256. Higher values add passes to the same sequence.
`method 1` selects classic photons. Omitting the method retains classic behavior,
including in version 4 scenes. Quality is not an exposure control.

## Estimator

The runnable comparison in `tests/source/progressive_photon.cpp` starts with SPPM
and checks surface and volume flux against analytic uniform-density references.
The renderer uses probabilistic progressive photon mapping: fresh bounded photon
maps and camera paths in each pass, averaging linear radiance. This fits recursive
reflection, refraction, layered shading and participating media without retaining
an unbounded collection of camera vertices. VCM would require a more general
bidirectional material interface and path-space MIS; it is not used here.

Surface bandwidth uses an independent local pilot and a recursive estimate of
shot noise and tangent-plane curvature. The latter is a four-offset finite
difference, with its support shrinking as `N^(-1/8)`. A uniform-disk plug-in
bandwidth balances variance proportional to `1/(N*r^2)` against squared bias
proportional to `r^4`. Automatic bounds shrink as `N^(-1/6)` and keep at least
half the pilot scale to suppress rare isolated bright pixels. Local camera path
weights guide this choice; these diagnostics are proxies, not confidence bounds
on the complete pixel integral. The surface estimator does not denoise the image.
Volume and subsurface reconstruction use locally chosen pilot supports with
shrinking radii; volume radii shrink as `N^(-1/7)`.

See [SPPM](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Stochastic_Progressive_Photon_Mapping),
[probabilistic PPM](https://www.cs.umd.edu/~zwicker/publications/PPMProbabilistic-TOG11.pdf)
and [adaptive PPM](https://doi.org/10.1145/2451236.2451242) for the estimator families.

## Light power and authoring

Point and spot source colors specify radiant intensity. Area-light intensity is
distributed over the emitting area; parallel source colors specify irradiance.
Sources are sampled through `Emitter`, including media emitters. Each deposit
carries emitted flux divided by the complete emission mixture probability and
batch size. Overlapping target cones are included in that mixture. Surface
reflection, refraction and spectral splitting retain their transport weights.

An unfaded conventional point light therefore becomes inverse-square under
method 2. An existing `fade_power 2 fade_distance F` is interpreted by its
far-field intensity, `2*F*F*color`; its artificial near-field softening is removed.
Other conventional fade powers are rejected. Parallel lights must be unfaded.
These are intentional differences from the distance-compensated classic map.

Targets, `collect off`, reflection/refraction switches and `pass_through` retain
their authoring roles. Numerical target density is replaced by measured targeting:
previous-pass hit rates guide the next pass, with an exploration floor and an
isotropic component for nonparallel sources. Area emitters are sampled over their
area without requiring the classic photon `area_light` option.

`light_group { ... photons on }` enables group photon transport; `photons off`
disables it. The default is off before scene version 4.0 and on from 4.0.
Photon-only lamps retain group membership without entering the direct-light
list. Target selection and deposits enforce membership and `global_lights off`;
surface gathers also match the receiving object, so nearby objects in another
group cannot receive those deposits. Group cloning and render-tag filtering
preserve this relationship. Photon maps are shared by a prepared scene set,
not allocated per light group.
Volumetric photon collection from group lights is currently rejected; the
surface membership checks do not substitute for medium ownership.

Classic count, spacing, gather, radius, jitter, autostop, expansion and map-file
controls do not apply and are rejected. Radiosity, oriented area emitters and
`projected_through` lights are currently rejected. Explicit antialiasing is also
rejected; use photon quality to increase the number of camera samples.

Media photons are enabled by the existing `media` photon setting. Transport uses
ordered extinction integration and deposits flux times segment length. The media
step limit and the medium's own sampling settings remain integration controls,
separate from photon reconstruction. Curved paths use the existing refractive
field marcher and its segment tolerances. No photon beam approximation is added.
Subsurface photon illumination is reconstructed before diffusion; its cache is
renewed each pass so illumination from an older map is not reused.

## Media lights

Media lights now use method 2 through the same emission interface. Their
`light_source { photons { refraction on reflection off } }` block controls
transport just as it does for ordinary lights. Omitting those switches leaves
the target's settings in control. `brightness` scales emitted photon power and
direct illumination together; it does not scale the glow seen by the camera.
Classic method 1 continues to ignore media-light photon blocks with a warning.
Media and conventional lights share the emitter sampling and flux normalization.
The media light's default direct falloff is inverse-square, clamped within one
source cell; photons acquire inverse-square falloff by spreading through space.
There is no independent photon fade override yet.

The position sample's weight already includes its power-table probability;
shooting applies the light-selection and directional probabilities once. The
emitter's own neutral container is crossed before the selected target, applying
transmission and extinction. A refractive emitting container should itself be a
photon target. Glow along a photon path never adds power to the photon. Direct
illumination through a participating target is excluded even in an empty batch,
so it cannot be counted again alongside the photon estimate.

The candle sample includes an optional water glass:

```sh
povray +Idistribution/scenes/interior/media/blend_candle.pov +PR -A \
  +W360 +H270 Declare=WaterGlass=1 Declare=PhotonQuality=1
```

`Shimmer`, `Smoke`, `Plume` and `Mist` allow separate transport checks. The
existing view and lighting remain the sample's default when `WaterGlass=0`.
`tests/render/progressive_media_light.pov` compares an emissive sphere with an
equal-power point light, an identity lens with direct lighting, source controls,
self-attenuation, refractive heat and arrays of small emitters.

## Memory and reproducibility

There is one current map and one fixed pilot, not an accumulating photon map.
Photon storage depends on batch size and deposits per traced path, including
spectral branches and media samples. The log reports peak photon-vector capacity
and preparation CPU separately from shooting and camera work. Pixel state is
40 bytes per image pixel: 16 bytes of accumulated RGBA and 24 bytes of bandwidth
statistics. This is about 316 MiB at 3840 by 2160, excluding the frontend image,
scene, worker scratch storage and photon vectors. Camera work is tiled, while
the accumulated image remains in memory.

Emission has sixteen fixed logical shards. Workers may process several shards;
their deposits are merged in shard order before building the map. Sample keys
depend on seed, prepared set, pass and sample index. Pixel accumulation is in pass
order. Thread count and scheduling do not change the result on the same build
and platform; floating-point differences between platforms are not covered.

`+C` reconstructs estimator state by deterministic replay. It currently repeats
earlier passes, including the pilot, instead of storing photon maps or sampling
statistics in the render-state file. The final image is identical to an
uninterrupted render under the same scene, seed and settings. Snapshot and block
timing output use the normal progressive-render messages.

## Validation

`tests/render/progressive_photons.sh` checks thread-count and restart equality,
classic selector compatibility, linear emitted power, and invalid quality values.
It also covers area, spot, parallel and cylindrical lights, heterogeneous media,
dispersion, subsurface receivers, thin and close surfaces, reflected and refracted
views, curved media paths, inactive lights, and emission along photon paths.
The analytic prototype and the render test are included in `make check`.
On the focused 96 by 64 glass-sphere caustic, method 2 quality 1 used 3.10 billion
instructions and had linear-light mean squared error `7.52e-6` against a
32-times supersampled classic reference. Classic count 16,384 with `+AM4` used
5.72 billion instructions and measured `8.71e-6` on the same scene. Quality 4
used 10.50 billion instructions and measured `4.00e-7`; the earlier small-map
schedule used 18.58 billion instructions and measured `7.46e-7`. Quality 16
measured `2.06e-7`, with mean energy 1.1% lower than the reference. The reference
itself has sampling uncertainty, and these numbers describe this one scene.
On the visible 256 by 170 version, the revised schedule reduced displayed-pixel
mean squared error against classic from 11.99 to 9.37 and visibly bright outliers
from 35 to 26. This displayed-pixel comparison is a diagnostic, not a linear
physical error measure or proof that classic is unbiased. Broader scene and
performance validation remains necessary before method 2 replaces classic.
