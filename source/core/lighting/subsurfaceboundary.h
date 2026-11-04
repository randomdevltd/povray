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

template<typename Contains>
bool SubsurfaceBoundaryExit(const Vector3d& point, const Vector3d& direction, Vector3d& normal,
    double tolerance, double extent, Contains&& contains)
{
    const double coordinates = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    const double minimum = std::max(tolerance, 32.0 * std::numeric_limits<double>::epsilon() * std::max(extent, coordinates));
    enum class Side { Exit, Entry, Outside, Inside };
    auto classify = [&](double probe)
    {
        const bool plus = contains(SubsurfaceBoundaryProbe(point, normal, probe, extent));
        const bool minus = contains(SubsurfaceBoundaryProbe(point, -normal, probe, extent));
        if (plus != minus)
        {
            if (plus) normal.invert();
            return dot(normal, direction) > 0.0 ? Side::Exit : Side::Entry;
        }
        if (!plus) return Side::Outside;
        // An outward primitive normal can enter a sibling at a CSG seam while the ray exits.
        const bool after = contains(SubsurfaceBoundaryProbe(point, direction, probe, extent));
        const bool before = contains(SubsurfaceBoundaryProbe(point, -direction, probe, extent));
        if (after == before) return Side::Inside;
        if (after) return Side::Entry;
        if (dot(normal, direction) < 0.0) normal.invert();
        return dot(normal, direction) > 0.0 ? Side::Exit : Side::Entry;
    };
    double probe = std::max(tolerance, extent * 1.0e-8);
    for (int attempt = 0; attempt < 8; ++attempt, probe *= 4.0)
    {
        auto result = classify(probe);
        if (result == Side::Exit || result == Side::Entry) return result == Side::Exit;
        if (result == Side::Inside)
        {
            while (probe > minimum)
            {
                probe = std::max(minimum, probe * 0.25);
                result = classify(probe);
                if (result == Side::Exit || result == Side::Entry) return result == Side::Exit;
            }
            return false;
        }
    }
    return false;
}

}

#endif
