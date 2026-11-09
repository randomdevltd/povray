#include <random>
#include "core/lighting/subsurfaceinternalboundary.h"
#include "core/shape/superellipsoid.h"
#include "core/shape/sphere.h"
#include "core/shape/cone.h"
#include "core/shape/box.h"
#include "core/shape/plane.h"
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
#include <array>
#include <vector>
#include <algorithm>
#include <functional>

using namespace pov;

class InternalTrace final : public Trace
{
  public:
    using Trace::SubsurfaceInternalBoundaryAt;
    using Trace::Trace;
};

struct JunctionOracle
{
    using Point = std::array<long double, 3>;
    Vector3d translation;
    Point Along(const Vector3d &p, const Vector3d &d, long double t) const
    {
        Point q;
        for (int i = 0; i < 3; ++i)
            q[i] = static_cast<long double>(p[i]) + t * d[i] - translation[i];
        return q;
    }
    bool Inside(const Point &p) const
    {
        const long double ellipsoid =
            p[0] * p[0] / (0.025L * 0.025L) + (p[1] + 0.006L) * (p[1] + 0.006L) / (0.028L * 0.028L) + p[2] * p[2] / (0.009L * 0.009L);
        const long double y = p[1] + 0.026L, length = std::sqrt(0.000577L), s = (-0.024L * y + 0.001L * p[2]) / length;
        const long double radius = 0.015L - 0.004L * s / length;
        return ellipsoid < 1.0L || (s > 0.0L && s < length && p[0] * p[0] + y * y + p[2] * p[2] - s * s < radius * radius);
    }
    double Exit(const Vector3d &p, const Vector3d &d, bool boundaryStart = false) const
    {
        std::vector<long double> roots;
        auto solve = [&](long double a, long double b, long double c, auto accept) {
            if (a == 0.0L)
            {
                if (b != 0.0L)
                    accept(-c / b);
                return;
            }
            const long double discriminant = b * b - 4.0L * a * c;
            if (discriminant < 0.0L)
                return;
            const long double q = -0.5L * (b + std::copysign(std::sqrt(discriminant), b));
            if (q == 0.0L)
                accept(-b / (2.0L * a));
            else
            {
                accept(q / a);
                accept(c / q);
            }
        };
        auto add = [&](long double t) { roots.push_back(t); };
        const auto q = Along(p, d, 0.0L);
        long double uncertainty = 0.0L;
        if (boundaryStart)
            for (int axis = 0; axis < 3; ++axis)
                uncertainty =
                    std::max(uncertainty, 64.0L * std::abs(std::nextafter(p[axis], std::numeric_limits<double>::infinity()) - p[axis]));
        if (boundaryStart && !Inside(Along(p, d, 2.0L * uncertainty)))
        {
            const long double length = std::sqrt(0.000577L), y = q[1] + 0.026L;
            const long double s = (-0.024L * y + 0.001L * q[2]) / length, ds = (-0.024L * d[1] + 0.001L * d[2]) / length;
            const long double radial = std::sqrt(std::max(0.0L, q[0] * q[0] + y * y + q[2] * q[2] - s * s));
            const long double k = -0.004L / length, radius = 0.015L + k * s;
            const long double radialDerivative = radial > 0.0L ? (q[0] * d[0] + y * d[1] + q[2] * d[2] - s * ds) / radial : 0.0L;
            const bool cap = (std::abs(s) <= uncertainty && radial <= 0.015L + uncertainty && ds < 0.0L) ||
                             (std::abs(s - length) <= uncertainty && radial <= 0.011L + uncertainty && ds > 0.0L);
            const bool side = s >= -uncertainty && s <= length + uncertainty &&
                              std::abs(radial - radius) <= uncertainty * std::sqrt(1.0L + k * k) && radialDerivative - k * ds > 0.0L;
            if (cap || side)
                return 0.0;
        }
        long double a = 0.0L, b = 0.0L, c = -1.0L;
        const long double radius[] = {0.025L, 0.028L, 0.009L};
        for (int i = 0; i < 3; ++i)
        {
            const long double v = q[i] + (i == 1 ? 0.006L : 0.0L), inv = 1.0L / (radius[i] * radius[i]);
            a += d[i] * static_cast<long double>(d[i]) * inv;
            b += 2.0L * v * d[i] * inv;
            c += v * v * inv;
        }
        solve(a, b, c, add);
        const long double length = std::sqrt(0.000577L), y = q[1] + 0.026L;
        const long double s = (-0.024L * y + 0.001L * q[2]) / length, ds = (-0.024L * d[1] + 0.001L * d[2]) / length;
        const long double k = -0.004L / length, r = 0.015L + k * s;
        const long double vv = q[0] * q[0] + y * y + q[2] * q[2], vd = q[0] * d[0] + y * d[1] + q[2] * d[2];
        const long double dd =
            static_cast<long double>(d[0]) * d[0] + static_cast<long double>(d[1]) * d[1] + static_cast<long double>(d[2]) * d[2];
        solve(dd - (1.0L + k * k) * ds * ds, 2.0L * (vd - s * ds - r * k * ds), vv - s * s - r * r, [&](long double t) {
            const long double z = s + t * ds;
            if (z >= 0.0L && z <= length)
                add(t);
        });
        if (ds != 0.0L)
            for (int cap = 0; cap < 2; ++cap)
            {
                const long double z = cap * length, t = (z - s) / ds, radius = 0.015L + cap * (-0.004L);
                const long double x = q[0] + t * d[0], py = y + t * d[1], pz = q[2] + t * d[2];
                if (x * x + py * py + pz * pz - z * z <= radius * radius + 1e-21L)
                    add(t);
            }
        std::sort(roots.begin(), roots.end());
        for (size_t i = 0; i < roots.size(); ++i)
        {
            const long double t = roots[i];
            if (t < -uncertainty)
                continue;
            size_t before = i, after = i + 1;
            while (before > 0 && std::abs(roots[before - 1] - t) < 1e-18L)
                --before;
            while (after < roots.size() && std::abs(roots[after] - t) < 1e-18L)
                ++after;
            const long double left = before > 0 ? (roots[before - 1] + t) * 0.5L : t - 1.0L;
            const long double right = after < roots.size() ? (roots[after] + t) * 0.5L : t + 1.0L;
            if (Inside(Along(p, d, left)) && !Inside(Along(p, d, right)))
                return static_cast<double>(std::max(0.0L, t));
        }
        return -1.0;
    }
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
    struct Query final : RayRootQuery
    {
        bool Admit(ConstObjectPtr, double) const override
        {
            return true;
        }
    } previous;
    Ray outer(ticket);
    outer.SetRootQuery(&previous);
    struct Case
    {
        Vector3d point, direction;
    };
    const Case cases[] = {{Vector3d(-0.036278719967199564, 0.02262937989364305, -0.0051814553919980926),
                           Vector3d(-0.23770568504688971, -0.69886309688862946, 0.67460090357456737)},
                          {Vector3d(0.036999999999999755, 0.0029463307359533248, -7.8795925460267008e-06),
                           Vector3d(-0.029659129710760068, -0.45773870763682362, 0.88859192633951545)}};
    for (const auto &test : cases)
    {
        const auto &point = test.point;
        const auto &direction = test.direction;
        auto find = [&](Intersection &hit, const Ray &ray, const RayObjectCondition &condition) {
            return Find_Intersection(&hit, &body, ray, condition, &thread);
        };
        auto owns = [](const Intersection &) { return true; };
        auto outward = [&](Intersection &hit, Vector3d &normal) {
            hit.Object->Normal(normal, &hit, &thread);
            normal.normalize();
        };
        auto fail = [](SubsurfaceExitFailure reason, double) { throw std::runtime_error(SubsurfaceExitFailureText(reason)); };
        auto inBody = [&](const Vector3d &p) { return body.Inside(p, &thread); };
        const auto boundary = FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, 0.1, 0.1, find, owns, inBody, outward, fail);
        auto implicit = [&](double distance) {
            const Vector3d q = point + direction * distance;
            double sum = 0.0;
            for (int axis = 0; axis < 3; ++axis)
                sum += std::pow(std::abs(q[axis] / scale[axis]), 2.0 / 0.18);
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
        auto *rounded = static_cast<Superellipsoid *>(body.Copy());
        auto *cavity = new Sphere;
        cavity->Center = point + direction * 0.001;
        cavity->Radius = 0.0003;
        cavity->Type |= IS_CHILD_OBJECT;
        Set_Flag(cavity, INVERTED_FLAG);
        cavity->Compute_BBox();
        difference.children = {rounded, cavity};
        difference.Compute_BBox();
        auto findDifference = [&](Intersection &hit, const Ray &ray, const RayObjectCondition &condition) {
            return Find_Intersection(&hit, &difference, ray, condition, &thread);
        };
        auto inDifference = [&](const Vector3d &p) { return difference.Inside(p, &thread); };
        const auto cut =
            FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, 0.1, 0.1, findDifference, owns, inDifference, outward, fail);
        assert(std::abs(cut.distance - 0.0007) < 1e-10);
        const auto afterCut =
            FindSubsurfaceInternalBoundary(point + direction * 0.0015, direction, ticket, 0.0002, 0.1, 0.1, findDifference, owns, inDifference, outward, fail);
        assert(std::abs(afterCut.distance - boundary.distance + 0.0015) < 1e-9);
        assert(outer.GetRootQuery() == &previous);
        bool caught = false;
        try
        {
            FindSubsurfaceInternalBoundary(
                point, direction, ticket, 0.0002, 0.1, 0.1,
                [](Intersection &, const Ray &, const RayObjectCondition &) -> bool { throw std::runtime_error("query failure"); }, owns, inBody,
                outward, fail);
        }
        catch (const std::runtime_error &)
        {
            caught = true;
        }
        assert(caught && outer.GetRootQuery() == &previous);
        caught = false;
        try
        {
            FindSubsurfaceInternalBoundary(point, direction, ticket, 0.0002, 0.1, 0.1, find, owns, inBody,
                                           [](Intersection &, Vector3d &) { throw std::runtime_error("boundary failure"); }, fail);
        }
        catch (const std::runtime_error &)
        {
            caught = true;
        }
        assert(caught && outer.GetRootQuery() == &previous);
    }
    {
        // Synthetic crossings along +x from the origin: only the material test between crossings decides the exit.
        struct Crossings
        {
            std::vector<double> roots;
            Sphere marker;
            bool operator()(Intersection &hit, const Ray &ray, const RayObjectCondition &condition) const
            {
                for (double root : roots)
                    if (condition(ray, &marker, root + 0.0002))
                    {
                        hit = Intersection(root + 0.0002, Vector3d(root, 0.0, 0.0), const_cast<Sphere *>(&marker));
                        return true;
                    }
                return false;
            }
        };
        const Vector3d origin(0.0), along(1.0, 0.0, 0.0);
        auto owns = [](const Intersection &) { return true; };
        auto outward = [&](Intersection &, Vector3d &normal) { normal = -along; };
        SubsurfaceExitFailure failure = SubsurfaceExitFailure::NoCrossing;
        auto fail = [&](SubsurfaceExitFailure reason, double) {
            failure = reason;
            throw std::runtime_error(SubsurfaceExitFailureText(reason));
        };
        auto exitOf = [&](std::vector<double> roots, auto contains) {
            Crossings crossings{roots, {}};
            return FindSubsurfaceInternalBoundary(origin, along, ticket, 0.0002, 1.0, 1.0, crossings, owns, contains, outward, fail);
        };
        // An inside test with a tolerance band past the surface (blob style) still exits at the root, normal turned outward.
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-15; };
        auto banded = exitOf({0.5}, [](const Vector3d &p) { return p[0] < 0.5 + 1e-7; });
        assert(near(banded.distance, 0.5) && dot(banded.normal, along) == 1.0);
        // Seams inside merged material and a disjoint second part are walked past and ignored.
        assert(near(exitOf({0.2, 0.2, 0.3, 0.6, 0.8}, [](const Vector3d &p) { return p[0] < 0.3 || (p[0] > 0.6 && p[0] < 0.8); }).distance, 0.3));
        // A gap far thinner than any shape tolerance, and a sheet 1e-12 thick, are both resolved.
        for (double gap : {1.0e-9, 1.0e-12})
        {
            assert(near(exitOf({0.4, 0.4 + gap}, [&](const Vector3d &p) { return p[0] < 0.4 || p[0] > 0.4 + gap; }).distance, 0.4));
            assert(near(exitOf({0.0, gap}, [&](const Vector3d &p) { return p[0] >= 0.0 && p[0] < gap; }).distance, gap));
        }
        // A crossing at the origin with no material after it is a contact exit at the start.
        assert(near(exitOf({0.0, 0.7, 0.9}, [](const Vector3d &p) { return p[0] > 0.7 && p[0] < 0.9; }).distance, 0.0));
        // A start just outside the material walks in through the entry gap.
        assert(near(exitOf({1.0e-6, 0.5}, [](const Vector3d &p) { return p[0] > 1.0e-6 && p[0] < 0.5; }).distance, 0.5));
        // A grazing ray meets one surface twice, a rounding error apart: the sliver between is never tested, since its
        // middle lies within rounding of that surface, and the path runs on to the real exit.
        {
            const Vector3d grazing = Vector3d(-1.0e-6, 1.0, 0.0).normalized();
            auto steep = [&](Intersection &hit, Vector3d &normal) { normal = (hit.IPoint[0] < 0.4) ? grazing : -along; };
            Crossings crossings{{0.3, 0.3 + 1.0e-12, 0.5}, {}};
            auto sliver = [](const Vector3d &p) { assert(!(p[0] > 0.3 && p[0] < 0.3 + 1.0e-12)); return p[0] < 0.5; };
            assert(near(FindSubsurfaceInternalBoundary(origin, along, ticket, 0.0002, 1.0, 1.0, crossings, owns, sliver, steep, fail).distance, 0.5));
        }
        // Material past the last reported crossing is a missed root, and a ray crossing nothing says so.
        bool thrown = false;
        try { exitOf({0.3}, [](const Vector3d &p) { return p[0] < 0.9; }); } catch (const std::runtime_error &) { thrown = true; }
        assert(thrown && failure == SubsurfaceExitFailure::MissedCrossing);
        thrown = false;
        try { exitOf({}, [](const Vector3d &) { return false; }); } catch (const std::runtime_error &) { thrown = true; }
        assert(thrown && failure == SubsurfaceExitFailure::NoCrossing);
    }
    {
        // A ray leaving a sphere inside a merged superellipsoid: the superellipsoid crosses in and out between two plane
        // samples that both lie outside it, and both crossings must be reported for the merged exit to exist.
        auto *slab = new Superellipsoid;
        slab->Power = Vector3d(2.0 / 0.3, 1.0, 2.0 / 0.3);
        const Vector3d slabScale(0.015, 0.008, 0.01);
        Compute_Scaling_Transform(&transform, slabScale);
        slab->Scale(slabScale, &transform);
        auto *knob = new Sphere;
        knob->Center = Vector3d(0.012, 0.004, 0.0);
        knob->Radius = 0.006;
        CSGMerge merged;
        merged.children = {slab, knob};
        for (auto *child : merged.children)
        {
            child->Type |= IS_CHILD_OBJECT;
            child->Compute_BBox();
        }
        merged.Compute_BBox();
        const Vector3d p(0.0152969118, 0.004842223, -0.0044575201), d = Vector3d(-0.536230093, 0.808588993, -0.242159301).normalized();
        auto find = [&](Intersection &hit, const Ray &ray, const RayObjectCondition &condition) {
            return Find_Intersection(&hit, &merged, ray, condition, &thread);
        };
        auto inMerged = [&](const Vector3d &q) { return merged.Inside(q, &thread); };
        auto owns = [](const Intersection &) { return true; };
        auto outward = [&](Intersection &hit, Vector3d &normal) {
            hit.Object->Normal(normal, &hit, &thread);
            normal.normalize();
        };
        auto fail = [](SubsurfaceExitFailure reason, double) { throw std::runtime_error(SubsurfaceExitFailureText(reason)); };
        const PreciseInsideScope precision(&thread);
        const auto exit = FindSubsurfaceInternalBoundary(p, d, ticket, 0.0002, 0.05, 0.05, find, owns, inMerged, outward, fail);
        // independent: the union of the implicit superellipsoid and the sphere, bisected from the last inside sample
        auto member = [&](double t) {
            const Vector3d q = p + d * t;
            const double a = std::pow(std::abs(q[0] / 0.015), 2.0 / 0.3) + std::pow(std::abs(q[2] / 0.01), 2.0 / 0.3);
            return a + std::pow(std::abs(q[1] / 0.008), 2.0 / 0.3) < 1.0 || (q - knob->Center).length() < 0.006;
        };
        double low = 0.0, high = 0.0;
        for (double t = 0.0; t < 0.05; t += 1e-5)
            if (!member(t)) { high = t; break; } else low = t;
        for (int i = 0; i < 80; ++i)
            (member(0.5 * (low + high)) ? low : high) = 0.5 * (low + high);
        assert(std::abs(exit.distance - low) < 1e-9 && dot(exit.normal, d) > 0.0);
    }
    CSGMerge junction;
    auto *palm = new Sphere;
    const Vector3d palmScale(0.025, 0.028, 0.009), translation(0.042, 0.48, 0.0);
    Compute_Scaling_Transform(&transform, palmScale);
    palm->Scale(palmScale, &transform);
    Compute_Translation_Transform(&transform, Vector3d(0.0, -0.006, 0.0));
    palm->Translate(Vector3d(0.0, -0.006, 0.0), &transform);
    auto *wrist = new Cone;
    wrist->apex = Vector3d(0.0, -0.026, 0.0);
    wrist->apex_radius = 0.015;
    wrist->base = Vector3d(0.0, -0.05, 0.001);
    wrist->base_radius = 0.011;
    wrist->Compute_Cone_Data();
    junction.children = {palm, wrist};
    Compute_Translation_Transform(&transform, translation);
    junction.Translate(translation, &transform);
    junction.interior = std::make_shared<Interior>();
    junction.interior->boundsLow = translation + Vector3d(-0.05, -0.06, -0.012);
    junction.interior->boundsHigh = translation + Vector3d(0.05, 0.06, 0.012);
    for (auto *child : junction.children)
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
    const Vector3d thinPoint(0.042, 0.4546244581705951, 0.0149869960962902);
    TraceThreadData otherThread(scene, 2, false);
    assert(!thread.preciseInside && !otherThread.preciseInside);
    assert(!wrist->Inside(thinPoint, &thread));
    {
        const PreciseInsideScope outerPrecision(&thread);
        assert(wrist->Inside(thinPoint, &thread));
        assert(!wrist->Inside(thinPoint, &otherThread));
        assert(!wrist->Inside(thinPoint, nullptr));
        {
            const PreciseInsideScope nestedPrecision(&thread);
            assert(wrist->Inside(thinPoint, &thread));
        }
        assert(thread.preciseInside);
        try
        {
            const PreciseInsideScope nestedPrecision(&thread);
            throw 1;
        }
        catch (int)
        {
        }
        assert(thread.preciseInside);
    }
    assert(!thread.preciseInside && !wrist->Inside(thinPoint, &thread));
    try
    {
        const PreciseInsideScope precision(&thread);
        throw 1;
    }
    catch (int)
    {
    }
    assert(!thread.preciseInside && !otherThread.preciseInside);
    JunctionOracle oracle{translation};
    size_t checked = 0;
    auto check = [&](const Vector3d &p, const Vector3d &d, bool reflect) {
        const double expected = oracle.Exit(p, d);
        assert(expected >= 0.0);
        try
        {
            const auto hit = trace.SubsurfaceInternalBoundaryAt(p, d, palm, &junction, ticket);
            const double error = std::max(2e-10, expected * 1e-7);
            if (std::abs(hit.distance - expected) > error || !(dot(hit.normal, d) > 0.0))
            {
                std::cerr << "ORACLE MISMATCH " << p[0] << " " << p[1] << " " << p[2] << " dir " << d[0] << " " << d[1] << " " << d[2]
                          << " actual " << hit.distance << " expected " << expected << "\n";
                assert(false);
            }
            ++checked;
            if (reflect)
            {
                const Vector3d reflected = d - hit.normal * (2.0 * dot(d, hit.normal));
                const double after = oracle.Exit(hit.position, reflected, true);
                if (!(after >= 0.0))
                {
                    std::cerr << "REFLECTED ORACLE MISS " << hit.position[0] << " " << hit.position[1] << " " << hit.position[2] << " dir "
                              << reflected[0] << " " << reflected[1] << " " << reflected[2] << "\n";
                    assert(false);
                }
                {
                    const auto next = trace.SubsurfaceInternalBoundaryAt(hit.position, reflected, palm, &junction, ticket);
                    if (!(std::abs(next.distance - after) < std::max(2e-10, after * 1e-7)))
                    {
                        std::cerr << "REFLECTED MISMATCH " << hit.position[0] << " " << hit.position[1] << " " << hit.position[2] << " dir "
                                  << reflected[0] << " " << reflected[1] << " " << reflected[2] << " actual " << next.distance
                                  << " expected " << after << "\n";
                        assert(false);
                    }
                    assert(dot(next.normal, reflected) > 0.0);
                    ++checked;
                }
            }
        }
        catch (...)
        {
            std::cerr << "ORACLE FAILURE " << p[0] << " " << p[1] << " " << p[2] << " dir " << d[0] << " " << d[1] << " " << d[2]
                      << " expected " << expected << " reflect " << reflect << "\n";
            throw;
        }
    };
    struct Captured
    {
        double column;
        Vector3d point, direction;
    };
    const Captured captured[] = {{0.042,
                                  {0.040691503042077144, 0.45369542758626968, -0.0059033021315275259},
                                  {-0.65191523398252094, 0.12302035982341911, -0.74824629552736943}},
                                 {0.187,
                                  {0.16971670340833381, 0.46005797552936667, -0.00050258162568812001},
                                  {0.78329591201477244, -0.42235259139271064, 0.45614230538814082}},
                                 {0.042,
                                  {0.049494887269371915, 0.45285845663710883, -0.0057527788844657412},
                                  {-0.53967534507717929, -0.65679013301398648, 0.52666615905267478}},
                                 {0.187,
                                  {0.18276372291018075, 0.45000229486034726, -0.0032025679733688056},
                                  {-0.31422183103365481, -0.41557795113983043, -0.8535570323231354}},
                                 {0.187,
                                  {0.18178982648340727, 0.44992380402823129, -0.011457814068781021},
                                  {0.015562096398488205, 0.79948263495491556, -0.60048758318655482}},
                                 {0.042,
                                  {0.05313648571798462, 0.44995952347054302, -0.0079095345779698137},
                                  {-0.34049746180401003, 0.87245536155978076, -0.35054688786326293}},
                                 {0.042,
                                  {0.031791497744236619, 0.44990516870784891, -0.0095384323261942378},
                                  {-0.42976658016073799, 0.35753977165805562, -0.82913569351442673}},
                                 {0.187,
                                  {0.19485870062649494, 0.45020998701212572, -0.008649037246441546},
                                  {-0.69262029864384689, -0.72125860575189316, -0.0079464165095627076}}};
    for (const auto &ray : captured)
    {
        const Vector3d p = ray.point - Vector3d(ray.column - translation[0], 0.0, 0.0);
        assert(oracle.Exit(p, ray.direction) > 0.0);
        check(p, ray.direction, true);
    }
    for (int shape = 0; shape < 2; ++shape)
        for (int angle = 0; angle < 12; ++angle)
        {
            const double phi = 2.0 * M_PI * angle / 12.0;
            Vector3d surface, normal, tangent, offsetDirection;
            if (shape == 0)
            {
                surface = translation + Vector3d(0.025 * std::cos(phi), -0.006 + 0.028 * std::sin(phi), 0.0);
                normal = Vector3d(std::cos(phi) / 0.025, std::sin(phi) / 0.028, 0.0).normalized();
                tangent = Vector3d(0.0, 0.0, 1.0);
                offsetDirection = -normal;
            }
            else
            {
                const Vector3d axis = Vector3d(0.0, -0.024, 0.001).normalized();
                const Vector3d radial = Vector3d(std::cos(phi), std::sin(phi) * axis[2], -std::sin(phi) * axis[1]);
                const double u = (angle % 3) * 0.5;
                surface = translation + Vector3d(0.0, -0.026, 0.0) + axis * (u * std::sqrt(0.000577)) + radial * (0.015 - 0.004 * u);
                normal = (radial + axis * (0.004 / std::sqrt(0.000577))).normalized();
                tangent = (axis - normal * dot(axis, normal)).normalized() * (u > 0.5 ? -1.0 : 1.0);
                offsetDirection = -normal + axis * (u == 0.0 ? 1.0 : (u == 1.0 ? -1.0 : 0.0));
            }
            for (int power = 0; power <= 6; ++power)
                for (double offset : {1e-4, 1e-7, 1e-10, 1e-13, 1e-15})
                {
                    const double cosine = std::pow(0.1, power);
                    const Vector3d d = -normal * cosine + tangent * std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
                    const Vector3d p = surface + offsetDirection * offset;
                    if (oracle.Inside(oracle.Along(p, d, 0.0L)))
                        check(p, d, true);
                }
        }
    {
        const Vector3d axis = Vector3d(0.0, -0.024, 0.001).normalized();
        const Vector3d radial(0.0, axis[2], -axis[1]);
        const Vector3d centre = translation + Vector3d(0.0, -0.038, 0.0005);
        for (int cap = 0; cap < 2; ++cap)
            for (double sign : {-1.0, 1.0})
            {
                const Vector3d rim =
                    translation + Vector3d(0.0, -0.026, 0.0) + axis * (cap * std::sqrt(0.000577)) + radial * (sign * (0.015 - 0.004 * cap));
                check(centre, (rim - centre).normalized(), true);
            }
    }
    for (double gap : {-1e-9, -1e-12, 0.0, 1e-12, 1e-9})
    {
        CSGMerge pair;
        auto *first = new Sphere;
        auto *second = new Sphere;
        first->Radius = second->Radius = 0.01;
        first->Center = translation;
        second->Center = translation + Vector3d(0.02 + gap, 0.0, 0.0);
        pair.children = {first, second};
        pair.interior = std::make_shared<Interior>();
        pair.interior->boundsLow = translation - Vector3d(0.01);
        pair.interior->boundsHigh = translation + Vector3d(0.04, 0.01, 0.01);
        for (auto *child : pair.children)
        {
            child->Type |= IS_CHILD_OBJECT;
            child->interior = pair.interior;
            child->Compute_BBox();
        }
        pair.Compute_BBox();
        scene->preparedSets.front()->objects = {&pair};
        const Vector3d direction(1.0, 0.0, 0.0);
        const auto hit = trace.SubsurfaceInternalBoundaryAt(translation, direction, first, &pair, ticket);
        const double expected = gap > 0.0 ? 0.01 : 0.03 + gap;
        assert(std::abs(hit.distance - expected) < 1e-14);
        assert(dot(hit.normal, direction) > 0.0);
        const auto reflected = trace.SubsurfaceInternalBoundaryAt(hit.position, -direction, first, &pair, ticket);
        assert(std::abs(reflected.distance - (gap > 0.0 ? 0.02 : 0.04 + gap)) < 1e-14);
        assert(dot(reflected.normal, -direction) > 0.0);
        scene->preparedSets.front()->objects = {&junction};
    }
    for (bool cylinder : {false, true})
        for (bool difference : {false, true})
            for (double inset : {1e-6, 1e-9, 1e-12, 1e-13})
            {
                CSGIntersection clipped(difference);
                auto *frustum = static_cast<Cone *>(wrist->Copy());
                if (cylinder)
                {
                    delete frustum;
                    frustum = new Cone;
                    frustum->Cylinder();
                    frustum->apex = translation + Vector3d(0.0, -0.026, 0.0);
                    frustum->base = translation + Vector3d(0.0, -0.05, 0.001);
                    frustum->apex_radius = frustum->base_radius = 0.015;
                    frustum->Compute_Cylinder_Data();
                }
                auto *clip = new Sphere;
                const Vector3d axis = Vector3d(0.0, -0.024, 0.001).normalized();
                const Vector3d apex = translation + Vector3d(0.0, -0.026, 0.0);
                clip->Center = apex + axis * 0.001;
                clip->Radius = 0.001 - inset;
                if (difference)
                    Set_Flag(clip, INVERTED_FLAG);
                clipped.children = {frustum, clip};
                clipped.interior = std::make_shared<Interior>();
                clipped.interior->boundsLow = junction.interior->boundsLow;
                clipped.interior->boundsHigh = junction.interior->boundsHigh;
                for (auto *child : clipped.children)
                {
                    child->Type |= IS_CHILD_OBJECT;
                    child->interior = clipped.interior;
                    child->Compute_BBox();
                }
                clipped.Compute_BBox();
                scene->preparedSets.front()->objects = {&clipped};
                const Vector3d p = difference ? apex + axis * (0.5 * inset) : clip->Center;
                const Vector3d d = difference ? axis : -axis;
                const double expected = difference ? 0.5 * inset : clip->Radius;
                const auto hit = trace.SubsurfaceInternalBoundaryAt(p, d, frustum, &clipped, ticket);
                assert(std::abs(hit.distance - expected) < 1e-14);
                assert(dot(hit.normal, d) > 0.0);
                const Vector3d reflected = d - hit.normal * (2.0 * dot(d, hit.normal));
                const auto next = trace.SubsurfaceInternalBoundaryAt(hit.position, reflected, frustum, &clipped, ticket);
                assert(std::abs(next.distance - (difference ? inset : 2.0 * clip->Radius)) < 1e-14);
                assert(dot(next.normal, reflected) > 0.0);
                scene->preparedSets.front()->objects = {&junction};
            }
    {
        CSGMerge duplicate;
        duplicate.children = {wrist->Copy(), wrist->Copy()};
        duplicate.interior = std::make_shared<Interior>();
        duplicate.interior->boundsLow = junction.interior->boundsLow;
        duplicate.interior->boundsHigh = junction.interior->boundsHigh;
        for (auto *child : duplicate.children)
        {
            child->Type |= IS_CHILD_OBJECT;
            child->interior = duplicate.interior;
            child->Compute_BBox();
        }
        duplicate.Compute_BBox();
        scene->preparedSets.front()->objects = {&duplicate};
        const Vector3d p = translation + Vector3d(0.0, -0.038, 0.0005), d(1.0, 0.0, 0.0);
        const auto hit = trace.SubsurfaceInternalBoundaryAt(p, d, duplicate.children.front(), &duplicate, ticket);
        assert(std::abs(hit.distance - 0.013) < 1e-14);
        scene->preparedSets.front()->objects = {&junction};
    }
    {
        // A material no placed object carries has no crossings to find: the search fails loudly.
        CSGMerge empty;
        Sphere stray;
        stray.interior = std::make_shared<Interior>();
        stray.interior->boundsLow = junction.interior->boundsLow;
        stray.interior->boundsHigh = junction.interior->boundsHigh;
        bool failed = false;
        try
        {
            trace.SubsurfaceInternalBoundaryAt(point, direction, &stray, &empty, ticket);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        assert(failed && !thread.preciseInside && outer.GetRootQuery() == &previous);
    }
    {
        // Coincident and coplanar faces inside CSG: equal spheres, halves cut on one plane, boxes sharing a face and a
        // cutter flush with the host. Each exit, from points on the seams as well as off them, must be where the solid ends.
        const double r = 0.01;
        auto child = [&](ObjectPtr object, CSG &csg) {
            object->Type |= IS_CHILD_OBJECT;
            object->interior = csg.interior;
            object->Compute_BBox();
            csg.children.push_back(object);
        };
        auto sphere = [&]() {
            auto *s = new Sphere;
            s->Center = translation;
            s->Radius = r;
            return s;
        };
        auto box = [&](const Vector3d &low, const Vector3d &high) {
            auto *b = new Box;
            b->bounds[0] = translation + low;
            b->bounds[1] = translation + high;
            return b;
        };
        auto half = [&](double side) {
            auto *cut = new CSGIntersection(false);
            cut->children = {sphere()};
            auto *plane = new Plane;
            plane->Normal_Vector = Vector3d(side, 0.0, 0.0);
            plane->Distance = -side * translation[X];
            cut->children.push_back(plane);
            for (auto *c : cut->children)
            {
                c->Type |= IS_CHILD_OBJECT;
                c->Compute_BBox();
            }
            cut->Compute_BBox();
            return cut;
        };
        auto inSphere = [&](const Vector3d &q) { return (q - translation).lengthSqr() < r * r; };
        auto inBox = [&](const Vector3d &q, const Vector3d &low, const Vector3d &high) {
            const Vector3d l = q - translation;
            return l[X] > low[X] && l[X] < high[X] && l[Y] > low[Y] && l[Y] < high[Y] && l[Z] > low[Z] && l[Z] < high[Z];
        };
        const Vector3d slotLow(-0.004, 0.004, -0.02), slotHigh(0.004, r, 0.02);
        std::uint64_t state = 0x9e3779b97f4a7c15ULL;
        auto uniform = [&]() {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return double(state >> 11) * (1.0 / 9007199254740992.0);
        };
        for (int shape = 0; shape < 4; ++shape)
        {
            CSG *csg = (shape == 3) ? static_cast<CSG *>(new CSGIntersection(true)) : static_cast<CSG *>(new CSGMerge);
            csg->interior = std::make_shared<Interior>();
            csg->interior->boundsLow = translation - Vector3d(r);
            csg->interior->boundsHigh = translation + Vector3d(r);
            std::function<bool(const Vector3d &)> member;
            switch (shape)
            {
            case 0: // two equal spheres on one centre
                child(sphere(), *csg);
                child(sphere(), *csg);
                member = inSphere;
                break;
            case 1: // a sphere's halves cut on the plane x = 0
                child(half(1.0), *csg);
                child(half(-1.0), *csg);
                for (auto *c : csg->children)
                    for (auto *g : static_cast<CSG *>(c)->children)
                        g->interior = csg->interior;
                member = inSphere;
                break;
            case 2: // a box's halves sharing the face x = 0
                child(box(Vector3d(-r), Vector3d(0.0, r, r)), *csg);
                child(box(Vector3d(0.0, -r, -r), Vector3d(r)), *csg);
                member = [&](const Vector3d &q) { return inBox(q, Vector3d(-r), Vector3d(r)); };
                break;
            default: // a box less a slot whose top is flush with the box's top
                child(box(Vector3d(-r), Vector3d(r)), *csg);
                child(box(slotLow, slotHigh), *csg);
                Set_Flag(csg->children.back(), INVERTED_FLAG);
                member = [&](const Vector3d &q) { return inBox(q, Vector3d(-r), Vector3d(r)) && !inBox(q, slotLow, slotHigh); };
                break;
            }
            csg->Compute_BBox();
            scene->preparedSets.front()->objects = {csg};
            for (int k = 0; k < 400; ++k)
            {
                Vector3d p;
                do
                {
                    p = translation + Vector3d(2.0 * uniform() - 1.0, 2.0 * uniform() - 1.0, 2.0 * uniform() - 1.0) * r;
                    if (k % 2 == 0)
                        p[X] = translation[X]; // on the seam plane
                } while (!member(p));
                const double zc = 2.0 * uniform() - 1.0, ring = std::sqrt(std::max(0.0, 1.0 - zc * zc)), phi = 2.0 * M_PI * uniform();
                const Vector3d d(ring * std::cos(phi), ring * std::sin(phi), zc);
                ObjectPtr part = csg->children[(k / 2) % 2 == 0 || shape == 3 ? 0 : 1];
                while (dynamic_cast<CSG *>(part) != nullptr)
                    part = static_cast<CSG *>(part)->children.front();
                const auto hit = trace.SubsurfaceInternalBoundaryAt(p, d, part, csg, ticket);
                double low = 0.0, high = 0.0;
                for (double t = 0.0; t < 0.05; t += 2e-5)
                    if (!member(p + d * t)) { high = t; break; } else low = t;
                for (int i = 0; i < 80; ++i)
                    (member(p + d * (0.5 * (low + high))) ? low : high) = 0.5 * (low + high);
                assert(std::abs(hit.distance - low) < 1e-12 && dot(hit.normal, d) > 0.0);
                ++checked;
            }
            scene->preparedSets.front()->objects = {&junction};
        }
    }
    {
        // Shallow concave faces dip a grazing ray in and out between two plane samples that both lie outside, the dip
        // anywhere in that bracket: at its near end, its middle or its far end. A precise query must report every crossing
        // an independent fine scan of the implicit finds, at scene scales 1 and 1000.
        struct Precise final : RayRootQuery
        {
            bool Admit(ConstObjectPtr, double) const override { return true; }
            bool AllRoots() const override { return true; }
            bool PreciseBoundary() const override { return true; }
        } precise;
        const double exponent = 2.0 / 2.2;  // e = n = 2.2: faces just concave
        std::mt19937_64 random(1829);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        size_t dips = 0, thirds[3] = {0, 0, 0};
        for (double size : {1.0, 1000.0})
        {
            Superellipsoid star;
            star.Power = Vector3d(exponent, 1.0, exponent);
            TRANSFORM scaling;
            Compute_Scaling_Transform(&scaling, Vector3d(size));
            star.Scale(Vector3d(size), &scaling);
            star.Compute_BBox();
            auto field = [&](const Vector3d &q) {
                const Vector3d u = q / size;
                return std::pow(std::abs(u[0]), exponent) + std::pow(std::abs(u[1]), exponent) + std::pow(std::abs(u[2]), exponent) - 1.0;
            };
            for (int n = 0; n < 4000; ++n)
            {
                // a surface point on a face, a tangent across it and a slight tilt inwards
                const Vector3d axis = Vector3d(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5).normalized();
                double low = 0.0, high = 2.0 * size;
                for (int i = 0; i < 80; ++i)
                    (field(axis * (0.5 * (low + high))) < 0.0 ? low : high) = 0.5 * (low + high);
                const Vector3d surface = axis * low;
                Vector3d gradient;
                for (int k = 0; k < 3; ++k)
                    gradient[k] = (surface[k] == 0.0) ? 0.0 :
                        exponent * std::pow(std::abs(surface[k] / size), exponent - 1.0) * (surface[k] > 0.0 ? 1.0 : -1.0);
                if (!(gradient.length() > 0.0)) continue;
                gradient.normalize();
                Vector3d tangent = cross(gradient, Vector3d(unit(random) - 0.5, unit(random) - 0.5, unit(random) - 0.5));
                if (!(tangent.length() > 0.0)) continue;
                tangent.normalize();
                const Vector3d direction = (tangent - gradient * (0.02 * unit(random))).normalized();
                const Vector3d origin = surface - direction * (3.0 * size);
                // the oracle: every sign change of the implicit on a fine scan, bisected
                std::vector<double> roots;
                const int steps = 400000;
                const double reach = 6.0 * size;
                double before = field(origin), smallest = HUGE_VAL, last = -HUGE_VAL;
                for (int i = 1; i <= steps; ++i)
                {
                    const double t = reach * i / steps, value = field(origin + direction * t);
                    if ((before < 0.0) != (value < 0.0))
                    {
                        double a = reach * (i - 1) / steps, b = t;
                        for (int k = 0; k < 80; ++k)
                            (((field(origin + direction * (0.5 * (a + b))) < 0.0) == (before < 0.0)) ? a : b) = 0.5 * (a + b);
                        roots.push_back(0.5 * (a + b));
                        smallest = std::min(smallest, roots.back() - last);
                        last = roots.back();
                    }
                    before = value;
                }
                // a dip thinner than the scan cannot be checked
                if (roots.empty() || (smallest < 4.0 * reach / steps)) continue;
                Ray ray(ticket, origin, direction);
                ray.SetRootQuery(&precise);
                IStack stack(thread.stackPool);
                star.All_Intersections(ray, stack, &thread);
                std::vector<double> found;
                for (; stack->size() > 0; stack->pop())
                    found.push_back(stack->top().Depth);
                std::sort(found.begin(), found.end());
                // every crossing the scan finds must be reported; one the scan misses must still be a real sign change,
                // a dip thinner than the scan step, and come in pairs
                bool good = ((found.size() - roots.size()) % 2 == 0) && (found.size() >= roots.size());
                for (double r : roots)
                    good = good && std::any_of(found.begin(), found.end(), [&](double f) { return std::abs(f - r) < 1e-7 * size; });
                for (size_t k = 0; k < found.size(); ++k)
                {
                    double delta = 1e-9 * size;
                    if (k > 0) delta = std::min(delta, 0.25 * (found[k] - found[k - 1]));
                    if (k + 1 < found.size()) delta = std::min(delta, 0.25 * (found[k + 1] - found[k]));
                    good = good && (delta > 0.0) &&
                        ((field(origin + direction * (found[k] - delta)) < 0.0) != (field(origin + direction * (found[k] + delta)) < 0.0));
                }
                if (!good)
                {
                    std::cerr << "superellipsoid scale " << size << " ray " << n << ": " << found.size() << " crossings, the scan finds "
                              << roots.size() << "\n";
                    assert(false);
                }
                // the intersector's samples: its box's entry and exit and its nine planes through the centre
                std::vector<double> samples;
                {
                    const Vector3d o = origin / size;
                    double t1 = -HUGE_VAL, t2 = HUGE_VAL;
                    for (int k = 0; k < 3; ++k)
                    {
                        const double a = (-1.01 - o[k]) / direction[k], b = (1.01 - o[k]) / direction[k];
                        t1 = std::max(t1, std::min(a, b));
                        t2 = std::min(t2, std::max(a, b));
                    }
                    samples = {t1, t2};
                    const double planes[9][3] = {{1, 1, 0}, {1, -1, 0}, {1, 0, 1}, {1, 0, -1}, {0, 1, 1}, {0, 1, -1},
                                                 {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                    for (const auto &plane : planes)
                    {
                        const Vector3d normal(plane[0], plane[1], plane[2]);
                        const double t = -dot(normal, o) / dot(normal, direction);
                        if ((t > t1) && (t < t2)) samples.push_back(t);
                    }
                    std::sort(samples.begin(), samples.end());
                    for (double &t : samples) t *= size;
                }
                // a dip whose two crossings fall between the same two samples, by where it lies in that bracket; with
                // these just-concave faces the field turns once between samples, so the dips run out of the solid
                for (size_t k = 0; k + 1 < roots.size(); ++k)
                {
                    for (size_t j = 0; j + 1 < samples.size(); ++j)
                        if ((samples[j] < roots[k]) && (roots[k + 1] < samples[j + 1]))
                        {
                            const double where = (0.5 * (roots[k] + roots[k + 1]) - samples[j]) / (samples[j + 1] - samples[j]);
                            ++thirds[std::min(2, int(where * 3.0))];
                            ++dips;
                        }
                }
                ++checked;
            }
        }
        std::cout << "Superellipsoid dips inside one sample bracket " << dips << " (near, middle, far third: " << thirds[0] << " " << thirds[1] << " " << thirds[2] << ")" << std::endl;
        assert(dips > 30 && thirds[0] > 5 && thirds[1] > 5 && thirds[2] > 5);
    }
    assert(!thread.preciseInside);
    assert(checked > 1000);
    std::cout << "Internal subsurface boundary oracle checks " << checked << " passed\n";

    Free_Noise_Tables();
}
