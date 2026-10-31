#include "core/lighting/surfaceguide.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

using pov::SurfaceAngularGuide;
using pov::Vector3d;

static void Check(const Vector3d& from, bool guided)
{
    const Vector3d lower(-1), size(2);
    SurfaceAngularGuide guide(lower, size);
    if (guided)
    {
        guide.Add(Vector3d(-0.75, -0.75, -0.75), 1.0);
        guide.Add(Vector3d(0.75, 0.75, 0.75), 7.0);
        guide.Add(Vector3d(0.25, 0.25, 0.25), 2.0);
        guide.Prepare();
    }
    const double distance2 = from.lengthSqr(), radius2 = 3.0;
    const double cap = distance2 <= radius2 ? 2.0 : (radius2 / distance2) / (1 + std::sqrt(1 - radius2 / distance2));
    const double baseline = 1 / (2 * M_PI * cap);
    std::mt19937_64 random(182093);
    std::uniform_real_distribution<double> unit(0, 1);
    constexpr unsigned count = 300000;
    double sphereIntegral = 0, firstRoot = 0, secondRoot = 0, square = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        double pdf;
        const Vector3d direction = guide.Sample(from, unit(random), unit(random), pdf);
        assert(pdf > 0 && std::isfinite(pdf));
        assert(std::fabs(direction.lengthSqr() - 1) < 1e-12);
        assert(pdf == guide.Pdf(from, direction));
        const bool whole = distance2 <= radius2 || (pov::dot(-from, direction) > 0 && pov::cross(-from, direction).lengthSqr() <= radius2);
        if (whole) assert(pdf >= baseline * (guided ? 0.045 : 0.09) * (1 - 1e-12));
        const double estimate = whole ? 1.0 / pdf : 0.0;
        sphereIntegral += estimate;
        square += estimate * estimate;
        if (unit(random) < 0.5) firstRoot += 2 * estimate; else secondRoot += 2 * estimate;
    }
    const double expected = 2 * M_PI * cap;
    const double mean = sphereIntegral / count;
    const double error = std::sqrt(std::max(0.0, square / count - mean * mean) / count);
    assert(std::fabs(mean - expected) < 6 * error + expected * 1e-10);
    assert(std::fabs(firstRoot / count - expected) < expected * 0.025);
    assert(std::fabs(secondRoot / count - expected) < expected * 0.025);
    std::cout << "angular integral " << mean << " expected " << expected << '\n';
}

static Vector3d OldCone(const Vector3d& from, double radius, double u, double v, double& pdf)
{
    const double distance = from.length();
    const double sin2 = radius * radius / (distance * distance);
    const double cap = distance > radius ? sin2 / (1 + std::sqrt(1 - sin2)) : 2;
    const Vector3d axis = distance > radius ? -from / distance : Vector3d(0, 0, 1);
    const Vector3d helper = std::fabs(axis[2]) < .9 ? Vector3d(0, 0, 1) : Vector3d(0, 1, 0);
    const Vector3d tangent = pov::cross(helper, axis).normalized();
    const double y = cap * u, r = std::sqrt(y * (2-y)), phi = 2 * M_PI * v;
    pdf = 1 / (2 * M_PI * cap);
    return axis * (1-y) + tangent * (r * std::cos(phi)) + pov::cross(axis, tangent) * (r * std::sin(phi));
}

static double Brightness(const Vector3d& point)
{
    return point[0] < -0.4 && point[2] < -0.7 ? 100.0 : 1.0;
}

static void BrightSphere()
{
    SurfaceAngularGuide uniform(Vector3d(-1), Vector3d(2));
    SurfaceAngularGuide guided(Vector3d(-1), Vector3d(2));
    constexpr unsigned rows = 128, columns = 256;
    for (unsigned row = 0; row < rows; ++row)
        for (unsigned column = 0; column < columns; ++column)
        {
            const double z = 2.0 * (row + 0.5) / rows - 1;
            const double phi = 2 * M_PI * (column + 0.5) / columns;
            const double r = std::sqrt(1 - z*z);
            const Vector3d point(r * std::cos(phi), r * std::sin(phi), z);
            guided.Add(point, Brightness(point), point);
        }
    guided.Prepare();
    const Vector3d from(0, 0, -3);
    auto value = [&](const Vector3d& direction)
    {
        const double b = pov::dot(from, direction), discriminant = b*b - 8;
        if (!(b < 0) || discriminant <= 0) return 0.0;
        return Brightness(from + direction * (-b - std::sqrt(discriminant)));
    };
    double reference = 0;
    constexpr unsigned referenceRows = 512, referenceColumns = 1024;
    for (unsigned row = 0; row < referenceRows; ++row)
        for (unsigned column = 0; column < referenceColumns; ++column)
        {
            double pdf;
            const Vector3d direction = OldCone(from, 1.0, (row + 0.5) / referenceRows, (column + 0.5) / referenceColumns, pdf);
            reference += value(direction) / pdf;
        }
    reference /= referenceRows * referenceColumns;
    double variance[4];
    for (unsigned mode = 0; mode < 4; ++mode)
    {
        const auto& guide = mode >= 2 ? guided : uniform;
        std::mt19937_64 random(984032);
        std::uniform_real_distribution<double> unit(0, 1);
        constexpr unsigned count = 300000;
        double sum = 0, square = 0;
        std::shared_ptr<pov::SurfaceAngularGuideCache> cache;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < count; ++i)
        {
            double pdf;
            const double u = unit(random), v = unit(random);
            const Vector3d direction = mode == 0 ? OldCone(from, std::sqrt(3.0), u, v, pdf) : mode == 3 ? guide.Sample(from, u, v, pdf, cache) : guide.Sample(from, u, v, pdf);
            const double estimate = unit(random) < 0.5 ? value(direction) * 2 / pdf : 0;
            sum += estimate;
            square += estimate * estimate;
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / count;
        const double mean = sum / count;
        variance[mode] = square / count - mean*mean;
        assert(std::fabs(mean - reference) < 6 * std::sqrt(variance[mode] / count) + reference * 0.002);
        std::cout << "bright sphere mode " << mode << " mean " << mean << " reference " << reference << " variance " << variance[mode] << " ns/draw " << ns << " variance*ns " << variance[mode] * ns << '\n';
    }
    assert(variance[2] < variance[0] * 0.7);
}

static void Cost()
{
    for (unsigned active : {0u, 3u, 64u})
    {
        SurfaceAngularGuide guide(Vector3d(-1), Vector3d(2));
        for (unsigned i = 0; i < active; ++i)
            guide.Add(Vector3d(-0.75 + (i / 16) * 0.5, -0.75 + ((i / 4) % 4) * 0.5, -0.75 + (i % 4) * 0.5), i + 1);
        guide.Prepare();
        const SurfaceAngularGuide::Conditioned batch(guide, Vector3d(0, 0, -3));
        for (bool cached : {false, true})
        {
            const auto begin = std::chrono::steady_clock::now();
            double result = 0;
            constexpr unsigned count = 1000000;
            for (unsigned i = 0; i < count; ++i)
            {
                double pdf;
                const double u = (i + 0.5) / count, v = std::fmod(i * 0.6180339887498949, 1.0);
                const Vector3d direction = cached ? batch.Sample(u, v, pdf) : guide.Sample(Vector3d(0, 0, -3), u, v, pdf);
                result += pdf + direction[0];
            }
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / count;
            std::cout << "active cells " << active << " cached " << cached << " ns/draw " << ns << " checksum " << result << '\n';
        }
    }
}

static void KernelChecks()
{
    for (double scale : {1e-12, 1.0, 1e12})
        for (double ratio : {0.0, 1e-12, 0.5, 1.0, 2.0, 1e6, 1e12})
        {
            const SurfaceAngularGuide::SphereKernel kernel(Vector3d(0, 0, ratio * scale), scale);
            const long double h = 1 / (2 * static_cast<long double>(ratio) + 1), h2 = h*h;
            for (double u : {0.0, 1e-12, 0.25, 0.5, 0.9, 1-1e-12})
            {
                const Vector3d direction = kernel.Sample(u, .173);
                const long double y = (direction - Vector3d(0, 0, 1)).lengthSqr() * 0.5;
                const long double cdf = y / (y + h2 * (2-y));
                assert(std::fabs(cdf-u) < 2e-12);
                assert(kernel.Pdf(direction) > 0 && std::isfinite(kernel.Pdf(direction)));
            }
            const double limit = std::log1p(1 / double(h2));
            double integral = 0;
            constexpr unsigned count = 8192;
            for (unsigned i = 0; i < count; ++i)
            {
                const double t = limit * (i + .5) / count;
                const double y = 2 * double(h2) * std::expm1(t);
                const Vector3d direction(std::sqrt(y * (2-y)), 0, 1-y);
                integral += kernel.Pdf(direction) * 2 * M_PI * 2 * double(h2) * std::exp(t) * limit / count;
            }
            assert(std::fabs(integral-1) < 1e-4);
        }
    std::cout << "kernel normalization, inverse CDF and scale checks passed\n";
}

static SurfaceAngularGuide MakeGuide(double scale = 1)
{
    SurfaceAngularGuide guide(Vector3d(-scale), Vector3d(2*scale));
    guide.Add(Vector3d(-.75)*scale, 1, Vector3d(0, 0, -1));
    guide.Add(Vector3d(.75)*scale, 7, Vector3d(1, 0, 0));
    guide.Add(Vector3d(.25)*scale, 2, Vector3d(0, 1, 0));
    guide.Prepare();
    return guide;
}

static double SmoothSecondMoment(const SurfaceAngularGuide& guide, const Vector3d& from)
{
    constexpr unsigned rows = 24, columns = 32;
    double result = 0;
    for (unsigned row = 0; row < rows; ++row)
        for (unsigned column = 0; column < columns; ++column)
        {
            const double z = 2.0 * (row+.5) / rows - 1, r = std::sqrt(1-z*z), phi = 2*M_PI*(column+.5)/columns;
            const Vector3d direction(r*std::cos(phi), r*std::sin(phi), z);
            const double value = 1 + .3 * direction[0];
            result += value * value / guide.Pdf(from, direction);
        }
    return result * (4 * M_PI / (rows*columns));
}

static double SphereSecondMoment(const SurfaceAngularGuide& guide, double distance, double* mean = nullptr, bool oldCone = false)
{
    const Vector3d from(0, 0, -distance);
    constexpr unsigned rows = 48, columns = 64;
    double result = 0, first = 0;
    for (unsigned row = 0; row < rows; ++row)
        for (unsigned column = 0; column < columns; ++column)
        {
            double uniformPdf;
            const Vector3d direction = OldCone(from, 1, (row+.5)/rows, (column+.5)/columns, uniformPdf);
            const double b = pov::dot(from, direction), discriminant = b*b - (distance*distance-1);
            const Vector3d point = from + direction*(-b-std::sqrt(std::max(0.0, discriminant)));
            const double value = 1 + .3*point[0];
            double oldPdf;
            if (oldCone) OldCone(from, std::sqrt(3.0), .5, .5, oldPdf);
            const double pdf = oldCone ? oldPdf : guide.Pdf(from, direction);
            result += 2 * value*value / (uniformPdf * pdf);
            first += value / uniformPdf;
        }
    if (mean) *mean = first / (rows*columns);
    return result / (rows*columns);
}

static void Continuity()
{
    const auto guide = MakeGuide();
    const double wholeRadius = std::sqrt(3.0), radius = wholeRadius / 4;
    const double sphereCenter = SphereSecondMoment(guide, wholeRadius);
    const double oldLeft = SphereSecondMoment(guide, wholeRadius*(1-1e-8), nullptr, true);
    const double oldRight = SphereSecondMoment(guide, wholeRadius*(1+1e-8), nullptr, true);
    assert(oldLeft/oldRight > 1.99 && oldLeft/oldRight < 2.01);
    std::cout << "old cone positive control second-moment ratio " << oldLeft/oldRight << '\n';
    for (unsigned k = 2; k <= 12; ++k)
    {
        const double epsilon = std::pow(10.0, -int(k));
        double meanLeft, meanRight;
        const double left = SphereSecondMoment(guide, wholeRadius*(1-epsilon), &meanLeft);
        const double right = SphereSecondMoment(guide, wholeRadius*(1+epsilon), &meanRight);
        assert(std::fabs(meanLeft-meanRight)/(meanLeft+meanRight) < 40*epsilon + 2e-12);
        assert(std::fabs(left-right)/sphereCenter < 40*epsilon + 2e-12);
        std::cout << "whole boundary epsilon " << epsilon << " second moments " << left << " / " << sphereCenter << " / " << right << '\n';
    }
    for (const Vector3d& center : {Vector3d(-.75), Vector3d(.75), Vector3d(.25)})
    {
        const Vector3d offset = Vector3d(.3, -.4, .5).normalized() * radius;
        const double middle = SmoothSecondMoment(guide, center+offset);
        for (unsigned k = 2; k <= 12; ++k)
        {
            const double epsilon = std::pow(10.0, -int(k));
            const Vector3d left = center + offset*(1-epsilon), right = center + offset*(1+epsilon);
            const double l = SmoothSecondMoment(guide, left), r = SmoothSecondMoment(guide, right);
            assert(std::fabs(l-r)/middle < 40*epsilon + 2e-12);
            for (const Vector3d& direction : {Vector3d(1,0,0), Vector3d(0,1,0), Vector3d(0,0,1), Vector3d(-1,0,0), Vector3d(0,-1,0), Vector3d(0,0,-1)})
            {
                const double q = guide.Pdf(center+offset, direction);
                assert(std::fabs(guide.Pdf(left,direction)-guide.Pdf(right,direction))/q < 40*epsilon + 2e-12);
            }
        }
    }
    for (double scale : {1e-12, 1e12})
    {
        const auto scaled = MakeGuide(scale);
        for (double distance : {0.0, 1.0, 1e6, 1e12})
        {
            const Vector3d from(distance, 0, -distance), direction = Vector3d(-1, 0, 1).normalized();
            const double q = guide.Pdf(from, direction), qs = scaled.Pdf(from*scale, direction);
            assert(std::fabs(q/qs-1) < 1e-3);
        }
    }
    std::cout << "whole and occupied-cell PDF/second-moment continuity and guide scale checks passed\n";
}

static void CacheChecks()
{
    auto guide = MakeGuide();
    std::shared_ptr<pov::SurfaceAngularGuideCache> cache;
    std::uint64_t maxUlps = 0;
    double maxPdfRelative = 0, maxDirectionError = 0;
    const auto ordered = [](double value)
    {
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits >> 63 ? ~bits : bits | (UINT64_C(1) << 63);
    };
    const auto check = [&](const SurfaceAngularGuide& source, const Vector3d& from)
    {
        if (!cache) cache = std::make_shared<pov::SurfaceAngularGuideCache>();
        const SurfaceAngularGuide::Conditioned fresh(source, from);
        const auto& stored = cache->Prepare(source, from);
        const auto kernelEqual = [](const auto& a, const auto& b)
        {
            return a.axis[0] == b.axis[0] && a.axis[1] == b.axis[1] && a.axis[2] == b.axis[2] && a.h2 == b.h2 && a.distance == b.distance;
        };
        assert(kernelEqual(fresh.whole, stored.whole) && fresh.count == stored.count);
        for (unsigned i = 0; i < fresh.count; ++i)
            assert(fresh.components[i].mass == stored.components[i].mass && kernelEqual(fresh.components[i].kernel, stored.components[i].kernel));
        for (unsigned i = 0; i < 256; ++i)
        {
            const double u = (i+.5)/256, v = std::fmod(i*.6180339887498949, 1.0);
            double expectedPdf, cachedPdf;
            const Vector3d expected = source.Sample(from, u, v, expectedPdf);
            const Vector3d actual = source.Sample(from, u, v, cachedPdf, cache);
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const auto a = ordered(expected[axis]), b = ordered(actual[axis]);
                maxUlps = std::max(maxUlps, a > b ? a-b : b-a);
            }
            maxPdfRelative = std::max(maxPdfRelative, std::fabs(expectedPdf/cachedPdf-1));
            maxDirectionError = std::max(maxDirectionError, (expected-actual).length());
            assert((expected-actual).lengthSqr() < 1e-28 && std::fabs(expectedPdf/cachedPdf-1) < 1e-13);
        }
    };
    check(guide, Vector3d(0,0,-3));
    check(guide, Vector3d(0,0,-3.000000000001));
    auto other = MakeGuide();
    other.Add(Vector3d(-.75), 100, Vector3d(1,0,0));
    other.Prepare();
    check(other, Vector3d(0,0,-3));
    check(guide, Vector3d(0,0,-3));
    guide.Add(Vector3d(.75), 100, Vector3d(0,1,0));
    guide.Prepare();
    check(guide, Vector3d(0,0,-3));
    SurfaceAngularGuide merged(Vector3d(-1), Vector3d(2));
    merged.AddScaled(guide, 5);
    merged.Prepare();
    const Vector3d from(1,2,-3), direction = Vector3d(-1,1,2).normalized();
    assert(std::fabs(merged.Pdf(from,direction)/guide.Pdf(from,direction)-1) < 1e-12);
#ifdef SURFACE_GUIDE_EXACT_CACHE
    assert(maxUlps == 0 && maxPdfRelative == 0);
#endif
    std::cout << "cache bitwise contexts and invalidation passed; max component ULP " << maxUlps << " direction error " << maxDirectionError << " PDF relative error " << maxPdfRelative << '\n';
}

int main()
{
    CacheChecks();
    KernelChecks();
    Continuity();
    Cost();
    BrightSphere();
    SurfaceAngularGuide overlap(Vector3d(-1), Vector3d(2));
    overlap.Add(Vector3d(-0.75, -0.75, -0.75), 1.0);
    overlap.Add(Vector3d(0.75, 0.75, 0.75), 7.0);
    overlap.Add(Vector3d(0.25, 0.25, 0.25), 2.0);
    overlap.Prepare();
    const double radius = std::sqrt(3.0) / 4;
    const Vector3d direction = Vector3d(1).normalized();
    const double expected = 0.5 / (4 * M_PI) + 0.5 * (
        SurfaceAngularGuide::SphereKernel(Vector3d(-0.75), radius).Pdf(direction) / 18 +
        SurfaceAngularGuide::SphereKernel(Vector3d(0.75), radius).Pdf(direction) * 7 / 18 +
        SurfaceAngularGuide::SphereKernel(Vector3d(0.25), radius).Pdf(direction) * 10 / 18);
    assert(std::fabs(overlap.Pdf(Vector3d(0.0), direction) - expected) < 1e-12);
    SurfaceAngularGuide merged(Vector3d(-1), Vector3d(2));
    merged.AddScaled(overlap, 5);
    merged.Prepare();
    assert(std::fabs(merged.Pdf(Vector3d(0.0), Vector3d(1).normalized()) - expected) < 1e-12);

    for (const Vector3d& from : {Vector3d(0.0), Vector3d(0.75,0.75,0.75), Vector3d(0,0,-2), Vector3d(20,10,-40), Vector3d(1e6,0,0)})
    {
        Check(from, false);
        Check(from, true);
    }
}
