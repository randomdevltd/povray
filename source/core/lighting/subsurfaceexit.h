#ifndef POVRAY_CORE_SUBSURFACEEXIT_H
#define POVRAY_CORE_SUBSURFACEEXIT_H

#include <cmath>

namespace pov
{

inline double SubsurfaceExitRatio(double cosine, double eta)
{
    const double g2 = (eta - 1.0) * (eta + 1.0) + cosine * cosine;
    if (!(g2 > 0.0)) return 0.0;
    const double g = std::sqrt(g2);
    const double rs = (cosine - g) / (cosine + g);
    const double rp = (eta * eta * cosine - g) / (eta * eta * cosine + g);
    return (1.0 - 0.5 * (rs * rs + rp * rp)) * (1.0 + eta) * (1.0 + eta) / (4.0 * eta);
}

inline double SubsurfaceExitFluxFactor(double eta)
{
    constexpr unsigned count = 1024;
    double integral = 0.0;
    for (unsigned i = 0; i <= count; ++i)
    {
        const double mu = double(i) / count;
        const double weight = (i == 0 || i == count) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        integral += weight * mu * SubsurfaceExitRatio(mu, eta);
    }
    return 2.0 * integral / (3.0 * count);
}

}

#endif
