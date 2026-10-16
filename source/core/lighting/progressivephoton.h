#ifndef POVRAY_CORE_PROGRESSIVEPHOTON_H
#define POVRAY_CORE_PROGRESSIVEPHOTON_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace pov
{

struct ProgressivePhotonBudget final
{
    static constexpr unsigned int batchSize = 65536;
    static constexpr unsigned int shards = 16;
    static constexpr double maxQuality = 64.0;
    static constexpr unsigned int minNeighbours = 8, maxNeighbours = 256;
    static constexpr double baseNeighbours = 48.0, capScale = 4.0;
    unsigned int batches, neighbours;

    explicit ProgressivePhotonBudget(double quality, double k0 = baseNeighbours)
    {
        if (!std::isfinite(quality) || quality <= 0.0 || quality > maxQuality)
            throw std::invalid_argument("Photon quality must be finite, greater than zero and at most 64.");
        batches = static_cast<unsigned int>(std::ceil(4.0 * quality));
        neighbours = Neighbours(batches, k0);
    }

    /// k grows as the emitted count to the 2/3, the kernel-density optimum for a surface.
    static unsigned int Neighbours(unsigned int batches, double k0 = baseNeighbours)
    {
        const double k = std::round(k0 * std::pow(batches / 4.0, 2.0 / 3.0));
        return unsigned(std::max(double(minNeighbours), std::min(double(maxNeighbours), k)));
    }

    std::uint64_t EmittedSamples() const { return std::uint64_t(batches) * batchSize; }

    /// Media beam segments shrink with the emitted count at the volume kernel rate.
    double BeamSpreadScale() const { return std::pow(double(batches), -1.0 / 7.0); }

    static double EmissionWeight(std::uint64_t sources, double directionPdf)
    {
        return double(sources) / (batchSize * directionPdf);
    }

    static double KernelVolume(double radius, unsigned int dimensions)
    {
        const double pi = std::acos(-1.0);
        return dimensions == 2 ? pi * radius * radius : (4.0 / 3.0) * pi * radius * radius * radius;
    }
};

/// Sobol points in the emission dimensions, digitally shifted per prepared set; every aligned 2^m block is a net.
class PhotonSobol final
{
    public:
        enum Dimension : unsigned int { kLight, kPositionS, kPositionT, kPolar, kAzimuth, kTargeted, kTarget, kDimensions };

        explicit PhotonSobol(std::uint64_t seed)
        {
            for (unsigned int d = 0; d < kDimensions; ++d)
            {
                std::uint64_t z = seed + 0x9E3779B97F4A7C15ull * (d + 1);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                shift[d] = std::uint32_t((z ^ (z >> 31)) >> 32);
            }
        }

        std::uint32_t Bits(std::uint64_t index, unsigned int dimension) const
        {
            const auto& v = Directions()[dimension];
            std::uint32_t x = shift[dimension];
            for (unsigned int bit = 0; index; ++bit, index >>= 1)
                if (index & 1)
                    x ^= v[bit];
            return x;
        }

        double operator()(std::uint64_t index, unsigned int dimension) const
        {
            return (Bits(index, dimension) + 0.5) * (1.0 / 4294967296.0);
        }

    private:
        std::uint32_t shift[kDimensions];

        struct Table final
        {
            std::uint32_t v[kDimensions][32];
            Table()
            {
                // Joe and Kuo primitive polynomials and initial numbers for dimensions 2 to 7
                static const unsigned int degree[kDimensions] = { 0, 1, 2, 3, 3, 4, 4 };
                static const unsigned int poly[kDimensions] = { 0, 0, 1, 1, 2, 1, 4 };
                static const std::uint32_t initial[kDimensions][4] = {
                    {}, { 1 }, { 1, 3 }, { 1, 3, 1 }, { 1, 1, 1 }, { 1, 1, 3, 3 }, { 1, 3, 5, 13 } };
                for (unsigned int k = 0; k < 32; ++k)
                    v[0][k] = 1u << (31 - k);
                for (unsigned int d = 1; d < kDimensions; ++d)
                {
                    const unsigned int s = degree[d];
                    for (unsigned int k = 0; k < 32; ++k)
                    {
                        if (k < s)
                        {
                            v[d][k] = initial[d][k] << (31 - k);
                            continue;
                        }
                        std::uint32_t value = v[d][k - s] ^ (v[d][k - s] >> s);
                        for (unsigned int i = 1; i < s; ++i)
                            if ((poly[d] >> (s - 1 - i)) & 1)
                                value ^= v[d][k - i];
                        v[d][k] = value;
                    }
                }
            }
        };

        static const std::uint32_t (&Directions())[kDimensions][32]
        {
            static const Table table;
            return table.v;
        }
};

// EXPERIMENT switches for the ablation runs; not part of the scene interface.
inline const char* PhotonSwitch(const char* name) { return std::getenv(name); }
inline bool LowDiscrepancyEmission()
{
    static const bool enabled = !PhotonSwitch("POVRAY_PHOTON_QMC") || std::string(PhotonSwitch("POVRAY_PHOTON_QMC")) != "0";
    return enabled;
}
inline double PhotonNeighbourScale()
{
    static const double k0 = PhotonSwitch("POVRAY_PHOTON_K0") ? std::atof(PhotonSwitch("POVRAY_PHOTON_K0")) : ProgressivePhotonBudget::baseNeighbours;
    return k0;
}
inline int PhotonKernelShape()
{
    static const int shape = PhotonSwitch("POVRAY_PHOTON_KERNEL") ? std::atoi(PhotonSwitch("POVRAY_PHOTON_KERNEL")) : 0;
    return shape;
}

/// Epanechnikov weight over the disc of radius support, normalised to integrate to one.
struct PhotonKernel final
{
    static double Weight(double distance2, double support2)
    {
        switch (PhotonKernelShape())
        {
            case 1: return distance2 < support2 ? 1.0 : 0.0;
            case 2: return std::max(0.0, 1.0 - std::sqrt(distance2 / support2));
            default: return std::max(0.0, 1.0 - distance2 / support2);
        }
    }
    static double Normaliser(double support2)
    {
        const double scale[3] = { 2.0, 1.0, 3.0 };
        return scale[PhotonKernelShape() % 3] / (std::acos(-1.0) * support2);
    }
};

}

#endif
