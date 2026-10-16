#ifndef POVRAY_CORE_PROGRESSIVEPHOTONMAP_H
#define POVRAY_CORE_PROGRESSIVEPHOTONMAP_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include "core/scene/tagfilter.h"
#include "core/lighting/progressivephoton.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <unordered_map>

namespace pov
{

struct ProgressivePhoton final
{
    Vector3d point, direction, normal;
    MathColour flux;
    ConstObjectPtr object = nullptr;
    PreparedSetId set = 0;
};

struct ProgressivePhotonFeedback final
{
    const LightSource* light;
    ObjectPtr target;
    PreparedSetId set;
    std::uint64_t attempted = 0, hit = 0;
};

struct ProgressivePhotonBatch final
{
    unsigned int shard;
    size_t surfaceBegin, surfaceEnd, mediaBegin, mediaEnd;
};

/// Least cosine between receiver and photon normals, and the symmetric tangent-plane offset as a share of the base radius.
constexpr double kPhotonNormalAgreement = 0.5, kPhotonSlab = 0.1;

struct MappedPhoton final
{
    Vector3d point;
    float direction[3], normal[3];
    MathColour flux;
    std::uint32_t receiver : 30, axis : 2;

    Vector3d Direction() const { return Vector3d(direction[0], direction[1], direction[2]); }
    Vector3d Normal() const { return Vector3d(normal[0], normal[1], normal[2]); }
};

/// Same sheet of the receiver: normals agree and the offset from the mean tangent plane, exact for spheres and cylinders, is small.
inline bool PhotonSurfaceMatch(const MappedPhoton& photon, const Vector3d& point, const Vector3d& normal, double slab)
{
    const Vector3d other = photon.Normal();
    return dot(other, normal) >= kPhotonNormalAgreement && std::abs(0.5 * dot(photon.point - point, normal + other)) <= slab;
}

struct PhotonNeighbour final
{
    double distance2;
    std::uint32_t index;
    bool operator<(const PhotonNeighbour& other) const
        { return distance2 < other.distance2 || (distance2 == other.distance2 && index < other.index); }
};

/// One photon map per prepared set holding every batch, gathered by k nearest compatible photons.
class ProgressivePhotonMap final
{
    public:
        static constexpr std::uint32_t noReceiver = (1u << 30) - 1;
        std::vector<MappedPhoton> photons;
        unsigned int neighbours = ProgressivePhotonBudget::baseNeighbours;

        template<class Source>
        void Add(const Source& photon, ConstObjectPtr object, double scale)
        {
            MappedPhoton stored;
            stored.point = photon.point;
            for (unsigned int a = 0; a < 3; ++a)
            {
                stored.direction[a] = float(photon.direction[a]);
                stored.normal[a] = float(photon.normal[a]);
            }
            stored.flux = photon.flux * scale;
            auto found = receiverIds.emplace(object, std::uint32_t(receivers.size()));
            if (found.second)
                receivers.push_back(object);
            stored.receiver = found.first->second;
            stored.axis = 0;
            photons.push_back(stored);
        }

        void Build(unsigned int k)
        {
            neighbours = k;
            photons.shrink_to_fit();
            BuildRange(0, photons.size(), 0);
            baseRadius.assign(receivers.size(), 0.0);
            std::vector<size_t> count(receivers.size(), 0), seen(receivers.size(), 0);
            for (const auto& photon : photons)
                ++count[photon.receiver];
            std::vector<std::vector<double>> radii(receivers.size());
            std::vector<PhotonNeighbour> heap;
            for (const auto& photon : photons)
            {
                const std::uint32_t id = photon.receiver;
                const size_t stride = std::max(size_t(1), count[id] / 128);
                if (seen[id]++ % stride)
                    continue;
                const Vector3d normal = photon.Normal();
                const double support = Nearest(photon.point, std::numeric_limits<double>::infinity(), heap,
                    [&](const MappedPhoton& other) { return other.receiver == id && dot(other.Normal(), normal) >= 0.5; });
                if (support > 0.0 && std::isfinite(support))
                    radii[id].push_back(support);
            }
            std::vector<double> all;
            for (size_t id = 0; id < receivers.size(); ++id)
            {
                auto& r = radii[id];
                if (r.empty())
                    continue;
                std::nth_element(r.begin(), r.begin() + r.size() / 2, r.end());
                baseRadius[id] = std::max(1.0e-8, r[r.size() / 2]);
                all.push_back(baseRadius[id]);
            }
            if (!all.empty())
            {
                std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
                globalRadius = all[all.size() / 2];
            }
        }

        std::uint32_t Receiver(ConstObjectPtr object) const
        {
            const auto found = receiverIds.find(object);
            return found == receiverIds.end() ? noReceiver : found->second;
        }

        ConstObjectPtr ReceiverObject(std::uint32_t id) const { return receivers[id]; }

        /// The typical support on a receiver, or over the whole map when the receiver holds no photons itself.
        double BaseRadius(std::uint32_t id) const
        {
            return id < baseRadius.size() && baseRadius[id] > 0.0 ? baseRadius[id] : globalRadius;
        }

        double Cap(std::uint32_t id) const { return ProgressivePhotonBudget::capScale * BaseRadius(id); }

        /// The k nearest accepted photons within cap; returns the kernel support (the k-th distance, or cap).
        template<class Accept>
        double Nearest(const Vector3d& point, double cap, std::vector<PhotonNeighbour>& heap, Accept&& accept) const
        {
            heap.clear();
            if (photons.empty())
                return cap;
            double bound = cap * cap;
            NearestRange(0, photons.size(), point, bound, heap, accept);
            if (heap.size() < neighbours)
                return cap;
            return std::sqrt(heap.front().distance2);
        }

    private:
        std::vector<ConstObjectPtr> receivers;
        std::unordered_map<ConstObjectPtr, std::uint32_t> receiverIds;
        std::vector<double> baseRadius;
        double globalRadius = 1.0e-8;

        void BuildRange(size_t begin, size_t end, unsigned int depth)
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            unsigned int axis = depth % 3;
            if (end - begin > 8)
            {
                Vector3d low = photons[begin].point, high = low;
                for (size_t i = begin + 1; i < end; ++i)
                    for (unsigned int a = 0; a < 3; ++a)
                    {
                        low[a] = std::min(low[a], photons[i].point[a]);
                        high[a] = std::max(high[a], photons[i].point[a]);
                    }
                const Vector3d span = high - low;
                axis = span[X] >= span[Y] ? X : Y;
                if (span[Z] > span[axis])
                    axis = Z;
            }
            std::nth_element(photons.begin() + begin, photons.begin() + middle, photons.begin() + end,
                [axis](const MappedPhoton& a, const MappedPhoton& b) { return a.point[axis] < b.point[axis]; });
            photons[middle].axis = axis;
            BuildRange(begin, middle, depth + 1);
            BuildRange(middle + 1, end, depth + 1);
        }

        template<class Accept>
        void NearestRange(size_t begin, size_t end, const Vector3d& point, double& bound,
                          std::vector<PhotonNeighbour>& heap, Accept& accept) const
        {
            while (begin != end)
            {
                const size_t middle = begin + (end - begin) / 2;
                const auto& photon = photons[middle];
                const double distance2 = (photon.point - point).lengthSqr();
                const PhotonNeighbour candidate{distance2, std::uint32_t(middle)};
                if (distance2 <= bound && (heap.size() < neighbours || candidate < heap.front()) && accept(photon))
                {
                    if (heap.size() == neighbours)
                    {
                        std::pop_heap(heap.begin(), heap.end());
                        heap.pop_back();
                    }
                    heap.push_back(candidate);
                    std::push_heap(heap.begin(), heap.end());
                    if (heap.size() == neighbours)
                        bound = std::min(bound, heap.front().distance2);
                }
                const double delta = point[photon.axis] - photon.point[photon.axis];
                const bool left = delta < 0.0;
                if (delta * delta <= bound)
                    NearestRange(left ? middle + 1 : begin, left ? end : middle, point, bound, heap, accept);
                if (left)
                    end = middle;
                else
                    begin = middle + 1;
            }
        }
};

}

#endif
