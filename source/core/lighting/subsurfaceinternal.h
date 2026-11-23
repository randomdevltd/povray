#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

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
// The boundary along a ray; one that takes a limit may stop at the first crossing past it, the material holding till there.
template<typename Boundary>
SubsurfaceInternalBoundary SubsurfaceInternalExit(Boundary& boundary, const Vector3d& position, const Vector3d& direction, double limit)
{
    if constexpr (std::is_invocable<Boundary&, const Vector3d&, const Vector3d&, double>::value)
        return boundary(position, direction, limit);
    else
        return boundary(position, direction);
}

/// Which of a path's light a walk returns: all of it, only the emission it gathers before its first collision (along the
/// starting ray and its internal reflections, which depends on that ray), or only what follows a collision (nearly the same
/// for every starting ray, as the collision forgets it). The two parts sum to the whole in expectation.
enum class SubsurfaceInternalPart { kAll, kUncollided, kCollided };

/// Russian roulette at every event: a path survives with its weight relative to the weight it started with, at most
/// certainly, and divides by that chance, so it is unbiased and the weight it carries never falls below its start. A
/// path whose weight has not fallen draws nothing.
template<typename Draw>
bool SubsurfaceInternalSurvives(double& throughput, double start, Draw& draw)
{
    const double survival = throughput / start;
    if (survival >= 1.0) return true;
    if (draw() >= survival) return false;
    throughput = start;
    return true;
}

// The walk ends by physics alone: absorption ends it through roulette, and a walk in a body of finite bounds leaves it
// through its surface, where Fresnel transmission is above zero for some directions at any finite relative index. With
// little absorption in an optically large body a walk is long (about the square of the body's optical size in
// scattering events); that is the cost of the light diffusing out, and the boundary search lets the render stop meanwhile.
// The one walk that cannot end is a boundary that returns the start point under total internal reflection with no
// loss, which makes no progress; that is a boundary fault and throws.
template<typename Boundary, typename Source, typename Connect, typename Draw, typename Bounce>
double SampleSubsurfaceInternalRadiance(Vector3d position, Vector3d direction,
    double absorption, double scattering, double relativeIor,
    Boundary&& boundary, Source&& source, Connect&& connect, Draw&& draw, Bounce&& bounce,
    SubsurfaceInternalPart part = SubsurfaceInternalPart::kAll)
{
    const double extinction = absorption + scattering;
    if (!(absorption >= 0.0) || !(scattering >= 0.0) || !(extinction > 0.0) ||
        !std::isfinite(extinction) || !(relativeIor > 0.0) || !std::isfinite(relativeIor))
        throw std::invalid_argument("Internal subsurface transport requires finite homogeneous coefficients");
    const double albedo = scattering / extinction;
    double radiance = 0.0, throughput = 1.0;
    const double start = throughput;
    bool gatherSegment = true, collided = false;
    for (;;)
    {
        // a segment that gathers needs its whole length; otherwise only whether the material outlasts the free flight
        double distance = 0.0;
        auto freeFlight = [&]()
        {
            const double uniform = std::min(std::nextafter(1.0, 0.0), std::max(0.5 * std::numeric_limits<double>::epsilon(), draw()));
            distance = -std::log1p(-uniform) / extinction;
        };
        if (!gatherSegment)
            freeFlight();
        const auto hit = SubsurfaceInternalExit(boundary, position, direction,
                                                gatherSegment ? std::numeric_limits<double>::infinity() : distance);
        if (!(hit.distance >= 0.0) || !std::isfinite(hit.distance))
            throw std::runtime_error("Internal subsurface transport requires an enclosing boundary");
        if (part == SubsurfaceInternalPart::kUncollided)
        {
            // the uncollided part is carried in expectation: the segment's emission, then on at the chance of crossing it
            // unscattered and of reflecting where it ends
            radiance += throughput * source(position, direction, hit.distance, extinction);
            const double cosine = dot(direction, hit.normal);
            if (!(cosine > 0.0)) throw std::runtime_error("Internal subsurface normal must point outwards");
            const double kept = std::exp(-extinction * hit.distance) * SubsurfaceInternalFresnel(cosine, relativeIor) * bounce(hit.position);
            if (!(kept < 1.0) && !(hit.distance > 0.0))
                throw std::runtime_error("Internal subsurface walk reflected without loss where it started, so it makes no progress: "
                                         "the boundary search returned its start point");
            throughput *= kept;
            if (!(throughput > 0.0)) return radiance;
            position = hit.position;
            direction -= hit.normal * (2.0 * cosine);
            if (!SubsurfaceInternalSurvives(throughput, start, draw)) return radiance;
            continue;
        }
        if (gatherSegment)
        {
            if (collided || (part == SubsurfaceInternalPart::kAll))
                radiance += throughput * source(position, direction, hit.distance, extinction);
            freeFlight();
        }
        if (distance < hit.distance)
        {
            position += direction * distance;
            throughput *= albedo;
            collided = true;
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
            const double fresnel = SubsurfaceInternalFresnel(cosine, relativeIor);
            if (draw() >= fresnel) return radiance;
            const double kept = bounce(hit.position);
            if (!(fresnel < 1.0) && !(kept < 1.0) && !(hit.distance > 0.0))
                throw std::runtime_error("Internal subsurface walk reflected without loss where it started, so it makes no progress: "
                                         "the boundary search returned its start point");
            throughput *= kept;
            if (!(throughput > 0.0)) return radiance;
            position = hit.position;
            direction -= hit.normal * (2.0 * cosine);
            gatherSegment = true;
        }
        if (!SubsurfaceInternalSurvives(throughput, start, draw)) return radiance;
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
