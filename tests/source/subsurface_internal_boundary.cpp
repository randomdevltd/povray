#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/superellipsoid.h"
#include "core/shape/sphere.h"
#include "core/shape/cone.h"
#include "core/shape/csg.h"
#include "core/material/noise.h"
#include "core/material/interior.h"
#include "core/render/trace.h"
#include "core/math/matrix.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;

class InternalTrace final : public Trace
{
public:
    using Trace::Trace;
    using Trace::SubsurfaceInternalBoundaryAt;
};

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
    for (double gap : {0.0, 1.0e-12, 1.0e-9})
    {
        const Vector3d point(0.0), direction = Vector3d(1.0, 2.0, 0.0).normalized();
        auto contains = [&](const Vector3d& p) { return p[0] < 0.0 || p[1] - p[0] < -gap; };
        Vector3d normal(1.0, 0.0, 0.0);
        assert(SubsurfaceBoundaryExit(point, direction, normal, 0.0, 1.0, contains));
        assert(dot(normal, direction) > 0.0);
        normal.invert();
        assert(SubsurfaceBoundaryExit(point, direction, normal, 0.0, 1.0, contains));
        assert(dot(normal, direction) > 0.0);
        assert(!SubsurfaceBoundaryExit(point, -direction, normal, 0.0, 1.0, contains));
    }
    for (double gap : {0.0, 1.0e-12, 1.0e-9})
    {
        const Vector3d point(0.0), direction(1.0, 0.0, 0.0);
        auto contains = [&](const Vector3d& p) { return p[0] < 0.0 || p[0] > gap; };
        Vector3d normal = direction;
        assert(SubsurfaceBoundaryExit(point, direction, normal, 0.0, 1.0, contains) == (gap > 0.0));
    }
    {
        Vector3d normal(1.0, 0.0, 0.0);
        assert(SubsurfaceBoundaryExit(Vector3d(1.0e-7, 0.0, 0.0), normal, normal, 0.0, 1.0,
            [](const Vector3d& p) { return p[0] < 0.0; }));
    }
    CSGMerge junction;
    auto* palm = new Sphere;
    const Vector3d palmScale(0.025, 0.028, 0.009), translation(0.042, 0.48, 0.0);
    Compute_Scaling_Transform(&transform, palmScale);
    palm->Scale(palmScale, &transform);
    Compute_Translation_Transform(&transform, Vector3d(0.0, -0.006, 0.0));
    palm->Translate(Vector3d(0.0, -0.006, 0.0), &transform);
    auto* wrist = new Cone;
    wrist->apex = Vector3d(0.0, -0.026, 0.0); wrist->apex_radius = 0.015;
    wrist->base = Vector3d(0.0, -0.05, 0.001); wrist->base_radius = 0.011;
    wrist->Compute_Cone_Data();
    junction.children = {palm, wrist};
    Compute_Translation_Transform(&transform, translation);
    junction.Translate(translation, &transform);
    junction.interior = std::make_shared<Interior>();
    junction.interior->boundsLow = translation + Vector3d(-0.05, -0.06, -0.012);
    junction.interior->boundsHigh = translation + Vector3d(0.05, 0.06, 0.012);
    for (auto* child : junction.children)
    {
        child->Type |= IS_CHILD_OBJECT;
        child->interior = junction.interior;
        child->Compute_BBox();
    }
    junction.Compute_BBox();
    scene->preparedSets.emplace_back(new PreparedSet);
    scene->preparedSets.front()->objects = {&junction};
    thread.subsurfaceCache = std::make_shared<SubsurfaceCache>();
    Trace::CooperateFunctor cooperate;
    Trace::MediaFunctor media;
    Trace::RadiosityFunctor radiosity;
    InternalTrace trace(scene, &thread, QualityFlags(9), cooperate, media, radiosity);
    const Vector3d point(0.040691503042077144, 0.45369542758626968, -0.0059033021315275259);
    const Vector3d direction(-0.65191523398252094, 0.12302035982341911, -0.74824629552736943);
    const auto exit = trace.SubsurfaceInternalBoundaryAt(point, direction, palm, &junction, ticket);
    assert(std::abs(exit.distance - 0.00038004395261667828) < 1e-14);
    assert(dot(exit.normal, direction) > 0.0);
    assert(junction.Inside(exit.position - direction * 1e-10, &thread));
    assert(!junction.Inside(exit.position + direction * 1e-10, &thread));
    assert(outer.GetRootQuery() == &previous);
    std::cout << "Internal subsurface rounded boundary, CSG seams and query lifetime passed\n";
    Free_Noise_Tables();
}
