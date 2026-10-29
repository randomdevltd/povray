#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/box.h"
#include "core/shape/sphere.h"
#include "core/shape/csg.h"
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/render/trace.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <cassert>
#include <iostream>
#include <random>

using namespace pov;

static SubsurfaceInternalBoundary Boundary(ObjectPtr owner, TraceThreadData& thread, TraceTicket& ticket,
    const Vector3d& point, const Vector3d& direction, double scale, double tolerance = -1.0)
{
    std::vector<double> crossed;
    auto find = [&](Intersection& hit, const Ray& ray, const RayObjectCondition& query)
    {
        const bool found = Find_Intersection(&hit, owner, ray, query, &thread);
        if (!found)
        {
            std::cerr << "Missing boundary from " << point[0] << ',' << point[1] << ',' << point[2]
                << " toward " << direction[0] << ',' << direction[1] << ',' << direction[2] << " scale=" << scale << " roots=";
            for (double depth : crossed) std::cerr << depth << ',';
            std::cerr << '\n';
        }
        return found;
    };
    auto exits = [&](Intersection& hit, Vector3d& normal)
    {
        crossed.push_back(hit.Depth);
        hit.Object->Normal(normal, &hit, &thread);
        normal.normalize();
        if (dot(normal, direction) < 0.0) normal.invert();
        const Vector3d beyond = SubsurfaceInternalProbe(hit.IPoint, direction, tolerance < 0.0 ? 1e-7 * scale : tolerance, scale);
        return !owner->Inside(beyond, &thread);
    };
    return FindSubsurfaceInternalBoundary(point, direction, ticket,
        std::max(2.0 * MIN_ISECT_DEPTH, 4e-6 * scale), find, exits);
}

static void Near(double actual, double expected, double scale)
{ assert(std::abs(actual - expected) < 5e-13 * scale); }

int main()
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    TraceTicket ticket(1, 0.0);
    Box originBox;
    originBox.bounds[0] = Vector3d(-0.001, -0.001, 0.0);
    originBox.bounds[1] = Vector3d(0.001, 0.001, 0.002);
    originBox.Compute_BBox();
    Near(Boundary(&originBox, thread, ticket, Vector3d(0.0), Vector3d(0.0, 0.0, 1.0), 0.001, 0.0).distance, 0.002, 0.001);
    Near(Boundary(&originBox, thread, ticket, Vector3d(0.0), Vector3d(0.0, 0.0, -1.0), 0.001, 0.0).distance, 0.0, 0.001);
    for (double scale : {0.001, 1000000.0})
    {
        CSGIntersection shell(true);
        Sphere* outside = new Sphere;
        Sphere* hole = new Sphere;
        outside->Center = hole->Center = Vector3d(0.0, 0.0, 1.0);
        outside->Radius = 1.0; hole->Radius = 0.5;
        TRANSFORM transform;
        Compute_Scaling_Transform(&transform, Vector3d(1.0, 2.0, 3.0) * scale);
        outside->Transform(&transform); hole->Transform(&transform);
        Set_Flag(hole, INVERTED_FLAG);
        shell.children.push_back(outside); shell.children.push_back(hole);
        shell.Compute_BBox();
        const double near = scale * 1e-10;
        Near(Boundary(&shell, thread, ticket, Vector3d(0.0, 0.0, near), Vector3d(0.0, 0.0, -1.0),
            scale * 6.0, 0.0).distance, near, scale * 6.0);
        Near(Boundary(&shell, thread, ticket, Vector3d(0.0), Vector3d(0.0, 0.0, 1.0),
            scale * 6.0, 0.0).distance, scale * 1.5, scale * 6.0);
    }
    for (double scale : {0.001, 1.0, 1000.0})
    {
        Box box;
        box.bounds[0] = Vector3d(-1.3, 0.2, 0.0) * scale;
        box.bounds[1] = Vector3d(1.3, 2.8, 1.8) * scale;
        box.Compute_BBox();
        const Vector3d front = Vector3d(0.0, 1.5, 0.0) * scale;
        const Vector3d near = front + Vector3d(0.0, 0.0, 2e-8 * scale);
        Ray old(ticket, near, Vector3d(0.0, 0.0, -1.0));
        Intersection dropped;
        if (scale <= 1.0) assert(!Find_Intersection(&dropped, &box, old, &thread));
        Near(Boundary(&box, thread, ticket, near, old.Direction, scale).distance, 2e-8 * scale, scale);
        Near(Boundary(&box, thread, ticket, front, -old.Direction, scale).distance, 1.8 * scale, scale);
        Near(Boundary(&box, thread, ticket, front + Vector3d(0.0, 0.0, 1.8 * scale), old.Direction, scale).distance,
            1.8 * scale, scale);
        Near(Boundary(&box, thread, ticket, front, old.Direction, scale).distance, 0.0, scale);

        Sphere sphere;
        sphere.Center = Vector3d(0.0, 1.5, 0.9) * scale;
        sphere.Radius = 1.2 * scale;
        sphere.Compute_BBox();
        const Vector3d north = sphere.Center + Vector3d(0.0, 0.0, sphere.Radius);
        const Vector3d almost = north - Vector3d(0.0, 0.0, 2e-8 * scale);
        Near(Boundary(&sphere, thread, ticket, almost, Vector3d(0.0, 0.0, 1.0), scale).distance,
            2e-8 * scale, scale);
        Near(Boundary(&sphere, thread, ticket, north, Vector3d(0.0, 0.0, -1.0), scale).distance,
            2.4 * scale, scale);
        const double cosine = 1e-4;
        const Vector3d grazing(std::sqrt(1.0 - cosine * cosine), 0.0, -cosine);
        assert(Boundary(&sphere, thread, ticket, north, grazing, scale).distance > 2e-4 * scale);

        Box transformed;
        TRANSFORM transform;
        Compute_Scaling_Transform(&transform, Vector3d(2.0, 3.0, 4.0) * scale);
        transformed.Transform(&transform);
        Compute_Rotation_Transform(&transform, Vector3d(17.0, 29.0, 11.0));
        transformed.Transform(&transform);
        Compute_Translation_Transform(&transform, Vector3d(0.5, 1.5, 0.9) * scale);
        transformed.Transform(&transform);
        Vector3d worldPoint, worldDirection;
        MTransPoint(worldPoint, Vector3d(0.0, 0.2, -1.0 + 2e-8), transformed.Trans);
        MTransDirection(worldDirection, Vector3d(0.0, 0.0, -1.0), transformed.Trans);
        const double length = worldDirection.length();
        worldDirection /= length;
        Near(Boundary(&transformed, thread, ticket, worldPoint, worldDirection, scale * 8.0).distance,
            2e-8 * length, scale * 8.0);

        CSGIntersection shell(true);
        Box* outside = new Box;
        outside->bounds[0] = Vector3d(-1.0) * scale;
        outside->bounds[1] = Vector3d(1.0) * scale;
        outside->Compute_BBox();
        Box* hole = new Box;
        hole->bounds[0] = Vector3d(-0.5) * scale;
        hole->bounds[1] = Vector3d(0.5) * scale;
        hole->Compute_BBox();
        Set_Flag(hole, INVERTED_FLAG);
        shell.children.push_back(outside);
        shell.children.push_back(hole);
        shell.Compute_BBox();
        Near(Boundary(&shell, thread, ticket, Vector3d(0.0, 0.0, -0.5 - 2e-8) * scale,
            Vector3d(0.0, 0.0, 1.0), scale).distance, 2e-8 * scale, scale);

        std::mt19937_64 random(7231);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        auto draw = [&]() { return unit(random); };
        auto boundary = [&](const Vector3d& p, const Vector3d& d) { return Boundary(&box, thread, ticket, p, d, scale); };
        auto source = [&](const Vector3d& p, const Vector3d&)
            { assert(box.Inside(p, &thread)); return 1.0 / scale; };
        for (int i = 0; i < 10000; ++i)
            assert(std::isfinite(SampleSubsurfaceInternalRadiance(front, Vector3d(0.0, 0.0, 1.0),
                0.2, 2.0, 1.0 / scale, 1.4, boundary, source, draw)));
    }
    const auto slab = [](const Vector3d& p, const Vector3d& d)
        { return SubsurfaceInternalBoundary{(1.0 - p[2]) / d[2], Vector3d(0.0, 0.0, 1.0), Vector3d(0.0, 0.0, 1.0)}; };
    bool moved = false;
    const auto source = [&](const Vector3d& p, const Vector3d&) { moved = p[2] > 0.0; return 1.0; };
    assert(SampleSubsurfaceInternalRadiance(Vector3d(0.0), Vector3d(0.0, 0.0, 1.0),
        1.0, 0.0, 1.0, 1.0, slab, source, []() { return 0.0; }) == 1.0);
    assert(moved);
    std::cout << "Production box/sphere near-wall exits, boundary starts, scales and zero RNG pass\n";
}
