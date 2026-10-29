#ifndef POVRAY_CORE_BOUNCEORDERS_H
#define POVRAY_CORE_BOUNCEORDERS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace pov
{

inline std::uint64_t BounceOrderMix(std::uint64_t value)
{
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

inline std::uint64_t BounceOrderSeed(std::uint64_t base, std::uint64_t order,
                                     std::uint64_t cell, std::uint64_t source)
{
    std::uint64_t seed = BounceOrderMix(base + UINT64_C(0x9e3779b97f4a7c15));
    seed = BounceOrderMix(seed ^ BounceOrderMix(order + UINT64_C(0x243f6a8885a308d3)));
    seed = BounceOrderMix(seed ^ BounceOrderMix(cell + UINT64_C(0x13198a2e03707344)));
    return BounceOrderMix(seed ^ BounceOrderMix(source + UINT64_C(0xa4093822299f31d0)));
}

// Scatter receives only the immutable previous order; Add accumulates one field into another.
template<class Field, class Add, class Scatter>
std::vector<Field> SolveBounceOrders(const std::vector<Field>& initial,
                                     const std::vector<unsigned int>& passes,
                                     const Field& zero, Add add, Scatter scatter)
{
    if (initial.size() != passes.size())
        throw std::invalid_argument("bounce order field and pass counts differ");
    std::vector<Field> accumulated(initial);
    std::vector<Field> previous(initial);
    const unsigned int maximum = passes.empty() ? 0 : *std::max_element(passes.begin(), passes.end());
    for (unsigned int index = 0; index < maximum; ++index)
    {
        const unsigned int order = index + 1;
        std::vector<Field> next(initial.size(), zero);
        for (std::size_t node = 0; node < initial.size(); ++node)
        {
            if (order <= passes[node])
            {
                next[node] = scatter(node, order, static_cast<const std::vector<Field>&>(previous));
                add(accumulated[node], next[node]);
            }
        }
        previous.swap(next);
    }
    return accumulated;
}

}

#endif
