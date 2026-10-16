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
            Partition();
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
                const double support = Nearest(photon.point, normal, id, std::numeric_limits<double>::infinity(), heap,
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
        /// Only partitions of receiver (any, for noReceiver) holding a normal within 60 degrees of normal are visited.
        template<class Accept>
        double Nearest(const Vector3d& point, const Vector3d& normal, std::uint32_t receiver, double cap,
                       std::vector<PhotonNeighbour>& heap, Accept&& accept) const
        {
            heap.clear();
            double bound = cap * cap;
            const bool any = receiver >= receiverPartitions.size();
            const std::uint32_t first = any ? 0 : receiverPartitions[receiver].first;
            const std::uint32_t last = any ? std::uint32_t(partitions.size()) : receiverPartitions[receiver].second;
            const double length = normal.length();
            auto usable = [&](const PhotonPartition& part) {
                const double cosine = length > 0.0 ? std::max(-1.0, std::min(1.0, dot(normal, part.axis) / length)) : 1.0;
                const double closest = cosine >= part.cosSpread ? 1.0 : cosine * part.cosSpread + std::sqrt(1.0 - cosine * cosine) * part.sinSpread;
                return part.length * length * closest >= kPhotonNormalAgreement ? cosine : -2.0;
            };
            std::uint32_t nearest = last;
            double best = -2.0;
            for (std::uint32_t i = first; i < last; ++i)
            {
                const double cosine = usable(partitions[i]);
                if (cosine > best && partitions[i].Distance2(point) <= bound)
                {
                    best = cosine;
                    nearest = i;
                }
            }
            if (nearest == last)
                return cap;
            NearestRange(partitions[nearest].begin, partitions[nearest].end, point, bound, heap, accept);
            for (std::uint32_t i = first; i < last; ++i)
                if (i != nearest && partitions[i].Distance2(point) <= bound && usable(partitions[i]) > -2.0)
                    NearestRange(partitions[i].begin, partitions[i].end, point, bound, heap, accept);
            if (heap.size() < neighbours)
                return cap;
            return std::sqrt(heap.front().distance2);
        }

    private:
        static constexpr unsigned int kNormalClusters = 6;

        /// A kd-tree over the photons of one receiver whose normals share a dominant axis, bounded in space and in normal angle.
        struct PhotonPartition final
        {
            size_t begin, end;
            Vector3d low, high, axis;
            double cosSpread, sinSpread, length;

            double Distance2(const Vector3d& point) const
            {
                double d2 = 0.0;
                for (unsigned int a = 0; a < 3; ++a)
                {
                    const double d = std::max(0.0, std::max(low[a] - point[a], point[a] - high[a]));
                    d2 += d * d;
                }
                return d2;
            }
        };

        std::vector<PhotonPartition> partitions;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> receiverPartitions;
        std::vector<ConstObjectPtr> receivers;
        std::unordered_map<ConstObjectPtr, std::uint32_t> receiverIds;
        std::vector<double> baseRadius;
        double globalRadius = 1.0e-8;

        static unsigned int NormalCluster(const MappedPhoton& photon)
        {
            const float* n = photon.normal;
            unsigned int axis = std::abs(n[X]) >= std::abs(n[Y]) ? X : Y;
            if (std::abs(n[Z]) > std::abs(n[axis]))
                axis = Z;
            return 2 * axis + (n[axis] < 0.0f);
        }

        /// Groups photons by receiver and normal cluster, each group a contiguous kd-tree.
        void Partition()
        {
            const size_t keys = receivers.size() * kNormalClusters;
            auto key = [](const MappedPhoton& photon) { return photon.receiver * kNormalClusters + NormalCluster(photon); };
            std::vector<size_t> start(keys + 1, 0);
            for (const auto& photon : photons)
                ++start[key(photon) + 1];
            for (size_t i = 1; i <= keys; ++i)
                start[i] += start[i - 1];
            std::vector<size_t> next(start.begin(), start.end() - 1);
            for (size_t bucket = 0; bucket < keys; ++bucket)
                while (next[bucket] < start[bucket + 1])
                {
                    const size_t target = key(photons[next[bucket]]);
                    if (target == bucket)
                        ++next[bucket];
                    else
                        std::swap(photons[next[bucket]], photons[next[target]++]);
                }
            partitions.clear();
            receiverPartitions.assign(receivers.size(), {0, 0});
            for (size_t begin = 0, end; begin < photons.size(); begin = end)
            {
                const std::uint32_t id = photons[begin].receiver;
                const unsigned int cluster = NormalCluster(photons[begin]);
                PhotonPartition part;
                part.begin = begin;
                part.low = part.high = photons[begin].point;
                part.axis = Vector3d(0.0);
                part.axis[cluster / 2] = (cluster & 1) ? -1.0 : 1.0;
                double spread = 0.0;
                part.length = 0.0;
                for (end = begin; end < photons.size() && photons[end].receiver == id && NormalCluster(photons[end]) == cluster; ++end)
                {
                    const Vector3d n = photons[end].Normal();
                    const double length = n.length();
                    for (unsigned int a = 0; a < 3; ++a)
                    {
                        part.low[a] = std::min(part.low[a], photons[end].point[a]);
                        part.high[a] = std::max(part.high[a], photons[end].point[a]);
                    }
                    part.length = std::max(part.length, length);
                    if (length > 0.0)
                        spread = std::max(spread, std::acos(std::max(-1.0, std::min(1.0, dot(n, part.axis) / length))));
                }
                part.end = end;
                part.cosSpread = std::cos(spread + 1.0e-6);
                part.sinSpread = std::sin(spread + 1.0e-6);
                part.length *= 1.0 + 1.0e-6;
                if (receiverPartitions[id].first == receiverPartitions[id].second)
                    receiverPartitions[id].first = std::uint32_t(partitions.size());
                receiverPartitions[id].second = std::uint32_t(partitions.size() + 1);
                partitions.push_back(part);
                BuildRange(begin, end, 0);
            }
        }

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
                NearestRange(left ? begin : middle + 1, left ? middle : end, point, bound, heap, accept);
                if (delta * delta > bound)
                    return;
                if (left)
                    begin = middle + 1;
                else
                    end = middle;
            }
        }
};

}

#endif
