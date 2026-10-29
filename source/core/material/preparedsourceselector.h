#ifndef POVRAY_CORE_PREPAREDSOURCESELECTOR_H
#define POVRAY_CORE_PREPAREDSOURCESELECTOR_H

#include "core/configcore.h"
#include "core/lighting/lightsource.h"
#include "core/material/preparedsourcelayout.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"

namespace pov
{
struct PreparedSourceDomainPolicy
{
    std::array<size_t, 3> cartesianCells{{80, 80, 80}};
    bool automaticCartesian = true;
    size_t maxNodes = 1048576;
};

inline std::array<size_t, 3> AutomaticSourceCartesianCounts(const Vector3d& size)
{
    const DBL largest = std::max(size[X], std::max(size[Y], size[Z]));
    if (!IsFiniteNumber(largest) || !(largest > 0))
        throw std::invalid_argument("Invalid source extent");
    const Vector3d normalized = size / largest;
    const DBL step =
        std::max(DBL(1.0 / 128), std::cbrt(normalized[X] * normalized[Y] * normalized[Z] / 500000));
    std::array<size_t, 3> counts;
    for (int a = X; a <= Z; ++a)
    {
        if (!IsFiniteNumber(size[a]) || !(size[a] > 0))
            throw std::invalid_argument("Invalid source extent");
        counts[a] = size_t(std::max(DBL(1), std::ceil(normalized[a] / step)));
    }
    return counts;
}

struct PreparedSourceGeometry
{
    Vector3d low, size;
};

inline PreparedSourceGeometry SourceGeometry(ConstObjectPtr owner)
{
    if (!owner || Test_Flag(owner, INVERTED_FLAG) || !owner->BBox.finiteInterior ||
        !UsableFiniteBounds(owner->BBox))
        throw std::invalid_argument("Prepared source requires finite geometry");
    PreparedSourceGeometry result;
    Vector3d high;
    Make_min_max_from_BBox(result.low, high, owner->BBox);
    result.size = high - result.low;
    return result;
}

/// One lattice over the owners' joint bounding box, whatever their shapes.
inline std::shared_ptr<const PreparedSourceDomain>
SelectSourceDomain(const std::vector<PreparedSourceGeometry>& owners, const PreparedSourceDomainPolicy& policy = {})
{
    if (owners.empty() || !policy.maxNodes)
        throw std::invalid_argument("Invalid prepared source domain policy");
    for (size_t count : policy.cartesianCells)
        if (!count)
            throw std::invalid_argument("Invalid prepared source Cartesian count");
    Vector3d low = owners.front().low, high = low + owners.front().size;
    for (const auto& owner : owners)
        for (int a = X; a <= Z; ++a)
        {
            low[a] = std::min(low[a], owner.low[a]);
            high[a] = std::max(high[a], owner.low[a] + owner.size[a]);
        }
    const auto cartesianCounts =
        policy.automaticCartesian ? AutomaticSourceCartesianCounts(high - low) : policy.cartesianCells;
    size_t count = 1;
    for (size_t cells : cartesianCounts)
    {
        if (cells >= policy.maxNodes || cells + 1 > policy.maxNodes / count)
            throw std::invalid_argument("Prepared source domain exceeds node budget");
        count *= cells + 1;
    }
    return std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(low, high - low, cartesianCounts));
}

struct PreparedSourceDomainGroup
{
    std::shared_ptr<const PreparedSourceDomain> domain;
    std::vector<ConstObjectPtr> owners;
};

/// Each owner's own domain, in first-seen order.
inline std::vector<PreparedSourceDomainGroup>
PrepareSourceDomains(const std::vector<ConstObjectPtr>& owners, const PreparedSourceDomainPolicy& policy = {})
{
    std::vector<PreparedSourceDomainGroup> result;
    for (const auto owner : owners)
        if (std::none_of(result.begin(), result.end(),
                         [owner](const PreparedSourceDomainGroup& group) { return group.owners.front() == owner; }))
            result.push_back({SelectSourceDomain({SourceGeometry(owner)}, policy), {owner}});
    return result;
}

inline std::shared_ptr<const PreparedSourceDomain>
PrepareSourceDomain(ConstObjectPtr owner, const PreparedSourceDomainPolicy& policy = {})
{
    return SelectSourceDomain({SourceGeometry(owner)}, policy);
}
} // namespace pov
#endif
