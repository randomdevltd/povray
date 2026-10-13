#ifndef POVRAY_CORE_PHOTONBEAM_H
#define POVRAY_CORE_PHOTONBEAM_H

#include <algorithm>
#include <cmath>

namespace pov
{

inline bool ClipPhotonBeam(double projection, double perpendicular2, double radius, double length,
                           double& first, double& last)
{
    const double chord2 = radius * radius - perpendicular2;
    if (chord2 <= 0.0)
        return false;
    const double chord = std::sqrt(chord2);
    first = std::max(0.0, projection - chord);
    last = std::min(length, projection + chord);
    return last > first;
}

inline double PhotonBeamIntegral(double extinction, double first, double last)
{
    const double length = last - first;
    if (extinction == 0.0)
        return length;
    return std::exp(-extinction * first) * -std::expm1(-extinction * length) / extinction;
}

}

#endif
