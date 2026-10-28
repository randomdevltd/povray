#ifndef POVRAY_CORE_CURVEDRAYSTEP_H
#define POVRAY_CORE_CURVEDRAYSTEP_H

#include "core/configcore.h"
#include "core/math/finite.h"
#include "core/math/vector.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pov
{

struct CurvedRayTrial final
{
    Vector3d next, turned, middleCurve;
};

template<class Bend>
CurvedRayTrial MidpointCurvedRayStep(const Vector3d& point, const Vector3d& direction,
                                   const Vector3d& curve, double step, Bend&& bend)
{
    const Vector3d half = (direction + (0.5 * step) * curve).normalized();
    const Vector3d middleCurve = bend(point + (0.5 * step) * direction, half);
    return {point + step * half, (direction + step * middleCurve).normalized(), middleCurve};
}

template<class Bend, class Intersect>
bool RefineCurvedRayBoundary(const Vector3d& point, const Vector3d& direction, const Vector3d& curve,
                            double step, double distance, Bend&& bend, Intersect&& intersect,
                            CurvedRayTrial& trial)
{
    const double scale = std::max(1.0, std::max(point.length(), step));
    const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() * scale;
    double clipped = distance;
    for (int iteration = 0; iteration < 12; ++iteration)
    {
        if (!(clipped > 0.0) || clipped > step + tolerance || !IsFiniteNumber(clipped))
            return false;
        trial = MidpointCurvedRayStep(point, direction, curve, clipped, bend);
        const double length = (trial.next - point).length();
        if (!(length > 0.0) || !intersect(trial, distance) || !(distance > 0.0))
            return false;
        if (std::abs(distance - length) <= tolerance)
            return true;
        clipped *= distance / length;
    }
    return false;
}

}

#endif
