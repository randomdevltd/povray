#include "core/shape/sphere.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using namespace pov;
    for (double scale : {0.001, 0.1, 1.0, 10.0, 1000.0})
    {
        const double radius = 0.03 * scale;
        const Vector3d centre(0.0, 0.12 * scale, 0.0);
        const BasicRay ray(Vector3d(0.0), Vector3d(0.2499993055545911, 0.9682460158566222, 0.0));
        double entry, exit;
        assert(Sphere::Intersect(ray, centre, radius * radius, &entry, &exit));
        assert(entry > 0.0 && exit > entry);
        assert(std::abs(entry / scale - 0.1161188112246831) < 1.0e-10);
        assert(std::abs(exit / scale - 0.1162602325809062) < 1.0e-10);
        double localEntry, localExit;
        const BasicRay local((ray.Origin - centre) / radius, ray.Direction);
        assert(Sphere::Intersect(local, Vector3d(0.0), 1.0, &localEntry, &localExit));
        assert(std::abs(entry / radius - localEntry) < 1.0e-10);
        assert(std::abs(exit / radius - localExit) < 1.0e-10);
        for (double side : {-1.0, 1.0})
        {
            const BasicRay miss(Vector3d(side * radius * (1.0 + 1.0e-8), -radius * 4.0, 0.0), Vector3d(0.0, 1.0, 0.0));
            assert(!Sphere::Intersect(miss, Vector3d(0.0), radius * radius, &entry, &exit));
        }
        const BasicRay inside(centre, Vector3d(0.0, 1.0, 0.0));
        assert(Sphere::Intersect(inside, centre, radius * radius, &entry, &exit));
        assert(entry < 0.0 && exit > 0.0);
        const BasicRay behind(Vector3d(0.0), Vector3d(0.0, -1.0, 0.0));
        assert(!Sphere::Intersect(behind, centre, radius * radius, &entry, &exit));
    }
    double entry, exit;
    const BasicRay tangent(Vector3d(1.0, -4.0, 0.0), Vector3d(0.0, 1.0, 0.0));
    assert(!Sphere::Intersect(tangent, Vector3d(0.0), 1.0, &entry, &exit));
    // The root kernel accepts tiny positive depths; All_Intersections retains its separate self-hit cutoff.
    const BasicRay close(Vector3d(0.0, -2.0e-12, 0.0), Vector3d(0.0, 1.0, 0.0));
    assert(Sphere::Intersect(close, Vector3d(0.0), 1.0e-24, &entry, &exit));
    assert(entry > 0.0 && exit > entry);
    for (double distance : {100.0, 3.844e8, 1.496e11})
    {
        const double radius = 1.0;
        const BasicRay distant(Vector3d(0.0), Vector3d(0.0, 0.0, 1.0));
        assert(Sphere::Intersect(distant, Vector3d(0.0, 0.0, distance), radius * radius, &entry, &exit));
        assert(std::abs(entry - (distance - radius)) < 1e-6);
        assert(std::abs(exit - (distance + radius)) < 1e-6);
        const BasicRay offAxis(Vector3d(radius * 1.001, 0.0, 0.0), Vector3d(0.0, 0.0, 1.0));
        assert(!Sphere::Intersect(offAxis, Vector3d(0.0, 0.0, distance), radius * radius, &entry, &exit));
    }
    std::cout << "sphere_intersection: shallow chords, scale, misses, tangency and interior roots passed\n";
}
