# Volume-light calibration fixtures

These targets separate published measurements from geometric approximations.
They do not assert that a product has uniform surface luminance or a Lambertian
angular distribution unless that assumption is stated explicitly.

| Target | Manufacturer data | Additional fixture assumption |
|---|---|---|
| Dell U2724D | Active area 596.74 × 335.66 mm; typical luminance 350 cd/m² | Uniform white image; ideal Lambertian front face, opaque rear housing |
| Philips frosted A60 | 806 lm, 2700 K, overall width 60 mm and length 110 mm; nominal beam 300° | The idealized 60 mm spherical diffuser is a separate analytic reference, not the manufacturer's complete bulb geometry |
| Hue Lightstrip Plus | 2000 × 15 × 5.5 mm; 1700 lm at 4000 K, 1140 lm at 2700 K | An ideal uniformly emitting 2000 × 15 mm face is a reference, not a measured emitting area or angular distribution |

Sources: [Dell manufacturer datasheet](https://www.delltechnologies.com/asset/en-us/products/electronics-and-accessories/technical-support/dell-ultrasharp-27-monitor-u2724d-datasheet.pdf.external),
[Philips A60 specification](https://www.signify.com/global/prof/led-lamps-and-tubes/led-bulbs/philips-classic-frosted-led-lamps/929001243082_EU/product),
[Hue Lightstrip specification](https://www.philips-hue.com/en-au/p/hue-white-and-color-ambiance-lightstrip-plus-2m-base-kit-with-power-supply/8718699703585).

The monitor provides a published luminance. Bulb and strip luminance must be
labelled as derived. For a uniform Lambertian surface, flux is πLA: the monitor
reference emits 220.24 lm; an ideal 60 mm spherical diffuser emitting 806 lm has
L = 22,684.7 cd/m²; an ideal one-sided 0.030 m² strip face has L = 18,037.6 cd/m²
at 1700 lm, or 12,095.8 cd/m² at 1140 lm. These derived numbers are not measured
product luminances. Closed thin volumes require explicit rear occlusion to model
one-sided displays or strips; front-only emission must not be inferred silently.

The bulb's linked IES file has a header identifying a 2016 test, a 671.60 lumen
lamp and 6.60 W, unlike the current 806 lumen, 7 W product page. Resolve that
provenance before using it as absolute photometry. Catalog total flux and product
dimensions alone cannot establish a real bulb's directional luminance.

## Measurement sequence

First use a black calibration enclosure, no participating media and no indirect
lighting. Compare uniform-source volume illumination with independent numerical
integration over the corresponding diffuse surface. The surface reference is
E = ∫ L cos(source) cos(receiver) / r² dA over visible emitting surface points;
it does not use a legacy area-light intensity normalization. Measure unit-white
Lambertian probes on axis, at 45° and 80°, at the side and rear, and at distances
0.05, 0.25, 1 and 2 m. Include a small occluder to measure shadow gradients.
Compare near illumination as well as distant angular intensity; equal total flux
alone is insufficient.

Then use a fixed 4 × 3 × 2.5 m room, with diffuse reflectances 0.7 on walls,
0.8 on the ceiling and 0.2 on the floor. Record source dimensions and positions,
probe normals and camera configuration before comparing source types. Enable
one source at a time. Measure direct lighting first; report indirect lighting as
a separate test of its transport implementation. No brightness, material or
exposure retuning between source variants is permitted.

Keep unclipped linear output for numerical measurements. Use one documented
exposure and display curve for every preview. Repeat the fixture at half scale:
nits illumination should be invariant; fixed-lumen illumination should change
inversely with area. Compare several sample counts with a converged reference,
reporting image error, preparation and tracing instructions, peak memory and
normalization discrepancy. A pleasant room image does not replace these checks.

The projected-area angular model remains exploratory. For a uniform source it
was proposed to match the
distant intensity of its diffuse surface. It does not promise identical near
illumination or equivalence between arbitrary volumetric and surface texture
patterns. Published product angular data, or explicitly labelled geometric
references, are required before claiming a realistic bulb, strip or display match.

## Room scene controls

`distribution/scenes/interior/media/lighting_calibration_room.pov` uses one metre
per scene unit and accepts `Source=1..5`, `Radio=0/1`, `Samples` and `ShowProbes`.
The five arms are the ideal sphere, approximate A60 envelope, monitor, strip at
1700 lm-derived luminance and strip at 1140 lm-derived luminance. All use neutral
white to isolate photometric calibration; the quoted product colour temperatures
are not a claim of spectral or colour matching. The A60 envelope reuses the
sphere-derived luminance and therefore does not claim to emit exactly 806 lm.

Render unclipped linear HDR with AA off; apply the same -6-stop preview exposure
(multiplier 1/64) and the same display conversion to every arm. The scene does
not scale source brightness to make previews fit an LDR output range. Source
appearance is authored independently at its nominal luminance; `no_radiosity`
keeps that appearance from becoming an additional gathered emission source.
The radiosity arm compares indirect surface illumination separately. No source
uses absorption to manufacture its angular distribution. Monitor and strip have
explicit opaque backing and 1 mm source thickness; side emission and the finite
thickness are part of the approximation and must be measured, not hidden.
