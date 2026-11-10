#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pov
{

struct SubsurfaceInternalBoundary final
{
    double distance;
    Vector3d position, normal;
};

inline double SubsurfaceInternalFresnel(double cosine, double relativeIor)
{
    if (relativeIor == 1.0) return 0.0;
    cosine = std::min(1.0, std::max(0.0, cosine));
    const double sineSquared = relativeIor * relativeIor * (1.0 - cosine * cosine);
    if (sineSquared >= 1.0) return 1.0;
    const double transmitted = std::sqrt(1.0 - sineSquared);
    const double s = (relativeIor * cosine - transmitted) / (relativeIor * cosine + transmitted);
    const double p = (cosine - relativeIor * transmitted) / (cosine + relativeIor * transmitted);
    return 0.5 * (s * s + p * p);
}

// Source is the attenuated line integral; connect is ∫j(y) exp(-σt r) / (4π r²) dV(y); bounce(p) is what an internal
// reflection at boundary point p keeps (the skin it crosses there).
template<typename Boundary, typename Source, typename Connect, typename Draw, typename Bounce>
double SampleSubsurfaceInternalRadiance(Vector3d position, Vector3d direction,
    double absorption, double scattering, double relativeIor,
    Boundary&& boundary, Source&& source, Connect&& connect, Draw&& draw, Bounce&& bounce)
{
    const double extinction = absorption + scattering;
    if (!(absorption >= 0.0) || !(scattering >= 0.0) || !(extinction > 0.0) ||
        !std::isfinite(extinction) || !(relativeIor > 0.0) || !std::isfinite(relativeIor))
        throw std::invalid_argument("Internal subsurface transport requires finite coefficients");
    const double albedo = scattering / extinction;
    double radiance = 0.0, throughput = 1.0;
    bool gatherSegment = true;
    size_t events = 0;
    for (;;)
    {
        const auto hit = boundary(position, direction);
        if (!(hit.distance >= 0.0) || !std::isfinite(hit.distance))
            throw std::runtime_error("Internal subsurface transport requires an enclosing boundary");
        if (gatherSegment)
            radiance += throughput * source(position, direction, hit.distance, extinction);
        const double uniform = std::min(std::nextafter(1.0, 0.0), std::max(0.5 * std::numeric_limits<double>::epsilon(), draw()));
        const double distance = -std::log1p(-uniform) / extinction;
        if (distance < hit.distance)
        {
            position += direction * distance;
            throughput *= albedo;
            if (!(throughput > 0.0)) return radiance;
            radiance += throughput * connect(position, extinction);
            const double cosine = 1.0 - 2.0 * draw();
            const double radius = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
            const double phi = 2.0 * M_PI * draw();
            direction = Vector3d(radius * std::cos(phi), radius * std::sin(phi), cosine);
            gatherSegment = false;
        }
        else
        {
            const double cosine = dot(direction, hit.normal);
            if (!(cosine > 0.0)) throw std::runtime_error("Internal subsurface normal must point outwards");
            if (draw() >= SubsurfaceInternalFresnel(cosine, relativeIor)) return radiance;
            throughput *= bounce(hit.position);
            if (!(throughput > 0.0)) return radiance;
            position = hit.position;
            direction -= hit.normal * (2.0 * cosine);
            gatherSegment = true;
        }
        if (++events >= 16)
        {
            const double survival = std::min(0.95, throughput);
            if (draw() >= survival) return radiance;
            throughput /= survival;
        }
    }
}

template<typename Boundary, typename Source, typename Connect, typename Draw>
double SampleSubsurfaceInternalRadiance(Vector3d position, Vector3d direction,
    double absorption, double scattering, double relativeIor,
    Boundary&& boundary, Source&& source, Connect&& connect, Draw&& draw)
{
    return SampleSubsurfaceInternalRadiance(position, direction, absorption, scattering, relativeIor,
        boundary, source, connect, draw, [](const Vector3d&) { return 1.0; });
}

}
#endif
