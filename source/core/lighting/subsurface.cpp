//******************************************************************************
///
/// @file core/lighting/subsurface.cpp
///
/// Implementations related to subsurface light transport.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/lighting/subsurface.h"

// C++ variants of C standard header files
// C++ standard header files
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>

// POV-Ray header files (base module)
#include "base/mathutil.h"

// POV-Ray header files (core module)
//  (none at the moment)

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

/// Computes the BRDF approximation of the diffuse reflectance term.
inline double DiffuseReflectance(double A, double alphaPrime)
{
    double root = sqrt(3*(1-alphaPrime));
    return (alphaPrime/2) * ( 1 + exp(-(4.0/3.0)*A*root) ) * exp(-root);
}


SubsurfaceInterior::SubsurfaceInterior(double ior) :
    precomputedReducedAlbedo(ior)
{}

SubsurfaceInterior::PrecomputedReducedAlbedo::PrecomputedReducedAlbedo(float ior)
{
    reducedAlbedo[ReducedAlbedoSamples] = 1.0;
    double Fdr = FresnelDiffuseReflectance(ior);
    double A = (1+Fdr)/(1-Fdr);
    double alphaPrime = 1.0;
    double Rd = 1.0;
    int it = 0;
    for (int i = ReducedAlbedoSamples-1; i > 0; i --)
    {
        double Rd0 = 0.0;
        double Rd1 = Rd;
        double diffuseReflectance = double(i)/ReducedAlbedoSamples;
        double alphaPrime0 = 0.0;
        double alphaPrime1 = alphaPrime;
        while(abs(Rd-diffuseReflectance) >= EPSILON)
        {
            double p = (diffuseReflectance-Rd0)/(Rd1-Rd0);
            alphaPrime = alphaPrime0 + p*(alphaPrime1-alphaPrime0);
            Rd = DiffuseReflectance(A, alphaPrime);
            if (Rd < diffuseReflectance)
            {
                alphaPrime0 = alphaPrime;
                Rd0 = Rd;
            }
            else
            {
                alphaPrime1 = alphaPrime;
                Rd1 = Rd;
            }
            it ++;
        }
        reducedAlbedo[i] = alphaPrime;
    }
    reducedAlbedo[0] = 0.0;
}

PreciseColourChannel SubsurfaceInterior::PrecomputedReducedAlbedo::operator()(PreciseColourChannel diffuseReflectance) const
{
    PreciseColourChannel Rd = clip(diffuseReflectance, 0.0, 1.0);
    PreciseColourChannel i = diffuseReflectance * ReducedAlbedoSamples;
    int i0 = floor(i);
    int i1 = ceil(i);
    PreciseColourChannel p = (i-i0);
    return (1-p)*reducedAlbedo[i0] + p*reducedAlbedo[i1];
}

PreciseMathColour SubsurfaceInterior::GetReducedAlbedo(const MathColour& diffuseReflectance) const
{
    PreciseMathColour result;
    for (int i = 0; i < MathColour::channels; i ++)
        result[i] = (precomputedReducedAlbedo.get())(diffuseReflectance[i]);
    return result;
}

// Points in all subsurface clouds together: about 110 bytes each with the hierarchy, and 12 more per light.
static const size_t kSubsurfacePointBudget = size_t(getenv("SSLT_BUDGETPTS") ? atol(getenv("SSLT_BUDGETPTS")) : 1 << 19);

size_t SubsurfaceCellKeyHash::operator()(const SubsurfaceCellKey& k) const
{
    size_t h = std::hash<const void*>()(k.object);
    for (int v : { k.sizeLevel, k.spacingLevel, k.x, k.y, k.z })
        h = h * 1000003u ^ std::hash<int>()(v);
    return h;
}

std::shared_ptr<SubsurfaceCell> SubsurfaceCache::Acquire(const SubsurfaceCellKey& key, bool& build)
{
    std::unique_lock<std::mutex> lock(mutex);
    std::shared_ptr<SubsurfaceCell>& slot = cells[key];
    build = (slot == nullptr);
    if (build)
    {
        slot = std::make_shared<SubsurfaceCell>();
        return slot;
    }
    std::shared_ptr<SubsurfaceCell> cell = slot;
    built.wait(lock, [&cell] { return cell->ready; });
    return cell;
}

void SubsurfaceCache::Publish(const std::shared_ptr<SubsurfaceCell>& cell, bool usable)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        cell->usable = usable;
        cell->ready = true;
    }
    built.notify_all();
}

bool SubsurfaceCache::FindPlan(const void *object, Plan& plan)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = plans.find(object);
    if (it == plans.end())
        return false;
    plan = it->second;
    return true;
}

SubsurfaceCache::Plan SubsurfaceCache::StorePlan(const void *object, const Plan& plan)
{
    std::lock_guard<std::mutex> lock(mutex);
    return plans.emplace(object, plan).first->second;
}

SubsurfaceCache::~SubsurfaceCache()
{
    if (getenv("SSLT_DEBUG"))
        fprintf(stderr, "SSLTDEBUG cells %zu points %zu\n", cells.size(), size_t(reserved));
}

bool SubsurfaceCache::Reserve(size_t points)
{
    size_t now = reserved.fetch_add(points) + points;
    return now <= kSubsurfacePointBudget;
}

// Splits the points at the median of the longest axis until four or fewer remain.
static int BuildSubsurfaceNode(std::vector<SubsurfacePoint>& points, std::vector<SubsurfaceNode>& nodes, int first, int count)
{
    int index = int(nodes.size());
    nodes.emplace_back();
    SubsurfaceNode node;
    node.lo = node.hi = points[first].position;
    node.centre = Vector3d(0.0);
    node.area = 0.0f;
    node.irradiance[0] = node.irradiance[1] = node.irradiance[2] = 0.0f;
    for (int i = first; i < first + count; i++)
    {
        const SubsurfacePoint& p = points[i];
        for (int a = 0; a < 3; a++)
        {
            node.lo[a] = std::min(node.lo[a], p.position[a]);
            node.hi[a] = std::max(node.hi[a], p.position[a]);
            node.irradiance[a] += p.irradiance[a] * p.area;
        }
        node.centre += p.position * p.area;
        node.area += p.area;
    }
    node.centre /= std::max(double(node.area), 1e-30);
    if (count <= 4)
    {
        node.first = first;
        node.count = count;
        nodes[index] = node;
        return index;
    }
    Vector3d extent = node.hi - node.lo;
    int axis = (extent[X] >= extent[Y] && extent[X] >= extent[Z]) ? X : (extent[Y] >= extent[Z] ? Y : Z);
    int half = count / 2;
    std::nth_element(points.begin() + first, points.begin() + first + half, points.begin() + first + count,
                     [axis](const SubsurfacePoint& a, const SubsurfacePoint& b) { return a.position[axis] < b.position[axis]; });
    node.count = 0;
    nodes[index] = node;
    BuildSubsurfaceNode(points, nodes, first, half);
    nodes[index].first = BuildSubsurfaceNode(points, nodes, first + half, count - half);
    return index;
}

void SubsurfaceCell::BuildHierarchy()
{
    nodes.clear();
    if (!points.empty())
    {
        nodes.reserve(points.size());
        BuildSubsurfaceNode(points, nodes, 0, int(points.size()));
    }
}

}
// end of namespace pov
