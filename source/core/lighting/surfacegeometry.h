#ifndef POVRAY_CORE_SURFACEGEOMETRY_H
#define POVRAY_CORE_SURFACEGEOMETRY_H

#include "core/configcore.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <limits>
#include <unordered_set>
#include "core/lighting/surfaceguide.h"
#include "core/lighting/surfacemap.h"
#include "core/shape/mesh.h"
#include "core/shape/portal.h"
#include "core/math/intersectiondepth.h"
#include "core/render/ray.h"
#include "core/scene/object.h"
#include "core/scene/tracethreaddata.h"

namespace pov
{

struct SurfaceGeometrySample final
{
    Intersection intersection;
    Vector3d normal;
    DBL areaWeight = 0.0;
    DBL solidAngleWeight = 0.0;
    std::size_t rootCount = 0;
};

class SurfaceGeometrySampler final
{
public:
    explicit SurfaceGeometrySampler(ObjectPtr object, const BoundingBox* proposalBounds = nullptr) :
        object(object), valid(object != nullptr)
    {
        if (!valid) return;
        const BoundingBox& bounds = proposalBounds != nullptr ? *proposalBounds : object->BBox;
        lower = Vector3d(bounds.lowerLeft);
        size = Vector3d(bounds.size);
        valid = bounds.finite;
        for (int a = 0; a < 3; ++a)
            valid = valid && IsFiniteIntersectionDepth(lower[a]) && IsFiniteIntersectionDepth(size[a]) &&
                    size[a] >= 0.0;
        scale = std::max(size[0], std::max(size[1], size[2]));
        if (!valid || !(scale > 0.0)) { valid = false; return; }
        center = lower + size * 0.5;
        MapGeometry(object);
        projected[0] = size[1] * size[2];
        projected[1] = size[0] * size[2];
        projected[2] = size[0] * size[1];
        totalProjection = projected[0] + projected[1] + projected[2];
        valid = valid && IsFiniteIntersectionDepth(totalProjection) && totalProjection > 0.0;
    }

    bool IsValid() const { return valid; }
    // A deferred mesh builds itself on first use, which writes its data, so it is built before parallel use.
    void ResolveDeferred() const
    {
        for (const Mesh* mesh : meshes) mesh->Resolve();
    }
    Vector3d Centre() const { return lower + size * 0.5; }
    DBL Radius() const { return std::sqrt(size.lengthSqr()) * 0.5; }
    const Vector3d& Lower() const { return lower; }
    const Vector3d& Size() const { return size; }

    // Random variates are independent and uniform on [0,1).
    bool SampleArea(Ray& ray, TraceThreadData* thread, DBL axisU, DBL u, DBL v, DBL rootU,
                    SurfaceGeometrySample& sample) const
    {
        sample = SurfaceGeometrySample();
        if (!valid) return false;
        DBL selector = Unit(axisU) * totalProjection;
        int axis = 0;
        while (axis < 2 && selector >= projected[axis]) selector -= projected[axis++];
        if (!(projected[axis] > 0.0)) return false;
        const int b = (axis + 1) % 3, c = (axis + 2) % 3;
        // The ray starts one box extent outside the bounds, so no root lies at its origin.
        ray.Origin = lower;
        ray.Origin[axis] -= scale;
        ray.Origin[b] += Unit(u) * size[b];
        ray.Origin[c] += Unit(v) * size[c];
        ray.Direction = Vector3d(0.0);
        ray.Direction[axis] = 1.0;
        if (!SelectRoot(ray, thread, rootU, 0.0, size[axis] + 2.0 * scale, sample)) return false;
        const DBL sum = std::fabs(sample.normal[0]) + std::fabs(sample.normal[1]) + std::fabs(sample.normal[2]);
        if (!(sum > 0.0)) return false;
        sample.areaWeight = totalProjection * sample.rootCount / sum;
        return IsFiniteIntersectionDepth(sample.areaWeight);
    }

    bool SampleToward(const Vector3d& origin, Ray& ray, TraceThreadData* thread,
                      DBL u, DBL v, DBL rootU, SurfaceGeometrySample& sample,
                      const SurfaceAngularGuide* guide = nullptr, bool facing = false) const
    {
        sample = SurfaceGeometrySample();
        if (!valid) return false;
        double angularPdf;
        if (guide != nullptr) ray.Direction = guide->Sample(origin, Unit(u), Unit(v), angularPdf, thread->surfaceAngularGuideCache);
        else ray.Direction = SurfaceAngularGuide::SampleSphere(center - origin, Radius(), Unit(u), Unit(v), angularPdf);
        if (!(angularPdf > 0.0) || !std::isfinite(angularPdf)) return false;
        // The ray starts two box extents behind the centre, so no root lies at its origin.
        const DBL back = dot(origin - center, ray.Direction) + 2.0 * scale;
        ray.Origin = origin - ray.Direction * back;
        // A receiver on this surface must not draw its own root, found within that part's error of the receiver.
        const DBL own = 64.0 * std::numeric_limits<DBL>::epsilon() * (back + MaxAbs(origin));
        const bool found = SelectRoot(ray, thread, rootU, back, HUGE_VAL, sample, own, facing);
        ray.Origin = origin;
        if (!found) return false;
        const DBL cosine = std::fabs(dot(sample.normal, ray.Direction));
        if (!(cosine > 0.0)) return false;
        sample.solidAngleWeight = sample.rootCount / angularPdf;
        sample.areaWeight = sample.solidAngleWeight * sample.intersection.Depth * sample.intersection.Depth / cosine;
        return IsFiniteIntersectionDepth(sample.areaWeight);
    }

private:
    static DBL Unit(DBL u) { return std::max(DBL(0.0), std::min(std::nextafter(DBL(1.0), DBL(0.0)), u)); }
    static DBL MaxAbs(const Vector3d& v) { return std::max(std::fabs(v[X]), std::max(std::fabs(v[Y]), std::fabs(v[Z]))); }

    // With `facing`, only roots whose surface faces back along the ray are candidates, so rootCount counts those.
    bool SelectRoot(Ray& ray, TraceThreadData* thread, DBL rootU, DBL minDepth, DBL maxDepth, SurfaceGeometrySample& sample,
                    DBL skip = 0.0, bool facing = false) const
    {
        std::vector<Intersection> roots;
        // with a receiver, roots a rounding behind it may be the surface touching it
        CollectRoots(ray, thread, skip > 0.0 ? 0.0 : minDepth, maxDepth, roots);
        if (facing || skip > 0.0)
        {
            // The receiver's own point is skipped, within skip / cos^2 as a grazing root is solved less exactly. Where another
            // surface touches it, facing it, that surface covers the receiver's side and is kept: a contact lights as an area.
            std::vector<char> near(roots.size(), 0), along(roots.size(), 0);
            bool touched = false, own = false;
            for (std::size_t i = 0; i < roots.size(); ++i)
            {
                Vector3d normal;
                if (!surface_map::OrientedNormal(thread, roots[i], normal)) { near[i] = 2; continue; }
                const DBL cosine = dot(normal, ray.Direction), depth = roots[i].Depth - minDepth;
                const DBL tolerance = std::max(skip, roots[i].Object->SurfaceTolerance());
                along[i] = cosine < 0.0 ? 0 : 1;
                near[i] = skip > 0.0 && (std::fabs(depth) <= tolerance || (depth > 0.0 && depth * cosine * cosine <= tolerance));
                if (depth < 0.0 && !near[i]) { near[i] = 2; continue; }
                touched = touched || (near[i] && !along[i]);
                own = own || (near[i] && along[i]);
            }
            const bool contact = touched && own;
            std::size_t kept = 0;
            for (std::size_t i = 0; i < roots.size(); ++i)
                if (near[i] != 2 && !(facing && along[i]) && (!near[i] || (contact && !along[i])))
                    roots[kept++] = roots[i];
            roots.resize(kept);
        }
        if (roots.empty()) return false;
        sample.rootCount = roots.size();
        sample.intersection = roots[std::min(roots.size() - 1, static_cast<std::size_t>(Unit(rootU) * roots.size()))];
        return Resolve(thread, minDepth, sample);
    }

    void CollectRoots(Ray& ray, TraceThreadData* thread, DBL minDepth, DBL maxDepth, std::vector<Intersection>& roots,
                      DBL skip = 0.0) const
    {
        IStack stack(thread->stackPool);
        struct CompleteSurfaceQuery final : RayRootQuery
        {
            bool Admit(ConstObjectPtr, double depth) const override { return IsFiniteIntersectionDepth(depth); }
            bool AllRoots() const override { return true; }
        } complete;
        const RayRootQueryDisabled noRootFilter(ray);
        ray.SetRootQuery(&complete);
        const IsoShadowWindow exactHits(thread, 0.0, 0.0);
        ray.SetFlags(Ray::OtherRay);
        object->All_Intersections(ray, stack, thread);
        while (!stack->empty())
        {
            const Intersection& hit = stack->top();
            if (hit.Object != nullptr && !windows.count(hit.Object) && IsFiniteIntersectionDepth(hit.Depth) &&
                hit.Depth > minDepth + (skip > 0.0 ? std::max(skip, hit.Object->SurfaceTolerance()) : 0.0) &&
                hit.Depth <= maxDepth)
                roots.push_back(hit);
            stack->pop();
        }
    }

    // Gives sample.intersection its unit normal and its depth from minDepth.
    bool Resolve(TraceThreadData* thread, DBL minDepth, SurfaceGeometrySample& sample) const
    {
        Intersection& hit = sample.intersection;
        if (!surface_map::OrientedNormal(thread, hit, sample.normal)) return false;
        hit.Depth -= minDepth;
        return true;
    }

    void MapGeometry(ObjectPtr node, bool window = false)
    {
        if (window) windows.insert(node);
        if (const Mesh* mesh = dynamic_cast<const Mesh*>(node)) meshes.push_back(mesh);
        // A portal body is a window, not a surface: it neither emits nor counts as a root.
        if (Portal* portal = dynamic_cast<Portal*>(node)) MapGeometry(portal->body, true);
        if (CompoundObject* compound = dynamic_cast<CompoundObject*>(node))
            for (ObjectPtr child : compound->children) MapGeometry(child, window);
        for (ObjectPtr bound : node->Bound) MapGeometry(bound);
        for (ObjectPtr clip : node->Clip) MapGeometry(clip);
    }

    ObjectPtr object;
    std::vector<const Mesh*> meshes;
    std::unordered_set<ObjectPtr> windows;
    Vector3d center;
    DBL scale = 1.0;
    bool valid;
    Vector3d lower, size;
    DBL projected[3] = {0.0, 0.0, 0.0};
    DBL totalProjection = 0.0;
};

}
#endif
