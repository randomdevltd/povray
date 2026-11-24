#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNAL_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

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
// A boundary met at zero distance is a reflection where faces meet (an edge, a corner, a face found again within rounding):
// the walk stays at its point and turns. A run of such reflections gathers nothing (its segments have no length, so no
// emission and no collision) until the walk travels again. One that turns back out through a face it has already
// reflected from at this point retraces itself and would never travel: it carries nothing more, so the walk ends there.
// Only a reflection at an edge or corner the walk hits exactly turns back like that, a measure-zero set of paths.
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
    auto uniform = [&]() { return std::min(std::nextafter(1.0, 0.0), std::max(0.5 * std::numeric_limits<double>::epsilon(), draw())); };
    auto exit = [&](const Vector3d& from, const Vector3d& along, double limit)
    {
        const auto hit = SubsurfaceInternalExit(boundary, from, along, limit);
        if (!(hit.distance >= 0.0) || !std::isfinite(hit.distance))
            throw std::runtime_error("Internal subsurface transport requires an enclosing boundary");
        return hit;
    };
    auto outwards = [](double cosine) { if (!(cosine > 0.0)) throw std::runtime_error("Internal subsurface normal must point outwards"); };
    // The faces reflected from at the walk's current point; false when a reflection there turns back out through one of them.
    auto turn = [](std::vector<Vector3d>& faces, const SubsurfaceInternalBoundary& hit, const Vector3d& along)
    {
        if (hit.distance > 0.0)
            faces.clear();
        else
            for (const Vector3d& face : faces)
                if (dot(along, face) > 0.0) return false;
        faces.push_back(hit.normal);
        return true;
    };
    // A collision at a point: its albedo, the emission it connects to, and an isotropic new direction; false if nothing is left.
    auto scatter = [&](const Vector3d& at, double& throughput, Vector3d& along, double& radiance)
    {
        throughput *= albedo;
        if (!(throughput > 0.0)) return false;
        radiance += throughput * connect(at, extinction);
        const double cosine = 1.0 - 2.0 * draw();
        const double radius = std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
        const double phi = 2.0 * M_PI * draw();
        along = Vector3d(radius * std::cos(phi), radius * std::sin(phi), cosine);
        return true;
    };
    // The walk itself, from a start on a segment that gathers its emission, or just after a collision, whose next straight
    // segment that collision's connection owns.
    auto follow = [&](Vector3d position, Vector3d direction, double throughput, bool gatherSegment)
    {
        const double start = throughput;
        double radiance = 0.0;
        std::vector<Vector3d> faces;
        for (;;)
        {
            // a segment that gathers needs its whole length; otherwise only whether the material outlasts the free flight
            double distance = gatherSegment ? 0.0 : -std::log1p(-uniform()) / extinction;
            const auto hit = exit(position, direction, gatherSegment ? std::numeric_limits<double>::infinity() : distance);
            if (gatherSegment)
            {
                radiance += throughput * source(position, direction, hit.distance, extinction);
                distance = -std::log1p(-uniform()) / extinction;
            }
            if (distance < hit.distance)
            {
                position += direction * distance;
                faces.clear();
                if (!scatter(position, throughput, direction, radiance)) return radiance;
                gatherSegment = false;
            }
            else
            {
                const double cosine = dot(direction, hit.normal);
                outwards(cosine);
                const double fresnel = SubsurfaceInternalFresnel(cosine, relativeIor);
                if (draw() >= fresnel) return radiance;
                throughput *= bounce(hit.position);
                if (!(throughput > 0.0)) return radiance;
                position = hit.position;
                direction -= hit.normal * (2.0 * cosine);
                if (!turn(faces, hit, direction)) return radiance;
                gatherSegment = true;
            }
            if (!SubsurfaceInternalSurvives(throughput, start, draw)) return radiance;
        }
    };
    if (part == SubsurfaceInternalPart::kAll)
        return follow(position, direction, 1.0, true);
    // Either part follows the uncollided chain in expectation: each straight segment, then on at the chance of crossing it
    // unscattered and of reflecting where it ends. The uncollided part is each segment's emission. The collided part is
    // each segment's first collision, drawn within the segment and weighted by the chance of one there, 1 - exp(-sigma d),
    // so that a path that would have left unscattered still collides: an optically thin host gives every path light.
    double radiance = 0.0, chain = 1.0;
    std::vector<Vector3d> faces;
    for (;;)
    {
        const auto hit = exit(position, direction, std::numeric_limits<double>::infinity());
        const double depth = extinction * hit.distance;
        if (part == SubsurfaceInternalPart::kUncollided)
            radiance += chain * source(position, direction, hit.distance, extinction);
        else
        {
            const double collides = -std::expm1(-depth);
            if (collides > 0.0)
            {
                const double distance = std::min(-std::log1p(-uniform() * collides) / extinction, std::nextafter(hit.distance, 0.0));
                double throughput = chain * collides, gathered = 0.0;
                const double start = throughput;
                const Vector3d at = position + direction * distance;
                Vector3d along;
                if (scatter(at, throughput, along, gathered) && SubsurfaceInternalSurvives(throughput, start, draw))
                    gathered += follow(at, along, throughput, false);
                radiance += gathered;
            }
        }
        const double cosine = dot(direction, hit.normal);
        outwards(cosine);
        chain *= std::exp(-depth) * SubsurfaceInternalFresnel(cosine, relativeIor) * bounce(hit.position);
        if (!(chain > 0.0)) return radiance;
        position = hit.position;
        direction -= hit.normal * (2.0 * cosine);
        if (!turn(faces, hit, direction)) return radiance;
        if (!SubsurfaceInternalSurvives(chain, 1.0, draw)) return radiance;
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
