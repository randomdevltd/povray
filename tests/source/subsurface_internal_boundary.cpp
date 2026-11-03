#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/superellipsoid.h"
#include "core/shape/sphere.h"
#include "core/shape/csg.h"
#include "core/material/noise.h"
#include "core/math/matrix.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    TraceTicket ticket(1, 0.0);
    Superellipsoid body;
    body.Power = Vector3d(2.0 / 0.18, 1.0, 2.0 / 0.18);
    TRANSFORM transform;
    const Vector3d scale(0.037, 0.045, 0.006);
    Compute_Scaling_Transform(&transform, scale);
    body.Scale(scale, &transform);
    body.Compute_BBox();
    body.Type |= IS_CHILD_OBJECT;
    struct Query final : RayRootQuery { bool Admit(ConstObjectPtr, double) const override { return true; } } previous;
    Ray outer(ticket);
    outer.SetRootQuery(&previous);
    struct Case { Vector3d point, direction; };
    const Case cases[] = {
        {Vector3d(-0.036278719967199564, 0.02262937989364305, -0.0051814553919980926),
         Vector3d(-0.23770568504688971, -0.69886309688862946, 0.67460090357456737)},
        {Vector3d(0.036999999999999755, 0.0029463307359533248, -7.8795925460267008e-06),
         Vector3d(-0.029659129710760068, -0.45773870763682362, 0.88859192633951545)}
    };
    for (const auto& test : cases)
    {
        const auto& point = test.point;
        const auto& direction = test.direction;
        auto find = [&](Intersection& hit, const Ray& ray, const RayObjectCondition& condition)
        { return Find_Intersection(&hit, &body, ray, condition, &thread); };
        auto exits = [&](Intersection& hit, Vector3d& normal)
        {
            body.Normal(normal, &hit, &thread);
            normal.normalize();
            return dot(normal, direction) > 0.0;
        };
        const auto boundary = FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, find, exits);
        auto implicit = [&](double distance)
        {
            const Vector3d q = point + direction * distance;
            double sum = 0.0;
            for (int axis = 0; axis < 3; ++axis) sum += std::pow(std::abs(q[axis] / scale[axis]), 2.0 / 0.18);
            return sum - 1.0;
        };
        double low = 0.002, high = 0.02;
        for (int i = 0; i < 60; ++i)
        {
            const double middle = 0.5 * (low + high);
            (implicit(middle) < 0.0 ? low : high) = middle;
        }
        assert(std::abs(boundary.distance - 0.5 * (low + high)) < 1e-9);
        Ray ordinary(ticket, point + direction * 0.001, direction);
        Intersection ordinaryHit;
        assert(Find_Intersection(&ordinaryHit, &body, ordinary, &thread));
        assert(std::abs(ordinaryHit.Depth + 0.001 - 0.5 * (low + high)) < 1e-9);
        const double residual = std::pow(implicit(ordinaryHit.Depth + 0.001) + 1.0, 0.18 / 2.0) - 1.0;
        const double radialError = (ordinaryHit.IPoint - ordinaryHit.IPoint / (1.0 + residual)).length();
        assert(radialError <= body.SurfaceTolerance());
        assert(outer.GetRootQuery() == &previous);
        CSGIntersection difference(true);
        auto* rounded = static_cast<Superellipsoid*>(body.Copy());
        auto* cavity = new Sphere;
        cavity->Center = point + direction * 0.001;
        cavity->Radius = 0.0003;
        cavity->Type |= IS_CHILD_OBJECT;
        Set_Flag(cavity, INVERTED_FLAG);
        cavity->Compute_BBox();
        difference.children = {rounded, cavity};
        difference.Compute_BBox();
        auto findDifference = [&](Intersection& hit, const Ray& ray, const RayObjectCondition& condition)
        { return Find_Intersection(&hit, &difference, ray, condition, &thread); };
        auto leaveDifference = [&](Intersection& hit, Vector3d& normal)
        {
            hit.Object->Normal(normal, &hit, &thread);
            normal.normalize();
            if (difference.Inside(hit.IPoint + normal * 1e-9, &thread)) normal.invert();
            return dot(normal, direction) > 0.0;
        };
        const auto cut = FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, findDifference, leaveDifference);
        assert(std::abs(cut.distance - 0.0007) < 1e-10);
        const auto afterCut = FindSubsurfaceInternalBoundary(point + direction * 0.0015, direction, ticket, 0.0002,
            findDifference, leaveDifference);
        assert(std::abs(afterCut.distance - boundary.distance + 0.0015) < 1e-9);
        assert(outer.GetRootQuery() == &previous);
        bool caught = false;
        try
        {
            FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002,
                [](Intersection&, const Ray&, const RayObjectCondition&) -> bool { throw std::runtime_error("query failure"); }, exits);
        }
        catch (const std::runtime_error&) { caught = true; }
        assert(caught && outer.GetRootQuery() == &previous);
        caught = false;
        try
        {
            FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, find,
                [](Intersection&, Vector3d&) -> bool { throw std::runtime_error("boundary failure"); });
        }
        catch (const std::runtime_error&) { caught = true; }
        assert(caught && outer.GetRootQuery() == &previous);
    }
    std::cout << "Internal subsurface rounded boundary and query lifetime passed\n";
    Free_Noise_Tables();
}
