#ifndef POVRAY_CORE_LIGHTING_SUBSURFACEPATCH_H
#define POVRAY_CORE_LIGHTING_SUBSURFACEPATCH_H

#include <algorithm>
#include <cmath>

namespace pov
{

/// The area that a disc of radius `r`, centred `d` from the origin, shares with the disc of radius `s` about the origin.
inline double SubsurfaceLensArea(double s, double d, double r)
{
    if (!(s > 0.0) || !(r > 0.0) || !(s + r > d))
        return 0.0;
    if (!(d + s > r))
        return M_PI * s * s;
    if (!(d + r > s))
        return M_PI * r * r;
    const double a = std::acos(std::max(-1.0, std::min(1.0, (d * d + s * s - r * r) / (2.0 * d * s))));
    const double b = std::acos(std::max(-1.0, std::min(1.0, (d * d + r * r - s * s) / (2.0 * d * r))));
    const double k = (-d + s + r) * (d + s - r) * (d - s + r) * (d + s + r);
    return s * s * a + r * r * b - 0.5 * std::sqrt(std::max(0.0, k));
}

/// A radial profile integrated over a disc of radius `r` centred `d` away, between radii `from` and `limit`, from its
/// integrals over whole annuli `annulus(s0, s1)`, its depth `shortest` and decay length `decay`; see doc/PERF.md.
template <typename Colour, typename Annulus>
Colour SubsurfaceDiscIntegral(const Annulus& annulus, double d, double r, double from, double limit, double shortest, double decay)
{
    Colour sum = Colour();
    const double outer = std::min(d + r, limit);
    if (!(r > 0.0) || !(outer > from))
        return sum;
    auto add = [&](double s0, double s1)
    {
        const double ring = M_PI * (s1 * s1 - s0 * s0);
        const double share = (ring > 0.0) ? (SubsurfaceLensArea(s1, d, r) - SubsurfaceLensArea(s0, d, r)) / ring : 0.0;
        if (share > 0.0)
            sum += annulus(s0, s1) * std::min(1.0, share);
    };
    double s0 = std::min(outer, std::max(from, std::fabs(d - r)));
    if (s0 > from)
        add(from, s0);
    // Each annulus spans an eighth of the length its profile or the disc's edge changes over there.
    while (s0 < outer)
    {
        const double width = 0.125 * std::min(std::min(std::max(s0, shortest), decay), r);
        const double s1 = (width > 0.0) && (outer - s0 > 1.5 * width) ? s0 + width : outer;
        add(s0, s1);
        s0 = s1;
    }
    return sum;
}

}

#endif
