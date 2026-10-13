#include "core/lighting/progressivephoton.h"

#include <cassert>
#include <iostream>
#include <random>

int main()
{
    using pov::ProgressivePhotonBudget;
    using pov::SppmEstimate;
    assert(ProgressivePhotonBudget(1).passes == 4);
    assert(ProgressivePhotonBudget::batchSize == 65536);
    assert(ProgressivePhotonBudget(0.01).passes == 1);
    constexpr std::uint64_t pixels = 4000 * 2667;
    assert(ProgressivePhotonBudget(1, pixels).passes == 1);
    assert(ProgressivePhotonBudget(1, pixels).batches == 4);
    assert(ProgressivePhotonBudget(4, pixels).passes == 4);
    for (unsigned int requested = 1; requested <= 1024; ++requested)
    {
        const ProgressivePhotonBudget budget(requested / 4.0, pixels);
        assert(budget.batches >= requested && budget.batches - requested < 4);
        assert(budget.EmittedSamples() == std::uint64_t(budget.batches + 1) * 65536);
        assert(budget.CameraSamples(pixels) == pixels * budget.passes);
        for (unsigned int batch = 1; batch <= budget.batches; ++batch)
        {
            const auto group = budget.EmissionBatch(batch);
            assert(group.batches == 4 && group.EndBatch() <= budget.batches);
            assert(group.firstBatch <= batch && batch <= group.EndBatch());
            assert(group.camera == (batch - 1) / 4);
        }
    }
    for (double invalid : {0.0, -1.0, 257.0, double(INFINITY), double(NAN)})
    {
        bool rejected = false;
        try { ProgressivePhotonBudget budget(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    SppmEstimate empty;
    empty.Add(0, 0);
    assert(empty.radius == 1 && empty.count == 0 && empty.Evaluate(100) == 0);
    pov::ProgressivePhotonState late;
    late.Initialize(0.5 * ProgressivePhotonBudget::RadiusScale(100, 2), 100);
    assert(std::abs(late.baseRadius - 0.5) < 1.0e-6);
    pov::ProgressivePhotonState dim, bright;
    dim.baseRadius = dim.radius = bright.baseRadius = bright.radius = 1.0f;
    for (unsigned int pass = 0; pass < 4096; ++pass)
    {
        dim.Update(pass, 1.0, 0.02, 4.0, 1.0);
        bright.Update(pass, 2.0, 0.08, 8.0, 1.0);
        assert(std::isfinite(dim.radius) && dim.radius > 0.0f);
        assert(std::abs(dim.radius - bright.radius) < 1.0e-6);
    }
    assert(dim.radius < 0.2f);
    assert(dim.radius >= 0.5 * ProgressivePhotonBudget::RadiusScale(4096, 2));
    pov::ProgressivePhotonState dark;
    dark.baseRadius = dark.radius = 1.0f;
    dark.Update(0, 0, 0, 0, 0);
    assert(std::isfinite(dark.radius) && dark.radius > 0);
    std::mt19937_64 random(37);
    std::uniform_real_distribution<double> uniform(-2.0, 2.0);
    for (unsigned int dimensions : {2u, 3u})
    {
        SppmEstimate sppm;
        double progressive = 0.0;
        constexpr unsigned int passes = 256, batch = 8192;
        const double expected = dimensions == 2 ? 1.0 / 16.0 : 1.0 / 64.0;
        for (unsigned int pass = 0; pass < passes; ++pass)
        {
            unsigned int hits = 0, progressiveHits = 0;
            const double r = ProgressivePhotonBudget::RadiusScale(pass, dimensions);
            for (unsigned int i = 0; i < batch; ++i)
            {
                double distance2 = 0.0;
                for (unsigned int d = 0; d < dimensions; ++d)
                {
                    const double x = uniform(random);
                    distance2 += x * x;
                }
                hits += distance2 < sppm.radius * sppm.radius;
                progressiveHits += distance2 < r * r;
            }
            sppm.Add(hits, hits, dimensions);
            progressive += progressiveHits / (batch * ProgressivePhotonBudget::KernelVolume(r, dimensions));
        }
        progressive /= passes;
        const double estimate = sppm.Evaluate(std::uint64_t(passes) * batch, dimensions);
        assert(std::abs(estimate / expected - 1) < 0.025);
        assert(std::abs(progressive / expected - 1) < 0.025);
        assert(sppm.radius < 0.6);
        std::cout << dimensions << "D uniform density: reference=" << expected
                  << " sppm=" << estimate << " probabilistic=" << progressive << '\n';
    }
}
