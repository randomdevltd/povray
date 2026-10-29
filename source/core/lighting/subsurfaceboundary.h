#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEBOUNDARY_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEBOUNDARY_H

#include "core/configcore.h"
#include "core/coretypes.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace pov
{

inline bool SubsurfaceFiniteBounds(const Vector3d& low, const Vector3d& high)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const double extent = high[axis] - low[axis];
        if (!(extent > 0.0) || !std::isfinite(extent) || !(extent < BOUND_HUGE * 0.5)) return false;
    }
    return true;
}

inline Vector3d SubsurfaceBoundaryProbe(const Vector3d& point, const Vector3d& direction, double tolerance, double extent)
{
    const double coordinates = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    const double step = std::max(tolerance, 32.0 * std::numeric_limits<double>::epsilon() * std::max(extent, coordinates));
    Vector3d result = point + direction * step;
    for (int axis = 0; axis < 3; ++axis)
        if (result[axis] == point[axis] && direction[axis] != 0.0)
            result[axis] = std::nextafter(point[axis], direction[axis] > 0.0 ? HUGE_VAL : -HUGE_VAL);
    return result;
}

}

#endif
