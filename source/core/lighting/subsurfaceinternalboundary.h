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
enum class SubsurfaceExitFailure { NoMaterial, MissedCrossing };

inline const char *SubsurfaceExitFailureText(SubsurfaceExitFailure reason)
{
    switch (reason)
    {
        case SubsurfaceExitFailure::NoMaterial: return "the ray never runs through the material";
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

// The first place the material ends along the ray. The host's own crossings cut the ray into runs; the material test at
// the middle of each run, never a probe a tolerance away from a root, says which runs hold material, so tolerance bands
// in a shape's inside test, approximate roots and thin parts all classify alike. A run too short to have a distinct middle
// takes its neighbours' side; the first such run at the origin counts as material (a start on the surface). The exit is
// the crossing that opens the first run without material after one with it. Shift moves only the geometry query
// backwards; physical distances are measured from point. `fail` throws with the caller's description.
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
    bool entered = false, opened = false;
    double start = 0.0;
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
        const double end = found ? query.Distance(hit.Depth) : std::max(reach, start);
        if (end - start > roundoff)
        {
            if (contains(point + direction * (0.5 * (start + end))))
                entered = true;
            else if (entered)
            {
                if (!opened)
                    return {0.0, point, direction};
                Vector3d normal;
                outward(crossing, normal);
                if (dot(normal, direction) < 0.0) normal.invert();
                if (!(dot(normal, direction) > 0.0)) normal = direction;
                return {query.Distance(crossing.Depth), crossing.IPoint, normal};
            }
        }
        else if (!opened)
            entered = true;
        if (!found)
        {
            if (entered && opened && !(end - start > roundoff))
            {
                Vector3d normal;
                outward(crossing, normal);
                if (dot(normal, direction) < 0.0) normal.invert();
                if (!(dot(normal, direction) > 0.0)) normal = direction;
                return {query.Distance(crossing.Depth), crossing.IPoint, normal};
            }
            fail(entered ? SubsurfaceExitFailure::MissedCrossing : SubsurfaceExitFailure::NoMaterial, start);
            throw std::runtime_error(SubsurfaceExitFailureText(entered ? SubsurfaceExitFailure::MissedCrossing : SubsurfaceExitFailure::NoMaterial));
        }
        crossing = hit;
        opened = true;
        start = std::max(start, end);
        query.Advance(hit.Depth);
    }
}

}
#endif
