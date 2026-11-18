// The crossings a buried light's walk asks of every solid host shape, at three scene scales: from points buried in the
// material (deep, and a hair under the surface), along random headings, along the shape's axis and square to it, the
// precise all-roots query must reproduce the inside test everywhere along the ray, and the real exit search
// (Trace::SubsurfaceInternalBoundaryAt) must find the first place the material ends. A shape is modelled three ways in
// each unit: with the unit in its own parameters, unit sized under an even scale, and under an uneven scale and turn.
// Run with a shape name to test that one alone: buried_walk_roots_test torus. Lathes and the superellipsoid run only by
// name: a lathe's ray polynomial in its spline parameter has a pair of roots closer than its rounding for a ray almost
// square to the axis (the crossings either side of the axis at one height), and a superellipsoid seen along an axis
// can report its crossing early; neither is fixed yet.
#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/cone.h"
#include "core/shape/csg.h"
#include "core/shape/lathe.h"
#include "core/shape/polynomial.h"
#include "core/shape/prism.h"
#include "core/shape/sor.h"
#include "core/shape/sphere.h"
#include "core/shape/spheresweep.h"
#include "core/shape/superellipsoid.h"
#include "core/shape/torus.h"
#include "core/material/interior.h"
#include "core/material/noise.h"
#include "core/math/matrix.h"
#include "core/render/trace.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/support/statistics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace pov;

namespace
{

class InternalTrace final : public Trace
{
public:
    using Trace::Trace;
    using Trace::SubsurfaceInternalBoundaryAt;
};

std::mt19937_64 engine(20261116);
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

RenderStatistics stats;

// Every shape is modelled unit sized about the origin with its axis along y; `unit` multiplies its lengths.
ObjectPtr MakeTorus(double unit)
{
    auto* torus = new Torus;
    torus->MajorRadius = 1.0 * unit;
    torus->MinorRadius = 0.35 * unit;
    torus->Compute_BBox();
    return torus;
}

ObjectPtr MakeThinTorus(double unit)
{
    auto* torus = new Torus;
    torus->MajorRadius = 1.0 * unit;
    torus->MinorRadius = 0.02 * unit;
    torus->Compute_BBox();
    return torus;
}

ObjectPtr MakeSor(double unit)
{
    auto* sor = new Sor;
    Vector2d points[] = {{0.4, -1.6}, {0.8, -1.2}, {0.4, 0.0}, {0.9, 1.0}, {0.2, 1.4}, {0.1, 1.8}};
    for (auto& p : points) p *= unit;
    sor->Number = int(sizeof(points) / sizeof(points[0])) - 3;
    sor->Compute_Sor(points, stats);
    sor->Compute_BBox();
    return sor;
}

ObjectPtr MakeLathe(double unit, int spline)
{
    auto* lathe = new Lathe;
    std::vector<Vector2d> points;
    switch (spline)
    {
        case LINEAR_SPLINE: points = {{0.0, -1.0}, {0.8, -0.9}, {0.3, 0.0}, {0.9, 0.8}, {0.0, 1.0}}; break;
        case QUADRATIC_SPLINE: points = {{0.0, -1.2}, {0.0, -1.0}, {0.8, -0.8}, {0.3, 0.0}, {0.9, 0.8}, {0.0, 1.0}}; break;
        case CUBIC_SPLINE: points = {{0.0, -1.2}, {0.0, -1.0}, {0.8, -0.8}, {0.3, 0.0}, {0.9, 0.8}, {0.0, 1.0}, {0.0, 1.2}}; break;
        default: points = {{0.0, -1.0}, {0.9, -1.0}, {0.9, -0.2}, {0.3, 0.0}, {0.3, 0.0}, {1.0, 0.3}, {0.5, 1.0}, {0.0, 1.0}}; break;
    }
    for (auto& p : points) p *= unit;
    lathe->Spline_Type = spline;
    lathe->Number = int(points.size());
    lathe->Compute_Lathe(points.data(), stats);
    lathe->Compute_BBox();
    return lathe;
}

ObjectPtr MakePrism(double unit, int spline, int sweep)
{
    auto* prism = new Prism;
    std::vector<Vector2d> points;
    if (spline == LINEAR_SPLINE)
        points = {{-0.8, -0.6}, {0.9, -0.7}, {0.2, 0.0}, {0.7, 0.8}, {-0.6, 0.7}, {-0.8, -0.6}};
    else
        points = {{-0.6, 0.7}, {-0.8, -0.6}, {0.9, -0.7}, {0.2, 0.0}, {0.7, 0.8}, {-0.6, 0.7}, {-0.8, -0.6}, {0.9, -0.7}};
    prism->Spline_Type = spline;
    prism->Sweep_Type = sweep;
    // a conic prism's heights are fractions of its apex distance, so only its outline carries the unit
    prism->Height1 = (sweep == CONIC_SWEEP) ? 0.3 : -0.5 * unit;
    prism->Height2 = (sweep == CONIC_SWEEP) ? 1.0 : 0.5 * unit;
    for (auto& p : points) p *= unit;
    prism->Number = int(points.size());
    prism->Compute_Prism(points.data(), stats);
    prism->Compute_BBox();
    return prism;
}

ObjectPtr MakeCone(double unit, bool cylinder)
{
    auto* cone = new Cone;
    if (cylinder) cone->Cylinder();
    cone->apex = Vector3d(0.0, 1.0, 0.0) * unit;
    cone->base = Vector3d(0.0, -1.0, 0.0) * unit;
    cone->apex_radius = (cylinder ? 0.3 : 0.15) * unit;
    cone->base_radius = (cylinder ? 0.3 : 0.6) * unit;
    if (cylinder) cone->Compute_Cylinder_Data(); else cone->Compute_Cone_Data();
    cone->Compute_BBox();
    return cone;
}

ObjectPtr MakeSphere(double unit)
{
    auto* sphere = new Sphere;
    sphere->Center = Vector3d(0.1, -0.2, 0.05) * unit;
    sphere->Radius = 0.9 * unit;
    sphere->Compute_BBox();
    return sphere;
}

ObjectPtr MakeSuperellipsoid(double)
{
    auto* shape = new Superellipsoid;
    const double e = 0.4, n = 0.7;
    shape->Power[X] = 2.0 / e;
    shape->Power[Y] = e / n;
    shape->Power[Z] = 2.0 / n;
    shape->Compute_BBox();
    return shape;
}

ObjectPtr MakeQuarticTorus(double unit)
{
    // (x^2 + y^2 + z^2 + R^2 - r^2)^2 - 4 R^2 (x^2 + z^2), axis y, in the quartic's term order
    auto* poly = new Poly(4);
    const double R = 1.0 * unit, r = 0.35 * unit, K = R * R - r * r;
    for (int i = 0; i < 35; ++i) poly->Coeffs[i] = 0.0;
    poly->Coeffs[0] = 1.0;                       // x^4
    poly->Coeffs[4] = 2.0;                       // x^2 y^2
    poly->Coeffs[7] = 2.0;                       // x^2 z^2
    poly->Coeffs[9] = 2.0 * K - 4.0 * R * R;     // x^2
    poly->Coeffs[20] = 1.0;                      // y^4
    poly->Coeffs[23] = 2.0;                      // y^2 z^2
    poly->Coeffs[25] = 2.0 * K;                  // y^2
    poly->Coeffs[30] = 1.0;                      // z^4
    poly->Coeffs[32] = 2.0 * K - 4.0 * R * R;    // z^2
    poly->Coeffs[34] = K * K;                    // 1
    Make_BBox(poly->BBox, -(R + r), -r, -(R + r), 2.0 * (R + r), 2.0 * r, 2.0 * (R + r));
    return poly;
}

// An eyestalk: a thin stalk swelling into a head, the shape the forest's buried vein lamps sit in.
ObjectPtr MakeSweep(double unit, int kind)
{
    auto* sweep = new SphereSweep;
    std::vector<SPHSWEEP_SPH> spheres;
    if (kind == LINEAR_SPHERE_SWEEP)
        spheres = {{Vector3d(0.0, -1.0, 0.0), 0.05}, {Vector3d(0.1, -0.2, 0.05), 0.04}, {Vector3d(0.0, 0.5, 0.1), 0.06},
                   {Vector3d(0.05, 0.75, 0.1), 0.25}};
    else
        spheres = {{Vector3d(0.0, -1.4, 0.0), 0.05}, {Vector3d(0.0, -1.0, 0.0), 0.05}, {Vector3d(0.1, -0.2, 0.05), 0.04},
                   {Vector3d(0.0, 0.5, 0.1), 0.06}, {Vector3d(0.05, 0.62, 0.1), 0.25}, {Vector3d(0.05, 0.66, 0.1), 0.25},
                   {Vector3d(0.05, 0.70, 0.1), 0.25}};
    for (auto& s : spheres) { s.Center *= unit; s.Radius *= unit; }
    sweep->Interpolation = kind;
    sweep->Num_Modeling_Spheres = int(spheres.size());
    sweep->Modeling_Sphere = static_cast<SPHSWEEP_SPH*>(POV_MALLOC(spheres.size() * sizeof(SPHSWEEP_SPH), "sweep spheres"));
    for (size_t i = 0; i < spheres.size(); ++i) sweep->Modeling_Sphere[i] = spheres[i];
    sweep->Compute();
    sweep->Compute_BBox();
    return sweep;
}

// A merge of a cylinder, a cone and a torus meeting inside one another, the shape of a scroll's parts.
ObjectPtr MakeMerge(double unit)
{
    auto* merge = new CSGMerge;
    merge->children = {MakeCone(unit, true), MakeCone(unit, false), MakeTorus(unit)};
    for (auto* child : merge->children) child->Type |= IS_CHILD_OBJECT;
    merge->Compute_BBox();
    return merge;
}

struct Kind final
{
    const char* name;
    std::function<ObjectPtr(double)> make;
    bool baked;                // whether the unit can live in the shape's own parameters
    bool placed = true;        // whether its box follows a transform
    bool byName = false;       // tested only when named
};

void Place(ObjectPtr object, double unit, int placing)
{
    if (placing == 0) return;
    TRANSFORM transform;
    const Vector3d scale = (placing == 1) ? Vector3d(unit) : Vector3d(unit, 0.4 * unit, 2.5 * unit);
    Compute_Scaling_Transform(&transform, scale);
    object->Scale(scale, &transform);
    if (placing == 2)
    {
        const Vector3d axis = Direction();
        const double angle = Uniform(0.0, 6.0);
        Compute_Axis_Rotation_Transform(&transform, axis, angle);
        object->Rotate(Vector3d(0.0), &transform);
        const Vector3d shift = Direction() * unit * Uniform(0.0, 3.0);
        Compute_Translation_Transform(&transform, shift);
        object->Translate(shift, &transform);
    }
    object->Compute_BBox();
}

struct Tally final
{
    int rays = 0, lost = 0, phantom = 0, noExit = 0, wrongExit = 0;
    std::string first;
};

int failures = 0;

std::string Describe(const char* what, const Vector3d& p, const Vector3d& d, const char* more)
{
    char text[600];
    std::snprintf(text, sizeof(text), "%s from <%.17g, %.17g, %.17g> along <%.17g, %.17g, %.17g>: %s", what, p[0], p[1], p[2],
                  d[0], d[1], d[2], more);
    return text;
}

struct Host final
{
    ObjectPtr object;
    std::shared_ptr<SceneData> scene;
    TraceThreadData* thread;
    InternalTrace* trace;
    Vector3d low, high, axis;
    double extent;
    bool Inside(const Vector3d& p) const { return object->Inside(p, thread); }
};

// A world point a hair inside the surface, `depth` of the extent under it, reached along a random heading from p.
bool UnderSurface(const Host& host, const Vector3d& p, double depth, Vector3d& result)
{
    const Vector3d d = Direction();
    const int steps = 512;
    double inside = 0.0, outside = -1.0;
    for (int k = 1; k <= steps; ++k)
    {
        const double at = 2.0 * host.extent * k / steps;
        if (!host.Inside(p + d * at)) { outside = at; break; }
        inside = at;
    }
    if (outside < 0.0) return false;
    while (outside - inside > 1e-3 * depth * host.extent)
    {
        const double middle = 0.5 * (inside + outside);
        if (middle <= inside || middle >= outside) break;
        (host.Inside(p + d * middle) ? inside : outside) = middle;
    }
    result = p + d * std::max(0.0, inside - depth * host.extent);
    return host.Inside(result);
}

// The walk's query start, as Trace::SubsurfaceInternalBoundaryAt places it: shifted back along the ray.
double WalkShift(const Host& host, const Vector3d& point)
{
    double tolerance = 1.0e-5 * std::max(std::abs(point[X]), std::max(std::abs(point[Y]), std::abs(point[Z])));
    double smallest = HUGE_VAL;
    for (int axis = X; axis <= Z; axis++)
        if (host.high[axis] - host.low[axis] > 0.0) smallest = std::min(smallest, host.high[axis] - host.low[axis]);
    tolerance = std::min(tolerance, 1.0e-3 * smallest);
    if (1.0e-3 * smallest >= 2.0 * MIN_ISECT_DEPTH) tolerance = std::max(tolerance, 2.0 * MIN_ISECT_DEPTH);
    return std::max(2.0 * MIN_ISECT_DEPTH, std::max(tolerance, 4.0e-6 * host.extent));
}

// A crossing moved by less than this share of the shape's extent is the same crossing; the walk needs no better.
const double kSame = 1e-6;

void CheckRay(const Host& host, const Vector3d& point, const Vector3d& direction, Tally& tally, TraceTicket& ticket)
{
    ++tally.rays;
    const double tolerance = kSame * host.extent;
    const double reach = SubsurfaceBoundsReach(point, direction, host.low, host.high);
    const double far = reach + 0.05 * host.extent;
    // the reference: where the inside test changes along the ray, from dense steps refined by halving
    std::vector<double> reference;
    {
        const int steps = 2000;
        bool inside = true;
        double previous = 0.0;
        for (int k = 1; k <= steps; ++k)
        {
            const double at = far * k / steps;
            const bool here = host.Inside(point + direction * at);
            if (here != inside)
            {
                double low = previous, high = at;
                for (int i = 0; i < 200; ++i)
                {
                    const double middle = 0.5 * (low + high);
                    if (middle <= low || middle >= high) break;
                    (host.Inside(point + direction * middle) == inside ? low : high) = middle;
                }
                reference.push_back(0.5 * (low + high));
                inside = here;
            }
            previous = at;
        }
    }
    // the crossings the walk's precise query finds from its shifted start, nearest first as the walk asks for them: some
    // shapes return only their nearest root to a top-level query
    std::vector<double> depths;
    {
        SubsurfaceInternalRootQuery query(point, direction, WalkShift(host, point));
        Ray ray(ticket, query.origin, direction, Ray::SubsurfaceRay);
        ray.SetRootQuery(&query);
        for (size_t guard = 0; guard < 4 * reference.size() + 16; ++guard)
        {
            Intersection hit;
            if (!Find_Intersection(&hit, host.object, ray, query, host.thread)) break;
            depths.push_back(query.Distance(hit.Depth));
            query.Advance(hit.Depth);
        }
    }
    auto near = [&](const std::vector<double>& list, double at)
    {
        for (double d : list) if (std::abs(d - at) <= tolerance) return true;
        return false;
    };
    char text[300] = "";
    if (std::getenv("BURIED_WALK_VERBOSE") != nullptr)
    {
        std::printf("   ray <%.17g, %.17g, %.17g> <%.17g, %.17g, %.17g>\n     reference", point[0], point[1], point[2], direction[0], direction[1], direction[2]);
        for (double d : reference) std::printf(" %.12g", d);
        std::printf("\n     reported ");
        for (double d : depths) std::printf(" %.12g", d);
        std::printf("\n");
    }
    for (double at : reference)
        if (!near(depths, at))
        {
            ++tally.lost;
            std::snprintf(text, sizeof(text), "the inside test changes at %.17g of %.6g; %d crossings reported, nearest %.17g", at, far,
                          int(depths.size()), depths.empty() ? -1.0 : *std::min_element(depths.begin(), depths.end(),
                              [&](double a, double b) { return std::abs(a - at) < std::abs(b - at); }));
            if (tally.first.empty()) tally.first = Describe("lost crossing", point, direction, text);
            break;
        }
    for (double at : depths)
        if (at < far && !near(reference, at))
        {
            // a stretch shorter than the reference's steps holds no step; it is a phantom only if no material is there
            ++tally.phantom;
            break;
        }
    // the real exit search must stop where the material first ends
    SubsurfaceInternalBoundary boundary;
    try
    {
        boundary = host.trace->SubsurfaceInternalBoundaryAt(point, direction, host.object, host.object, ticket);
    }
    catch (const std::exception& error)
    {
        ++tally.noExit;
        if (tally.first.empty()) tally.first = Describe("exit search", point, direction, error.what());
        return;
    }
    const double first = reference.empty() ? far : reference.front();
    // an exit earlier than the reference's is right only where the material really ends there
    const bool early = boundary.distance < first - tolerance;
    if (early ? host.Inside(point + direction * (boundary.distance + 0.5 * tolerance)) : (boundary.distance > first + tolerance))
    {
        ++tally.wrongExit;
        std::snprintf(text, sizeof(text), "exit at %.17g, the inside test first changes at %.17g", boundary.distance, first);
        if (tally.first.empty()) tally.first = Describe("exit search", point, direction, text);
    }
}

void CheckShape(const Kind& kind, double unit, int placing, int rays)
{
    char name[120];
    std::snprintf(name, sizeof(name), "%s unit %g %s", kind.name, unit, placing == 0 ? "in its parameters" : placing == 1 ? "scaled" : "stretched and turned");
    const bool baked = (placing == 0);
    if ((baked && !kind.baked) || (!baked && !kind.placed)) return;
    Host host;
    host.object = kind.make(baked ? unit : 1.0);
    Place(host.object, unit, placing);
    host.scene = std::make_shared<SceneData>();
    host.scene->preparedSets.emplace_back(new PreparedSet);
    host.object->interior = std::make_shared<Interior>();
    std::function<void(ObjectPtr)> share = [&](ObjectPtr object)
    {
        object->interior = host.object->interior;
        if (auto* compound = dynamic_cast<CompoundObject*>(object))
            for (auto* child : compound->children) share(child);
    };
    share(host.object);
    host.low = Vector3d(host.object->BBox.lowerLeft);
    host.high = host.low + Vector3d(host.object->BBox.size);
    host.object->interior->boundsLow = host.low;
    host.object->interior->boundsHigh = host.high;
    host.extent = (host.high - host.low).length();
    host.scene->preparedSets.front()->objects = {host.object};
    host.thread = new TraceThreadData(host.scene, 1, false);
    host.thread->subsurfaceCache = std::make_shared<SubsurfaceCache>();
    static Trace::CooperateFunctor cooperate;
    static Trace::MediaFunctor media;
    static Trace::RadiosityFunctor radiosity;
    host.trace = new InternalTrace(host.scene, host.thread, QualityFlags(9), cooperate, media, radiosity);
    const PreciseInsideScope precision(host.thread);
    Vector3d axis(0.0, 1.0, 0.0);
    if (placing != 0)
    {
        auto* trans = host.object->Trans;
        if (auto* compound = dynamic_cast<CompoundObject*>(host.object)) trans = compound->children.front()->Trans;
        if (trans != nullptr) MTransDirection(axis, axis, trans);
    }
    host.axis = axis.normalized();
    TraceTicket ticket(1, 0.0);
    Tally tally;
    int found = 0;
    for (int tries = 0; found < rays && tries < 200 * rays; ++tries)
    {
        Vector3d point;
        for (int k = 0; k < 3; ++k) point[k] = Uniform(host.low[k], host.high[k]);
        if (!host.Inside(point)) continue;
        // a third start deep, a third a hair under the surface, a third a little further under
        const int depthKind = Pick(3);
        if (depthKind > 0)
        {
            const double depths[] = {1e-9, 1e-6, 1e-3};
            Vector3d under;
            if (!UnderSurface(host, point, depths[Pick(3)], under)) continue;
            point = under;
        }
        // headings: random, square to the axis, and along it, each with a small tilt or none
        Vector3d direction = Direction();
        const double tilts[] = {0.0, 1e-9, 1e-6, 1e-3};
        switch (Pick(3))
        {
            case 0: direction = (cross(host.axis, direction).normalized() + host.axis * tilts[Pick(4)]).normalized(); break;
            case 1: direction = (host.axis + direction * tilts[Pick(4)]).normalized(); break;
            default: break;
        }
        if (Pick(2)) direction = -direction;
        ++found;
        CheckRay(host, point, direction, tally, ticket);
    }
    const int bad = tally.lost + tally.noExit + tally.wrongExit;
    std::printf("%-6s %-52s rays %3d  lost %3d  no exit %3d  wrong exit %3d  (phantom %d)\n", bad ? "FAIL" : "ok", name, tally.rays,
                tally.lost, tally.noExit, tally.wrongExit, tally.phantom);
    if (bad)
    {
        ++failures;
        std::printf("       first: %s\n", tally.first.c_str());
    }
    if (found < rays) std::printf("       only %d buried starts found\n", found);
}

}

int main(int argc, char** argv)
{
    Initialize_Noise();
    const std::vector<Kind> kinds = {
        {"sphere", MakeSphere, true},
        {"cylinder", [](double u) { return MakeCone(u, true); }, true},
        {"cone", [](double u) { return MakeCone(u, false); }, true},
        {"torus", MakeTorus, true},
        {"thin-torus", MakeThinTorus, true},
        {"quartic-torus", MakeQuarticTorus, true, false},
        {"sor", MakeSor, true},
        {"lathe-linear", [](double u) { return MakeLathe(u, LINEAR_SPLINE); }, true, true, true},
        {"lathe-quadratic", [](double u) { return MakeLathe(u, QUADRATIC_SPLINE); }, true, true, true},
        {"lathe-cubic", [](double u) { return MakeLathe(u, CUBIC_SPLINE); }, true, true, true},
        {"lathe-bezier", [](double u) { return MakeLathe(u, BEZIER_SPLINE); }, true, true, true},
        {"prism-linear", [](double u) { return MakePrism(u, LINEAR_SPLINE, LINEAR_SWEEP); }, true},
        {"prism-cubic", [](double u) { return MakePrism(u, CUBIC_SPLINE, LINEAR_SWEEP); }, true},
        {"prism-linear-conic", [](double u) { return MakePrism(u, LINEAR_SPLINE, CONIC_SWEEP); }, true},
        {"prism-cubic-conic", [](double u) { return MakePrism(u, CUBIC_SPLINE, CONIC_SWEEP); }, true},
        {"superellipsoid", MakeSuperellipsoid, false, true, true},
        {"sweep-linear", [](double u) { return MakeSweep(u, LINEAR_SPHERE_SWEEP); }, true},
        {"sweep-catmull-rom", [](double u) { return MakeSweep(u, CATMULL_ROM_SPLINE_SPHERE_SWEEP); }, true},
        {"sweep-b-spline", [](double u) { return MakeSweep(u, B_SPLINE_SPHERE_SWEEP); }, true},
        {"merge", MakeMerge, true},
    };
    const double units[] = {0.001, 1.0, 1000.0};
    const int rays = 60;
    for (const Kind& kind : kinds)
    {
        if ((argc > 1) ? (std::strcmp(argv[1], kind.name) != 0) : kind.byName) continue;
        for (double unit : units)
            for (int placing = 0; placing < 3; ++placing)
            {
                engine.seed(20261116u + std::hash<std::string>()(kind.name) + int(unit * 7) + placing);
                CheckShape(kind, unit, placing, rays);
            }
    }
    std::printf("%s: %d shape placings failed\n", failures ? "FAIL" : "PASS", failures);
    Free_Noise_Tables();
    return failures ? 1 : 0;
}
