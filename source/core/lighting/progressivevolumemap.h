#ifndef POVRAY_CORE_PROGRESSIVEVOLUMEMAP_H
#define POVRAY_CORE_PROGRESSIVEVOLUMEMAP_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include "core/scene/tagfilter.h"
#include "core/lighting/progressivephoton.h"
#include "core/lighting/photonbeam.h"
#include <memory>
#include <limits>

#include <algorithm>
#include <queue>
#include <vector>

namespace pov
{

struct ProgressiveVolumeDomain final
{
    std::vector<const Media*> media;
    std::vector<const Interior*> modifiers;
    std::vector<size_t> below;

    bool operator==(const ProgressiveVolumeDomain& other) const
        { return media == other.media && modifiers == other.modifiers && below == other.below; }
};

struct ProgressiveVolumePhoton final
{
    Vector3d point, direction;
    MathColour flux, extinction;
    std::shared_ptr<const ProgressiveVolumeDomain> domain;
    double length = 0.0;
    PreparedSetId set = 0;
    unsigned char axis = 0;

    double DistanceSquared(const Vector3d& position) const
    {
        const Vector3d delta = position - point;
        const double along = std::max(-length * 0.5, std::min(length * 0.5, dot(delta, direction)));
        return (delta - direction * along).lengthSqr();
    }

    MathColour Integral(const Vector3d& position, double radius) const
    {
#ifdef POV_PROGRESSIVE_VOLUME_POINTS
        return (position - point).lengthSqr() <= radius * radius ? flux * Exp(-extinction * (length * 0.5)) * length : MathColour();
#else
        const Vector3d delta = position - point;
        const double along = -dot(delta, direction);
        double first, last;
        if (!ClipPhotonBeam(along + length * 0.5, (delta + direction * along).lengthSqr(), radius, length, first, last))
            return MathColour();
        MathColour result;
        for (int channel = 0; channel < MathColour::channels; ++channel)
            result[channel] = flux[channel] * PhotonBeamIntegral(extinction[channel], first, last);
        return result;
#endif
    }
};

class ProgressiveVolumePhotonMap final
{
    public:
        std::vector<ProgressiveVolumePhoton> photons;
        double initialRadius = 0.0;
        struct Bounds { Vector3d low, high; };
        std::vector<Bounds> bounds;

        void Build()
        {
            if (photons.empty())
                return;
            Vector3d low = photons.front().point, high = low;
            for (const auto& photon : photons)
                for (unsigned int a = 0; a < 3; ++a)
                {
                    low[a] = std::min(low[a], photon.point[a]);
                    high[a] = std::max(high[a], photon.point[a]);
                }
            initialRadius = std::max(1.0e-8, (high - low).length() * 0.025);
            bounds.resize(photons.size());
            BuildRange(0, photons.size(), 0);
            std::vector<double> radii;
            const size_t stride = std::max(size_t(1), photons.size() / 128);
            for (size_t i = 0; i < photons.size(); i += stride)
            {
                std::priority_queue<double> nearest;
                Nearest(0, photons.size(), photons[i].point, *photons[i].domain, nearest);
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

        /// The distance to the 32nd nearest photon of the same medium, at most four median distances.
        double RadiusAt(const Vector3d& point, const ProgressiveVolumeDomain& domain) const
        {
            std::priority_queue<double> nearest;
            Nearest(0, photons.size(), point, domain, nearest, 16.0 * initialRadius * initialRadius);
            const double local = nearest.size() == 32 ? std::sqrt(nearest.top()) : 4.0 * initialRadius;
            return std::max(1.0e-8, local);
        }

        template<class Visitor>
        void Visit(const Vector3d& point, double support, Visitor&& visitor) const
        {
            VisitRange(0, photons.size(), point, support, visitor);
        }

    private:
        static double DistanceSquared(const Bounds& box, const Vector3d& point)
        {
            double result = 0.0;
            for (unsigned int axis = 0; axis < 3; ++axis)
            {
                const double d = std::max(0.0, std::max(box.low[axis] - point[axis], point[axis] - box.high[axis]));
                result += d * d;
            }
            return result;
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
                [axis](const ProgressiveVolumePhoton& a, const ProgressiveVolumePhoton& b) { return a.point[axis] < b.point[axis]; });
            photons[middle].axis = axis;
            BuildRange(begin, middle, depth + 1);
            BuildRange(middle + 1, end, depth + 1);
            const auto& photon = photons[middle];
            for (unsigned int a = 0; a < 3; ++a)
            {
                const double reach = std::abs(photon.direction[a]) * photon.length * 0.5;
                bounds[middle].low[a] = photon.point[a] - reach;
                bounds[middle].high[a] = photon.point[a] + reach;
                for (size_t child : {begin + (middle - begin) / 2, middle + 1 + (end - middle - 1) / 2})
                    if (child != middle && child < end)
                    {
                        bounds[middle].low[a] = std::min(bounds[middle].low[a], bounds[child].low[a]);
                        bounds[middle].high[a] = std::max(bounds[middle].high[a], bounds[child].high[a]);
                    }
            }
        }

        template<class Visitor>
        void VisitRange(size_t begin, size_t end, const Vector3d& point, double support, Visitor& visitor) const
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            if (DistanceSquared(bounds[middle], point) > support * support)
                return;
            const auto& photon = photons[middle];
            if (photon.DistanceSquared(point) <= support * support)
                visitor(photon);
            VisitRange(begin, middle, point, support, visitor);
            VisitRange(middle + 1, end, point, support, visitor);
        }

        void Nearest(size_t begin, size_t end, const Vector3d& point, const ProgressiveVolumeDomain& domain,
                     std::priority_queue<double>& nearest, double limit = std::numeric_limits<double>::infinity()) const
        {
            if (begin == end)
                return;
            const size_t middle = begin + (end - begin) / 2;
            if (DistanceSquared(bounds[middle], point) > (nearest.size() == 32 ? nearest.top() : limit))
                return;
            const auto& photon = photons[middle];
            const Vector3d delta = photon.point - point;
            const double distance2 = photon.DistanceSquared(point);
            if (distance2 <= limit && *photon.domain == domain && (nearest.size() < 32 || distance2 < nearest.top()))
            {
                nearest.push(distance2);
                if (nearest.size() > 32)
                    nearest.pop();
            }
            const bool left = delta[photon.axis] >= 0;
            Nearest(left ? begin : middle + 1, left ? middle : end, point, domain, nearest, limit);
            Nearest(left ? middle + 1 : begin, left ? end : middle, point, domain, nearest, limit);
        }
};

}

#endif
