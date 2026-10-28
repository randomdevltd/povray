#ifndef POVRAY_CORE_HALFSPACEBOUNDS_H
#define POVRAY_CORE_HALFSPACEBOUNDS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace pov
{
namespace halfspace_bounds
{

inline std::uint64_t MagnitudeBits(double value)
{
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits & UINT64_C(0x7fffffffffffffff);
}
inline double WidenLow(double value)
{
    const std::uint64_t magnitude = MagnitudeBits(value);
    return magnitude != 0 && magnitude < UINT64_C(0x0010000000000000) ? -std::numeric_limits<double>::min() : value;
}
inline double WidenHigh(double value)
{
    const std::uint64_t magnitude = MagnitudeBits(value);
    return magnitude != 0 && magnitude < UINT64_C(0x0010000000000000) ? std::numeric_limits<double>::min() : value;
}
struct Interval
{
    double low, high;
    Interval(double value = 0.0) : low(WidenLow(value)), high(WidenHigh(value)) {}
    Interval(double lo, double hi) : low(WidenLow(lo)), high(WidenHigh(hi)) {}
};

inline double Down(double value)
{
    if (MagnitudeBits(value) <= UINT64_C(0x0010000000000000))
        return -std::numeric_limits<double>::min();
    return WidenLow(std::nextafter(value, -std::numeric_limits<double>::infinity()));
}
inline double Up(double value)
{
    if (MagnitudeBits(value) <= UINT64_C(0x0010000000000000))
        return std::numeric_limits<double>::min();
    return WidenHigh(std::nextafter(value, std::numeric_limits<double>::infinity()));
}
inline bool Finite(double value)
{
    return (MagnitudeBits(value) & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}
inline bool Finite(Interval value) { return Finite(value.low) && Finite(value.high); }
inline Interval Unknown()
{
    return Interval(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
}
inline Interval operator+(Interval a, Interval b)
{
    if (!Finite(a) || !Finite(b))
        return Unknown();
    if (a.low == 0.0 && a.high == 0.0)
        return b;
    if (b.low == 0.0 && b.high == 0.0)
        return a;
    volatile double lo = a.low + b.low, hi = a.high + b.high;
    return Interval(Down(lo), Up(hi));
}
inline Interval operator-(Interval a, Interval b)
{
    if (!Finite(a) || !Finite(b))
        return Unknown();
    if (b.low == 0.0 && b.high == 0.0)
        return a;
    volatile double lo = a.low - b.high, hi = a.high - b.low;
    return Interval(Down(lo), Up(hi));
}
inline Interval operator-(Interval a) { return Interval(-a.high, -a.low); }
inline Interval operator*(Interval a, Interval b)
{
    if (!Finite(a) || !Finite(b))
        return Unknown();
    if ((a.low == 0.0 && a.high == 0.0) || (b.low == 0.0 && b.high == 0.0))
        return Interval();
    volatile double p0 = a.low * b.low, p1 = a.low * b.high;
    volatile double p2 = a.high * b.low, p3 = a.high * b.high;
    return Interval(Down(std::min(std::min(p0, p1), std::min(p2, p3))),
                    Up(std::max(std::max(p0, p1), std::max(p2, p3))));
}
inline Interval operator/(Interval a, Interval b)
{
    if (!Finite(a) || !Finite(b) || !(b.low > 0.0 || b.high < 0.0))
        return Unknown();
    volatile double lo = 1.0 / b.high, hi = 1.0 / b.low;
    return a * Interval(Down(lo), Up(hi));
}
inline double Mid(Interval value) { return value.low * 0.5 + value.high * 0.5; }
using Vector = std::array<Interval, 3>;
inline Interval Dot(const Vector& a, const Vector& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Vector Cross(const Vector& a, const Vector& b)
{
    return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
}

struct Plane
{
    Vector normal;
    Interval offset;
    Interval interiorOffset;
};

struct Certificate
{
    double offset = std::numeric_limits<double>::infinity();
    double residual = std::numeric_limits<double>::infinity();
};

inline bool StrictInterior(const std::vector<Plane>& planes, const Vector& witness)
{
    for (const Plane& plane : planes)
        if (!(Dot(plane.normal, witness).high < plane.interiorOffset.low))
            return false;
    return true;
}

inline bool CompleteBounds(const std::vector<Plane>& planes, const std::array<Certificate, 6>& certificates,
                           const std::array<long double, 3>& centroid, size_t vertices,
                           std::array<double, 3>& low, std::array<double, 3>& high)
{
    double maximumOffset = 0.0, maximumResidual = 0.0;
    for (const Certificate& certificate : certificates)
    {
        if (!Finite(certificate.offset) || !(certificate.residual < 1.0))
            return false;
        maximumOffset = std::max(maximumOffset, certificate.offset);
        maximumResidual = std::max(maximumResidual, certificate.residual);
    }
    // Each signed coordinate is at most B + rho*max(abs(x)); rho < 1 proves boundedness.
    const Interval radius = Interval(maximumOffset) / (Interval(1.0) - Interval(maximumResidual));
    if (!Finite(radius) || radius.high < 0.0)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const Certificate& upper = certificates[2 * axis];
        const Certificate& lower = certificates[2 * axis + 1];
        high[axis] = (Interval(upper.offset) + Interval(upper.residual) * Interval(radius.high)).high;
        low[axis] = -(Interval(lower.offset) + Interval(lower.residual) * Interval(radius.high)).high;
        if (!Finite(low[axis]) || !Finite(high[axis]) || !(low[axis] < high[axis]))
            return false;
    }
    Vector centre;
    for (int axis = 0; axis < 3; ++axis)
        centre[axis] = Interval(low[axis] * 0.5 + high[axis] * 0.5);
    if (StrictInterior(planes, centre))
        return true;
    if (vertices < 4)
        return false;
    Vector witness;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double value = double(centroid[axis] / vertices);
        if (!Finite(value))
            return false;
        witness[axis] = Interval(value);
    }
    for (const Plane& plane : planes)
        if (!(Dot(plane.normal, witness).high < plane.interiorOffset.low))
            return false;
    return true;
}

inline bool Bounds(const std::vector<Plane>& input, std::array<double, 3>& low, std::array<double, 3>& high)
{
    if (input.size() < 4)
        return false;
    for (const Plane& plane : input)
    {
        if (!Finite(plane.offset) || !Finite(plane.interiorOffset))
            return false;
        for (Interval value : plane.normal)
            if (!Finite(value))
                return false;
    }
    for (int row = 0; row < 6; ++row)
    {
        Vector direction = {{Interval(), Interval(), Interval()}};
        direction[row / 2] = Interval(row % 2 ? -1.0 : 1.0);
        bool recession = true;
        for (const Plane& plane : input)
            recession = recession && Dot(plane.normal, direction).high <= 0.0;
        if (recession)
            return false;
    }
    std::vector<size_t> order;
    std::vector<bool> selected(input.size(), false);
    for (int row = 0; row < 6; ++row)
    {
        size_t best = 0;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (size_t index = 0; index < input.size(); ++index)
        {
            const Vector& normal = input[index].normal;
            const double scale = std::max(std::fabs(Mid(normal[0])), std::max(std::fabs(Mid(normal[1])), std::fabs(Mid(normal[2]))));
            if (!(scale > 0.0))
                continue;
            const double x = Mid(normal[0]) / scale, y = Mid(normal[1]) / scale, z = Mid(normal[2]) / scale;
            const double score = (row % 2 ? -1.0 : 1.0) * Mid(normal[row / 2]) / scale / std::sqrt(x * x + y * y + z * z);
            if (score > bestScore)
            {
                bestScore = score;
                best = index;
            }
        }
        if (!selected[best])
        {
            order.push_back(best);
            selected[best] = true;
        }
    }
    for (size_t index = 0; index < input.size(); ++index)
        if (!selected[index])
            order.push_back(index);
    std::vector<Plane> planes;
    planes.reserve(input.size());
    for (size_t index : order)
        planes.push_back(input[index]);
    size_t limit = std::min<size_t>(8, planes.size());
    for (;;)
    {
        std::array<Certificate, 6> certificates;
        std::array<long double, 3> centroid = {{0.0, 0.0, 0.0}};
        size_t vertices = 0;
        for (size_t i = 0; i < limit; ++i)
            for (size_t j = i + 1; j < limit; ++j)
                for (size_t k = j + 1; k < limit; ++k)
                {
                    const std::array<size_t, 3> indices = {{i, j, k}};
                    const Vector cross0 = Cross(planes[j].normal, planes[k].normal);
                    const Vector cross1 = Cross(planes[k].normal, planes[i].normal);
                    const Vector cross2 = Cross(planes[i].normal, planes[j].normal);
                    const Interval determinant = Dot(planes[i].normal, cross0);
                    if (!Finite(determinant) || !(determinant.low > 0.0 || determinant.high < 0.0))
                        continue;
                    const std::array<Vector, 3> crosses = {{cross0, cross1, cross2}};
                    for (int row = 0; row < 6; ++row)
                    {
                        const int axis = row / 2;
                        const double sign = row % 2 ? -1.0 : 1.0;
                        std::array<double, 3> weights;
                        bool usable = true;
                        for (int n = 0; n < 3; ++n)
                        {
                            weights[n] = std::max(0.0, sign * Mid(crosses[n][axis]) / Mid(determinant));
                            usable = usable && Finite(weights[n]);
                        }
                        if (!usable)
                            continue;
                        Interval bound;
                        Vector residual = {{Interval(axis == 0 ? sign : 0.0), Interval(axis == 1 ? sign : 0.0), Interval(axis == 2 ? sign : 0.0)}};
                        for (int n = 0; n < 3; ++n)
                        {
                            const Plane& plane = planes[indices[n]];
                            bound = bound + Interval(weights[n]) * plane.offset;
                            for (int coordinate = 0; coordinate < 3; ++coordinate)
                                residual[coordinate] = residual[coordinate] - Interval(weights[n]) * plane.normal[coordinate];
                        }
                        Interval norm;
                        for (Interval value : residual)
                            norm = norm + Interval(std::max(std::fabs(value.low), std::fabs(value.high)));
                        if (Finite(bound) && Finite(norm) && norm.high < 1.0)
                        {
                            Certificate& best = certificates[row];
                            if (norm.high < best.residual || (norm.high == best.residual && bound.high < best.offset))
                            {
                                best.offset = bound.high;
                                best.residual = norm.high;
                            }
                        }
                    }
                    if (CompleteBounds(planes, certificates, centroid, vertices, low, high))
                        return true;
                    Vector vertex;
                    bool possible = true;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        vertex[axis] = (cross0[axis] * planes[i].interiorOffset + cross1[axis] * planes[j].interiorOffset +
                                        cross2[axis] * planes[k].interiorOffset) / determinant;
                        possible = possible && Finite(vertex[axis]);
                    }
                    if (!possible)
                        continue;
                    for (const Plane& plane : planes)
                        if (Dot(plane.normal, vertex).low > plane.interiorOffset.high)
                        {
                            possible = false;
                            break;
                        }
                    if (possible)
                    {
                        ++vertices;
                        for (int axis = 0; axis < 3; ++axis)
                            centroid[axis] += Mid(vertex[axis]);
                        if (CompleteBounds(planes, certificates, centroid, vertices, low, high))
                            return true;
                    }
                }
        if (limit == planes.size())
            return false;
        limit += std::min(limit, planes.size() - limit);
    }
}

}
}

#endif
