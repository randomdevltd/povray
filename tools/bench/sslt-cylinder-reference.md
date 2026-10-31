# Finite-cylinder subsurface reference

`sslt-reference.cpp` has a `Mode=cylinder_point` forward Monte Carlo reference for
all light escaping a homogeneous capped cylinder illuminated by an isotropic
point source. It is independent of the renderer's diffusion approximation and
uses explicit coefficients rather than the renderer's material lookup.

Build and check it from the repository root:

```sh
c++ -O3 -std=c++11 -pthread tools/bench/sslt-reference.cpp -o sslt-reference
python3 tools/bench/sslt-cylinder-check.py ./sslt-reference
```

The standalone check exercises energy closure, all-exit profiles, the analytic
interception probability, reproducibility across thread counts, unresolved
step-cap accounting, and the nearly transparent ballistic limit. It does not
need a renderer executable or render any scenes.

Compare transport lengths with the same source power and geometry:

```sh
./sslt-reference Mode=cylinder_point Photons=1000000 Threads=2 Scale=1 > cylinder-long.json
./sslt-reference Mode=cylinder_point Photons=1000000 Threads=2 Scale=.125 > cylinder-short.json
```

## Inputs and units

All lengths are millimetres. The cylinder axis is y, its bottom is y=0, its top is
y=`Height`, and the source is `(0, Height + Above, 0)`.

| Argument | Default | Meaning |
|---|---:|---|
| `Radius` | 10.5 | Cylinder radius, mm |
| `Height` | 160 | Cylinder height, mm |
| `Above` | 21 | Source distance above top, mm |
| `Flux` | 12 | Emitted flux in each separate scalar trial, lm |
| `Eta` | 1.44 / 1.000277 | Interior/exterior refractive-index ratio |
| `Scale` | 1 | Multiplier of both scattering and absorption lengths |
| `Photons` | 1000000 | Emitted photons per scalar channel |
| `Threads` | 2 | Worker threads |
| `Seed` | 24301 | Random seed |
| `Bins` | 16 | Uniform bins along cylinder height |
| `MaxSteps` | 1000000 | Maximum collision/boundary iterations per photon |

The three reported channels are **separate scalar-channel transfer trials**, each
with the stated source flux. Do not sum their lumen outputs or interpret them as
a calibrated RGB spectrum. A 12-lm scalar trial is not a model of a volume flame's
shape, directionality, or spectral distribution.

Default coefficients at `Scale=1` are:

| Channel | Scattering coefficient σs, mm⁻¹ | Absorption coefficient σa, mm⁻¹ |
|---|---:|---:|
| 0 | 0.125 | 0.000437339 |
| 1 | 0.2 | 0.001065426 |
| 2 | 1/3 | 0.003168666 |

These are prescribed isotropic material inputs, not measured wax data. They
correspond approximately to scattering lengths `(8, 5, 3)` mm and reduced albedos
`(.996513486, .994701099, .990583515)`. Their material inversion uses reflectance
`(.7, .651, .574)` and the quantized boundary-profile index `1.439697265625`.
Actual interface Fresnel transport uses the unquantized `Eta` argument. With
isotropic scattering g=0, scattering and reduced scattering coefficients agree.

`Scale` divides both coefficients, preserving σs/(σs+σa). Thus `Scale=.125`
multiplies both coefficients by eight. The source flux, source position, cylinder
geometry and albedo remain fixed. Scattering lengths are not transmission or
diffusion attenuation lengths.

## Transport and energy accounting

Launch directions are uniform over 4π, so each photon carries `Flux / Photons`.
Intersections with the side and both caps are analytic. The first interface
samples unpolarized Fresnel reflection; transmitted directions follow Snell's
law. Inside the cylinder, free paths are exponential with rate σt=σs+σa. At a
collision absorption occurs with probability σa/σt; otherwise the photon scatters
isotropically. Internal boundaries sample Fresnel reflection, including total
internal reflection. Every transmitted exit is recorded. Photon flux needs no
η² factor at refraction.

After internal reflection the next exponential flight can be sampled afresh:
conditional on surviving distance d, an exponential path's remaining distance
has the original distribution. Scattering order counts scattering collisions,
not interface reflections.

The mutually exclusive terminal ledger is:

- `miss`: the emitted ray misses the cylinder;
- `entry_reflected`: first-interface reflection;
- `absorbed`: absorption inside;
- `ballistic`, `single`, `multiple`: exit after zero, one, or at least two scatters;
- `unresolved`: the iteration cap or a failed interior intersection prevents completion.

The sum of these counts equals `Photons`. Intercepted count is `Photons - miss`;
its count equals the sum of all other ledger categories. Each count converts to
flux by multiplication by `Flux / Photons`. An unresolved photon retains its full
residual weight in that category; it is not silently treated as absorption.
Closure verifies accounting, not transport accuracy by itself.

The convex cylinder seen from its on-axis exterior source presents only its top
disk. Its exact intercepted fraction is

```text
p = Ωdisk / (4π) = (1 - Above / sqrt(Above² + Radius²)) / 2.
```

For the default geometry this is approximately `0.0527864045`. The JSON records
this value, sampled intercepted flux, coefficients, source geometry, seed,
thread count, iteration cap, and terminal counts/fractions/fluxes.

## Exit profiles, uncertainty and comparison limits

`exit_counts` flattens dimensions in this order: surface `(top, side, bottom)`,
scattering order `(0, 1, 2+)`, height bin `(bottom to top)`. Index an entry as
`(surface * 3 + order) * Bins + height_bin`. Cap exits occupy the corresponding
edge height bin. Summing the array recovers the sum of all three exit categories.
Use the `2+` entries when comparing with a renderer's multiple-scattering term.

The reported marginal standard error for a category with count k among N emitted
photons is `Flux * sqrt((k/N) * (1-k/N) / N)`. Counts from different categories
are correlated. A reported count of zero is not proof of zero transport: its
plug-in standard error is zero, but its exact individual one-sided 95% upper
bound is `Flux * (1 - 0.05^(1/N))`, approximately `3 * Flux / N`. This bound does
not include corrections for examining multiple bins or channels.

Random streams depend on seed, channel and photon number. Changing thread count
or scheduling preserves integer counts exactly; increasing photon count retains
the earlier photon streams. The older `Mode=forward` half-space benchmark also
honors an explicit `Threads` argument, but uses its existing thread-based streams.

This mode measures finite-body point-source transport. It does not establish
agreement with the renderer or validate its treatment of a nearby volume source.
A renderer comparison must match geometry, incident illumination, coefficients,
interface indices and scattering-order selection. The older backward image
reference assumes nonfalling point-light intensity; those image results must not
be treated as an absolute-flux point-source comparison.

## Near-normal angular flux

`normal_cones` adds nested angular tallies for external outgoing cosine
`mu_min` = 0.9, 0.95 and 0.99 relative to the local outward normal. The cosine is
computed after refraction, not from the internal direction. Each cone's
`counts`, `flux_lm` and `flux_se_lm` arrays use the same surface/order/height
indexing as `exit_counts`. Angular tallies do not change the energy ledger. Nested cones share photons, so
their estimates are correlated; they are not independent uncertainty checks.
`zero_count_flux_upper95_lm` is the common individual one-sided 95% upper bound
for any zero-count bin in that cone; it is not a simultaneous bound across bins.

For the axisymmetric point-lit cylinder, a side ring's area is
`A = 2*pi*Radius*(Height/Bins)`. Convert mm² to m² when comparing with scene
radiance in metre units. The cosine-weighted mean external radiance in a normal
cone is

```text
L_cone = Phi_cone / [A * pi * (1 - mu_min^2)].
```

This is averaged over the ring, its height interval and all azimuths in the
cone. It tends toward the normal radiance as the cone narrows, but is not an
exact normal-direction measurement. Source position and finite geometry can
make the outgoing field anisotropic in elevation. Compare multiple cone widths
with their Monte Carlo uncertainties before treating their values as normal
radiance. Narrower cones have fewer samples and weaker tail constraints. Sum
counts before computing the uncertainty of a combined scattering-order category.

A single orthographic front view can supply a near-normal side radiance profile
along its central column, after matching height intervals and absolute source
units. It cannot supply the cylinder's total escaping energy. For method 2's
multiple-scattering term, the explicit outgoing factor is `Ft(mu)*exitScale`;
its weighted cone mean relative to the normal is
`2*integral(mu*Ft(mu), mu_min, 1)/[(1-mu_min^2)*Ft(1)]`. At the default index,
these ratios are approximately 0.9997719033, 0.9999464411 and 0.9999979610.
This bounds that renderer angular-factor difference, not the random walk's
physical angular anisotropy or single scattering's angular behavior.
