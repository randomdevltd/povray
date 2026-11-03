#include "core/lighting/subsurfaceinternal.h"
#include "core/lighting/subsurfaceexit.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

using namespace pov;

static void Near(double actual, double expected, double tolerance = 1e-12)
{
    assert(std::abs(actual - expected) < tolerance);
}

int main()
{
    // A directional exit L=3*cosine^2 has flux/pi=1.5, independent of its diffuse export profile.
    for (double eta : {1.0, 1.33, 1.5, 2.0})
    {
        const unsigned samples = 65536;
        double meanRadiance = 0.0, exportedFlux = 0.0;
        const double factor = SubsurfaceExitFluxFactor(eta);
        Near(SubsurfaceExitRatio(1.0, eta), 1.0);
        for (unsigned i = 0; i < samples; ++i)
        {
            const double mu = std::sqrt((i + 0.5) / samples);
            meanRadiance += 3.0 * mu * mu / samples;
            exportedFlux += SubsurfaceExitRatio(mu, eta) / samples;
        }
        Near(meanRadiance, 1.5);
        Near(exportedFlux * meanRadiance / factor, 1.5, 1e-7);
    }
    Near(SubsurfaceInternalFresnel(1.0, 1.5), 0.04);
    Near(SubsurfaceInternalFresnel(0.5, 1.5), 1.0);
    Near(SubsurfaceInternalFresnel(0.2, 1.0), 0.0);
    auto boundary = [](const Vector3d& p, const Vector3d& d)
    { return SubsurfaceInternalBoundary{1.0, p + d, d}; };
    auto source = [](const Vector3d&, const Vector3d&, double length, double extinction)
    { return -std::expm1(-extinction * length) / extinction; };
    auto zero = [](const Vector3d&, double) { return 0.0; };
    Near(SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1), 2.0, 0.0, 1.0,
        boundary, source, zero, [] { return 0.99; }), -std::expm1(-2.0) / 2.0);
    Near(SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1), 2.0, 0.0, 1.0,
        boundary, source, zero, [] { return 0.01; }), -std::expm1(-2.0) / 2.0);

    // The same millimetre-thick source authored in metres or millimetres has the same radiance.
    for (double mmPerUnit : {1.0, 1000.0})
    {
        const double length = 1.0 / mmPerUnit;
        auto scaledBoundary = [=](const Vector3d& p, const Vector3d& d)
        { return SubsurfaceInternalBoundary{length, p + d * length, d}; };
        auto scaledSource = [=](const Vector3d&, const Vector3d&, double reach, double extinction)
        { return mmPerUnit * -std::expm1(-extinction * reach) / extinction; };
        Near(SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
            2.0 * mmPerUnit, 0.0, 1.0, scaledBoundary, scaledSource, zero,
            [] { return 0.99; }), -std::expm1(-2.0) / 2.0);
    }

    // Collision NEE owns the next straight segment; a reflected continuation owns its new segment.
    const std::vector<double> sequence{0.1, 0.5, 0.5, 0.99, 0.0, 0.99, 0.99};
    size_t index = 0, segments = 0, connections = 0;
    const double reflected = SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
        1.0, 1.0, 1.5, boundary,
        [&](const Vector3d&, const Vector3d&, double, double) { ++segments; return 1.0; },
        [&](const Vector3d&, double) { ++connections; return 2.0; },
        [&] { assert(index < sequence.size()); return sequence[index++]; });
    Near(reflected, 2.5);
    assert(segments == 2 && connections == 1 && index == sequence.size());

    // In an infinite homogeneous source, L=j/sigma_a, independent of scattering.
    std::mt19937_64 generator(736411);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    auto distant = [](const Vector3d& p, const Vector3d& d)
    { return SubsurfaceInternalBoundary{1e6, p + d * 1e6, d}; };
    for (double scattering : {0.0, 1.0, 4.0})
    {
        double sum = 0.0;
        for (size_t sample = 0; sample < 100000; ++sample)
            sum += SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
                1.0, scattering, 1.0, distant, source,
                [](const Vector3d&, double extinction) { return 1.0 / extinction; },
                [&] { return uniform(generator); });
        Near(sum / 100000, 1.0, 0.008);
    }
    bool rejected = false;
    try
    {
        SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1), 0.0, 0.0, 1.0,
            boundary, source, zero, [] { return 0.5; });
    }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "Internal subsurface analytical transport passed\n";
}
