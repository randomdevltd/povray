#ifndef POVRAY_CORE_SURFACEGUIDE_H
#define POVRAY_CORE_SURFACEGUIDE_H

#include "core/configcore.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
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
        for (unsigned i = 0; i < masses.size(); ++i)
        {
            masses[i] += source.masses[i] * scale;
            moments[i] += source.moments[i] * scale;
        }
    }

    void Prepare()
    {
        generation = nextGeneration.fetch_add(1, std::memory_order_relaxed);
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

    // The normalized spherical Cauchy density has h = R / (2d + R).
    struct SphereKernel final
    {
        SphereKernel() = default;
        SphereKernel(const Vector3d& offset, double radius)
        {
            distance = std::hypot(offset[0], offset[1], offset[2]);
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

    static Vector3d SampleSphere(const Vector3d& offset, double radius, double u, double v, double& pdf)
    {
        const SphereKernel kernel(offset, radius);
        const Vector3d direction = kernel.Sample(Unit(u), Unit(v));
        pdf = kernel.Pdf(direction);
        return direction;
    }

    Vector3d Sample(const Vector3d& from, double choose, double azimuth, double& pdf) const
    {
        if (!count) return SampleSphere(lower + size * 0.5 - from, std::hypot(size[0], size[1], size[2]) * 0.5, choose, azimuth, pdf);
        const Conditioned conditioned(*this, from);
        return conditioned.Sample(Unit(choose), Unit(azimuth), pdf);
    }

    Vector3d Sample(const Vector3d& from, double choose, double azimuth, double& pdf,
                    std::shared_ptr<SurfaceAngularGuideCache>& cache) const;

    double Pdf(const Vector3d& from, const Vector3d& direction) const
    {
        if (!count) return SphereKernel(lower + size * 0.5 - from, std::hypot(size[0], size[1], size[2]) * 0.5).Pdf(direction);
        return Conditioned(*this, from).Pdf(direction);
    }

private:
    static double Unit(double u) { return std::max(0.0, std::min(std::nextafter(1.0, 0.0), u)); }
    struct Cell { Vector3d center, normal; double mass, normalStrength; };
public:
    struct Conditioned
    {
        Conditioned(const SurfaceAngularGuide& guide, const Vector3d& from) :
            whole(guide.lower + guide.size * 0.5 - from, std::hypot(guide.size[0], guide.size[1], guide.size[2]) * 0.5)
        {
            const double radius = std::hypot(guide.size[0], guide.size[1], guide.size[2]) * 0.125;
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
            double pdf = whole.Pdf(direction) * (count ? 0.5 : 1.0);
            for (unsigned i = 0; i < count; ++i) pdf += 0.5 * components[i].mass * components[i].kernel.Pdf(direction);
            return pdf;
        }
        Vector3d Sample(double choose, double azimuth, double& pdf) const
        {
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
        SphereKernel whole;
        std::array<Entry, 64> components;
        unsigned count;
    };
private:
    friend class SurfaceAngularGuideCache;
    inline static std::atomic<std::uint64_t> nextGeneration{1};
    std::uint64_t generation = nextGeneration.fetch_add(1, std::memory_order_relaxed);
    Vector3d lower, size;
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
            conditioned.emplace(guide, from);
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
    std::optional<SurfaceAngularGuide::Conditioned> conditioned;
};

inline Vector3d SurfaceAngularGuide::Sample(const Vector3d& from, double choose, double azimuth, double& pdf,
                                          std::shared_ptr<SurfaceAngularGuideCache>& cache) const
{
    if (!count) return Sample(from, choose, azimuth, pdf);
    if (!cache) cache = std::make_shared<SurfaceAngularGuideCache>();
    return cache->Sample(*this, from, Unit(choose), Unit(azimuth), pdf);
}

}
#endif
