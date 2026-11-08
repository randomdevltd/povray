#ifndef POVRAY_CORE_PREPAREDSOURCESELECTOR_H
#define POVRAY_CORE_PREPAREDSOURCESELECTOR_H

#include "core/configcore.h"
#include <limits>
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
    /// the automatic lattice's cell budget (the light's domain_cells)
    double automaticCells = 500000;
    size_t maxNodes = 1048576;
};

inline size_t AutomaticSourceAxisLimit(double cells)
{
    if (!IsFiniteNumber(cells) || !(cells > 0))
        throw std::invalid_argument("Invalid source cell budget");
    const double limit = std::ceil(128 * std::max(1.0, std::cbrt(cells / 500000)));
    if (limit >= double(std::numeric_limits<size_t>::max()))
        throw std::invalid_argument("Source cell budget is too large");
    return size_t(limit);
}

inline std::array<size_t, 3> AutomaticSourceCartesianCounts(const Vector3d& size, double cells = 500000)
{
    const size_t axisLimit = AutomaticSourceAxisLimit(cells);
    const DBL largest = std::max(size[X], std::max(size[Y], size[Z]));
    if (!IsFiniteNumber(largest) || !(largest > 0))
        throw std::invalid_argument("Invalid source extent");
    const Vector3d normalized = size / largest;
    const DBL step =
        std::max(DBL(1) / axisLimit, std::cbrt(normalized[X] * normalized[Y] * normalized[Z] / cells));
    std::array<size_t, 3> counts;
    for (int a = X; a <= Z; ++a)
    {
        if (!IsFiniteNumber(size[a]) || !(size[a] > 0))
            throw std::invalid_argument("Invalid source extent");
        counts[a] = size_t(std::max(DBL(1), std::min(DBL(axisLimit), std::ceil(normalized[a] / step))));
    }
    return counts;
}

inline size_t AutomaticSourceNodeBound(double cells)
{
    const size_t nodes = AutomaticSourceAxisLimit(cells) + 1;
    size_t bound = 1;
    for (int a = X; a <= Z; ++a)
    {
        if (nodes > std::numeric_limits<size_t>::max() / bound)
            throw std::invalid_argument("Source node budget is too large");
        bound *= nodes;
    }
    return bound;
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
    const bool automatic = policy.automaticCartesian;
    const auto cartesianCounts = automatic ? AutomaticSourceCartesianCounts(high - low, policy.automaticCells) : policy.cartesianCells;
    // The automatic path never refuses its own budget; the explicit path keeps the stated node ceiling.
    const size_t nodeBudget = automatic ? std::max(policy.maxNodes, AutomaticSourceNodeBound(policy.automaticCells)) : policy.maxNodes;
    size_t count = 1;
    for (size_t cells : cartesianCounts)
    {
        if (cells >= nodeBudget || cells + 1 > nodeBudget / count)
            throw std::invalid_argument("Prepared source domain exceeds node budget");
        count *= cells + 1;
    }
    return std::make_shared<const PreparedSourceDomain>(
        PreparedSourceDomain::Cartesian(low, high - low, cartesianCounts));
}

inline PreparedSourceDomainPolicy SceneSourceDomainPolicy(const SceneData& scene)
{
    PreparedSourceDomainPolicy policy;
    policy.automaticCells = scene.lightDomainCells;
    return policy;
}

/// Each owner's domain with its own policy: the light's domain_cells, else the scene's light default.
inline std::shared_ptr<const PreparedSourceDomain>
PrepareSourceDomain(ConstObjectPtr owner, const PreparedSourceDomainPolicy& policy = {})
{
    return SelectSourceDomain({SourceGeometry(owner)}, policy);
}
} // namespace pov
#endif
