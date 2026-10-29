#include "core/lighting/subsurfaceinternal.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>

using namespace pov;

struct Slab final
{
    double thickness;
    SubsurfaceInternalBoundary operator()(const Vector3d& position, const Vector3d& direction) const
    {
        const double z = direction[2] > 0.0 ? thickness : 0.0;
        const double distance = (z - position[2]) / direction[2];
        Vector3d point = position + direction * distance;
        point[2] = z;
        return {distance, point, Vector3d(0.0, 0.0, direction[2] > 0.0 ? 1.0 : -1.0)};
    }
};

struct Moments final
{
    double sum = 0.0, square = 0.0;
    size_t count = 0;
    void Add(double value) { sum += value; square += value * value; ++count; }
    double Mean() const { return sum / count; }
    double Error() const { return std::sqrt(std::max(0.0, square / count - Mean() * Mean()) / count); }
    void Check(double exact, double errorLimit = 0.005) const
    {
        std::cout << "estimate=" << Mean() << " exact=" << exact << " standard_error=" << Error() << '\n';
        assert(Error() < errorLimit * exact);
        assert(std::abs(Mean() - exact) < 6.0 * Error() + 1e-10 * exact);
    }
};

template<typename Source>
static Moments Estimate(double absorption, double scattering, double thickness, double cosine,
    double eta, Source source, size_t count = 300000, size_t rouletteStart = 32)
{
    std::mt19937_64 random(7613);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto draw = [&]() { return unit(random); };
    const Vector3d direction(std::sqrt(1.0 - cosine * cosine), 0.0, cosine);
    Moments result;
    for (size_t i = 0; i < count; ++i)
        result.Add(SampleSubsurfaceInternalRadiance(Vector3d(0.0), direction,
            absorption, scattering, 1.0, eta, Slab{thickness}, source, draw, nullptr, rouletteStart));
    return result;
}

static void CheckAbsorptionAndDepth()
{
    const auto constant = [](const Vector3d&, const Vector3d&) { return 1.0; };
    for (double thickness : {0.02, 2.0})
    {
        const double sigma = 1.3, cosine = 0.6;
        const double exact = -std::expm1(-sigma * thickness / cosine) / sigma;
        Estimate(sigma, 0.0, thickness, cosine, 1.0, constant).Check(exact, 0.015);
    }
    double previous = 1.0;
    for (double depth : {0.1, 0.7, 1.4})
    {
        const double width = 0.3, sigma = 1.3, cosine = 0.6;
        const auto layer = [=](const Vector3d& p, const Vector3d&)
            { return p[2] >= depth && p[2] < depth + width ? 1.0 : 0.0; };
        const double exact = std::exp(-sigma * depth / cosine) *
            -std::expm1(-sigma * width / cosine) / sigma;
        const auto result = Estimate(sigma, 0.0, 2.0, cosine, 1.0, layer);
        result.Check(exact, 0.015);
        assert(result.Mean() < previous);
        previous = result.Mean();
    }
    const double eta = 1.5, cosine = 0.9, thickness = 0.4, sigma = 1.3;
    const double reflectance = SubsurfaceInternalFresnel(cosine, eta);
    const double transmission = std::exp(-sigma * thickness / cosine);
    const double exact = (1.0 - transmission) / (sigma * (1.0 - reflectance * transmission));
    Estimate(sigma, 0.0, thickness, cosine, eta, constant).Check(exact);
}

static void CheckPowerAndRoulette()
{
    for (const auto setup : {std::pair<double, double>(1.0, 0.4), {1.5, 0.4}, {1.0, 5.0}})
    {
        std::mt19937_64 random(91873);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        auto draw = [&]() { return unit(random); };
        const double eta = setup.first, thickness = setup.second, source = 1.0;
        Moments result;
        SubsurfaceInternalStatistics statistics;
        const auto emission = [=](const Vector3d&, const Vector3d&) { return source; };
        for (size_t i = 0; i < 400000; ++i)
        {
            const double outsideCosine = std::sqrt(draw());
            const double insideCosine = std::sqrt(1.0 - (1.0 - outsideCosine * outsideCosine) / (eta * eta));
            const Vector3d inward(std::sqrt(1.0 - insideCosine * insideCosine), 0.0, insideCosine);
            const double internal = SampleSubsurfaceInternalRadiance(Vector3d(0.0), inward,
                0.0, 2.0, 1.0, eta, Slab{thickness}, emission, draw, &statistics);
            result.Add(internal * (1.0 - SubsurfaceInternalFresnel(insideCosine, eta)) / (eta * eta));
        }
        // Two faces each integrate pi L under a cosine proposal; input power per area is 4 pi j thickness.
        result.Check(2.0 * source * thickness, 0.007);
        assert(statistics.collisions > 0);
        if (eta > 1.0) assert(statistics.reflections > 0);
    }
    const auto constant = [](const Vector3d&, const Vector3d&) { return 1.0; };
    const auto early = Estimate(0.0, 2.0, 0.4, 0.8, 1.0, constant, 300000, 1);
    const auto late = Estimate(0.0, 2.0, 0.4, 0.8, 1.0, constant, 300000, 128);
    assert(std::abs(early.Mean() - late.Mean()) < 6.0 * std::hypot(early.Error(), late.Error()));
}

static void CheckAbsorbingEscape()
{
    const double thickness = 5.0, absorption = 0.02, scattering = 2.0, extinction = absorption + scattering;
    const size_t count = 300000;
    std::mt19937_64 random(36917);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto draw = [&]() { return unit(random); };
    size_t escaped = 0;
    for (size_t i = 0; i < count; ++i)
    {
        double z = thickness * draw(), cosine = 1.0 - 2.0 * draw();
        for (;;)
        {
            const double step = -std::log1p(-draw()) / extinction;
            z += step * cosine;
            if (z < 0.0 || z > thickness) { ++escaped; break; }
            if (draw() < absorption / extinction) break;
            cosine = 1.0 - 2.0 * draw();
        }
    }
    Moments adjoint;
    const auto emission = [](const Vector3d&, const Vector3d&) { return 1.0; };
    for (size_t i = 0; i < count; ++i)
    {
        const double cosine = std::sqrt(draw());
        const auto inward = Vector3d(std::sqrt(1.0 - cosine * cosine), 0.0, cosine);
        adjoint.Add(SampleSubsurfaceInternalRadiance(Vector3d(0.0), inward,
            absorption, scattering, 1.0, 1.0, Slab{thickness}, emission, draw));
    }
    const double probability = double(escaped) / count;
    const double expected = 2.0 * thickness * probability;
    const double forwardError = 2.0 * thickness * std::sqrt(probability * (1.0 - probability) / count);
    std::cout << "absorbing forward_escape=" << expected << " adjoint=" << adjoint.Mean() << '\n';
    assert(std::abs(adjoint.Mean() - expected) < 6.0 * std::hypot(adjoint.Error(), forwardError));
    assert(adjoint.Error() < 0.007 * expected);
}

static double OnePath(double units, double brightness)
{
    std::mt19937_64 random(6671);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto draw = [&]() { return unit(random); };
    const auto source = [=](const Vector3d& point, const Vector3d& outgoing)
        { return brightness * units * (1.0 + 0.2 * point[2] * units) * (1.0 + 0.3 * outgoing[2]); };
    double result = 0.0;
    for (size_t i = 0; i < 20000; ++i)
        result += SampleSubsurfaceInternalRadiance(Vector3d(0.0), Vector3d(0.6, 0.0, 0.8),
            0.2, 2.0, units, 1.5, Slab{0.4 / units}, source, draw);
    return result;
}

int main()
{
    assert(SubsurfaceInternalFiniteBounds(Vector3d(-1.0), Vector3d(1.0)));
    assert(!SubsurfaceInternalFiniteBounds(Vector3d(-BOUND_HUGE), Vector3d(BOUND_HUGE)));
    assert(!SubsurfaceInternalFiniteBounds(Vector3d(0.0), Vector3d(0.0, 1.0, 1.0)));
    assert(std::abs(SubsurfaceInternalFresnel(1.0, 1.5) - 0.04) < 1e-15);
    assert(SubsurfaceInternalFresnel(0.5, 1.5) == 1.0);
    CheckAbsorptionAndDepth();
    CheckPowerAndRoulette();
    CheckAbsorbingEscape();
    const double reference = OnePath(1.0, 1.0);
    assert(std::abs(OnePath(1.0, 7.0) / reference - 7.0) < 1e-12);
    for (double units : {0.001, 1000.0})
        assert(std::abs(OnePath(units, 1.0) / reference - 1.0) < 1e-12);
    bool rejected = false;
    try
    {
        SampleSubsurfaceInternalRadiance(Vector3d(0.0), Vector3d(0.0, 0.0, 1.0),
            0.0, 0.0, 1.0, 1.0, Slab{1.0},
            [](const Vector3d&, const Vector3d&) { return 1.0; }, []() { return 0.5; });
    }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "Internal source normalization, depth, energy, roulette, units and linearity pass\n";
}
