#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALBOUNDARY_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALBOUNDARY_H

#include "core/configcore.h"
#include "core/lighting/subsurfaceinternal.h"
#include "core/lighting/subsurfaceboundary.h"
#include "core/render/ray.h"
#include <limits>
#include <stdexcept>

namespace pov
{

class SubsurfaceInternalRootQuery final : public RayRootQuery, public RayObjectCondition
{
public:
    SubsurfaceInternalRootQuery(const Vector3d& point, const Vector3d& direction, double shift) :
        origin(point - direction * shift), offset(dot(point - origin, direction))
    {
        const double scale = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2]))) + shift;
        cursor = offset - 8.0 * std::numeric_limits<double>::epsilon() * scale;
    }
    bool Admit(ConstObjectPtr, double depth) const override { return std::isfinite(depth) && depth >= cursor; }
    bool AllRoots() const override { return true; }
    bool PreciseBoundary() const override { return true; }
    bool operator()(const Ray&, ConstObjectPtr object, double depth) const override { return Admit(object, depth); }
    void Advance(double depth) { cursor = std::nextafter(depth, std::numeric_limits<double>::infinity()); }
    double Distance(double depth) const { return std::max(0.0, depth - offset); }
    const Vector3d origin;
private:
    double offset, cursor;
};

// Why an exit search failed, for the caller's message; the search never returns past one.
enum class SubsurfaceExitFailure { NoCrossing, MissedCrossing };

inline const char *SubsurfaceExitFailureText(SubsurfaceExitFailure reason)
{
    switch (reason)
    {
        case SubsurfaceExitFailure::NoCrossing: return "the ray crosses no surface of the material";
        case SubsurfaceExitFailure::MissedCrossing: return "the material still holds the ray after its last surface crossing";
    }
    return "unknown";
}

// Distance from point along direction to where it leaves the box, 0 if it starts outside.
inline double SubsurfaceBoundsReach(const Vector3d& point, const Vector3d& direction, const Vector3d& low, const Vector3d& high)
{
    double reach = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (direction[axis] > 0.0) reach = std::min(reach, (high[axis] - point[axis]) / direction[axis]);
        else if (direction[axis] < 0.0) reach = std::min(reach, (low[axis] - point[axis]) / direction[axis]);
    }
    return std::isfinite(reach) ? std::max(0.0, reach) : 0.0;
}

// The first place the material ends along the ray. Every caller starts in the material: at a point inside it, or on its
// surface heading in (a refracted entry, a reflection off its inside, a light connection already found to enter). So the
// run from the start to the first crossing is material by construction and is never tested; a start on the surface would
// otherwise be asked which side it lies on. The host's later crossings cut the ray into runs, and the material test at the
// middle of each run says which hold material. A run whose middle lies within rounding of either crossing's surface (half
// its length times the cosine there) has no decidable side and takes its neighbours'; on a grazing ray that covers the
// start surface found again a rounding error ahead. The exit is the crossing that opens the first run without material.
// Shift moves only the geometry query backwards; physical distances are measured from point. `fail` throws with the
// caller's description.
template<typename Find, typename Owns, typename Contains, typename Outward, typename Fail>
SubsurfaceInternalBoundary FindSubsurfaceInternalBoundary(const Vector3d& point, const Vector3d& direction,
    TraceTicket& ticket, double shift, double extent, double reach,
    Find&& find, Owns&& owns, Contains&& contains, Outward&& outward, Fail&& fail)
{
    SubsurfaceInternalRootQuery query(point, direction, shift);
    Ray ray(ticket, query.origin, direction, Ray::SubsurfaceRay);
    const RayRootQueryDisabled restoreQuery(ray);
    ray.SetRootQuery(&query);
    const double coordinates = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    const double roundoff = 32.0 * std::numeric_limits<double>::epsilon() * std::max(extent, coordinates);
    auto leave = [&](Intersection& at)
    {
        Vector3d normal;
        outward(at, normal);
        if (dot(normal, direction) < 0.0) normal.invert();
        if (!(dot(normal, direction) > 0.0)) normal = direction;
        return SubsurfaceInternalBoundary{query.Distance(at.Depth), at.IPoint, normal};
    };
    bool opened = false;
    double start = 0.0, openCosine = 1.0;
    Intersection crossing;
    for (;;)
    {
        Intersection hit;
        const bool found = find(hit, ray, query);
        if (found && !owns(hit))
        {
            query.Advance(hit.Depth);
            continue;
        }
        double closeCosine = 1.0;
        if (found)
        {
            Intersection at(hit);
            Vector3d normal;
            outward(at, normal);
            closeCosine = std::abs(dot(normal, direction));
        }
        const double end = found ? query.Distance(hit.Depth) : std::max(reach, start);
        const bool decided = opened && (0.5 * (end - start) * std::min(openCosine, closeCosine) > roundoff);
        const bool material = decided && contains(point + direction * (0.5 * (start + end)));
        if (decided && !material)
            return leave(crossing);
        if (!found)
        {
            if (!opened)
                fail(SubsurfaceExitFailure::NoCrossing, end);
            else if (decided)
                fail(SubsurfaceExitFailure::MissedCrossing, start);
            else
                return leave(crossing);
            throw std::runtime_error(SubsurfaceExitFailureText(opened ? SubsurfaceExitFailure::MissedCrossing : SubsurfaceExitFailure::NoCrossing));
        }
        crossing = hit;
        opened = true;
        openCosine = closeCosine;
        start = std::max(start, end);
        query.Advance(hit.Depth);
    }
}

}
#endif
