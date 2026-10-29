#ifndef POVRAY_CORE_SURFACEGEOMETRY_H
#define POVRAY_CORE_SURFACEGEOMETRY_H

#include "core/configcore.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include "core/math/matrix.h"
#include "core/lighting/emittergeometry.h"
#include "core/lighting/surfaceguide.h"
#include "core/shape/isosurface.h"
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
        object(object), valid(object != nullptr), boundedProposal(proposalBounds != nullptr)
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
        geometry.reset(Copy_Object(object), [](ObjectBase* p) { Destroy_Object(p); });
        MapGeometry(geometry.get(), object);
        TRANSFORM transform;
        const Vector3d translation = -center;
        Compute_Translation_Transform(&transform, translation);
        Translate_Object(geometry.get(), translation, &transform);
        const Vector3d scaling(1.0 / scale);
        Compute_Scaling_Transform(&transform, scaling);
        Scale_Object(geometry.get(), scaling, &transform);
        normalizedSize = size / scale;
        projected[0] = normalizedSize[1] * normalizedSize[2];
        projected[1] = normalizedSize[0] * normalizedSize[2];
        projected[2] = normalizedSize[0] * normalizedSize[1];
        totalProjection = projected[0] + projected[1] + projected[2];
        valid = valid && IsFiniteIntersectionDepth(totalProjection) && totalProjection > 0.0;
    }

    bool IsValid() const { return valid; }
    DBL ExactArea() const
    {
        PrimitiveEmitterGeometry primitive;
        if (!valid || boundedProposal || !primitive.Initialize(object)) return 0.0;
        const DBL area = 4.0 * primitive.MeanProjectedArea();
        return IsFiniteIntersectionDepth(area) && area > 0.0 ? area : 0.0;
    }
    Vector3d Centre() const { return lower + size * 0.5; }
    DBL Radius() const { return std::sqrt(size.lengthSqr()) * 0.5; }

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
        const DBL margin = 1.0;
        ray.Origin = normalizedSize * -0.5;
        ray.Origin[axis] -= margin;
        ray.Origin[b] += Unit(u) * normalizedSize[b];
        ray.Origin[c] += Unit(v) * normalizedSize[c];
        ray.Direction = Vector3d(0.0);
        ray.Direction[axis] = 1.0;
        const Vector3d worldOrigin = center + ray.Origin * scale;
        const bool found = SelectRoot(ray, thread, rootU, 0.0, normalizedSize[axis] + 2.0 * margin, sample);
        ray.Origin = worldOrigin;
        if (!found) return false;
        const DBL sum = std::fabs(sample.normal[0]) + std::fabs(sample.normal[1]) + std::fabs(sample.normal[2]);
        if (!(sum > 0.0)) return false;
        sample.areaWeight = totalProjection * scale * scale * sample.rootCount / sum;
        return IsFiniteIntersectionDepth(sample.areaWeight);
    }

    bool SampleToward(const Vector3d& origin, Ray& ray, TraceThreadData* thread,
                      DBL u, DBL v, DBL rootU, SurfaceGeometrySample& sample,
                      const SurfaceAngularGuide* guide = nullptr) const
    {
        sample = SurfaceGeometrySample();
        if (!valid) return false;
        double angularPdf;
        if (guide != nullptr) ray.Direction = guide->Sample(origin, Unit(u), Unit(v), angularPdf, thread->surfaceAngularGuideCache);
        else ray.Direction = SurfaceAngularGuide::SampleSphere(center - origin, Radius(), Unit(u), Unit(v), angularPdf);
        if (!(angularPdf > 0.0) || !std::isfinite(angularPdf)) return false;
        const Vector3d normalizedOrigin = (origin - center) / scale;
        const DBL back = dot(normalizedOrigin, ray.Direction) + 2.0;
        ray.Origin = normalizedOrigin - ray.Direction * back;
        const bool found = SelectRoot(ray, thread, rootU, back, HUGE_VAL, sample);
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

    bool SelectRoot(Ray& ray, TraceThreadData* thread, DBL rootU, DBL minDepth, DBL maxDepth, SurfaceGeometrySample& sample) const
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
        geometry->All_Intersections(ray, stack, thread);
        std::vector<Intersection> roots;
        while (!stack->empty())
        {
            const Intersection& hit = stack->top();
            if (hit.Object != nullptr && !windows.count(hit.Object) && IsFiniteIntersectionDepth(hit.Depth) &&
                hit.Depth > minDepth && hit.Depth <= maxDepth)
                roots.push_back(hit);
            stack->pop();
        }
        if (roots.empty()) return false;
        sample.rootCount = roots.size();
        sample.intersection = roots[std::min(roots.size() - 1, static_cast<std::size_t>(Unit(rootU) * roots.size()))];
        Intersection& hit = sample.intersection;
        hit.Object->Normal(sample.normal, &hit, thread);
        Vector3d face;
        if (hit.Object->FaceNormal(face, &hit))
        {
            if (dot(face, sample.normal) < 0.0) face.invert();
            sample.normal = face;
        }
        if (Test_Flag(hit.Object, INVERTED_FLAG)) sample.normal.invert();
        const DBL length2 = sample.normal.lengthSqr();
        if (!IsFiniteIntersectionDepth(length2) || !(length2 > 0.0)) return false;
        sample.normal /= std::sqrt(length2);
        hit.Depth = (hit.Depth - minDepth) * scale;
        hit.IPoint = center + hit.IPoint * scale;
        hit.Object = originals.at(hit.Object);
        if (hit.Csg != nullptr) hit.Csg = originals.at(hit.Csg);
        return true;
    }

    void MapGeometry(ObjectPtr clone, ObjectPtr original, bool window = false)
    {
        originals[clone] = original;
        if (window) windows.insert(clone);
        if (IsoSurface* iso = dynamic_cast<IsoSurface*>(clone))
            iso->max_trace = std::numeric_limits<IsosurfaceMaxTrace>::max();
        // A portal body is a window, not a surface: it neither emits nor counts as a root.
        if (Portal* portal = dynamic_cast<Portal*>(clone))
            MapGeometry(portal->body, static_cast<Portal*>(original)->body, true);
        if (CompoundObject* compound = dynamic_cast<CompoundObject*>(clone))
        {
            CompoundObject* source = dynamic_cast<CompoundObject*>(original);
            for (std::size_t i = 0; i < compound->children.size(); ++i)
                MapGeometry(compound->children[i], source->children[i], window);
        }
        for (std::size_t i = 0; i < clone->Bound.size(); ++i) MapGeometry(clone->Bound[i], original->Bound[i]);
        for (std::size_t i = 0; i < clone->Clip.size(); ++i) MapGeometry(clone->Clip[i], original->Clip[i]);
    }

    ObjectPtr object;
    std::shared_ptr<ObjectBase> geometry;
    std::unordered_map<ObjectPtr, ObjectPtr> originals;
    std::unordered_set<ObjectPtr> windows;
    Vector3d center, normalizedSize;
    DBL scale = 1.0;
    bool valid;
    bool boundedProposal;
    Vector3d lower, size;
    DBL projected[3] = {0.0, 0.0, 0.0};
    DBL totalProjection = 0.0;
};

}
#endif
