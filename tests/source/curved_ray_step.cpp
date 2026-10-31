#include "core/render/curvedraystep.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace pov;

struct Result final
{
    Vector3d point, direction;
    double screen;
    int steps;
};

static Result Integrate(double x, double h, double shape = 0.0, bool refine = true)
{
    Vector3d point(x, 0.0, 0.0), direction(0.0, 0.0, 1.0);
    auto surface = [&](const Vector3d& p) { return 1.0 + shape * p[X] * p[X]; };
    auto bend = [&](const Vector3d& p, const Vector3d& d) {
        if (p[Z] < 0.0 || p[Z] > surface(p))
            return Vector3d(0.0);
        const Vector3d gradient(0.1, 0.0, 0.0);
        return (gradient - dot(gradient, d) * d) / (1.1 + 0.1 * p[X]);
    };
    for (int steps = 1; steps < 10000; ++steps)
    {
        const Vector3d curve = bend(point, direction);
        CurvedRayTrial trial = MidpointCurvedRayStep(point, direction, curve, h, bend);
        auto intersect = [&](const CurvedRayTrial& candidate, double& distance) {
            const Vector3d d = (candidate.next - point).normalized();
            const double b = d[Z] - 2.0 * shape * point[X] * d[X];
            const double c = point[Z] - surface(point);
            const double a = -shape * d[X] * d[X];
            const double discriminant = b * b - 4.0 * a * c;
            if (!(discriminant > 0.0))
                return false;
            distance = -2.0 * c / (b + std::sqrt(discriminant));
            return distance > 0.0 && distance <= h;
        };
        double distance;
        if (intersect(trial, distance))
        {
            if (refine)
            {
                assert(RefineCurvedRayBoundary(point, direction, curve, h, distance, bend, intersect, trial));
                assert(intersect(trial, distance));
            }
            else
                trial.turned = ((1.0 - distance / h) * direction + (distance / h) * trial.turned).normalized();
            const Vector3d exit = point + distance * (trial.next - point).normalized();
            const double sine = (1.1 + 0.1 * exit[X]) * trial.turned[X];
            return {exit, trial.turned, exit[X] + sine / std::sqrt(1.0 - sine * sine), steps};
        }
        point = trial.next;
        direction = trial.turned;
    }
    assert(false);
    return {};
}

static double ExactScreen(double x)
{
    const double entry = 1.1 + 0.1 * x;
    const double a = 0.1 / entry;
    const double sine = entry * std::sinh(a);
    return x + entry / 0.1 * (std::cosh(a) - 1.0) + sine / std::sqrt(1.0 - sine * sine);
}

int main()
{
    // A full trial midpoint past the support boundary loses the remaining bend before clipping.
    const double x = -0.00625;
    const Result old = Integrate(x, 0.03, 0.0, false);
    assert(old.steps == 34);
    assert(std::abs(old.screen - 0.13876988889827008) < 1e-12);
    assert(std::abs(old.screen - ExactScreen(x)) > 0.001);
    for (double h : {0.03, 0.015, 0.0075, 0.015625})
    {
        const Result result = Integrate(x, h);
        assert(std::abs(result.point[Z] - 1.0) < 1e-12);
        assert(std::abs(result.screen - ExactScreen(x)) < 2e-6);
        const double invariant = (1.1 + 0.1 * result.point[X]) * result.direction[Z];
        assert(std::abs(invariant - (1.1 + 0.1 * x)) < 2e-7);
    }
    const Result reference = Integrate(x, 0.0005, 2.0);
    const Result coarse = Integrate(x, 0.03, 2.0);
    const Result fine = Integrate(x, 0.015, 2.0);
    assert(std::abs(coarse.point[Z] - (1.0 + 2.0 * coarse.point[X] * coarse.point[X])) < 1e-12);
    assert((fine.direction - reference.direction).length() < 0.3 * (coarse.direction - reference.direction).length());
    const Vector3d p(0.0), d(0.0, 0.0, 1.0), c(0.0);
    auto zero = [](const Vector3d&, const Vector3d&) { return Vector3d(0.0); };
    int queries = 0;
    auto missing = [&](const CurvedRayTrial&, double&) { ++queries; return false; };
    CurvedRayTrial trial;
    assert(!RefineCurvedRayBoundary(p, d, c, 0.03, 0.01, zero, missing, trial));
    assert(queries == 1);
    queries = 0;
    auto drifting = [&](const CurvedRayTrial& candidate, double& distance) {
        ++queries;
        distance = 0.99 * (candidate.next - p).length();
        return true;
    };
    assert(!RefineCurvedRayBoundary(p, d, c, 0.03, 0.01, zero, drifting, trial));
    assert(queries == 12);
    std::cout << "curved ray midpoint boundary tests passed\n";
}
