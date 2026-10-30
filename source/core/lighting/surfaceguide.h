#ifndef POVRAY_CORE_SURFACEGUIDE_H
#define POVRAY_CORE_SURFACEGUIDE_H

#include "core/configcore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <cmath>
#include "core/math/vector.h"

namespace pov
{

class SurfaceAngularGuideCache;

class SurfaceAngularGuide final
{
public:
    SurfaceAngularGuide(const Vector3d& lower, const Vector3d& size) : lower(lower), size(size) {}

    void Add(const Vector3d& point, double power, const Vector3d& normal = Vector3d(0.0))
    {
        if (!(power > 0.0) || !std::isfinite(power)) return;
        reach2 = std::max(reach2, (point - (lower + size * 0.5)).lengthSqr());
        unsigned index = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const double coordinate = size[axis] > 0.0 ? (point[axis] - lower[axis]) / size[axis] : 0.0;
            const unsigned bin = unsigned(std::max(0.0, std::min(3.0, std::floor(coordinate * 4.0))));
            index = index * 4 + bin;
        }
        masses[index] += power;
        moments[index] += normal * power;
    }

    void AddScaled(const SurfaceAngularGuide& source, double scale)
    {
        if (!(scale > 0.0) || !std::isfinite(scale)) return;
        reach2 = std::max(reach2, source.reach2);
        if (!leafCount && !(coneShare > 0.0))
        {
            leaves = source.leaves;
            leafCount = source.leafCount;
            coneShare = source.coneShare;
            leafShare = source.leafShare;
        }
        for (unsigned i = 0; i < masses.size(); ++i)
        {
            masses[i] += source.masses[i] * scale;
            moments[i] += source.moments[i] * scale;
        }
    }

    void Prepare()
    {
        generation = NextGeneration();
        count = 0;
        double largest = 0.0;
        for (double mass : masses) largest = std::max(largest, mass);
        if (!(largest > 0.0) || !std::isfinite(largest)) return;
        for (unsigned index = 0; index < masses.size(); ++index)
        {
            if (!(masses[index] > 0.0)) continue;
            Cell& cell = cells[count++];
            const unsigned x = index / 16, y = (index / 4) % 4, z = index % 4;
            cell.center = lower + Vector3d((x + 0.5) * size[0], (y + 0.5) * size[1], (z + 0.5) * size[2]) / 4.0;
            cell.mass = masses[index] / largest;
            cell.normal = moments[index] / masses[index];
            const double length = cell.normal.length();
            cell.normalStrength = std::min(1.0, length);
            if (length > 0.0) cell.normal /= length;
        }
    }

    struct ConePoint { Vector3d point, normal; double power; };
    // Splits the emitting points at the median of their longest extent into up to 64 spheres about their points' boxes.
    void PrepareCones(std::vector<ConePoint> points)
    {
        leafCount = 0;
        Split(points, 0, points.size(), 0);
    }
    void SetShares(double cone, double leaf) { coneShare = cone; leafShare = leaf; generation = NextGeneration(); }
    Vector3d ConeCentre() const { return lower + size * 0.5; }
    double ConeReach() const { return std::sqrt(reach2); }
    // The bounding cone's, the leaf cones' and the kernels' densities; false where no leaf cone applies.
    bool StrategyPdfs(const Vector3d& from, const Vector3d& direction, double pdfs[3],
                      std::shared_ptr<SurfaceAngularGuideCache>& cache) const;

    // The normalized spherical Cauchy density has h = R / (2d + R).
    struct SphereKernel final
    {
        SphereKernel() = default;
        SphereKernel(const Vector3d& offset, double radius)
        {
            distance = std::hypot(std::hypot(offset[0], offset[1]), offset[2]);
            axis = distance > 0.0 ? offset / distance : Vector3d(0, 0, 1);
            const double scale = std::max(distance, radius);
            const double a = radius / scale, b = distance / scale;
            const double h = a / (2.0 * b + a);
            h2 = h * h;
        }
        Vector3d Sample(double u, double v) const
        {
            const double y = 2.0 * u * h2 / ((1.0 - u) + u * h2);
            const double z = 1.0 - y, r = std::sqrt(std::max(0.0, y * (2.0 - y)));
            const Vector3d helper = std::fabs(axis[2]) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(0, 1, 0);
            const Vector3d tangent = cross(helper, axis).normalized();
            const double phi = 2.0 * M_PI * v;
            return axis * z + tangent * (r * std::cos(phi)) + cross(axis, tangent) * (r * std::sin(phi));
        }
        double Pdf(const Vector3d& direction) const
        {
            const double y = (axis - direction).lengthSqr() * 0.5;
            const double denominator = y + h2 * (2.0 - y);
            return h2 / (M_PI * denominator * denominator);
        }
        Vector3d axis;
        double distance, h2;
    };

    // Uniform over the cone of directions that meets a sphere, or over all directions from inside it.
    struct SphereCone final
    {
        SphereCone() = default;
        SphereCone(const Vector3d& offset, double radius)
        {
            const double distance2 = offset.lengthSqr();
            axis = distance2 > 0.0 ? offset / std::sqrt(distance2) : Vector3d(0, 0, 1);
            const double sine2 = radius * radius / distance2;
            span = sine2 < 1.0 ? sine2 / (1.0 + std::sqrt(1.0 - sine2)) : 2.0;
            density = 1.0 / (2.0 * M_PI * span);
        }
        Vector3d Sample(double u, double v) const
        {
            const double y = u * span, z = 1.0 - y, r = std::sqrt(std::max(0.0, y * (2.0 - y)));
            const Vector3d helper = std::fabs(axis[2]) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(0, 1, 0);
            const Vector3d tangent = cross(helper, axis).normalized();
            const double phi = 2.0 * M_PI * v;
            return axis * z + tangent * (r * std::cos(phi)) + cross(axis, tangent) * (r * std::sin(phi));
        }
        double Pdf(const Vector3d& direction) const { return (axis - direction).lengthSqr() * 0.5 <= span ? density : 0.0; }
        Vector3d axis;
        double span = 2.0, density = 0.25 / M_PI;
    };

    static Vector3d SampleSphere(const Vector3d& offset, double radius, double u, double v, double& pdf)
    {
        const SphereKernel kernel(offset, radius);
        const Vector3d direction = kernel.Sample(Unit(u), Unit(v));
        pdf = kernel.Pdf(direction);
        return direction;
    }

    Vector3d Sample(const Vector3d& from, double choose, double azimuth, double& pdf) const
    {
        if (!count) return SampleSphere(lower + size * 0.5 - from, std::hypot(std::hypot(size[0], size[1]), size[2]) * 0.5, choose, azimuth, pdf);
        const Conditioned conditioned(*this, from);
        return conditioned.Sample(Unit(choose), Unit(azimuth), pdf);
    }

    Vector3d Sample(const Vector3d& from, double choose, double azimuth, double& pdf,
                    std::shared_ptr<SurfaceAngularGuideCache>& cache) const;

    double Pdf(const Vector3d& from, const Vector3d& direction) const
    {
        if (!count) return SphereKernel(lower + size * 0.5 - from, std::hypot(std::hypot(size[0], size[1]), size[2]) * 0.5).Pdf(direction);
        return Conditioned(*this, from).Pdf(direction);
    }

private:
    static double Unit(double u) { return std::max(0.0, std::min(std::nextafter(1.0, 0.0), u)); }
    struct Cell { Vector3d center, normal; double mass, normalStrength; };
    struct Leaf { Vector3d centre, normal; double radius, power, normalStrength; };
    void Split(std::vector<ConePoint>& points, std::size_t begin, std::size_t end, unsigned depth)
    {
        if (begin == end) return;
        Vector3d low = points[begin].point, high = low;
        for (std::size_t i = begin; i < end; ++i)
            for (int a = 0; a < 3; ++a)
            {
                low[a] = std::min(low[a], points[i].point[a]);
                high[a] = std::max(high[a], points[i].point[a]);
            }
        const Vector3d extent = high - low;
        const int axis = extent[0] >= extent[1] ? (extent[0] >= extent[2] ? 0 : 2) : (extent[1] >= extent[2] ? 1 : 2);
        if (depth < 6 && end - begin > 1 && extent[axis] > 0.0)
        {
            const std::size_t middle = begin + (end - begin) / 2;
            std::nth_element(points.begin() + begin, points.begin() + middle, points.begin() + end,
                             [axis](const ConePoint& a, const ConePoint& b) { return a.point[axis] < b.point[axis]; });
            Split(points, begin, middle, depth + 1);
            Split(points, middle, end, depth + 1);
            return;
        }
        Leaf& leaf = leaves[leafCount];
        leaf.centre = (low + high) * 0.5;
        double radius2 = 0.0, power = 0.0;
        Vector3d moment(0.0);
        for (std::size_t i = begin; i < end; ++i)
        {
            radius2 = std::max(radius2, (points[i].point - leaf.centre).lengthSqr());
            power += points[i].power;
            moment += points[i].normal * points[i].power;
        }
        if (!(radius2 > 0.0) || !(power > 0.0) || !std::isfinite(power)) return;
        leaf.radius = std::sqrt(radius2);
        leaf.power = power;
        leaf.normal = moment / power;
        const double length = leaf.normal.length();
        leaf.normalStrength = std::min(1.0, length);
        if (length > 0.0) leaf.normal /= length;
        ++leafCount;
    }
public:
    // The cone about the bounds' centre through the farthest emitting point and a cone per leaf, mixed with the kernels.

    struct Conditioned
    {
        Conditioned(const SurfaceAngularGuide& guide, const Vector3d& from) :
            cone(guide.lower + guide.size * 0.5 - from, std::sqrt(guide.reach2)),
            whole(guide.lower + guide.size * 0.5 - from, std::hypot(std::hypot(guide.size[0], guide.size[1]), guide.size[2]) * 0.5)
        {
            share = guide.coneShare;
            leafShare = guide.leafShare;
            leafCount = 0;
            double leafTotal = 0.0;
            for (unsigned i = 0; leafShare > 0.0 && i < guide.leafCount; ++i)
            {
                const Leaf& leaf = guide.leaves[i];
                const Vector3d offset = leaf.centre - from;
                const double distance2 = offset.lengthSqr(), radius2 = leaf.radius * leaf.radius;
                const double d = std::sqrt(distance2 / (distance2 + radius2));
                const double cosine = distance2 > 0.0 ? -dot(leaf.normal, offset) / std::sqrt(distance2) : 0.0;
                const double orientation = (1.0 - leaf.normalStrength) + leaf.normalStrength * ((1.0 - d) + d * std::max(0.0, cosine));
                const double mass = leaf.power * orientation / (distance2 + radius2);
                if (mass > 0.0 && std::isfinite(mass))
                {
                    leafComponents[leafCount++] = {SphereCone(offset, leaf.radius), mass};
                    leafTotal += mass;
                }
            }
            if (!(leafTotal > 0.0) || !std::isfinite(leafTotal)) leafCount = 0;
            for (unsigned i = 0; i < leafCount; ++i) leafComponents[i].mass /= leafTotal;
            if (!leafCount) leafShare = 0.0;
            kernelShare = 1.0 - share - leafShare;
            const double radius = std::hypot(std::hypot(guide.size[0], guide.size[1]), guide.size[2]) * 0.125;
            double total = 0.0;
            count = 0;
            for (unsigned i = 0; i < guide.count; ++i)
            {
                const Cell& cell = guide.cells[i];
                const Vector3d offset = cell.center - from;
                SphereKernel kernel(offset, radius);
                const double regularized = std::hypot(kernel.distance, radius);
                const double d = kernel.distance / regularized, r = radius / regularized;
                const double cosine = dot(cell.normal, -kernel.axis);
                const double facing = cosine < 0.0 ? r * r / (1.0 + d) + d * (cell.normal - kernel.axis).lengthSqr() * 0.5 : 1.0 + d * cosine;
                const double orientation = (1.0 - cell.normalStrength) + cell.normalStrength * facing;
                const double mass = cell.mass * r * r * orientation;
                if (mass > 0.0)
                {
                    components[count++] = {kernel, mass};
                    total += mass;
                }
            }
            if (!(total > 0.0) || !std::isfinite(total)) count = 0;
            if (count)
                for (unsigned i = 0; i < count; ++i) components[i].mass /= total;
        }
        double Pdf(const Vector3d& direction) const
        {
            return (share > 0.0 ? share * cone.Pdf(direction) : 0.0) + (leafShare > 0.0 ? leafShare * LeafPdf(direction) : 0.0) +
                kernelShare * KernelPdf(direction);
        }
        double LeafPdf(const Vector3d& direction) const
        {
            double pdf = 0.0;
            for (unsigned i = 0; i < leafCount; ++i) pdf += leafComponents[i].mass * leafComponents[i].cone.Pdf(direction);
            return pdf;
        }
        double KernelPdf(const Vector3d& direction) const
        {
            double pdf = whole.Pdf(direction) * (count ? 0.5 : 1.0);
            for (unsigned i = 0; i < count; ++i) pdf += 0.5 * components[i].mass * components[i].kernel.Pdf(direction);
            return pdf;
        }
        Vector3d Sample(double choose, double azimuth, double& pdf) const
        {
            if (choose < share)
            {
                const Vector3d direction = cone.Sample(Unit(choose / share), azimuth);
                pdf = Pdf(direction);
                return direction;
            }
            if (choose < share + leafShare)
            {
                choose = Unit((choose - share) / leafShare);
                double cumulative = 0.0;
                unsigned i = 0;
                for (; i + 1 < leafCount && choose >= cumulative + leafComponents[i].mass; ++i) cumulative += leafComponents[i].mass;
                const Vector3d direction = leafComponents[i].cone.Sample(Unit((choose - cumulative) / leafComponents[i].mass), azimuth);
                pdf = Pdf(direction);
                return direction;
            }
            choose = Unit((choose - share - leafShare) / kernelShare);
            const SphereKernel* selected = &whole;
            if (count)
            {
                choose *= 2.0;
                if (choose >= 1.0)
                {
                    choose -= 1.0;
                    double cumulative = 0.0;
                    unsigned i = 0;
                    for (; i + 1 < count && choose >= cumulative + components[i].mass; ++i) cumulative += components[i].mass;
                    choose = Unit((choose - cumulative) / components[i].mass);
                    selected = &components[i].kernel;
                }
            }
            const Vector3d direction = selected->Sample(choose, azimuth);
            pdf = Pdf(direction);
            return direction;
        }
        struct Entry { SphereKernel kernel; double mass; };
        struct LeafEntry { SphereCone cone; double mass; };
        SphereCone cone;
        double share, leafShare, kernelShare;
        std::array<LeafEntry, 64> leafComponents;
        unsigned leafCount;
        SphereKernel whole;
        std::array<Entry, 64> components;
        unsigned count;
    };
private:
    friend class SurfaceAngularGuideCache;
    static std::uint64_t NextGeneration() { static std::atomic<std::uint64_t> next{1}; return next.fetch_add(1, std::memory_order_relaxed); }
    std::uint64_t generation = NextGeneration();
    Vector3d lower, size;
    double reach2 = 0.0, coneShare = 0.0, leafShare = 0.0;
    std::array<Leaf, 64> leaves;
    unsigned leafCount = 0;
    std::array<double, 64> masses{};
    std::array<Vector3d, 64> moments{};
    std::array<Cell, 64> cells;
    unsigned count = 0;
};

class SurfaceAngularGuideCache final
{
public:
    const SurfaceAngularGuide::Conditioned& Prepare(const SurfaceAngularGuide& guide, const Vector3d& from)
    {
        if (!conditioned || generation != guide.generation || origin[0] != from[0] || origin[1] != from[1] || origin[2] != from[2])
        {
            if (conditioned) *conditioned = SurfaceAngularGuide::Conditioned(guide, from);
            else conditioned.reset(new SurfaceAngularGuide::Conditioned(guide, from));
            origin = from;
            generation = guide.generation;
        }
        return *conditioned;
    }
    Vector3d Sample(const SurfaceAngularGuide& guide, const Vector3d& from, double choose, double azimuth, double& pdf)
    {
        return Prepare(guide, from).Sample(choose, azimuth, pdf);
    }
private:
    std::uint64_t generation = 0;
    Vector3d origin;
    std::unique_ptr<SurfaceAngularGuide::Conditioned> conditioned;
};

inline Vector3d SurfaceAngularGuide::Sample(const Vector3d& from, double choose, double azimuth, double& pdf,
                                          std::shared_ptr<SurfaceAngularGuideCache>& cache) const
{
    if (!count) return Sample(from, choose, azimuth, pdf);
    if (!cache) cache = std::make_shared<SurfaceAngularGuideCache>();
    return cache->Sample(*this, from, Unit(choose), Unit(azimuth), pdf);
}

inline bool SurfaceAngularGuide::StrategyPdfs(const Vector3d& from, const Vector3d& direction, double pdfs[3],
                                              std::shared_ptr<SurfaceAngularGuideCache>& cache) const
{
    if (!count)
    {
        pdfs[0] = pdfs[1] = 0.0;
        pdfs[2] = Pdf(from, direction);
        return false;
    }
    if (!cache) cache = std::make_shared<SurfaceAngularGuideCache>();
    const Conditioned& conditioned = cache->Prepare(*this, from);
    pdfs[0] = conditioned.cone.Pdf(direction);
    pdfs[1] = conditioned.LeafPdf(direction);
    pdfs[2] = conditioned.KernelPdf(direction);
    return conditioned.leafCount > 0;
}

}
#endif
