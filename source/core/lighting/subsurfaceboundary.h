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
    double tolerance, double extent, Contains&& contains,
    double originDistance = std::numeric_limits<double>::infinity(), bool inverted = false)
{
    const double coordinates = std::max(std::abs(point[0]), std::max(std::abs(point[1]), std::abs(point[2])));
    const double roundoff = 32.0 * std::numeric_limits<double>::epsilon() * std::max(extent, coordinates);
    const double minimum = std::max(tolerance, roundoff);
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
        // At corners both normal probes can be inside or outside while the ray crosses.
        const bool after = contains(SubsurfaceBoundaryProbe(point, direction, probe, extent));
        const bool before = contains(SubsurfaceBoundaryProbe(point, -direction, probe, extent));
        if (after == before)
        {
            const Vector3d contact = inverted ? -normal : normal;
            if (!after && probe <= roundoff && std::abs(originDistance) <= roundoff && dot(contact, direction) > 0.0)
            {
                normal = contact;
                return Side::Exit;
            }
            return plus ? Side::Inside : Side::Outside;
        }
        if (after) return Side::Entry;
        if (dot(normal, direction) < 0.0) normal.invert();
        return dot(normal, direction) > 0.0 ? Side::Exit : Side::Entry;
    };
    double probe = std::max(tolerance, extent * 1.0e-8);
    auto result = classify(probe);
    if (result == Side::Exit || result == Side::Entry) return result == Side::Exit;
    const bool outside = result == Side::Outside;
    for (double smaller = probe; smaller > minimum;)
    {
        smaller = std::max(minimum, smaller * 0.25);
        result = classify(smaller);
        if (result == Side::Exit || result == Side::Entry) return result == Side::Exit;
    }
    if (!outside) return false;
    for (int attempt = 1; attempt < 8; ++attempt)
    {
        probe *= 4.0;
        result = classify(probe);
        if (result == Side::Exit || result == Side::Entry) return result == Side::Exit;
        if (result == Side::Inside) return false;
    }
    return false;
}

}

#endif
