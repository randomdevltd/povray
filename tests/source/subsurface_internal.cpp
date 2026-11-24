#include "core/lighting/subsurfaceinternal.h"
#include "core/lighting/subsurfaceinternalsampling.h"
#include "core/lighting/subsurfaceexit.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using namespace pov;

static void Near(double actual, double expected, double tolerance = 1e-12)
{
    assert(std::abs(actual - expected) < tolerance);
}

int main()
{
    for (unsigned count : {1u, 2u, 3u, 7u, 10u, 32u, 37u, 128u})
        for (unsigned dimension = 0; dimension < 32; ++dimension)
        {
            std::vector<bool> visited(count, false);
            for (unsigned sample = 0; sample < count; ++sample)
            {
                const double value = SubsurfaceInternalDraw(718, sample, count, dimension);
                assert(value >= 0.0 && value < 1.0);
                const unsigned stratum = unsigned(value * count);
                assert(!visited[stratum]);
                visited[stratum] = true;
                Near(value, SubsurfaceInternalDraw(718, sample, count, dimension));
            }
        }
    for (unsigned dimension = 0; dimension < 8; ++dimension)
    {
        double mean = 0.0;
        for (unsigned seed = 0; seed < 8192; ++seed)
            mean += SubsurfaceInternalDraw(seed, 7, 32, dimension) / 8192;
        Near(mean, 0.5, 0.012);
    }
    double sums[2] = {}, squares[2] = {};
    const unsigned seeds = 8192, count = 32;
    // A sparse patterned source on a Beer-weighted slab chord, behind an attenuating layer.
    auto slab = [](double flight, double source)
    {
        return (flight < 0.45 && (source < 0.08 || (source > 0.45 && source < 0.53))) ? 1.0 : 0.0;
    };
    for (unsigned seed = 0; seed < seeds; ++seed)
        for (unsigned method = 0; method < 2; ++method)
        {
            double estimate = 0.0;
            for (unsigned sample = 0; sample < count; ++sample)
            {
                const auto pathKey = DeriveKey(seed, kDrawSubsurface, sample);
                auto draw = [&](unsigned d)
                { return method ? SubsurfaceInternalDraw(seed, sample, count, d) : Draw(pathKey, kDrawSubsurface, d); };
                estimate += slab(draw(0), draw(1)) / count;
            }
            sums[method] += estimate;
            squares[method] += estimate * estimate;
        }
    double variance[2];
    for (unsigned method = 0; method < 2; ++method)
    {
        const double mean = sums[method] / seeds;
        variance[method] = squares[method] / seeds - mean * mean;
        Near(mean, 0.45 * 0.16, 4.0 * std::sqrt(variance[method] / seeds));
        std::cout << "patterned slab " << method << " mean=" << mean << " variance=" << variance[method] << '\n';
    }
    assert(variance[1] < variance[0] * 0.8);
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

    double gray[3] = {};
    for (unsigned channel = 0; channel < 3; ++channel)
        for (unsigned sample = 0; sample < 32; ++sample)
        {
            std::uint64_t dimension = 0;
            gray[channel] += SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
                1.0, 1.0, 1.0, boundary, source,
                [](const Vector3d& p, double) { return p[Z] > 0.2 ? 1.0 : 0.0; },
                [&] { return SubsurfaceInternalDraw(718, sample, 32, dimension++); });
        }
    assert(gray[0] == gray[1]);
    assert(gray[1] == gray[2]);

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

    // Collision NEE owns the next straight segment; a reflected continuation owns its new segment. Roulette follows each
    // event at the weight's fall since the start (here the albedo 0.5, survived by the draw 0.0), and restores the weight.
    const std::vector<double> sequence{0.1, 0.5, 0.5, 0.0, 0.99, 0.0, 0.99, 0.99};
    size_t index = 0, segments = 0, connections = 0;
    const double reflected = SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
        1.0, 1.0, 1.5, boundary,
        [&](const Vector3d&, const Vector3d&, double, double) { ++segments; return 1.0; },
        [&](const Vector3d&, double) { ++connections; return 2.0; },
        [&] { assert(index < sequence.size()); return sequence[index++]; });
    Near(reflected, 3.0);
    assert(segments == 2 && connections == 1 && index == sequence.size());

    // An internal reflection keeps what the skin it crosses there passes, as the walk's chance to go on: the draw 0.0
    // survives the skin's 0.25 and gathers the reflected segment, the draw 0.3 ends the walk there.
    for (double survive : {0.0, 0.3})
    {
        const std::vector<double> skinSequence{0.1, 0.5, 0.5, 0.0, 0.99, 0.0, survive, 0.99, 0.99};
        index = 0;
        const double skinned = SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1),
            1.0, 1.0, 1.5, boundary,
            [&](const Vector3d&, const Vector3d&, double, double) { return 1.0; },
            [&](const Vector3d&, double) { return 2.0; },
            [&] { assert(index < skinSequence.size()); return skinSequence[index++]; },
            [](const Vector3d&) { return 0.25; });
        Near(skinned, (survive < 0.25) ? 3.0 : 2.0);
        assert(index == ((survive < 0.25) ? skinSequence.size() : 7));
    }

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
    // In a sphere of radius 1 with internal reflection, the uncollided and collided parts sum to the whole walk in
    // expectation; the collided part's first collision is drawn within each segment, so in an optically thin sphere every
    // path collides and none returns zero.
    auto sphere = [](const Vector3d& p, const Vector3d& d)
    {
        const double b = dot(p, d), c = p.lengthSqr() - 1.0;
        const double t = -b + std::sqrt(std::max(0.0, b * b - c));
        const Vector3d at = p + d * t;
        return SubsurfaceInternalBoundary{t, at, at.normalized()};
    };
    auto falling = [](const Vector3d& p, double extinction) { return (1.0 - 0.5 * p.length()) / extinction; };
    for (double depth : {0.6, 6.0})
    {
        const Vector3d entry(0.0, 0.0, 1.0), inward = Vector3d(0.3, 0.0, -1.0).normalized();
        const size_t walks = 200000;
        double mean[3] = {}, square[3] = {};
        size_t zeros = 0;
        const SubsurfaceInternalPart parts[3] = {SubsurfaceInternalPart::kAll, SubsurfaceInternalPart::kUncollided, SubsurfaceInternalPart::kCollided};
        for (int k = 0; k < 3; ++k)
            for (size_t w = 0; w < walks; ++w)
            {
                const double value = SampleSubsurfaceInternalRadiance(entry, inward, 0.1 * depth, 0.9 * depth, 1.3, sphere, source, falling,
                    [&] { return uniform(generator); }, [](const Vector3d&) { return 1.0; }, parts[k]);
                mean[k] += value / walks;
                square[k] += value * value / walks;
                if ((k == 2) && !(value > 0.0)) ++zeros;
            }
        double spread = 0.0;
        for (int k = 0; k < 3; ++k) spread += (square[k] - mean[k] * mean[k]) / walks;
        std::cout << "sphere of optical radius " << depth << ": whole " << mean[0] << ", parts " << mean[1] << " + " << mean[2] << '\n';
        Near(mean[1] + mean[2], mean[0], 4.0 * std::sqrt(spread));
        assert(zeros == 0);
    }
    bool rejected = false;
    try
    {
        SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(0, 0, 1), 0.0, 0.0, 1.0,
            boundary, source, zero, [] { return 0.5; });
    }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    // Zero-distance reflections under lossless total internal reflection. In a unit cube a path aimed at an edge reflects off
    // one face there and meets the other at zero distance: it turns and travels on, as every part. A zero-thickness sheet
    // returns the walk's point each time and its normal facing the walk: the walk turns back out through the face it just
    // reflected from and ends, never hangs, as every part.
    auto cube = [](const Vector3d& p, const Vector3d& d)
    {
        double t = std::numeric_limits<double>::infinity();
        int axis = 0;
        for (int a = 0; a < 3; ++a)
            if (d[a] != 0.0)
            {
                const double s = std::max(0.0, (((d[a] > 0.0) ? 1.0 : 0.0) - p[a]) / d[a]);
                if (s < t) { t = s; axis = a; }
            }
        Vector3d n, at = p + d * t;
        n[axis] = (d[axis] > 0.0) ? 1.0 : -1.0;
        for (int a = 0; a < 3; ++a) // a hit within rounding of a face lies on it, so an edge is met exactly
            if (std::abs(at[a] - std::round(at[a])) < 1e-12) at[a] = std::round(at[a]);
        return SubsurfaceInternalBoundary{t, at, n};
    };
    auto sheet = [](const Vector3d& p, const Vector3d& d)
    { return SubsurfaceInternalBoundary{0.0, p, Vector3d(0, 0, (d[Z] > 0.0) ? 1.0 : -1.0)}; };
    // The same sheet as a boundary search finds it: each face found again a rounding error ahead rather than at zero, within
    // the rounding the search states. The walk still ends rather than ping-pong between the two faces for ever.
    auto refound = [](const Vector3d& p, const Vector3d& d)
    { return SubsurfaceInternalBoundary{1e-16, p + d * 1e-16, Vector3d(0, 0, (d[Z] > 0.0) ? 1.0 : -1.0), 1e-12}; };
    for (SubsurfaceInternalPart part : {SubsurfaceInternalPart::kAll, SubsurfaceInternalPart::kUncollided, SubsurfaceInternalPart::kCollided})
    {
        size_t corners = 0;
        auto counting = [&](const Vector3d& p, const Vector3d& d)
        {
            const auto hit = cube(p, d);
            if (!(hit.distance > 0.0)) ++corners;
            return hit;
        };
        const double value = SampleSubsurfaceInternalRadiance(Vector3d(0.4, 0.2, 0.4), Vector3d(0.6, 0.53, 0.6).normalized(),
            1e-6, 0.0, 1.5, counting, source, zero, [&] { return uniform(generator); }, [](const Vector3d&) { return 1.0; }, part);
        assert(std::isfinite(value) && corners > 0);
        const double flat = SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(1, 0, 0.2).normalized(), 0.0, 1.0, 1.5,
            sheet, source, zero, [&] { return uniform(generator); }, [](const Vector3d&) { return 1.0; }, part);
        assert(flat == 0.0);
        const double near = SampleSubsurfaceInternalRadiance(Vector3d(), Vector3d(1, 0, 0.2).normalized(), 0.0, 1.0, 1.5,
            refound, source, zero, [&] { return uniform(generator); }, [](const Vector3d&) { return 1.0; }, part);
        assert(std::isfinite(near) && near >= 0.0);
    }
    std::cout << "Internal subsurface analytical transport passed\n";
}
