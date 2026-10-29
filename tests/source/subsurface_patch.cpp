#include "core/lighting/subsurfacepatch.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{

// A dipole's real source at depth z with transport coefficient sigma, and its closed-form integral over a disc.
const double kSigma = 1.0, kDepth = 0.2;

double Profile(double radius)
{
    const double u = std::sqrt(radius * radius + kDepth * kDepth);
    return kDepth * (1.0 + kSigma * u) * std::exp(-kSigma * u) / (2.0 * M_PI * u * u * u);
}

double Cumulative(double radius)
{
    const double u = std::sqrt(radius * radius + kDepth * kDepth);
    return std::exp(-kSigma * kDepth) - kDepth * std::exp(-kSigma * u) / u;
}

double Share(double s, double d, double r)
{
    if (d <= 0.0)
        return (s < r) ? 1.0 : 0.0;
    return std::acos(std::max(-1.0, std::min(1.0, (s * s + d * d - r * r) / (2.0 * s * d)))) / M_PI;
}

// The same integral by fine quadrature over each span where the share is smooth, clustered at the span's ends.
double Reference(double d, double r, double h, double from)
{
    double edges[3] = { from, std::max(from, std::fabs(d - r)), std::max(from, d + r) };
    double sum = 0.0;
    const int steps = 200000;
    for (int span = 0; span < 2; span++)
    {
        const double a = edges[span], b = edges[span + 1];
        for (int i = 0; (b > a) && (i < steps); i++)
        {
            const double t = M_PI * (i + 0.5) / steps, s = a + (b - a) * 0.5 * (1.0 - std::cos(t));
            const double ds = (b - a) * 0.5 * std::sin(t) * M_PI / steps;
            sum += Profile(std::sqrt(s * s + h * h)) * 2.0 * M_PI * s * Share(s, d, r) * ds;
        }
    }
    return sum;
}

}

int main()
{
    const double radii[] = { 0.05, 0.3, 1.0, 3.0, 13.0 };
    double worst = 0.0;
    int cases = 0;
    for (double r : radii)
    for (double place : { 0.0, 0.5, 1.0, 2.0 })
    for (double h : { 0.0, 0.3 })
    for (double from : { 0.0, 0.5 * std::min(r, 1.0) })
    {
        const double d = place * r + ((place == 2.0) ? 1.0 : 0.0);
        auto annulus = [h](double s0, double s1)
        {
            return Cumulative(std::sqrt(s1 * s1 + h * h)) - Cumulative(std::sqrt(s0 * s0 + h * h));
        };
        const double value = pov::SubsurfaceDiscIntegral<double>(annulus, d, r, from, HUGE_VAL, kDepth, 1.0 / kSigma);
        const double reference = Reference(d, r, h, from);
        if (!(reference > 1e-9 * Cumulative(HUGE_VAL)))
            continue;
        const double error = std::fabs(value / reference - 1.0);
        worst = std::max(worst, error);
        cases++;
        if (error > 0.01)
            std::fprintf(stderr, "subsurface_patch: r %g d %g h %g from %g: %.6g against %.6g\n", r, d, h, from, value, reference);
    }
    std::fprintf(stderr, "subsurface_patch: %d discs within %.2e of the fine quadrature\n", cases, worst);
    assert(worst < 0.01);
    return 0;
}
