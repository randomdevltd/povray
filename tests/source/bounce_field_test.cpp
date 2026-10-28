#include "core/lighting/bouncefield.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace pov;

static void Near(const MathColour& actual, const MathColour& expected, double tolerance = 1e-6)
{
    for (int c = 0; c < MathColour::channels; ++c)
        assert(std::abs(actual[c] - expected[c]) < tolerance);
}

int main()
{
    const double fourPi = 4.0 * std::acos(-1.0);
    const Vector3d forward(0, 0, 1), perpendicular(1, 0, 0), backward(0, 0, -1);
    MathColour sigma(2.0), incident(3.0), authored(0.25);
    sigma[1] = 0.5;
    incident[2] = 1.5;
    const MathColour mean = sigma * incident / fourPi;
    BounceFieldNode isotropic;
    isotropic.AddIsotropic(authored);
    isotropic.AddScatteringBeam(ISOTROPIC_SCATTERING, 0.0, sigma, forward, incident);
    Near(isotropic.Evaluate(forward), authored + mean);
    Near(isotropic.Evaluate(perpendicular), authored + mean);
    Near(isotropic.AngularAverage(), authored + mean);

    BounceFieldNode rayleigh;
    rayleigh.AddScatteringBeam(RAYLEIGH_SCATTERING, 0.0, sigma, forward, incident);
    Near(rayleigh.Evaluate(forward), mean * 1.5);
    Near(rayleigh.Evaluate(perpendicular), mean * 0.75);
    Near(rayleigh.AngularAverage(), mean);
    const Vector3d diagonal(1.0 / std::sqrt(3.0), -1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0));
    rayleigh.AddScatteringBeam(RAYLEIGH_SCATTERING, 0.0, sigma, diagonal, incident);
    Near(rayleigh.Evaluate(diagonal), mean * 2.5);

    BounceFieldNode hg;
    hg.AddScatteringBeam(HENYEY_GREENSTEIN_SCATTERING, 0.65, sigma, forward, incident);
    assert(hg.Evaluate(forward)[0] > hg.Evaluate(perpendicular)[0]);
    assert(hg.Evaluate(perpendicular)[0] > hg.Evaluate(backward)[0]);
    for (int type : {RAYLEIGH_SCATTERING, HENYEY_GREENSTEIN_SCATTERING, MIE_HAZY_SCATTERING, MIE_MURKY_SCATTERING})
    {
        BounceFieldNode field;
        field.AddScatteringBeam(type, 0.65, sigma, forward, incident);
        MathColour integral;
        const int count = 40000;
        for (int i = 0; i < count; ++i)
        {
            const double mu = -1.0 + 2.0 * (i + 0.5) / count;
            const MathColour value = field.Evaluate(Vector3d(std::sqrt(1.0 - mu * mu), 0, mu));
            for (int c = 0; c < MathColour::channels; ++c) assert(value[c] >= 0.0);
            integral += value / count;
        }
        Near(integral, field.AngularAverage(), 2e-4);
    }
    BounceFieldNode blend;
    blend.AddScaled(isotropic, 0.7);
    blend.AddScaled(hg, 0.3);
    Near(blend.Evaluate(diagonal), isotropic.Evaluate(diagonal) * 0.7 + hg.Evaluate(diagonal) * 0.3);
    Near(blend.AngularAverage(), isotropic.AngularAverage() * 0.7 + hg.AngularAverage() * 0.3);
    BounceFieldNode copy = hg;
    copy.AddScaled(copy, 0.5);
    Near(copy.Evaluate(forward), hg.Evaluate(forward) * 1.5);
    assert(isotropic.StorageBytes() == sizeof(BounceFieldNode));
    assert(rayleigh.StorageBytes() > isotropic.StorageBytes());
    assert(hg.StorageBytes() > isotropic.StorageBytes());
    BounceFieldNode assigned;
    assigned = rayleigh;
    Near(assigned.Evaluate(diagonal), rayleigh.Evaluate(diagonal));
    assigned.AddIsotropic(authored);
    Near(assigned.Evaluate(diagonal), rayleigh.Evaluate(diagonal) + authored);
    BounceFieldNode zeroScaled;
    zeroScaled.AddScaled(hg, 0.0);
    assert(zeroScaled.StorageBytes() == sizeof(BounceFieldNode));
    MathColour blue(0.0), red(0.0);
    blue[2] = 9.0;
    red[0] = 6.0;
    red[1] = 2.0;
    const auto populate = [&](BounceFieldNode& node) {
        node.AddScatteringBeam(HENYEY_GREENSTEIN_SCATTERING, 0.65, MathColour(1.0), forward, incident);
        node.AddScatteringBeam(MIE_HAZY_SCATTERING, 0.0, MathColour(1.0), backward, blue);
        node.AddScatteringBeam(HENYEY_GREENSTEIN_SCATTERING, -0.4, MathColour(1.0), backward, red);
    };
    BounceFieldNode reservoir(8, 1234), repeated(8, 1234);
    populate(reservoir);
    populate(repeated);
    Near(reservoir.Evaluate(forward), repeated.Evaluate(forward), 1e-12);
    const MathColour expectedAverage = (incident + blue + red) / fourPi;
    Near(reservoir.AngularAverage(), expectedAverage);
    MathColour integrated;
    for (int i = 0; i < 40000; ++i)
    {
        const double mu = -1.0 + 2.0 * (i + 0.5) / 40000;
        const MathColour value = reservoir.Evaluate(Vector3d(std::sqrt(1.0 - mu * mu), 0, mu));
        for (int ch = 0; ch < MathColour::channels; ++ch) assert(value[ch] >= 0.0);
        integrated += value / 40000;
    }
    Near(integrated, expectedAverage, 3e-4);
    const size_t boundedBytes = reservoir.StorageBytes();
    for (int i = 0; i < 10000; ++i) populate(reservoir);
    assert(reservoir.StorageBytes() == boundedBytes);
    MathColour ensemble, mergedEnsemble;
    const MathColour exact = (incident * PhysicalPhaseFactor(HENYEY_GREENSTEIN_SCATTERING, 0.65, 1.0) +
        blue * PhysicalPhaseFactor(MIE_HAZY_SCATTERING, 0.0, -1.0) +
        red * PhysicalPhaseFactor(HENYEY_GREENSTEIN_SCATTERING, -0.4, -1.0)) / fourPi;
    for (int seed = 1; seed <= 4096; ++seed)
    {
        BounceFieldNode realization(8, seed), merged(8, seed + 100000);
        populate(realization);
        ensemble += realization.Evaluate(forward) / 4096;
        merged.AddScaled(realization, 0.5);
        merged.AddIsotropic(authored);
        Near(merged.AngularAverage(), expectedAverage * 0.5 + authored);
        mergedEnsemble += merged.Evaluate(forward) / 4096;
    }
    Near(ensemble, exact, 0.035);
    Near(mergedEnsemble, exact * 0.5 + authored, 0.035);
    std::cout << "node bytes isotropic=" << isotropic.StorageBytes() << " rayleigh=" << rayleigh.StorageBytes()
              << " hg=" << hg.StorageBytes() << "\n";
    std::cout << "positive directional bounce source, phase energy and interpolation pass\n";
}
