#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace pov
{

struct SubsurfaceInternalBoundary final
{
    double distance;
    Vector3d position, normal;
};

struct SubsurfaceInternalStatistics final
{
    size_t collisions = 0, reflections = 0, rouletteStops = 0;
};

inline bool SubsurfaceInternalFiniteBounds(const Vector3d& low, const Vector3d& high)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const double extent = high[axis] - low[axis];
        if (!(extent > 0.0) || !std::isfinite(extent) || !(extent < BOUND_HUGE * 0.5)) return false;
    }
    return true;
}

inline double SubsurfaceInternalFresnel(double cosine, double relativeIor)
{
    if (relativeIor == 1.0)
        return 0.0;
    cosine = std::min(1.0, std::max(0.0, cosine));
    const double sineSquared = relativeIor * relativeIor * (1.0 - cosine * cosine);
    if (sineSquared >= 1.0)
        return 1.0;
    const double transmitted = std::sqrt(1.0 - sineSquared);
    const double s = (relativeIor * cosine - transmitted) / (relativeIor * cosine + transmitted);
    const double p = (cosine - relativeIor * transmitted) / (cosine + relativeIor * transmitted);
    return 0.5 * (s * s + p * p);
}

// Source-only homogeneous isotropic RTE: j is radiance/world-length; coefficients are inverse mm.
template<typename Boundary, typename Source, typename Draw>
double SampleSubsurfaceInternalRadiance(Vector3d position, Vector3d direction,
    double sigmaAbsorption, double sigmaScattering, double mmPerUnit, double relativeIor,
    Boundary&& boundary, Source&& source, Draw&& draw,
    SubsurfaceInternalStatistics* statistics = nullptr, size_t rouletteStart = 32)
{
    const double sigma = (sigmaAbsorption + sigmaScattering) * mmPerUnit;
    if (!(sigmaAbsorption >= 0.0) || !(sigmaScattering >= 0.0) || !(mmPerUnit > 0.0) ||
        !(relativeIor > 0.0) || !std::isfinite(relativeIor) || !(sigma > 0.0) || !std::isfinite(sigma))
        throw std::invalid_argument("Internal subsurface transport requires finite positive extinction and units");
    const double albedo = sigmaScattering / (sigmaAbsorption + sigmaScattering);
    double radiance = 0.0, throughput = 1.0;
    size_t events = 0;
    for (;;)
    {
        const auto hit = boundary(position, direction);
        if (!(hit.distance >= 0.0) || !std::isfinite(hit.distance))
            throw std::runtime_error("Internal subsurface transport requires a finite enclosing boundary");
        const double uniform = std::min(std::nextafter(1.0, 0.0),
            std::max(0.5 * std::numeric_limits<double>::epsilon(), draw()));
        const double distance = std::max(std::nextafter(0.0, 1.0), -std::log1p(-uniform) / sigma);
        if (distance < hit.distance)
        {
            position += direction * distance;
            const double emission = source(position, -direction);
            if (!(emission >= 0.0) || !std::isfinite(emission))
                throw std::runtime_error("Internal subsurface source must be finite and nonnegative");
            radiance += throughput * emission / sigma;
            if (statistics) ++statistics->collisions;
            throughput *= albedo;
            if (!(throughput > 0.0))
                return radiance;
            const double cosine = 1.0 - 2.0 * draw();
            const double radius = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
            const double phi = 2.0 * M_PI * draw();
            direction = Vector3d(radius * std::cos(phi), radius * std::sin(phi), cosine);
        }
        else
        {
            const double cosine = dot(direction, hit.normal);
            if (!(cosine > 0.0))
                throw std::runtime_error("Internal subsurface boundary normal must point outwards");
            if (draw() >= SubsurfaceInternalFresnel(cosine, relativeIor))
                return radiance;
            position = hit.position;
            direction -= hit.normal * (2.0 * cosine);
            if (statistics) ++statistics->reflections;
        }
        if (++events >= rouletteStart)
        {
            const double survival = std::min(1.0, throughput);
            if (draw() >= survival)
            {
                if (statistics) ++statistics->rouletteStops;
                return radiance;
            }
            throughput /= survival;
        }
    }
}

}

#endif
