#ifndef POVRAY_CORE_MATERIAL_PHYSICALPHASE_H
#define POVRAY_CORE_MATERIAL_PHYSICALPHASE_H

#include "core/material/media.h"
#include <algorithm>
#include <cmath>

namespace pov
{
// The angular factor has spherical mean one; the physical phase is this divided by 4 pi.
inline double PhysicalPhaseFactor(int type, double eccentricity, double cosine)
{
    const double mu = std::max(-1.0, std::min(1.0, cosine));
    switch (type)
    {
        case RAYLEIGH_SCATTERING: return 0.75 * (1.0 + mu * mu);
        case MIE_HAZY_SCATTERING: return 0.5 * (1.0 + 9.0 * std::pow(0.5 * (1.0 + mu), 8.0));
        case MIE_MURKY_SCATTERING: return (33.0 / 83.0) * (1.0 + 50.0 * std::pow(0.5 * (1.0 + mu), 32.0));
        case HENYEY_GREENSTEIN_SCATTERING:
            return (1.0 - eccentricity * eccentricity) /
                std::pow(1.0 + eccentricity * eccentricity - 2.0 * eccentricity * mu, 1.5);
        default: return 1.0;
    }
}
}
#endif
