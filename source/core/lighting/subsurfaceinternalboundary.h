#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALBOUNDARY_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEINTERNALBOUNDARY_H

#include "core/configcore.h"
#include "core/lighting/subsurfaceinternal.h"
#include "core/render/ray.h"
#include <limits>

namespace pov
{

inline Vector3d SubsurfaceInternalProbe(const Vector3d& point, const Vector3d& direction, double tolerance, double extent)
{
    const double coordinates = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    const double step = std::max(tolerance, 32.0 * std::numeric_limits<double>::epsilon() * std::max(extent, coordinates));
    Vector3d result = point + direction * step;
    for (int axis = 0; axis < 3; ++axis)
        if (result[axis] == point[axis] && direction[axis] != 0.0)
            result[axis] = std::nextafter(point[axis], direction[axis] > 0.0 ? HUGE_VAL : -HUGE_VAL);
    return result;
}

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

// Shift only the geometry query; subtract it before comparing physical free-flight distances.
template<typename Find, typename Exits>
SubsurfaceInternalBoundary FindSubsurfaceInternalBoundary(const Vector3d& point, const Vector3d& direction,
    TraceTicket& ticket, double shift, Find&& find, Exits&& exits)
{
    SubsurfaceInternalRootQuery query(point, direction, shift);
    Ray ray(ticket, query.origin, direction, Ray::SubsurfaceRay);
    ray.SetRootQuery(&query);
    for (;;)
    {
        Intersection hit;
        if (!find(hit, ray, query))
            throw std::runtime_error("Internal subsurface light requires an enclosing material boundary");
        Vector3d normal;
        if (exits(hit, normal))
            return {query.Distance(hit.Depth), hit.IPoint, normal};
        query.Advance(hit.Depth);
    }
}

}
#endif
