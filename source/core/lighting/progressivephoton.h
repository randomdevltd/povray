#ifndef POVRAY_CORE_PROGRESSIVEPHOTON_H
#define POVRAY_CORE_PROGRESSIVEPHOTON_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace pov
{

struct ProgressivePhotonBudget final
{
    static constexpr unsigned int batchSize = 16384;
    static constexpr unsigned int shards = 16;
    unsigned int passes;

    explicit ProgressivePhotonBudget(double quality)
    {
        if (!std::isfinite(quality) || quality <= 0.0 || quality > 256.0)
            throw std::invalid_argument("Photon quality must be finite, greater than zero and at most 256.");
        passes = static_cast<unsigned int>(std::ceil(16.0 * quality));
    }

    static double RadiusScale(std::uint64_t pass, unsigned int dimensions)
    {
        return std::pow(double(pass + 1), -1.0 / double(dimensions + 4));
    }

    static double KernelVolume(double radius, unsigned int dimensions)
    {
        const double pi = std::acos(-1.0);
        return dimensions == 2 ? pi * radius * radius : (4.0 / 3.0) * pi * radius * radius * radius;
    }
};

struct SppmEstimate final
{
    double radius = 1.0;
    double count = 0.0;
    double flux = 0.0;

    void Add(double batchFlux, unsigned int hits, unsigned int dimensions = 2)
    {
        if (hits == 0)
            return;
        const double alpha = 4.0 / double(dimensions + 4);
        const double next = count + alpha * hits;
        const double ratio = next / (count + hits);
        radius *= std::pow(ratio, 1.0 / double(dimensions));
        flux = (flux + batchFlux) * ratio;
        count = next;
    }

    double Evaluate(std::uint64_t emitted, unsigned int dimensions = 2) const
    {
        return emitted ? flux / (double(emitted) * ProgressivePhotonBudget::KernelVolume(radius, dimensions)) : 0.0;
    }
};

struct ProgressivePhotonState final
{
    float baseRadius = 0.0f, radius = 0.0f;
    float mean = 0.0f, noise = 0.0f, laplacian = 0.0f, density = 0.0f;

    void Initialize(double support, unsigned int pass)
    {
        baseRadius = float(support / ProgressivePhotonBudget::RadiusScale(pass, 2));
        radius = float(support);
    }

    void Update(unsigned int pass, double value, double varianceCoefficient, double curvature, double photonDensity)
    {
        if (!(baseRadius > 0.0f))
            return;
        const double count = double(pass + 1);
        mean += (value - mean) / count;
        noise += (varianceCoefficient - noise) / count;
        laplacian += (curvature - laplacian) / count;
        density += (photonDensity - density) / count;
        const double next = double(pass + 2);
        const double scale = baseRadius * ProgressivePhotonBudget::RadiusScale(pass + 1, 2);
        const double curvature2 = double(laplacian) * laplacian;
        const double optimum = curvature2 > 0.0 ? std::pow(32.0 * noise / (next * curvature2), 1.0 / 6.0) : 4.0 * scale;
        radius = float(std::max(0.0625 * scale, std::min(4.0 * scale, optimum)));
    }
};

}

#endif
