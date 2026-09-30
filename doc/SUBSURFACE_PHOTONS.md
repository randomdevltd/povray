# Photons entering subsurface materials

Surface photons illuminate the multiple-scattering diffusion term of subsurface methods 1 and 2. A caustic enters at sampled surface points and spreads through the existing diffusion profile. Reflected surface highlights remain separate. Photon-driven single scattering and unscattered transmission are unsupported.

The entry estimator sums stored incident flux times exterior-to-material Fresnel transmission and divides by the actual gather disc area, `pi * radius^2`. Stored photon density already includes projected area: there is no additional incident cosine or diffuse/pigment multiplier. Flesh and skin entry factors, exit transmission and texture-layer weights use the existing subsurface path. Emission is added independently once.

Method 1 gathers incident irradiance at diffusion entries independently of the light loop. It works with a loaded map and no light sources. Finite boxes, spheres and closed meshes use projected surface importance sampling. Three projection axes retain positive support; a mixture of the dipole poles samples the absorbed radial profile. Each entry uses the full mixture area PDF, geometric normals and the existing interior-base visibility domain. Missed lines count as zero samples, while separate visible crossings contribute separately. The photon term has its own average, so changing live-light count cannot multiply it. Other geometry retains the original sampled estimator. Method 2 gathers once per cloud point; its hierarchy carries the combined illumination. Its reconstructed core uses a profile-weighted mean of the disc's already-filtered photon illumination. Local diffusion, missing core coverage and unsupported photon clouds sample the photon term with method 1. Where the original analytic cloud succeeds, it remains in use independently of that photon fallback. Photon clouds with smooth mesh vertex normals use projected sampling, since the legacy cloud areas and facing weights use those shading normals. The analytic cloud remains unchanged.

Clouds distinguish effective photon collection and belong to one render view. A shared lighting job owns its gather scratch and restores the receiver's photon shadow state when it finishes. Geometry seeds do not depend on photon mode. The map and gather settings are read after the photon sorting barrier and must remain immutable during rendering; the existing scene-wide photon map still does not support concurrent rebuilds by different views.

## Boundaries and spatial resolution

The flattened adaptive gather uses the existing radius and count settings. Rejections preserve its returned aperture. Cloud queries recover a separate geometric normal without changing the cloud's original normals, areas or analytic illumination. Candidate deposits are checked against the receiving solid with a bounded normal probe, a position tolerance derived from photon float precision, outward normal agreement and an incoming exterior hemisphere test. Mesh probes use face normals. Multiple matching crossings of the receiver are rejected, except coincident coplanar triangles belonging to the same mesh. A scoped mesh-inside probe includes near-boundary parity crossings; ordinary ray-hit thresholds remain unchanged and the thread state is restored before subsequent shading. Spatially separated receivers are checked geometrically, rather than only by shared interior. Photon records contain no receiver identity, so deposits on perfectly coincident surfaces belonging to different objects cannot be distinguished; separations below the matching tolerance can remain ambiguous too.

Open surfaces without an orientable solid boundary, unresolved small apertures at large coordinates, coincident boundaries and unusual transformed shapes that cannot reconstruct their deposit can lose photon illumination conservatively. Light-group source attribution is an existing photon-map limitation: saved photons contain neither source nor receiver identity, and photon shooting's light-group check is incomplete. Mixed receiver collection flags, light groups or interior identities in a cloud require the sampled path.

A retained CSG root is the receiver domain. The geometric test does not distinguish disconnected descendants of that root: similarly oriented nearby parts inside the aperture can mix irradiance. Ordinary unions may be dissolved into separate receivers by the parser. A stricter connected-material domain policy needs separate validation, since child identity alone can create seams across joined solids.

Cloud spacing and photon aperture are independent smoothing scales, in scene units. The diffusion length is `1 / (sigma_tr * mm_per_unit)`. A caustic narrower than the point spacing can be missed or overrepresented; lowering `spacing` is necessary for convergence. A nearest-photon count cap can shrink the aperture far below the requested radius at a tight focus. Increase the maximum gather count, broaden the source or decrease spacing to resolve it. No automatic clamp changes the user's photon aperture.

Requested spacing can exceed the cloud's fixed point limits: 262144 points per cell and 1048576 across the view. Such clouds use sampled fallback, which may look noisier even at a finer requested spacing. Check cloud/sample/fallback statistics before treating a spacing change as a cloud-resolution comparison; increasing diffuse samples then controls the fallback. Neither point-limit failure nor a sparse useful gather is repaired by photon count alone.

In method 2, points behind the shading point's tangent plane that turn away from it (the far face of a wall thinner than the cloud's discs, and the side walls at its edges) are summed point by point, with the distance taken through the object, instead of sending the shading point to method 1. A thin closed solid such as a leaf therefore gathers its photons once per cloud point rather than once per sample per pixel; on the thin-receiver fixture (`Thin=1`, 320x320) sampled gathers fall from 7.9 million to 0.31 million and fallback shading points from 45150 to 3088. Thin, weakly scattering objects can need the unsupported directional single-scattering term. This is an exterior-entry diffusion model, not a volumetric photon tracer. It does not test opacity or prevent future transmitting/refracting subsurface finishes; those require a separate directional or volumetric transport model. Entry IOR is relative to the scene atmosphere; nested participating media need a defined boundary-medium model.

Projected photon sampling avoids the original uniform-direction concentration on thin surfaces and its grazing area-weight clamp. It uses the configured diffuse sample count at both permitted subsurface recursion depths; analytic diffuse and single scattering retain their existing recursive sample limit. This adds linear work for secondary receivers without creating additional recursive rays. Unsupported geometry retains the classic estimator and its variance, clamp and recursive-count limitations. Receiver and hemisphere filtering happens after the capped gather, so rejected deposits can occupy its allowance. The cloud refines from geometry and analytic illumination, rather than photon colour variation; a resolved total flux does not prove that narrow spectral bands are resolved.

An analytic retry uses separate cache cells. Under point-budget pressure, photon work can exhaust the shared budget before that retry and force the analytic sampled fallback too. Inactive photon paths retain the original rendering; arbitrary unrelated nonempty maps are not guaranteed to preserve the analytic approximation once the point budget is exhausted.

## Reproducible fixtures

`tests/render/subsurface_photons.pov` focuses a light through a glass sphere onto a subsurface block beside an ordinary diffuse control. `SurfaceOnly=1` makes the main block a second ordinary control. Defaults use a fixed aperture with a high count cap, avoiding an unresolved point focus. Render linear 16-bit PPMs with `+FP16 File_Gamma=1`; use one thread and the same saved map for method comparisons.

```
povray +Itests/render/subsurface_photons.pov +W64 +H48 +WT1 -A -D +FP16 File_Gamma=1 Declare=SaveMap=1 +Omap.ppm
povray +Itests/render/subsurface_photons.pov +W64 +H48 +WT1 -A -D +FP16 File_Gamma=1 Declare=LoadMap=1 Declare=Method=1 Declare=Samples=4096 +Oreference.ppm
povray +Itests/render/subsurface_photons.pov +W64 +H48 +WT1 -A -D +FP16 File_Gamma=1 Declare=LoadMap=1 Declare=Spacing=0.175 +Ocloud.ppm
python3 tools/bench/check-subsurface-photons.py reference.ppm cloud.ppm --rect 20 15 38 30
```

Run these commands through your render scheduler when one is configured. Save maps and outputs in a writable output directory, outside the repository. Add the source tree's `distribution/include` to the library path as needed.

Useful variants are `Method`, `Samples`, `Single`, `Spacing`, `GatherMin`, `GatherMax`, `GatherRadius`, `PhotonCount`, `Shift`, `Mfp`, `Power`, `Skin` (0 none, 1 constant, 2 patterned), `Thickness`, `Glow`, `Thin`, `Nearby`, `Collect`, `Photons`, `ExtraLights`, `Area`, `RefractPhotons`, `ReflectPhotons`, `LightPhotons`, `CSG` and `Radiosity`. `LoadMap` and `SaveMap` select `subsurface-photons.ph`; `NoLights` exercises map-only lighting. `HideLens` removes the lens for a map-only comparison.

`tests/render/subsurface_photon_prism.pov` provides a full-view dispersive prism and a neutral slab. Render at 640 by 400: save a map with `SurfaceOnly=1 SaveMap=1`, then load it for method 1 and method 2. Defaults use 21 spectral bands, a 200000 shooting target, aperture 0.025 and maximum gather count 20000. The measured ordinary control resolves this spectrum before subsurface diffusion broadens it. Compare the spectral order, colour profiles and smoothness, rather than expecting ordinary and subsurface surfaces to have identical brightness or sharpness.

For photon-only linear comparisons, set `Fill=0 Backdrop=0 NoLights=1 HidePrism=1 LoadMap=1`. The black backdrop prevents existing unscattered transmission from contaminating thin-receiver measurements. A photon-map filename is relative to the renderer's working directory; an isolated job can set `MapFile` to an absolute readable filename in an SDL wrapper. `Method`, `Samples`, `Spacing`, `Mfp`, `Skin`, `Flesh`, `Thickness`, `Thin`, `Nearby`, `Bands`, `Dispersion`, `PhotonCount` and the gather controls support separate comparisons. `Flesh` and `Thickness` require `Skin` to be nonzero. Regenerate maps when changing dispersion, `Bands`, `PhotonCount`, source `Power` or prism geometry, and verify deposit compatibility before reusing a map with different receiver geometry. Increase `Bands` and photon density independently to check spectral sampling convergence.

`tests/render/subsurface_photon_flux.pov` loads the same filename over a wide planar receiver. Build `tools/bench/make-photon-flux.cpp` against a configured build with `tools/bench/make-photon-flux.sh BUILD OUTPUT_EXECUTABLE`, then write a native photon-map fixture:

```
make-photon-flux subsurface-photons.ph 0.05 1 0 0
```

Arguments after the filename are deposit spacing, incident irradiance, incident angle in degrees, deposit height and illuminated patch radius. The map uses the renderer's native `Photon` layout and a balanced tree. Uniform incident irradiance 1 should match `Photons=0 Direct=1` at normal incidence. At angle `A`, compare against direct light power `1/cos(A)` to preserve planar irradiance, accounting for the map's direction quantisation; angle 180 tests backside rejection. Test `Skin`, `Thickness`, `Glow`, `Layers`, `Mixed` (1 differing collection, 2 differing interiors), `ChildEta`, `Thin`, `Extent`, `Infinite`, `MeshMode` (1 flat, 2 smooth), `Group`, `GlobalLights`, `Offset`, `Mm`, `Mfp`, `ExtraLights`, `Collect`, empty maps, `GatherMax=0` and rendering quality. Translate both deposits and receiver for a positive boundary reconstruction check; translate only one for leakage rejection.

Render statistics report cloud and sampled gather calls, fallback shading points, full tree searches (including expansion attempts), accepted/candidate deposits and the mean and standard deviation of actual radii. Counts include empty gathers and pretrace work. Radius summaries include zero contributions. Surface-map memory and file format are unchanged; a photon cloud's core side array adds 12 bytes per point.

## Validation and measured costs

Measurements use linear 16-bit PPMs at 64 by 48, one tracing thread, no antialiasing, identical loaded maps and a GCC 15 native release build on an AMD EPYC VM. Hardware counts include parsing and tracing; CPU figures below come from the renderer's tracing statistics. Short timings are noisy, so instruction counts are the stronger cost comparison. All outputs and maps were kept outside the source tree.

The uniform-irradiance fixture agrees with its direct-light reference to 0.058% in mean energy for method 2. Halving incident power halves the output within one 16-bit level. Quadrupling photon density changes mean energy by 0.0083%; a quantised 60-degree entry agrees with its direct reference within 0.36%. A constant skin tint of 0.25 attenuates the result by 0.25 across both crossings; zero skin thickness preserves the image. Emission adds once, independently of the photon term. Moving only the receiver by 0.006 rejects the map; translating both preserves mean energy within 0.016%. Backside and entry total-internal-reflection maps produce zero light.

Both methods match the pre-change build pixel for pixel with photons absent, collection disabled, maximum gather count zero, an empty map or quality-disabled subsurface rendering. Additional controls cover a collect-off CSG child under a nonzero photon-target light, unrelated maps on smooth meshes and local/mixed-interior/thin receivers, positive wide thin and infinite slabs, transformed flat/smooth meshes, area photon emission, refraction/light photon flags, light groups and radiosity. One/four tracing threads and a different render pattern produce identical uniform-cloud pixels. Five zero-power lights do not multiply method 1's map-only result. Enabling single-scattering samples with no live lights leaves the photon-only image unchanged.

For the focused fixture, an 8192-sample method 1 map-only reference isolates photon diffusion from analytic lights. The pre-change build produces zero light in the measured receiver rectangle. The following comparisons use rectangle `(20,15)-(38,30)` and a fixed 0.12 photon aperture:

| Method 2 spacing | RMS error in linear RGB | Mean energy error |
|---|---:|---:|
| 0.7 | 0.002918 | +5.43% |
| 0.35 | 0.000915 | -0.98% |
| 0.175 | 0.000752 | -0.62% |
| 0.0875 | 0.000728 | -1.38% |

The 4096-sample reference itself differs from the 8192-sample reference by RMS 0.000682. Spatial error decreases as spacing falls, while integrated energy is not strictly monotonic near that sampling floor. These results do not establish convergence for every count-limited sharp caustic.

Two hardware-counted repeats of the focused fixture with its live source and 128 diffuse samples give:

| Build/method | Ginstructions | Gcycles | Trace CPU seconds | Peak RSS MiB |
|---|---:|---:|---:|---:|
| Pre-change method 2 | 0.731 | 0.283-0.289 | 0.070-0.105 | 17 |
| Photon method 1 | 3.828 | 1.404-1.416 | 0.406-0.411 | 16 |
| Photon method 2, spacing 0.7 | 1.054 | 0.401-0.418 | 0.120-0.131 | 18 |
| Photon method 2, spacing 0.175 | 4.643 | 1.542-1.565 | 0.465-0.473 | 47 |

The default cloud adds about 44% instructions for light that was previously missing. It performs 6771 cloud gathers plus 22144 sampled gathers at 173 fallback shading points; spacing 0.175 performs 107762 cloud gathers plus 9344 sampled gathers at 73 fallback points. Increasing photon count caps, resolving finer cloud spacing and repeated boundary intersections can all raise cost substantially. Sampled gather buffers are retained per trace recursion level. A broader adaptive-gather scratch refactor was deferred after measurements showed an ordinary-surface regression.

### Initial dispersive prism validation

These measurements precede projected photon sampling and the mesh boundary fixes. Full 640 by 400 images cover an ordinary slab, neutral and skinned subsurface slabs, both methods and a thin slab. With the same saved map, method 2 at spacing 0.35 visibly blends the spectrum similarly to method 1 at 1024 samples. Their measured trace CPU times are 8.791 and 91.777 seconds respectively. These visual renders ran alongside another queued render; they are not isolated hardware benchmarks or proof of equal error.

Photon-only measurements use linear 16-bit PPM, one tracing thread, no antialiasing, aperture 0.025, maximum gather count 20000 and rectangle `(332,239)-(513,362)` in the same 640 by 400 camera. The fixed map contains 143913 deposits from the 200000 shooting target with 21 spectral bands. A 4096-sample method 1 reference has mean RGB 0.01724979; the 2048-sample reference differs by RMS 0.00033617 and mean energy -0.00542%.

| Method 2 spacing, 256 fallback samples | RMS against 4096 reference | Mean energy error | Cloud gathers | Sampled gathers | Fallback points |
|---|---:|---:|---:|---:|---:|
| 0.35 | 0.00077476 | -0.96912% | 525191 | 35840 | 140 |
| 0.25, partial fallback | 0.00200904 | -0.03659% | 877740 | 4070400 | 15900 |
| 0.175, entirely sampled | 0.00205123 | -0.03849% | 0 | 5368832 | 20972 |

The last two rows expose cloud-limit failures, rather than continued cloud-spacing convergence. For example, one lower cell at spacing 0.175 needs approximately 268203 points, exceeding the per-cell limit. Raising that limit alone is insufficient: the fine slab would need about 2.19 million points across the view. Near-correct integrated energy can conceal spatial noise. At spacing 0.35, the red/green/blue image-column centroids differ from the reference by -0.030, -0.037 and +0.002 pixels. This supports spectral-position agreement in this fixture, not arbitrary band-width or thin-surface accuracy.

Increasing 21 to 41 spectral bands with the same shooting target yields 280973 deposits; at spacing 0.35 it changes mean energy by +0.16936% and RMS by 0.00025340. Increasing that 41-band shooting target to 800000 yields 1125040 deposits and changes mean energy by another +0.16168%, RMS 0.00018348. These are separate spectral-sampling and density checks, using regenerated maps and the same aperture. Zero skin thickness preserves the neutral photon-only image pixel for pixel.

### Projected entry sampling validation

`tools/bench/reference-subsurface-plane.py` independently integrates the dipole profile over a finite rectangle with uniform incident irradiance. It uses angular quadrature and the radial antiderivative, rather than either renderer sampling algorithm. The native uniform fixture map supplies its quantized flux and incidence. This reference treats irradiance as continuous and omits the finite photon lattice and gather-kernel edge reconstruction; density and aperture convergence remain separate checks.

At 64 by 48, 256 projected samples, a wide receiver and the default uniform map, the photon image mean is 0.75616352 against quadrature mean 0.75618799: -0.00324%, RMS 0.00182539. Changing receiver thickness from 4 to 0.02 preserves these photon-only pixels exactly. This checks the planar multiple-scattering model; it does not validate directional single scattering in thin media.

Positive mesh tests exposed a second issue: the ordinary mesh inside-test cutoff omitted nearby crossings during boundary probes. Allowing those crossings only inside the photon probe and merging coplanar triangles restores the smooth-mesh sampled mean from 0.12172 to 0.75372680. Method 2's legacy smooth-normal photon cloud disagreed by 18.7%; its photon contribution now uses projected sampling and matches method 1 pixel for pixel. The analytic cloud is retained.

Lit absent-photon, collect-off, empty-map and smooth-mesh controls remain pixel-identical across both methods against the preceding renderer. A nonclipped ordinary smooth-mesh photon control is also pixel-identical. Loaded-map results are unchanged by five zero-power lights, one/four threads, equivalent millimetre scaling or zero skin thickness.
