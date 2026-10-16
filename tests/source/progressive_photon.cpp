#include "core/lighting/progressivephoton.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace
{

double StarDiscrepancy(std::vector<std::pair<double, double>> points)
{
    const size_t n = points.size();
    std::sort(points.begin(), points.end());
    std::vector<double> ys;
    for (const auto& p : points)
        ys.push_back(p.second);
    std::sort(ys.begin(), ys.end());
    ys.push_back(1.0);
    std::vector<double> inserted;
    double worst = 0.0;
    for (size_t i = 0; i <= n; ++i)
    {
        const double a = i < n ? points[i].first : 1.0;
        size_t open = 0;
        for (double b : ys)
        {
            while (open < inserted.size() && inserted[open] < b)
                ++open;
            size_t closed = open;
            while (closed < inserted.size() && inserted[closed] <= b)
                ++closed;
            worst = std::max(worst, a * b - double(open) / n);
            if (i < n)
                closed += points[i].second <= b;
            worst = std::max(worst, double(closed) / n - a * b);
        }
        if (i < n)
            inserted.insert(std::upper_bound(inserted.begin(), inserted.end(), points[i].second), points[i].second);
    }
    return worst;
}

}

int main()
{
    using pov::PhotonSobol;
    const PhotonSobol sobol(7), same(7), other(8);
    for (std::uint64_t i = 0; i < 4096; ++i)
        for (unsigned int d = 0; d < PhotonSobol::kDimensions; ++d)
        {
            assert(sobol(i, d) == same(i, d));
            assert(sobol(i, d) > 0.0 && sobol(i, d) < 1.0);
        }
    assert(sobol(5, 0) != other(5, 0));
    for (unsigned int m = 0; m <= 14; ++m)
        for (unsigned int d = 0; d < PhotonSobol::kDimensions; ++d)
            for (std::uint64_t block = 0; block < 3; ++block)
            {
                std::vector<bool> stratum(size_t(1) << m, false);
                for (std::uint64_t i = 0; i < stratum.size(); ++i)
                {
                    const size_t cell = size_t(sobol(block * stratum.size() + i, d) * stratum.size());
                    assert(!stratum[cell]);
                    stratum[cell] = true;
                }
            }
    std::mt19937_64 random(3);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (unsigned int m = 4; m <= 12; m += 2)
    {
        const size_t n = size_t(1) << m;
        double worst = 0.0, randomWorst = 1.0;
        for (unsigned int a = 0; a < PhotonSobol::kDimensions; ++a)
            for (unsigned int b = a + 1; b < PhotonSobol::kDimensions; ++b)
            {
                std::vector<std::pair<double, double>> points;
                for (size_t i = 0; i < n; ++i)
                    points.emplace_back(sobol(i, a), sobol(i, b));
                worst = std::max(worst, StarDiscrepancy(points));
            }
        for (int trial = 0; trial < 4; ++trial)
        {
            std::vector<std::pair<double, double>> points;
            for (size_t i = 0; i < n; ++i)
                points.emplace_back(unit(random), unit(random));
            randomWorst = std::min(randomWorst, StarDiscrepancy(points));
        }
        std::cout << "2^" << m << " points: worst pairwise star discrepancy " << worst << ", best of four random " << randomWorst << "\n";
        assert(worst * n <= 2.0 * (m + 2));
        assert(m < 8 || worst < 0.5 * randomWorst);
    }

    using pov::ProgressivePhotonBudget;
    assert(ProgressivePhotonBudget::batchSize == 65536 && ProgressivePhotonBudget::shards == 16);
    for (unsigned int requested = 1; requested <= 256; ++requested)
    {
        const ProgressivePhotonBudget budget(requested / 4.0);
        assert(budget.batches == requested && budget.EmittedSamples() == std::uint64_t(requested) * 65536);
        assert(budget.neighbours >= ProgressivePhotonBudget::minNeighbours && budget.neighbours <= ProgressivePhotonBudget::maxNeighbours);
        assert(requested == 1 || budget.neighbours >= ProgressivePhotonBudget(double(requested - 1) / 4.0).neighbours);
    }
    assert(ProgressivePhotonBudget(0.01).batches == 1);
    assert(pov::ProgressivePhotonBudget::Neighbours(4) == 48 && pov::ProgressivePhotonBudget::Neighbours(1) >= 8);
    assert(pov::ProgressivePhotonBudget(1).batches == 4 && pov::ProgressivePhotonBudget(64).batches == 256);
    assert(pov::ProgressivePhotonBudget(64).neighbours == pov::ProgressivePhotonBudget::maxNeighbours);
    assert(std::abs(pov::ProgressivePhotonBudget(1).EmissionWeight(2, 0.5) - 2.0 / (65536 * 0.5)) < 1e-18);
    for (double invalid : {0.0, -1.0, 16384.5, double(INFINITY), double(NAN)})
    {
        bool rejected = false;
        try { pov::ProgressivePhotonBudget budget(invalid); (void)budget; }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
    std::cout << "progressive photon budget and emission sequence passed\n";
    return 0;
}
