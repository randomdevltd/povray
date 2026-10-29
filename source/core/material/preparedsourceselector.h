#ifndef POVRAY_CORE_PREPAREDSOURCESELECTOR_H
#define POVRAY_CORE_PREPAREDSOURCESELECTOR_H

#include "core/configcore.h"
#include "core/lighting/lightsource.h"
#include "core/material/preparedsourcelayout.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/sphere.h"

namespace pov
{
struct PreparedSourceDomainPolicy
{
    size_t angularCells = 16;
    size_t radialCells = 8;
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
    Vector3d low, size, centre;
    DBL radius = 0;
    bool sphere = false;
};

inline bool SameSourceCentre(const Vector3d& a, const Vector3d& b)
{
    return a[X] == b[X] && a[Y] == b[Y] && a[Z] == b[Z];
}

inline PreparedSourceGeometry SourceGeometry(ConstObjectPtr owner)
{
    if (!owner || Test_Flag(owner, INVERTED_FLAG) || !owner->BBox.finiteInterior ||
        !UsableFiniteBounds(owner->BBox))
        throw std::invalid_argument("Prepared source requires finite geometry");
    PreparedSourceGeometry result;
    Vector3d high;
    Make_min_max_from_BBox(result.low, high, owner->BBox);
    result.size = high - result.low;
    const auto sphere = dynamic_cast<const Sphere*>(owner);
    if (sphere && sphere->Spherical() && owner->Clip.empty() && IsFiniteNumber(sphere->Radius) &&
        sphere->Radius > 0)
    {
        result.sphere = true;
        result.centre = sphere->Center;
        result.radius = sphere->Radius;
        for (int a = X; a <= Z; ++a)
            if (!IsFiniteNumber(result.centre[a]))
                result.sphere = false;
    }
    return result;
}

inline std::shared_ptr<const PreparedSourceDomain>
SelectSourceDomain(const std::vector<PreparedSourceGeometry>& owners,
                   const std::vector<PreparedSourceGeometry>& additionalGeometry = {},
                   const PreparedSourceDomainPolicy& policy = {})
{
    if (owners.empty() || !policy.angularCells || !policy.radialCells || !policy.maxNodes ||
        policy.radialCells >= policy.maxNodes)
        throw std::invalid_argument("Invalid prepared source domain policy");
    for (size_t count : policy.cartesianCells)
        if (!count)
            throw std::invalid_argument("Invalid prepared source Cartesian count");
    bool spherical = owners.front().sphere;
    DBL radius = 0;
    for (const auto& owner : owners)
    {
        spherical = spherical && owner.sphere && SameSourceCentre(owner.centre, owners.front().centre);
        radius = std::max(radius, owner.radius);
    }
    if (spherical)
    {
        std::vector<DBL> radial;
        for (size_t i = 0; i <= policy.radialCells; ++i)
            radial.push_back(DBL(i) / policy.radialCells);
        for (const auto& owner : owners)
            radial.push_back(owner.radius / radius);
        for (const auto& geometry : additionalGeometry)
            if (geometry.sphere && SameSourceCentre(geometry.centre, owners.front().centre) &&
                geometry.radius > 0 && geometry.radius < radius)
                radial.push_back(geometry.radius / radius);
        std::sort(radial.begin(), radial.end());
        radial.erase(std::unique(radial.begin(), radial.end()), radial.end());
        const size_t n = policy.angularCells;
        if (n > policy.maxNodes / 6 || n > policy.maxNodes / (6 * n) ||
            6 * n * n > policy.maxNodes - std::min(size_t(2), policy.maxNodes))
            throw std::invalid_argument("Prepared source domain exceeds node budget");
        const size_t surfaceNodes = 6 * n * n + 2;
        if (radial.size() - 1 > (policy.maxNodes - 1) / surfaceNodes)
            throw std::invalid_argument("Prepared source domain exceeds node budget");
        return std::make_shared<const PreparedSourceDomain>(
            PreparedSourceDomain::Sphere(owners.front().centre, radius, n, std::move(radial)));
    }
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

inline PreparedSourceDomainGroup
PrepareCompatibleSourceDomainGroup(const std::vector<ConstObjectPtr>& owners,
                                   const std::vector<ConstObjectPtr>& additionalGeometry = {},
                                   const PreparedSourceDomainPolicy& policy = {})
{
    std::vector<PreparedSourceGeometry> supports, boundaries;
    for (auto owner : owners)
        supports.push_back(SourceGeometry(owner));
    for (auto object : additionalGeometry)
        if (object && !Test_Flag(object, INVERTED_FLAG) && object->BBox.finiteInterior &&
            UsableFiniteBounds(object->BBox))
            boundaries.push_back(SourceGeometry(object));
    return {SelectSourceDomain(supports, boundaries, policy), owners};
}

inline std::vector<std::vector<size_t>>
PartitionSourceGeometry(const std::vector<PreparedSourceGeometry>& supports)
{
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < supports.size(); ++i)
    {
        bool matched = false;
        if (supports[i].sphere)
            for (auto& group : groups)
                if (supports[group.front()].sphere &&
                    SameSourceCentre(supports[i].centre, supports[group.front()].centre))
                {
                    group.push_back(i);
                    matched = true;
                    break;
                }
        if (!matched)
            groups.push_back({i});
    }
    return groups;
}

inline std::vector<PreparedSourceDomainGroup>
PrepareSourceDomains(const std::vector<ConstObjectPtr>& owners,
                     const std::vector<ConstObjectPtr>& additionalGeometry = {},
                     const PreparedSourceDomainPolicy& policy = {})
{
    std::vector<ConstObjectPtr> unique;
    std::vector<PreparedSourceGeometry> supports;
    for (const auto owner : owners)
        if (std::find(unique.begin(), unique.end(), owner) == unique.end())
        {
            unique.push_back(owner);
            supports.push_back(SourceGeometry(owner));
        }
    std::vector<PreparedSourceDomainGroup> result;
    for (const auto& indices : PartitionSourceGeometry(supports))
    {
        std::vector<ConstObjectPtr> group;
        for (size_t i : indices)
            group.push_back(unique[i]);
        result.push_back(PrepareCompatibleSourceDomainGroup(group, additionalGeometry, policy));
    }
    return result;
}

inline PreparedSourceDomainGroup PrepareSourceDomainGroup(ConstObjectPtr owner, TraceThreadData* td,
                                                          const PreparedSourceDomainPolicy& policy = {})
{
    if (!td)
        throw std::invalid_argument("Prepared source domain requires scene context");
    const auto geometry = SourceGeometry(owner);
    std::vector<ConstObjectPtr> owners{owner}, boundaries;
    const auto scene = td->GetSceneData();
    if (geometry.sphere)
    {
        for (const auto light : scene->mediaLights)
        {
            const auto candidate = light->emitterContainer;
            if (!candidate || candidate == owner || Test_Flag(candidate, INVERTED_FLAG) ||
                !candidate->BBox.finiteInterior || !UsableFiniteBounds(candidate->BBox))
                continue;
            const auto support = SourceGeometry(candidate);
            if (support.sphere && SameSourceCentre(support.centre, geometry.centre) &&
                std::find(owners.begin(), owners.end(), candidate) == owners.end())
                owners.push_back(candidate);
        }
        for (const auto object : scene->objects)
            boundaries.push_back(object);
    }
    return PrepareCompatibleSourceDomainGroup(owners, boundaries, policy);
}

inline std::shared_ptr<const PreparedSourceDomain>
PrepareSourceDomain(ConstObjectPtr owner, TraceThreadData* td, const PreparedSourceDomainPolicy& policy = {})
{
    return PrepareSourceDomainGroup(owner, td, policy).domain;
}
} // namespace pov
#endif
