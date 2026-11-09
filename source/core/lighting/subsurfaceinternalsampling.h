#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALSAMPLING_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALSAMPLING_H

#include "core/math/randomsequence.h"

namespace pov
{

inline double SubsurfaceInternalDraw(std::uint64_t key, unsigned sample, unsigned count, std::uint64_t dimension)
{
    const auto dimensionKey = DeriveKey(key, kDrawSubsurface, dimension);
    const auto pathKey = DeriveKey(key, kDrawSubsurface, sample);
    const double jitter = Draw(pathKey, kDrawSubsurface, dimension);
    if (count <= 1) return jitter;
    unsigned mask = 1;
    while (mask < count - 1) mask = (mask << 1) | 1;
    unsigned stratum = sample;
    // Cycle-walk a bijection of the enclosing power-of-two domain into the sample range.
    do
    {
        for (unsigned round = 0; round < 3; ++round)
        {
            const auto bits = MixBits(dimensionKey + round);
            stratum ^= stratum >> 1;
            stratum = (stratum * (unsigned(bits) | 1u) + unsigned(bits >> 32)) & mask;
        }
    } while (stratum >= count);
    // Independent rotations give every sample a uniform stratum marginal in every dimension.
    const unsigned rotation = unsigned(Draw(dimensionKey, kDrawSubsurface, 0) * count);
    stratum = (stratum + rotation) % count;
    return (stratum + jitter) / count;
}

}
#endif
