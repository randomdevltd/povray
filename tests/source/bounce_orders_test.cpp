#include "core/lighting/bounceorders.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

using namespace pov;

static void Near(double actual, double expected)
{
    assert(std::abs(actual - expected) < 1e-12);
}

int main()
{
    const std::vector<double> initial{2.0, 3.0};
    const auto add = [](double& total, const double& term) { total += term; };
    const auto exchange = [](std::size_t node, unsigned int, const std::vector<double>& previous)
    {
        return (node == 0 ? 0.25 : 0.5) * previous[1 - node];
    };
    for (unsigned int passes = 0; passes <= 3; ++passes)
    {
        const auto result = SolveBounceOrders(initial, std::vector<unsigned int>(2, passes), 0.0, add, exchange);
        double a = 2.0, b = 3.0, expectedA = a, expectedB = b;
        for (unsigned int order = 1; order <= passes; ++order)
        {
            const double nextA = 0.25 * b, nextB = 0.5 * a;
            expectedA += nextA;
            expectedB += nextB;
            a = nextA;
            b = nextB;
        }
        Near(result[0], expectedA);
        Near(result[1], expectedB);
    }
    const auto three = SolveBounceOrders(initial, {3, 3}, 0.0, add, exchange);
    Near(three[0], 2.0 + 0.75 + 0.25 + 0.09375);
    Near(three[1], 3.0 + 1.0 + 0.375 + 0.125);
    const auto stopped = SolveBounceOrders(initial, {1, 3}, 0.0, add, exchange);
    Near(stopped[0], 2.75);
    Near(stopped[1], 4.375);
    const auto asymmetric = SolveBounceOrders(initial, {0, 3}, 0.0, add, exchange);
    Near(asymmetric[0], 2.0);
    Near(asymmetric[1], 4.0);
    assert(SolveBounceOrders(std::vector<double>(), {}, 0.0, add, exchange).empty());
    bool threw = false;
    try { SolveBounceOrders(initial, {1}, 0.0, add, exchange); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    typedef std::vector<double> Field;
    const auto fields = SolveBounceOrders(std::vector<Field>{{2.0, 4.0}, {3.0, 6.0}},
        {2, 2}, Field(2, 0.0),
        [](Field& total, const Field& term)
        { for (std::size_t i = 0; i < total.size(); ++i) total[i] += term[i]; },
        [](std::size_t node, unsigned int, const std::vector<Field>& previous)
        {
            Field result = previous[1 - node];
            for (double& value : result) value *= (node == 0 ? 0.25 : 0.5);
            return result;
        });
    Near(fields[0][0], 3.0);
    Near(fields[0][1], 6.0);
    Near(fields[1][0], 4.375);
    Near(fields[1][1], 8.75);

    std::set<std::uint64_t> seeds;
    for (unsigned int order = 1; order <= 3; ++order)
        for (unsigned int cell = 0; cell < 4; ++cell)
            for (unsigned int source = 0; source < 2; ++source)
            {
                const auto seed = BounceOrderSeed(42, order, cell, source);
                assert(seed == BounceOrderSeed(42, order, cell, source));
                assert(seed != BounceOrderSeed(43, order, cell, source));
                assert(seeds.insert(seed).second);
            }
    const auto sampled = [&](const std::vector<unsigned int>& passes)
    {
        return SolveBounceOrders(initial, passes, 0.0, add,
            [](std::size_t node, unsigned int order, const std::vector<double>&)
            { return double(BounceOrderSeed(42, order, node, 0) % 1024); });
    };
    const auto one = sampled({1, 1});
    const auto more = sampled({3, 1});
    Near(one[1], more[1]);
    Near(sampled({3, 1})[0], more[0]);
    std::cout << "bounce orders, per-node cutoffs, and deterministic seeds pass\n";
}
