#ifndef POVRAY_CORE_PROGRESSIVEPHOTONMAP_H
#define POVRAY_CORE_PROGRESSIVEPHOTONMAP_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include "core/scene/tagfilter.h"
#include "core/lighting/progressivephoton.h"

#include <algorithm>
#include <queue>
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
    unsigned char axis = 0;
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

class ProgressivePhotonMap final
{
    public:
        std::vector<ProgressivePhoton> photons;
        std::vector<ProgressivePhoton> pilot;
        double initialRadius = 0.0;
        double radius = 0.0;

        void Build(unsigned int pass, unsigned int dimensions)
        {
            if (photons.empty())
                return;
            if (initialRadius == 0.0)
            {
                Vector3d low = photons.front().point, high = low;
                for (const auto& photon : photons)
                    for (unsigned int a = 0; a < 3; ++a)
                    {
                        low[a] = std::min(low[a], photon.point[a]);
                        high[a] = std::max(high[a], photon.point[a]);
                    }
                initialRadius = std::max(1.0e-8, (high - low).length() * 0.025);
            }
            BuildRange(0, photons.size(), 0);
            if (pilot.empty())
            {
                pilot = photons;
                for (size_t i = 0; i < pilot.size(); ++i)
                    pilotReceivers[pilot[i].object].push_back({i, 0});
                for (auto& receiver : pilotReceivers)
                    BuildPilotRange(receiver.second, 0, receiver.second.size());
                std::vector<double> radii;
                const size_t stride = std::max(size_t(1), pilot.size() / 128);
                for (size_t i = 0; i < pilot.size(); i += stride)
                {
                    std::priority_queue<double> nearest;
                    NearestAt(pilot[i].point, pilot[i].object, nearest);
                    if (!nearest.empty() && nearest.top() > 0.0)
                        radii.push_back(std::sqrt(nearest.top()));
                }
                if (!radii.empty())
                {
                    const size_t middle = radii.size() / 2;
                    std::nth_element(radii.begin(), radii.begin() + middle, radii.end());
                    initialRadius = std::max(1.0e-8, radii[middle]);
                }
            }
            radius = initialRadius * ProgressivePhotonBudget::RadiusScale(pass, dimensions);
        }

        double RadiusAt(const Vector3d& point, ConstObjectPtr object = nullptr, size_t* visited = nullptr) const
        {
            std::priority_queue<double> nearest;
            NearestAt(point, object, nearest, visited);
            if (nearest.empty())
                return radius;
            const double local = object ? std::sqrt(nearest.top()) : std::min(std::sqrt(nearest.top()), 4.0 * initialRadius);
            return std::max(1.0e-8, local) * radius / initialRadius;
        }

        template<class Visitor>
        void Visit(const Vector3d& point, double support, Visitor&& visitor) const
        {
            VisitRange(0, photons.size(), point, support, visitor);
        }

    private:
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
                [axis](const ProgressivePhoton& a, const ProgressivePhoton& b) { return a.point[axis] < b.point[axis]; });
            photons[middle].axis = axis;
            BuildRange(begin, middle, depth + 1);
            BuildRange(middle + 1, end, depth + 1);
        }

        template<class Visitor>
        void VisitRange(size_t begin, size_t end, const Vector3d& point, double support, Visitor& visitor) const
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            const auto& photon = photons[middle];
            const Vector3d delta = photon.point - point;
            if (delta.lengthSqr() <= support * support)
                visitor(photon);
            if (delta[photon.axis] >= -support)
                VisitRange(begin, middle, point, support, visitor);
            if (delta[photon.axis] <= support)
                VisitRange(middle + 1, end, point, support, visitor);
        }

        struct PilotNode final
        {
            size_t photon;
            unsigned char axis;
        };
        std::unordered_map<ConstObjectPtr, std::vector<PilotNode>> pilotReceivers;

        void BuildPilotRange(std::vector<PilotNode>& nodes, size_t begin, size_t end)
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            Vector3d low = pilot[nodes[begin].photon].point, high = low;
            for (size_t i = begin + 1; i < end; ++i)
                for (unsigned int a = 0; a < 3; ++a)
                {
                    low[a] = std::min(low[a], pilot[nodes[i].photon].point[a]);
                    high[a] = std::max(high[a], pilot[nodes[i].photon].point[a]);
                }
            const Vector3d span = high - low;
            unsigned int axis = span[X] >= span[Y] ? X : Y;
            if (span[Z] > span[axis])
                axis = Z;
            std::nth_element(nodes.begin() + begin, nodes.begin() + middle, nodes.begin() + end,
                [&](const PilotNode& a, const PilotNode& b) { return pilot[a.photon].point[axis] < pilot[b.photon].point[axis]; });
            nodes[middle].axis = axis;
            BuildPilotRange(nodes, begin, middle);
            BuildPilotRange(nodes, middle + 1, end);
        }

        void NearestAt(const Vector3d& point, ConstObjectPtr object, std::priority_queue<double>& nearest, size_t* visited = nullptr) const
        {
            const auto found = pilotReceivers.find(object);
            if (found != pilotReceivers.end())
                Nearest(found->second, 0, found->second.size(), point, nearest, visited);
        }

        void Nearest(const std::vector<PilotNode>& nodes, size_t begin, size_t end, const Vector3d& point,
                     std::priority_queue<double>& nearest, size_t* visited) const
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            const auto& node = nodes[middle];
            if (visited)
                ++*visited;
            const Vector3d delta = pilot[node.photon].point - point;
            const double distance2 = delta.lengthSqr();
            if (nearest.size() < 32 || distance2 < nearest.top())
            {
                nearest.push(distance2);
                if (nearest.size() > 32)
                    nearest.pop();
            }
            const bool left = delta[node.axis] >= 0;
            Nearest(nodes, left ? begin : middle + 1, left ? middle : end, point, nearest, visited);
            if (nearest.size() < 32 || delta[node.axis] * delta[node.axis] <= nearest.top())
                Nearest(nodes, left ? middle + 1 : begin, left ? end : middle, point, nearest, visited);
        }
};

}

#endif
