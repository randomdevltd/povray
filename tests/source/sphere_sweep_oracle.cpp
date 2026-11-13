// Randomized oracle for sphere sweeps of every spline kind: the inside test against a dense search of the spheres, the
// crossings a precise ray query reports against the inside test along the ray, and the exit a buried light's path finds
// from the bare sweep and from the sweep in an intersection, a difference and a merge inside a difference.
#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/sphere.h"
#include "core/shape/spheresweep.h"
#include "core/shape/csg.h"
#include "core/math/matrix.h"
#include "core/material/noise.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using namespace pov;

namespace
{

std::mt19937_64 engine(20261110);
double Uniform(double low = 0.0, double high = 1.0) { return low + (high - low) * std::generate_canonical<double, 53>(engine); }
int Pick(int count) { return int(Uniform() * count) % count; }
Vector3d Direction()
{
    for (;;)
    {
        const Vector3d v(Uniform(-1.0, 1.0), Uniform(-1.0, 1.0), Uniform(-1.0, 1.0));
        const double length = v.length();
        if (length > 0.1 && length <= 1.0) return v / length;
    }
}

int failures = 0, checks = 0;
void Fail(const char* what, const std::string& where)
{
    if (++failures <= 40) std::printf("FAIL %s: %s\n", what, where.c_str());
}

// The sweep's spheres straight from its control spheres, in object space.
struct Reference final
{
    int kind;
    std::vector<SPHSWEEP_SPH> spheres;
    double size;
    int Segments() const { return int(spheres.size()) - (kind == LINEAR_SPHERE_SWEEP ? 1 : 3); }
    void Weights(long double u, long double* w) const
    {
        if (kind == LINEAR_SPHERE_SWEEP) { w[0] = 1.0L - u; w[1] = u; w[2] = w[3] = 0.0L; }
        else if (kind == CATMULL_ROM_SPLINE_SPHERE_SWEEP)
        {
            w[0] = 0.5L * (-u + 2.0L * u * u - u * u * u);
            w[1] = 0.5L * (2.0L - 5.0L * u * u + 3.0L * u * u * u);
            w[2] = 0.5L * (u + 4.0L * u * u - 3.0L * u * u * u);
            w[3] = 0.5L * (-u * u + u * u * u);
        }
        else
        {
            w[0] = (1.0L - 3.0L * u + 3.0L * u * u - u * u * u) / 6.0L;
            w[1] = (4.0L - 6.0L * u * u + 3.0L * u * u * u) / 6.0L;
            w[2] = (1.0L + 3.0L * u + 3.0L * u * u - 3.0L * u * u * u) / 6.0L;
            w[3] = u * u * u / 6.0L;
        }
    }
    void At(int segment, long double u, long double* centre, long double& radius) const
    {
        long double w[4];
        Weights(u, w);
        centre[0] = centre[1] = centre[2] = radius = 0.0L;
        for (int k = 0; k < (kind == LINEAR_SPHERE_SWEEP ? 2 : 4); ++k)
        {
            for (int axis = 0; axis < 3; ++axis) centre[axis] += w[k] * spheres[segment + k].Center[axis];
            radius += w[k] * spheres[segment + k].Radius;
        }
    }
    long double Gap(int segment, long double u, const Vector3d& p) const
    {
        long double centre[3], radius, sum = 0.0L;
        At(segment, u, centre, radius);
        for (int axis = 0; axis < 3; ++axis) sum += (p[axis] - centre[axis]) * (p[axis] - centre[axis]);
        return std::sqrt(sum) - std::abs(radius);
    }
    // Least distance from p to a sphere's surface over the whole sweep, negative inside.
    long double Gap(const Vector3d& p) const
    {
        const int steps = 256;
        long double least = HUGE_VALL;
        for (int segment = 0; segment < Segments(); ++segment)
            for (int k = 0; k <= steps; ++k)
            {
                const long double here = Gap(segment, static_cast<long double>(k) / steps, p);
                if (here > least + 4.0L * size / steps) continue;
                long double a = std::max(0.0L, static_cast<long double>(k - 1) / steps), b = std::min(1.0L, static_cast<long double>(k + 1) / steps);
                for (int i = 0; i < 60; ++i)
                {
                    const long double m1 = a + (b - a) / 3.0L, m2 = b - (b - a) / 3.0L;
                    if (Gap(segment, m1, p) < Gap(segment, m2, p)) b = m2; else a = m1;
                }
                least = std::min(least, std::min(here, Gap(segment, 0.5L * (a + b), p)));
            }
        return least;
    }
};

struct Shape final
{
    Reference reference;
    SphereSweep* sweep;
    std::string name;
};

SphereSweep* Build(const Reference& reference, double tolerance)
{
    auto* sweep = new SphereSweep;
    sweep->Interpolation = reference.kind;
    sweep->Num_Modeling_Spheres = int(reference.spheres.size());
    sweep->Modeling_Sphere = static_cast<SPHSWEEP_SPH*>(POV_MALLOC(reference.spheres.size() * sizeof(SPHSWEEP_SPH), "sweep spheres"));
    for (size_t i = 0; i < reference.spheres.size(); ++i) sweep->Modeling_Sphere[i] = reference.spheres[i];
    if (tolerance > 0.0) sweep->Depth_Tolerance = tolerance;
    sweep->Compute();
    sweep->Compute_BBox();
    sweep->Type |= IS_CHILD_OBJECT;
    return sweep;
}

// family: 0 thin, 1 fat, 2 stalk swelling into a head, 3 sharp bends with repeated points, 4 many segments, 5 tips of
// zero or tiny radius, 6 four identical points and radii, a constant segment; unit is the size of a coordinate.
Shape Random(int kind, int family, double unit, int placing, int index)
{
    Shape shape;
    Reference& reference = shape.reference;
    reference.kind = kind;
    const int least = (kind == LINEAR_SPHERE_SWEEP) ? 2 : 4;
    int count = least + Pick(5);
    if (family == 4) count = 10 + Pick(6);
    Vector3d at(Uniform(-0.5, 0.5), Uniform(-0.5, 0.5), Uniform(-0.5, 0.5)), heading = Direction();
    if (family == 6) count = std::max(count, 4);
    const int still = (family == 6) ? Pick(count - 3) : count;
    for (int i = 0; i < count; ++i)
    {
        SPHSWEEP_SPH sphere;
        if (family == 6 && i > still && i <= still + 3)
        {
            reference.spheres.push_back(reference.spheres.back());
            continue;
        }
        switch (family)
        {
            case 0: sphere.Center = Vector3d(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)); sphere.Radius = Uniform(0.002, 0.03); break;
            case 1: sphere.Center = Vector3d(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)); sphere.Radius = Uniform(0.2, 0.7); break;
            case 2:
                heading = (heading + Direction() * 0.35).normalized();
                at += heading * ((i >= count - 2) ? Uniform(0.01, 0.08) : Uniform(0.15, 0.4));
                sphere.Center = at;
                sphere.Radius = (i >= count - 2) ? Uniform(0.12, 0.3) : Uniform(0.02, 0.06);
                break;
            case 3:
                if (i > 0 && Pick(4) == 0) at += Direction() * (Pick(2) ? 0.0 : 1e-9);
                else at += Vector3d((i % 2) ? 0.6 : -0.6, Uniform(-0.05, 0.05), Uniform(-0.05, 0.05)) + heading * 0.05;
                sphere.Center = at;
                sphere.Radius = Pick(2) ? Uniform(0.005, 0.02) : Uniform(0.1, 0.4);
                break;
            case 4:
                heading = (heading + Direction() * 0.8).normalized();
                at += heading * Uniform(0.05, 0.3);
                sphere.Center = at;
                sphere.Radius = Uniform(0.005, 0.15);
                break;
            case 6: sphere.Center = Vector3d(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)); sphere.Radius = Uniform(0.01, 0.4); break;
            default:
                sphere.Center = Vector3d(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1));
                sphere.Radius = (i == 0 || i == count - 1 || Pick(3) == 0) ? (Pick(2) ? 0.0 : 1e-4) : Uniform(0.01, 0.2);
                break;
        }
        reference.spheres.push_back(sphere);
    }
    // placing 0 holds the unit in the coordinates; 1 and 2 leave the sweep unit sized under a transform
    const double baked = (placing == 0) ? unit : 1.0;
    Vector3d low(HUGE_VAL), high(-HUGE_VAL);
    for (auto& sphere : reference.spheres)
    {
        sphere.Center *= baked;
        sphere.Radius *= baked;
        low = min(low, sphere.Center - Vector3d(sphere.Radius));
        high = max(high, sphere.Center + Vector3d(sphere.Radius));
    }
    reference.size = (high - low).length();
    const double tolerances[] = {0.0, 0.0, 1.5e-6, 0.01};
    shape.sweep = Build(reference, tolerances[Pick(4)]);
    if (placing != 0)
    {
        TRANSFORM transform;
        const Vector3d scale = (placing == 1) ? Vector3d(unit) : Vector3d(unit, 0.4 * unit, 2.5 * unit);
        Compute_Scaling_Transform(&transform, scale);
        shape.sweep->Scale(scale, &transform);
        Compute_Axis_Rotation_Transform(&transform, Direction(), Uniform(0.0, 6.0));
        shape.sweep->Rotate(Vector3d(0.0), &transform);
        const Vector3d shift = Direction() * unit * Uniform(0.0, 3.0);
        Compute_Translation_Transform(&transform, shift);
        shape.sweep->Translate(shift, &transform);
    }
    // a translation or an even scale moves the control spheres themselves
    Vector3d lowest(HUGE_VAL), highest(-HUGE_VAL);
    for (size_t i = 0; i < reference.spheres.size(); ++i)
    {
        reference.spheres[i] = shape.sweep->Modeling_Sphere[i];
        lowest = min(lowest, reference.spheres[i].Center - Vector3d(std::abs(reference.spheres[i].Radius)));
        highest = max(highest, reference.spheres[i].Center + Vector3d(std::abs(reference.spheres[i].Radius)));
    }
    reference.size = (highest - lowest).length();
    char text[160];
    std::snprintf(text, sizeof(text), "kind %d family %d unit %g placing %d #%d", kind, family, unit, placing, index);
    shape.name = text;
    return shape;
}

Vector3d World(const SphereSweep& sweep, const Vector3d& p)
{
    Vector3d result(p);
    if (sweep.Trans != nullptr) MTransPoint(result, p, sweep.Trans);
    return result;
}

Vector3d WorldDirection(const SphereSweep& sweep, const Vector3d& d)
{
    Vector3d result(d);
    if (sweep.Trans != nullptr) MTransDirection(result, d, sweep.Trans);
    return result.normalized();
}

// A point in object space near the spine at `fraction` of the local radius, with the spine's heading there.
Vector3d Near(const Reference& reference, double fraction, Vector3d* tangent = nullptr)
{
    const int segment = Pick(reference.Segments());
    const double ends[] = {0.0, 1.0, 1e-9, 1.0 - 1e-9, 0.5};
    const long double u = Pick(3) ? Uniform() : ends[Pick(5)];
    long double centre[3], radius, ahead[3], other;
    reference.At(segment, u, centre, radius);
    reference.At(segment, u + ((u < 0.5L) ? 1e-4L : -1e-4L), ahead, other);
    const Vector3d aside = Direction();
    Vector3d result;
    for (int axis = 0; axis < 3; ++axis) result[axis] = double(centre[axis] + aside[axis] * std::abs(radius) * fraction);
    if (tangent != nullptr)
    {
        for (int axis = 0; axis < 3; ++axis) (*tangent)[axis] = double(ahead[axis] - centre[axis]);
        *tangent = (tangent->length() > 0.0) ? tangent->normalized() : Direction();
    }
    return result;
}

std::string Describe(const Shape& shape, const Vector3d& p, const Vector3d& d, const std::string& more)
{
    char text[400];
    std::snprintf(text, sizeof(text), "%s from <%.17g, %.17g, %.17g> along <%.17g, %.17g, %.17g> %s", shape.name.c_str(),
                  p[0], p[1], p[2], d[0], d[1], d[2], more.c_str());
    return text;
}

double Fraction()
{
    const double near[] = {0.999, 1.0 - 1e-6, 1.0 - 1e-9, 1e-3, 0.0};
    return Pick(3) ? Uniform(0.0, 0.98) : near[Pick(5)];
}

void CheckInside(const Shape& shape, TraceThreadData& thread, int count)
{
    const Reference& reference = shape.reference;
    for (int i = 0; i < count; ++i)
    {
        const double fractions[] = {1.0 - 1e-6, 1.0 + 1e-6, 0.999, 1.001, 0.0};
        const Vector3d p = Near(reference, Pick(2) ? Uniform(0.0, 1.6) : fractions[Pick(5)]);
        const long double gap = reference.Gap(p);
        if (std::abs(gap) < 1e-9L * reference.size) continue;
        ++checks;
        if (shape.sweep->Inside(World(*shape.sweep, p), &thread) != (gap < 0.0L))
            Fail("inside test", Describe(shape, World(*shape.sweep, p), Vector3d(0.0), "gap " + std::to_string(double(gap / reference.size))));
    }
}

// A start in the material in world space and a heading, some along the spine and some grazing the wall.
bool Start(const Shape& shape, Vector3d& point, Vector3d& direction)
{
    const Reference& reference = shape.reference;
    Vector3d tangent;
    const Vector3d p = Near(reference, Fraction(), &tangent);
    if (!(reference.Gap(p) < -1e-9L * reference.size)) return false;
    Vector3d d = Direction();
    const double tilt[] = {0.0, 1e-9, 1e-6, 1e-3, 0.05};
    switch (Pick(4))
    {
        case 0: d = (tangent + d * tilt[Pick(5)]).normalized(); break;
        case 1: d = (cross(tangent, d).normalized() + tangent * tilt[Pick(5)]).normalized(); break;
        default: break;
    }
    if (Pick(2)) d = -d;
    point = World(*shape.sweep, p);
    direction = WorldDirection(*shape.sweep, d);
    return true;
}

struct Extent final
{
    double extent, shift, reach, slack;
};

Extent Measure(ConstObjectPtr object, const Vector3d& point, const Vector3d& direction)
{
    Extent result;
    const Vector3d low(object->BBox.lowerLeft), high = low + Vector3d(object->BBox.size);
    result.extent = (high - low).length();
    const double coordinate = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    result.shift = std::max(2.0 * MIN_ISECT_DEPTH, std::max(1.0e-5 * coordinate, 4.0e-6 * result.extent));
    result.reach = SubsurfaceBoundsReach(point, direction, low, high);
    result.slack = 1e-8 * std::max(result.extent, coordinate);
    return result;
}

// The crossings a precise query reports must reproduce the inside test everywhere along the ray.
void CheckCrossings(const Shape& shape, const Vector3d& point, const Vector3d& direction, TraceThreadData& thread, TraceTicket& ticket)
{
    SphereSweep& sweep = *shape.sweep;
    const Extent measure = Measure(&sweep, point, direction);
    SubsurfaceInternalRootQuery query(point, direction, measure.shift);
    Ray ray(ticket, query.origin, direction, Ray::SubsurfaceRay);
    ray.SetRootQuery(&query);
    IStack stack(thread.stackPool);
    sweep.All_Intersections(ray, stack, &thread);
    std::vector<double> depths;
    bool sound = true;
    while (!stack->empty())
    {
        const Intersection& hit = stack->top();
        if (query.Admit(&sweep, hit.Depth)) depths.push_back(query.Distance(hit.Depth));
        sound = sound && std::isfinite(hit.Depth) && std::abs(hit.INormal.length() - 1.0) < 1e-9;
        stack->pop();
    }
    std::sort(depths.begin(), depths.end());
    ++checks;
    // a crossing lies within rounding of the wall, where the inside test must still answer
    for (double depth : depths) sweep.Inside(point + direction * depth, &thread);
    if (!sound) { Fail("crossing depth or normal", Describe(shape, point, direction, "")); return; }
    const double far = measure.reach + 0.05 * measure.extent;
    auto expected = [&](double distance, bool& decided)
    {
        size_t passed = 0;
        decided = true;
        for (double depth : depths)
        {
            if (std::abs(depth - distance) <= measure.slack) decided = false;
            if (depth < distance) ++passed;
        }
        return (passed % 2) == 0;
    };
    auto test = [&](double distance)
    {
        bool decided;
        const bool inside = expected(distance, decided);
        if (!decided || sweep.Inside(point + direction * distance, &thread) == inside) return true;
        // the wall may lie within rounding of the probe
        Vector3d local = point + direction * distance;
        if (sweep.Trans != nullptr) MInvTransPoint(local, local, sweep.Trans);
        if (std::abs(shape.reference.Gap(local)) < 1e-9L * shape.reference.size) return true;
        char text[200];
        std::snprintf(text, sizeof(text), "at %.17g of %.6g: inside test says %d, %d crossings reported, first %.17g", distance, far,
                      int(!inside), int(depths.size()), depths.empty() ? -1.0 : depths[0]);
        Fail("crossings against the inside test", Describe(shape, point, direction, text));
        return false;
    };
    const int steps = 400;
    for (int k = 0; k <= steps; ++k)
        if (!test(far * k / steps)) return;
    // between the crossings and close on either side of each, where the steps above may not fall
    double previous = 0.0;
    for (double depth : depths)
    {
        if (!test(0.5 * (previous + depth)) || !test(std::max(0.0, depth - 4.0 * measure.slack)) || !test(depth + 4.0 * measure.slack)) return;
        previous = depth;
    }
    if (depths.size() % 2 == 0) Fail("crossings from inside are not odd", Describe(shape, point, direction, std::to_string(depths.size())));
}

// The exit the search finds from `object` against the first end of material along the ray.
void CheckExit(const Shape& shape, ObjectPtr object, const char* form, const Vector3d& point, const Vector3d& direction,
               TraceThreadData& thread, TraceTicket& ticket)
{
    if (!object->Inside(point, &thread)) return;
    const Extent measure = Measure(object, point, direction);
    auto find = [&](Intersection& hit, const Ray& ray, const RayObjectCondition& condition) { return Find_Intersection(&hit, object, ray, condition, &thread); };
    auto owns = [](const Intersection&) { return true; };
    auto contains = [&](const Vector3d& p) { return object->Inside(p, &thread); };
    auto outward = [&](Intersection& hit, Vector3d& normal) { hit.Object->Normal(normal, &hit, &thread); normal.normalize(); };
    std::string failure;
    auto fail = [&](SubsurfaceExitFailure reason, double distance)
    {
        failure = std::string(SubsurfaceExitFailureText(reason)) + " after " + std::to_string(distance);
        throw 0;
    };
    ++checks;
    SubsurfaceInternalBoundary boundary;
    try
    {
        boundary = FindSubsurfaceInternalBoundary(point, direction, ticket, measure.shift, measure.extent, measure.reach, find, owns, contains, outward, fail);
    }
    catch (int)
    {
        Fail("no exit", Describe(shape, point, direction, std::string(form) + ": " + failure));
        return;
    }
    const int steps = 400;
    const double far = measure.reach + 0.05 * measure.extent;
    double first = far;
    for (int k = 1; k <= steps; ++k)
        if (!contains(point + direction * (far * k / steps))) { first = far * k / steps; break; }
    const bool early = boundary.distance < first - far / steps - measure.slack;
    // the material may end for less than the slack: then it must be absent midway to the object's next crossing
    auto gap = [&]()
    {
        SubsurfaceInternalRootQuery query(point, direction, measure.shift);
        Ray ray(ticket, query.origin, direction, Ray::SubsurfaceRay);
        ray.SetRootQuery(&query);
        IStack stack(thread.stackPool);
        object->All_Intersections(ray, stack, &thread);
        double next = HUGE_VAL;
        for (; !stack->empty(); stack->pop())
            if (query.Distance(stack->top().Depth) > boundary.distance) next = std::min(next, query.Distance(stack->top().Depth));
        return next < HUGE_VAL && !contains(point + direction * (0.5 * (boundary.distance + next)));
    };
    const bool leaves = !contains(point + direction * (boundary.distance + 4.0 * measure.slack)) ||
                        !contains(point + direction * (boundary.distance + 1e-5 * measure.extent)) || gap();
    const bool held = boundary.distance < 8.0 * measure.slack || contains(point + direction * (boundary.distance - 4.0 * measure.slack)) ||
                      contains(point + direction * (0.5 * boundary.distance));
    if (boundary.distance > first + measure.slack || !leaves || !held || !(dot(boundary.normal, direction) > 0.0))
    {
        char text[200];
        std::snprintf(text, sizeof(text), "%s: exit at %.17g, material first ends before %.17g, leaves %d held %d early %d", form,
                      boundary.distance, first, int(leaves), int(held), int(early));
        Fail("exit", Describe(shape, point, direction, text));
    }
}

ObjectPtr Ball(const Vector3d& centre, double radius, bool inverted)
{
    auto* ball = new Sphere;
    ball->Center = centre;
    ball->Radius = radius;
    ball->Type |= IS_CHILD_OBJECT;
    if (inverted) Set_Flag(ball, INVERTED_FLAG);
    ball->Compute_BBox();
    return ball;
}

void CheckForms(const Shape& shape, const Vector3d& point, const Vector3d& direction, TraceThreadData& thread, TraceTicket& ticket)
{
    const double size = Vector3d(shape.sweep->BBox.size).length();
    CheckExit(shape, shape.sweep, "bare", point, direction, thread, ticket);
    {
        CSGIntersection cut(false);
        cut.children = {shape.sweep->Copy(), Ball(point + Direction() * size * Uniform(0.0, 0.2), size * Uniform(0.05, 0.6), false)};
        cut.Compute_BBox();
        CheckExit(shape, &cut, "intersection", point, direction, thread, ticket);
    }
    {
        CSGIntersection cut(true);
        const double radius = size * Uniform(0.01, 0.3);
        cut.children = {shape.sweep->Copy(), Ball(point + Direction() * radius * Uniform(1.0 + 1e-6, 3.0), radius, true)};
        cut.Compute_BBox();
        CheckExit(shape, &cut, "difference", point, direction, thread, ticket);
    }
    {
        CSGIntersection cut(true);
        auto* merge = new CSGMerge;
        merge->children = {shape.sweep->Copy(), Ball(point + Direction() * size * Uniform(0.0, 0.3), size * Uniform(0.02, 0.3), false)};
        merge->Type |= IS_CHILD_OBJECT;
        merge->Compute_BBox();
        const double radius = size * Uniform(0.01, 0.3);
        cut.children = {merge, Ball(point + Direction() * radius * Uniform(1.0 + 1e-6, 3.0), radius, true)};
        cut.Compute_BBox();
        CheckExit(shape, &cut, "merge in a difference", point, direction, thread, ticket);
    }
}

}

int main(int argc, char** argv)
{
    Initialize_Noise();
    auto scene = std::make_shared<SceneData>();
    TraceThreadData thread(scene, 1, false);
    TraceTicket ticket(1, 0.0);
    const PreciseInsideScope precision(&thread);
    const int rounds = (argc > 1) ? std::atoi(argv[1]) : 4;
    const double units[] = {1.0, 1000.0, 0.01};
    int sweeps = 0;
    for (int round = 0; round < rounds; ++round)
        for (int kind = LINEAR_SPHERE_SWEEP; kind <= B_SPLINE_SPHERE_SWEEP; ++kind)
            for (int family = 0; family < 7; ++family)
                for (double unit : units)
                    for (int placing = 0; placing < 3; ++placing)
                    {
                        // each sweep draws from its own seed, so one can be rerun alone: sphere_sweep_oracle <rounds> <index>
                        const int index = sweeps++;
                        if (argc > 2 && std::atoi(argv[2]) != index) continue;
                        engine.seed(20261110u + index);
                        Shape shape = Random(kind, family, unit, placing, index);
                        if (argc > 2)
                            for (const auto& sphere : shape.reference.spheres)
                                std::printf("<%.17g, %.17g, %.17g>, %.17g\n", sphere.Center[0], sphere.Center[1], sphere.Center[2], sphere.Radius);
                        CheckInside(shape, thread, 12);
                        for (int ray = 0, tries = 0; ray < 6 && tries < 200; ++tries)
                        {
                            Vector3d point, direction;
                            if (!Start(shape, point, direction)) continue;
                            ++ray;
                            CheckCrossings(shape, point, direction, thread, ticket);
                            CheckForms(shape, point, direction, thread, ticket);
                        }
                        delete shape.sweep;
                    }
    std::printf("%d sphere sweeps, %d checks, %d failures\n", sweeps, checks, failures);
    Free_Noise_Tables();
    return failures == 0 ? 0 : 1;
}
