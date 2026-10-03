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
#include <cmath>

// C++ standard header files
#include <algorithm>
#include <functional>
#include <limits>

// POV-Ray header files (base module)
#include "base/mathutil.h"

// POV-Ray header files (core module)
#include "core/lighting/photons.h"

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
    PreciseColourChannel i = Rd * ReducedAlbedoSamples;
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

// Points in all subsurface clouds together: about 120 bytes each with the hierarchy, and 12 more per light.
static const size_t kSubsurfacePointBudget = size_t(1) << 20;

size_t SubsurfaceCellKeyHash::operator()(const SubsurfaceCellKey& k) const
{
    size_t h = std::hash<const void*>()(k.object) ^ (std::hash<const void*>()(k.medium) * 31u);
    for (int v : { k.sizeLevel, k.x, k.y, k.z, int(k.radiosity), int(k.photons) })
        h = h * 1000003u ^ std::hash<int>()(v);
    return h;
}

// Slots of 16 bytes: at most 64 MiB, or about 32 bytes per photon below that.
static const std::uint64_t kBoundarySlotsMin = std::uint64_t(1) << 12, kBoundarySlotsMax = std::uint64_t(1) << 22;
static const int kBoundaryProbes = 32;

static std::uint64_t MixBoundaryKey(std::uint64_t key)
{
    key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9u;
    key = (key ^ (key >> 27)) * 0x94d049bb133111ebu;
    return key ^ (key >> 31);
}

void SubsurfacePhotonBoundaries::Allocate(const PhotonMap& map)
{
    std::uint64_t count = kBoundarySlotsMin;
    while ((count < kBoundarySlotsMax) && (count < 2 * std::uint64_t(std::max(map.numPhotons, 0))))
        count *= 2;
    slots.reset(new Slot[count]);
    mask = count - 1;
    blockSize = PhotonMap::GetBlockSize();
    for (std::uint32_t id = 0; id < map.mBlockList.size(); id++)
        blocks.emplace_back(reinterpret_cast<std::uintptr_t>(map.GetBlockStart(id)), id);
    std::sort(blocks.begin(), blocks.end());
}

bool SubsurfacePhotonBoundaries::Key(const PhotonMap& map, const Photon* photon, const void* receiver, std::uint64_t& key)
{
    std::call_once(once, [&]() { Allocate(map); });
    std::uintptr_t address = reinterpret_cast<std::uintptr_t>(photon);
    auto block = std::upper_bound(blocks.begin(), blocks.end(), std::make_pair(address, std::numeric_limits<std::uint32_t>::max()));
    if (block == blocks.begin())
        return false;
    --block;
    std::uintptr_t offset = address - block->first;
    if ((offset % sizeof(Photon) != 0) || (offset / sizeof(Photon) >= blockSize))
        return false;
    std::uint64_t index = std::uint64_t(block->second) * blockSize + offset / sizeof(Photon);
    for (int id = 0; id < kReceivers; id++)
    {
        const void* expected = receivers[id].load(std::memory_order_acquire);
        if ((expected == nullptr) && receivers[id].compare_exchange_strong(expected, receiver, std::memory_order_acq_rel))
            expected = receiver;
        if (expected == receiver)
        {
            key = (std::uint64_t(id + 1) << 32) | index;
            return true;
        }
    }
    return false;
}

bool SubsurfacePhotonBoundaries::Find(std::uint64_t key, float folded[2]) const
{
    for (std::uint64_t i = MixBoundaryKey(key), probe = 0; probe < kBoundaryProbes; i++, probe++)
    {
        const Slot& slot = slots[i & mask];
        std::uint64_t word = slot.word.load(std::memory_order_acquire);
        if ((word >> 1) == key)
        {
            if ((word & 1) == 0)
                return false;
            std::copy(slot.folded, slot.folded + 2, folded);
            return true;
        }
        if (word == 0)
            return false;
    }
    return false;
}

void SubsurfacePhotonBoundaries::Store(std::uint64_t key, const float folded[2])
{
    for (std::uint64_t i = MixBoundaryKey(key), probe = 0; probe < kBoundaryProbes; i++, probe++)
    {
        Slot& slot = slots[i & mask];
        std::uint64_t word = slot.word.load(std::memory_order_relaxed);
        if ((word == 0) && slot.word.compare_exchange_strong(word, key << 1, std::memory_order_relaxed))
        {
            std::copy(folded, folded + 2, slot.folded);
            slot.word.store((key << 1) | 1, std::memory_order_release);
            return;
        }
        if ((word >> 1) == key)
            return;
    }
}

void SubsurfacePhotonBoundaries::Fold(const Vector3d& normal, bool valid, float folded[2])
{
    double l1 = valid ? fabs(normal[X]) + fabs(normal[Y]) + fabs(normal[Z]) : 0.0;
    if (!(l1 > 0.0) || !std::isfinite(l1))
    {
        folded[0] = folded[1] = 2.0f;
        return;
    }
    double u = normal[X] / l1, v = normal[Y] / l1;
    if (normal[Z] < 0.0)
    {
        double w = u;
        u = std::copysign(1.0 - fabs(v), w);
        v = std::copysign(1.0 - fabs(w), v);
    }
    folded[0] = float(u);
    folded[1] = float(v);
}

bool SubsurfacePhotonBoundaries::Unfold(const float folded[2], Vector3d& normal)
{
    double u = folded[0], v = folded[1], z = 1.0 - fabs(u) - fabs(v);
    if (fabs(u) > 1.0)
        return false;
    if (z < 0.0)
        normal = Vector3d(std::copysign(1.0 - fabs(v), u), std::copysign(1.0 - fabs(u), v), z);
    else
        normal = Vector3d(u, v, z);
    normal.normalize();
    return true;
}

std::shared_ptr<SubsurfaceCell> SubsurfaceCache::Acquire(const SubsurfaceCellKey& key, bool& build)
{
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<SubsurfaceCell>& slot = cells[key];
    build = (slot == nullptr);
    if (build)
        slot = std::make_shared<SubsurfaceCell>();
    return slot;
}

bool SubsurfaceCache::Reserve(size_t points)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (reserved + points > kSubsurfacePointBudget)
        return false;
    reserved += points;
    return true;
}


void SubsurfaceCache::Release(size_t points)
{
    std::lock_guard<std::mutex> lock(mutex);
    reserved -= std::min(points, reserved);
}

void SubsurfaceCache::SetCamera(const Vector3d& location, double pixelSize, double pixelAngle)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (cameraSet)
        return;
    cameraLocation = location;
    cameraPixelSize = pixelSize;
    cameraPixelAngle = pixelAngle;
    cameraSet = true;
}

bool SubsurfaceCache::GetCamera(Vector3d& location, double& pixelSize, double& pixelAngle) const
{
    std::lock_guard<std::mutex> lock(mutex);
    location = cameraLocation;
    pixelSize = cameraPixelSize;
    pixelAngle = cameraPixelAngle;
    return cameraSet;
}

// Splits the points until four or fewer remain.
static int BuildSubsurfaceNode(std::vector<SubsurfacePoint>& points, std::vector<SubsurfaceNode>& nodes, int first, int count)
{
    int index = int(nodes.size());
    nodes.emplace_back();
    SubsurfaceNode node;
    node.lo = node.hi = points[first].position;
    node.centre = Vector3d(0.0);
    node.area = 0.0f;
    Vector3d normal(0.0);
    for (int j = 0; j < MathColour::channels; j++)
        node.irradiance[j] = 0.0f;
    for (int i = first; i < first + count; i++)
    {
        const SubsurfacePoint& p = points[i];
        for (int a = 0; a < 3; a++)
        {
            node.lo[a] = std::min(node.lo[a], p.position[a]);
            node.hi[a] = std::max(node.hi[a], p.position[a]);
            normal[a] += p.normal[a] * p.area;
        }
        for (int j = 0; j < MathColour::channels; j++)
            node.irradiance[j] += p.irradiance[j] * p.area;
        node.centre += p.position * p.area;
        node.area += p.area;
    }
    node.centre /= std::max(double(node.area), 1e-30);
    normal = (normal.length() > 0.0) ? normal.normalized() : Vector3d(0.0);
    node.cone = 1.0f;
    for (int i = first; i < first + count; i++)
        node.cone = std::min(node.cone, float(points[i].normal[0] * normal[X] + points[i].normal[1] * normal[Y] + points[i].normal[2] * normal[Z]));
    for (int a = 0; a < 3; a++)
        node.normal[a] = float(normal[a]);
    if (count <= 4)
    {
        node.first = first;
        node.count = count;
        nodes[index] = node;
        return index;
    }
    // Points facing opposite ways (the two faces of a thin part) are split by facing first, so each face's groups can
    // be summed as one; otherwise at the median of the longest axis.
    int half = 0;
    if (node.cone < 0.0f)
    {
        const float d[3] = { points[first].normal[0], points[first].normal[1], points[first].normal[2] };
        half = int(std::partition(points.begin() + first, points.begin() + first + count, [&d](const SubsurfacePoint& p)
                                  { return p.normal[0] * d[0] + p.normal[1] * d[1] + p.normal[2] * d[2] >= 0.0f; }) - points.begin()) - first;
    }
    if ((half <= 0) || (half >= count))
    {
        Vector3d extent = node.hi - node.lo;
        int axis = (extent[X] >= extent[Y] && extent[X] >= extent[Z]) ? X : (extent[Y] >= extent[Z] ? Y : Z);
        half = count / 2;
        std::nth_element(points.begin() + first, points.begin() + first + half, points.begin() + first + count,
                         [axis](const SubsurfacePoint& a, const SubsurfacePoint& b) { return a.position[axis] < b.position[axis]; });
    }
    node.count = 0;
    nodes[index] = node;
    BuildSubsurfaceNode(points, nodes, first, half);
    nodes[index].first = BuildSubsurfaceNode(points, nodes, first + half, count - half);
    return index;
}

double SubsurfaceCell::Spacing(const Vector3d& normal) const
{
    double facing = 0.0, density = 0.0;
    for (int a = 0; a < 3; a++)
    {
        facing += fabs(normal[a]);
        density += fabs(normal[a]) / (step[a] * step[a]);
    }
    return (density > 0.0) ? sqrt(facing / density) : std::max(step[0], std::max(step[1], step[2]));
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
