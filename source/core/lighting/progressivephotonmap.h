#ifndef POVRAY_CORE_PROGRESSIVEPHOTONMAP_H
#define POVRAY_CORE_PROGRESSIVEPHOTONMAP_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include "core/scene/tagfilter.h"
#include "core/lighting/progressivephoton.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace pov
{

struct ProgressivePhoton final
{
    Vector3d point, direction, normal;
    MathColour flux;
    ConstObjectPtr object = nullptr;
    PreparedSetId set = 0;
    unsigned int bands = 1;   ///< dispersion bands its photon sample was split into, which land as one cluster
};

/// One light's target in a prepared set: the samples aimed at it and the deposits they made, summed over batches.
struct ProgressivePhotonFeedback final
{
    const LightSource* light;
    const void* target;
    PreparedSetId set;
    unsigned int ordinal;
    double weight;
    std::uint64_t aimed = 0, deposits = 0;
};

struct ProgressivePhotonBatch final
{
    unsigned int shard;
    size_t surfaceBegin, surfaceEnd, mediaBegin, mediaEnd;
};

/// Least cosine between receiver and photon normals, and the symmetric tangent-plane offset as a share of the base radius.
constexpr double kPhotonNormalAgreement = 0.5, kPhotonSlab = 0.1;
/// Narrowing tests: standard deviations allowed for an inner disc's flux share and for the weighted centroid's offset.
constexpr double kPhotonShareSigma = 3.0, kPhotonCentroidSigma = 2.0;
/// Boundaries: deposits expected in each eighth of a disc's outer band, how far a gather continues onto other receivers
/// (in base radii) and the probes that measure the covered share of a disc. why: doc/progressive-photons.md#boundaries
constexpr double kPhotonSectorDeposits = 3.0, kPhotonReach = 3.0;
/// The tangent-plane slab, as a share of the base radius, within which another object continues a receiver's surface.
constexpr double kPhotonJoinSlab = 0.01;
constexpr unsigned int kPhotonCoverageProbes = 16, kPhotonFineCoverageProbes = 64;
/// A covered share below this is measured again with the finer probes.
constexpr double kPhotonFineCoverage = 0.75;

struct MappedPhoton final
{
    Vector3d point;
    float direction[3], normal[3];
    MathColour flux;
    std::uint32_t receiver : 26, bandCode : 4, axis : 2;

    Vector3d Direction() const { return Vector3d(direction[0], direction[1], direction[2]); }
    Vector3d Normal() const { return Vector3d(normal[0], normal[1], normal[2]); }
    /// The dispersion bands of its sample, stored to within a factor of the square root of two (1 to 181).
    double Bands() const { return std::exp2(0.5 * bandCode); }
    static std::uint32_t BandCode(unsigned int bands) { return std::uint32_t(std::min(15.0, std::round(2.0 * std::log2(std::max(1u, bands))))); }
};

/// Same sheet of the receiver: normals agree and the offset from the mean tangent plane, exact for spheres and cylinders, is small.
inline bool PhotonSurfaceMatch(const MappedPhoton& photon, const Vector3d& point, const Vector3d& normal, double slab)
{
    const Vector3d other = photon.Normal();
    return dot(other, normal) >= kPhotonNormalAgreement && std::abs(0.5 * dot(photon.point - point, normal + other)) <= slab;
}

/// The same sheet, for a photon that arrived on its far side.
inline bool PhotonSurfaceMatchBehind(const MappedPhoton& photon, const Vector3d& point, const Vector3d& normal, double slab)
{
    const Vector3d other = -photon.Normal();
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
        static constexpr std::uint32_t noReceiver = (1u << 26) - 1;
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
            {
                if (receivers.size() >= noReceiver)
                    throw std::length_error("Photon method 2 supports at most 67,108,863 receiving objects per prepared set.");
                receivers.push_back(object);
            }
            stored.receiver = found.first->second;
            stored.bandCode = MappedPhoton::BandCode(photon.bands);
            stored.axis = 0;
            photons.push_back(stored);
        }

        /// The scene object holding a collecting object and the class of receivers lit by the same lights, by object.
        using Placement = std::unordered_map<ConstObjectPtr, std::pair<ConstObjectPtr, std::uint32_t>>;

        /// Builds the trees. classes[id] groups the receivers lit by the same lights, whose surfaces a gather may continue
        /// onto, tops[id] is the scene object holding each receiver (both default to the receiver itself), and bare places
        /// the collecting objects that received no deposit.
        void Build(unsigned int k, const std::vector<std::uint32_t>& classes = {}, const std::vector<ConstObjectPtr>& tops = {},
                   Placement bare = {})
        {
            neighbours = k;
            bareObjects = std::move(bare);
            receiverTop = tops.size() == receivers.size() ? tops : receivers;
            receiverClass = classes;
            if (receiverClass.size() != receivers.size())
            {
                receiverClass.resize(receivers.size());
                for (std::uint32_t id = 0; id < receivers.size(); ++id)
                    receiverClass[id] = id;
            }
            Partition();
            baseRadius.assign(receivers.size(), 0.0);
            sparseRadius.assign(receivers.size(), 0.0);
            std::vector<size_t> count(receivers.size(), 0), seen(receivers.size(), 0);
            for (const auto& photon : photons)
                ++count[photon.receiver];
            std::vector<std::vector<double>> radii(receivers.size());
            std::vector<PhotonNeighbour> heap;
            for (const auto& photon : photons)
            {
                const std::uint32_t id = photon.receiver;
                const size_t stride = std::max(size_t(1), count[id] / 128);
                if (count[id] < k || seen[id]++ % stride)
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
            const size_t receiversWithRadius = all.size();
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                if (count[id] < k && count[id] > 1)
                {
                    Vector3d low = partitions[receiverPartitions[id].first].low, high = low;
                    for (std::uint32_t i = receiverPartitions[id].first; i < receiverPartitions[id].second; ++i)
                        for (unsigned int a = 0; a < 3; ++a)
                        {
                            low[a] = std::min(low[a], partitions[i].low[a]);
                            high[a] = std::max(high[a], partitions[i].high[a]);
                        }
                    sparseRadius[id] = std::max(1.0e-8, ExtentRadius(low, high, count[id], k));
                }
            const size_t stride = std::max(size_t(1), photons.size() / 128);
            for (size_t i = 0; receiversWithRadius == 0 && i < photons.size(); i += stride)
            {
                const Vector3d normal = photons[i].Normal();
                const double support = Nearest(photons[i].point, normal, noReceiver, std::numeric_limits<double>::infinity(), heap,
                    [&](const MappedPhoton& other) { return dot(other.Normal(), normal) >= 0.5; });
                if (support > 0.0 && std::isfinite(support))
                    all.push_back(support);
            }
            if (!all.empty())
            {
                std::nth_element(all.begin(), all.begin() + all.size() / 2, all.end());
                globalRadius = std::max(1.0e-8, all[all.size() / 2]);
            }
            else if (!photons.empty())
            {
                Vector3d low = photons.front().point, high = low;
                for (const auto& photon : photons)
                    for (unsigned int a = 0; a < 3; ++a)
                    {
                        low[a] = std::min(low[a], photon.point[a]);
                        high[a] = std::max(high[a], photon.point[a]);
                    }
                globalRadius = std::max(1.0e-8, ExtentRadius(low, high, photons.size(), k));
            }
            BuildClasses(count);
            generation = ++Generations();
        }

        /// Identifies one build of one map, for caches derived from it.
        std::uint64_t Generation() const { return generation; }

        /// The median base radius of the receivers selected by member, or the global one when none has its own.
        template<class Member>
        double MedianBaseRadius(Member&& member) const
        {
            std::vector<double> selected;
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                if (baseRadius[id] > 0.0 && member(receivers[id]))
                    selected.push_back(baseRadius[id]);
            if (selected.empty())
                return globalRadius;
            std::nth_element(selected.begin(), selected.begin() + selected.size() / 2, selected.end());
            return selected[selected.size() / 2];
        }

        /// The receivers by id, as the photons' receiver fields index them.
        const std::vector<ConstObjectPtr>& Receivers() const { return receivers; }

        /// Replaces an unbuilt map with stored photons whose receiver fields index objects.
        void Restore(std::vector<ConstObjectPtr> objects, std::vector<MappedPhoton> stored)
        {
            receivers = std::move(objects);
            receiverIds.clear();
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                receiverIds.emplace(receivers[id], id);
            photons = std::move(stored);
        }

        std::uint32_t Receiver(ConstObjectPtr object) const
        {
            const auto found = receiverIds.find(object);
            return found == receiverIds.end() ? noReceiver : found->second;
        }

        ConstObjectPtr ReceiverObject(std::uint32_t id) const { return receivers[id]; }

        /// The scene object holding a receiver, whose surface coverage probes test.
        ConstObjectPtr Top(std::uint32_t id) const { return receiverTop[id]; }

        /// The class of receivers whose deposits a receiver's gathers may continue onto.
        std::uint32_t Class(std::uint32_t id) const { return id < receiverClass.size() ? receiverClass[id] : noReceiver; }

        /// The median base radius over a class's receivers.
        double ClassRadius(std::uint32_t cls) const { return cls < classRadius.size() ? classRadius[cls] : globalRadius; }

        /// Whether a scene object holds a collecting object of the class.
        bool InClass(ConstObjectPtr top, std::uint32_t cls) const
        {
            return cls < classTops.size() && classTops[cls].count(top) > 0;
        }

        /// Places a collecting object that received no deposit: its scene object and class; false when it has none.
        bool Bare(ConstObjectPtr object, ConstObjectPtr& top, std::uint32_t& cls) const
        {
            const auto found = bareObjects.find(object);
            if (found == bareObjects.end())
                return false;
            top = found->second.first;
            cls = found->second.second;
            return true;
        }

        /// Whether another receiver of the receiver's class holds deposits facing normal whose bounds come within reach of point.
        bool OtherNear(const Vector3d& point, const Vector3d& normal, std::uint32_t receiver, double reach) const
        {
            if (receiver >= crowded.size() || !crowded[receiver])
                return false;
            const auto range = classRanges[receiverClass[receiver]];
            const double bound = reach * reach, length = normal.length();
            return OtherNearParts(range.first, range.second, point, normal / length, receiver, bound);
        }

        /// Adds to heap the nearest accepted photons within reach on the receivers of class other than skip, keeping the k
        /// nearest overall; the heap holds skip's own neighbours, as Nearest left it. why: doc/progressive-photons.md#boundaries
        template<class Accept>
        void NearestOther(const Vector3d& point, const Vector3d& normal, std::uint32_t cls, std::uint32_t skip, double reach,
                          std::vector<PhotonNeighbour>& heap, Accept&& accept, bool twoSided = false) const
        {
            if (cls >= classRanges.size())
                return;
            const auto range = classRanges[cls];
            double bound = reach * reach;
            if (heap.size() == neighbours)
                bound = std::min(bound, heap.front().distance2);
            const double length = normal.length();
            auto usable = [&](const PhotonPartition& part) {
                const double cosine = length > 0.0 ? std::max(-1.0, std::min(1.0, dot(normal, part.axis) / length)) : 1.0;
                auto facing = [&](double c) {
                    const double closest = c >= part.cosSpread ? 1.0 : c * part.cosSpread + std::sqrt(1.0 - c * c) * part.sinSpread;
                    return part.length * length * closest >= kPhotonNormalAgreement;
                };
                return facing(cosine) || (twoSided && facing(-cosine));
            };
            NearestParts(range.first, range.second, point, skip, bound, heap, accept, usable);
        }

        /// The typical support on a receiver, from its spacing when it holds fewer than k photons, else over the whole map.
        double BaseRadius(std::uint32_t id) const
        {
            if (id >= baseRadius.size())
                return globalRadius;
            return baseRadius[id] > 0.0 ? baseRadius[id] : sparseRadius[id] > 0.0 ? sparseRadius[id] : globalRadius;
        }

        double Cap(std::uint32_t id) const { return ProgressivePhotonBudget::capScale * BaseRadius(id); }

        /// The k nearest accepted photons within cap, visiting only partitions of receiver (any, for noReceiver) with a normal
        /// within 60 degrees of normal, or of -normal when twoSided; returns the kernel support (the k-th distance, or cap).
        template<class Accept>
        double Nearest(const Vector3d& point, const Vector3d& normal, std::uint32_t receiver, double cap,
                       std::vector<PhotonNeighbour>& heap, Accept&& accept, bool twoSided = false) const
        {
            heap.clear();
            double bound = cap * cap;
            const bool any = receiver >= receiverPartitions.size();
            const std::uint32_t first = any ? 0 : receiverPartitions[receiver].first;
            const std::uint32_t last = any ? std::uint32_t(partitions.size()) : receiverPartitions[receiver].second;
            const double length = normal.length();
            auto facing = [&](const PhotonPartition& part, double cosine) {
                const double closest = cosine >= part.cosSpread ? 1.0 : cosine * part.cosSpread + std::sqrt(1.0 - cosine * cosine) * part.sinSpread;
                return part.length * length * closest >= kPhotonNormalAgreement ? cosine : -2.0;
            };
            auto usable = [&](const PhotonPartition& part) {
                const double cosine = length > 0.0 ? std::max(-1.0, std::min(1.0, dot(normal, part.axis) / length)) : 1.0;
                const double front = facing(part, cosine);
                return twoSided ? std::max(front, facing(part, -cosine)) : front;
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

        /// The kernel estimate from sorted (nearest first, as Nearest gathered them) and each photon's contribution, over the
        /// widest support down to k/8 samples where the density looks uniform. why: doc/progressive-photons.md#narrowing
        template<class Colour>
        Colour NarrowedEstimate(const std::vector<PhotonNeighbour>& sorted, const std::vector<Colour>& parts,
                                const Vector3d& point, const Vector3d& normal, double support,
                                double* chosenSupport2 = nullptr) const
        {
            constexpr int kC = Colour::channels;
            static thread_local std::vector<double> samples, flux, spread;
            const size_t n = sorted.size();
            samples.assign(n + 1, 0.0);
            flux.assign((n + 1) * kC, 0.0);
            spread.assign((n + 1) * kC, 0.0);
            for (size_t i = 0; i < n; ++i)
            {
                const double bands = photons[sorted[i].index].Bands();
                samples[i + 1] = samples[i] + 1.0 / bands;
                for (int c = 0; c < kC; ++c)
                {
                    flux[(i + 1) * kC + c] = flux[i * kC + c] + parts[i][c];
                    spread[(i + 1) * kC + c] = spread[i * kC + c] + bands * parts[i][c] * parts[i][c];
                }
            }
            const double top = support * support, length = normal.length();
            const Vector3d unit = length > 0.0 ? normal / length : Vector3d(0.0);
            double radii[4];
            unsigned int levels = 0;
            const double least = std::min(double(neighbours), 8.0);
            for (const unsigned int shift : {3u, 2u, 1u})
            {
                const double wanted = std::max(least, double(neighbours >> shift));
                const size_t at = size_t(std::lower_bound(samples.begin() + 1, samples.end(), wanted - 1.0e-9) - samples.begin());
                const double r2 = at <= n ? std::min(top, sorted[at - 1].distance2) : top;
                if (r2 > 0.0 && (levels == 0 || r2 > radii[levels - 1]))
                    radii[levels++] = r2;
            }
            if (levels == 0 || top > radii[levels - 1])
                radii[levels++] = top;
            size_t inside[4];
            Colour chosen;
            for (unsigned int j = 0; j < levels; ++j)
            {
                const double r2 = radii[j];
                size_t m = 0;
                Colour sum;
                Vector3d offset(0.0);
                double weight = 0.0;
                for (; m < n && sorted[m].distance2 < r2; ++m)
                {
                    const double w = PhotonKernel::Weight(sorted[m].distance2, r2);
                    sum += parts[m] * w;
                    weight += w;
                    offset += (photons[sorted[m].index].point - point) * w;
                }
                inside[j] = m;
                bool uniform = true;
                for (unsigned int i = 0; uniform && i < j; ++i)
                {
                    const double share = radii[i] / r2;
                    for (int c = 0; uniform && c < kC; ++c)
                        uniform = std::abs(flux[inside[i] * kC + c] - share * flux[m * kC + c]) <=
                                  kPhotonShareSigma * std::sqrt(share * (1.0 - share) * spread[m * kC + c]) + 1.0e-12 * std::abs(flux[m * kC + c]);
                }
                // a uniform density puts the weighted centroid within about r / sqrt(3 m) of the point, in the tangent plane
                offset -= unit * dot(offset, unit);
                if (uniform && j > 0 && m > 0 && offset.lengthSqr() > kPhotonCentroidSigma * kPhotonCentroidSigma * r2 * weight * weight / (3.0 * double(m)))
                    uniform = false;
                if (j > 0 && !uniform)
                    break;
                chosen = sum * PhotonKernel::Normaliser(r2);
                if (chosenSupport2)
                    *chosenSupport2 = r2;
            }
            return chosen;
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
        std::vector<double> baseRadius, sparseRadius;
        std::vector<std::uint32_t> receiverClass;
        std::vector<ConstObjectPtr> receiverTop;
        std::vector<std::uint32_t> classOrder;
        std::vector<char> crowded;
        std::vector<double> classRadius;
        std::vector<std::unordered_set<ConstObjectPtr>> classTops;
        Placement bareObjects;
        std::vector<std::pair<Vector3d, Vector3d>> classBounds;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> classRanges;
        double globalRadius = 1.0e-8;
        std::uint64_t generation = 0;

        static std::atomic<std::uint64_t>& Generations()
        {
            static std::atomic<std::uint64_t> counter(0);
            return counter;
        }

        /// The k-th neighbour distance of count photons spread evenly over a square with the given diagonal.
        static double ExtentRadius(const Vector3d& low, const Vector3d& high, size_t count, unsigned int k)
        {
            return (high - low).length() * std::sqrt(k / (2.0 * 3.14159265358979 * count));
        }

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

        /// A tree over the partitions of every class shared by several receivers: each class's partition ids in classOrder
        /// form an implicit binary tree whose middle element of each range carries the bounds of the range.
        void BuildClasses(const std::vector<size_t>& count)
        {
            classOrder.clear();
            classBounds.clear();
            classRanges.clear();
            const std::uint32_t classes = receiverClass.empty() ? 0 : 1 + *std::max_element(receiverClass.begin(), receiverClass.end());
            std::vector<std::uint32_t> members(classes, 0);
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                if (count[id] > 0)
                    ++members[receiverClass[id]];
            std::vector<std::vector<std::uint32_t>> parts(classes);
            for (std::uint32_t i = 0; i < partitions.size(); ++i)
            {
                const std::uint32_t cls = receiverClass[photons[partitions[i].begin].receiver];
                if (members[cls] > 1)
                    parts[cls].push_back(i);
            }
            std::vector<std::vector<double>> radii(classes);
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                if (count[id] > 0)
                    radii[receiverClass[id]].push_back(BaseRadius(id));
            classRadius.assign(classes, globalRadius);
            for (std::uint32_t cls = 0; cls < classes; ++cls)
                if (!radii[cls].empty())
                {
                    std::nth_element(radii[cls].begin(), radii[cls].begin() + radii[cls].size() / 2, radii[cls].end());
                    classRadius[cls] = radii[cls][radii[cls].size() / 2];
                }
            classTops.assign(classes, {});
            for (std::uint32_t id = 0; id < receivers.size(); ++id)
                classTops[receiverClass[id]].insert(receiverTop[id]);
            for (const auto& placed : bareObjects)
                if (placed.second.second < classes)
                    classTops[placed.second.second].insert(placed.second.first);
            classRanges.assign(classes, {0, 0});
            for (std::uint32_t cls = 0; cls < classes; ++cls)
            {
                classRanges[cls].first = std::uint32_t(classOrder.size());
                classOrder.insert(classOrder.end(), parts[cls].begin(), parts[cls].end());
                classRanges[cls].second = std::uint32_t(classOrder.size());
            }
            classBounds.resize(classOrder.size());
            for (const auto& range : classRanges)
                BuildParts(range.first, range.second);
            // receivers whose deposit bounds meet another receiver's within the tangent-plane slab, facing alike
            crowded.assign(receivers.size(), 0);
            for (std::uint32_t i = 0; i < partitions.size(); ++i)
            {
                const std::uint32_t id = photons[partitions[i].begin].receiver;
                const auto range = receiverClass[id] < classRanges.size() ? classRanges[receiverClass[id]] : std::make_pair(0u, 0u);
                if (!crowded[id] && range.first < range.second)
                    crowded[id] = Touches(range.first, range.second, partitions[i], id, kPhotonSlab * BaseRadius(id));
            }
        }

        bool Touches(std::uint32_t begin, std::uint32_t end, const PhotonPartition& part, std::uint32_t skip, double reach) const
        {
            if (begin >= end)
                return false;
            const std::uint32_t middle = begin + (end - begin) / 2;
            auto apart = [&](const Vector3d& low, const Vector3d& high) {
                for (unsigned int a = 0; a < 3; ++a)
                    if (low[a] > part.high[a] + reach || high[a] < part.low[a] - reach)
                        return true;
                return false;
            };
            if (apart(classBounds[middle].first, classBounds[middle].second))
                return false;
            const PhotonPartition& other = partitions[classOrder[middle]];
            if (photons[other.begin].receiver != skip && !apart(other.low, other.high) && dot(other.axis, part.axis) > 0.0)
                return true;
            return Touches(begin, middle, part, skip, reach) || Touches(middle + 1, end, part, skip, reach);
        }

        void BuildParts(std::uint32_t begin, std::uint32_t end)
        {
            if (begin >= end)
                return;
            Vector3d low = partitions[classOrder[begin]].low, high = partitions[classOrder[begin]].high;
            for (std::uint32_t i = begin + 1; i < end; ++i)
                for (unsigned int a = 0; a < 3; ++a)
                {
                    low[a] = std::min(low[a], partitions[classOrder[i]].low[a]);
                    high[a] = std::max(high[a], partitions[classOrder[i]].high[a]);
                }
            const Vector3d span = high - low;
            unsigned int axis = span[X] >= span[Y] ? X : Y;
            if (span[Z] > span[axis])
                axis = Z;
            const std::uint32_t middle = begin + (end - begin) / 2;
            std::nth_element(classOrder.begin() + begin, classOrder.begin() + middle, classOrder.begin() + end, [&](std::uint32_t a, std::uint32_t b) {
                return partitions[a].low[axis] + partitions[a].high[axis] < partitions[b].low[axis] + partitions[b].high[axis]; });
            classBounds[middle] = {low, high};
            BuildParts(begin, middle);
            BuildParts(middle + 1, end);
        }

        double RangeDistance2(std::uint32_t middle, const Vector3d& point) const
        {
            double d2 = 0.0;
            for (unsigned int a = 0; a < 3; ++a)
            {
                const double d = std::max(0.0, std::max(classBounds[middle].first[a] - point[a], point[a] - classBounds[middle].second[a]));
                d2 += d * d;
            }
            return d2;
        }

        bool OtherNearParts(std::uint32_t begin, std::uint32_t end, const Vector3d& point, const Vector3d& normal, std::uint32_t skip, double bound) const
        {
            if (begin >= end)
                return false;
            const std::uint32_t middle = begin + (end - begin) / 2;
            if (RangeDistance2(middle, point) > bound)
                return false;
            const PhotonPartition& part = partitions[classOrder[middle]];
            if (photons[part.begin].receiver != skip && part.Distance2(point) <= bound)
            {
                const double c = std::max(-1.0, std::min(1.0, dot(normal, part.axis)));
                const double closest = c >= part.cosSpread ? 1.0 : c * part.cosSpread + std::sqrt(1.0 - c * c) * part.sinSpread;
                if (part.length * closest >= kPhotonNormalAgreement)
                    return true;
            }
            return OtherNearParts(begin, middle, point, normal, skip, bound) || OtherNearParts(middle + 1, end, point, normal, skip, bound);
        }

        template<class Accept, class Usable>
        void NearestParts(std::uint32_t begin, std::uint32_t end, const Vector3d& point, std::uint32_t skip, double& bound,
                          std::vector<PhotonNeighbour>& heap, Accept& accept, Usable& usable) const
        {
            if (begin >= end)
                return;
            const std::uint32_t middle = begin + (end - begin) / 2;
            if (RangeDistance2(middle, point) > bound)
                return;
            const PhotonPartition& part = partitions[classOrder[middle]];
            if (photons[part.begin].receiver != skip && part.Distance2(point) <= bound && usable(part))
                NearestRange(part.begin, part.end, point, bound, heap, accept);
            NearestParts(begin, middle, point, skip, bound, heap, accept, usable);
            NearestParts(middle + 1, end, point, skip, bound, heap, accept, usable);
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
