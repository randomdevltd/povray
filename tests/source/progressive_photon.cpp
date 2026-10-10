#include "core/lighting/progressivephoton.h"

#include <cassert>
#include <iostream>
#include <random>

int main()
{
    using pov::ProgressivePhotonBudget;
    using pov::SppmEstimate;
    assert(ProgressivePhotonBudget(1).passes == 16);
    assert(ProgressivePhotonBudget(0.01).passes == 1);
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
