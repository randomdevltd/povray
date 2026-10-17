#ifndef POVRAY_CORE_PROGRESSIVEPHOTON_H
#define POVRAY_CORE_PROGRESSIVEPHOTON_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace pov
{

struct ProgressivePhotonBudget final
{
    static constexpr unsigned int batchSize = 65536;
    static constexpr unsigned int shards = 16;
    static constexpr int pilotPass = -2; ///< the pass number of the learning pilot shot before the first batch
    /// Quality 16384 emits 2^32 samples per prepared set, the length of the emission sequence; memory is bounded separately.
    static constexpr double maxQuality = 16384.0;
    static constexpr double memoryShare = 0.4, memoryCeiling = 6.0 * 1024 * 1024 * 1024, memoryMargin = 1.2;
    static constexpr unsigned int minNeighbours = 8, maxNeighbours = 256;
    static constexpr double baseNeighbours = 48.0, capScale = 8.0;
    unsigned int batches, neighbours;

    explicit ProgressivePhotonBudget(double quality)
    {
        if (!std::isfinite(quality) || quality <= 0.0 || quality > maxQuality)
            throw std::invalid_argument("Photon quality must be finite, greater than zero and at most 16384.");
        batches = static_cast<unsigned int>(std::ceil(4.0 * quality));
        neighbours = Neighbours(batches);
    }

    /// k grows as the emitted count to the 2/3, the kernel-density optimum for a surface.
    static unsigned int Neighbours(unsigned int batches)
    {
        const double k = std::round(baseNeighbours * std::pow(batches / 4.0, 2.0 / 3.0));
        return unsigned(std::max(double(minNeighbours), std::min(double(maxNeighbours), k)));
    }

    std::uint64_t EmittedSamples() const { return std::uint64_t(batches) * batchSize; }

    /// Media beam segments shrink with the emitted count at the volume kernel rate.
    double BeamSpreadScale() const { return std::pow(double(batches), -1.0 / 7.0); }

    static double EmissionWeight(std::uint64_t sources, double directionPdf)
    {
        return double(sources) / (batchSize * directionPdf);
    }

    static double KernelVolume(double radius)
    {
        return (4.0 / 3.0) * std::acos(-1.0) * radius * radius * radius;
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

/// A density learned over a target's emission square [0,1)^2: a piecewise-constant grid mixed with a uniform floor and
/// sampled by inverse CDF, so stratified inputs stay stratified. Its mean is 1, so it never changes the target's share.
class PhotonTargetDensity final
{
    public:
        static constexpr unsigned int resolution = 16, cells = resolution * resolution;
        static constexpr double exploration = 0.5; ///< the uniform share every cell keeps
        static constexpr unsigned int pilotSamples = 16384; ///< shot before the first batch to learn from, then discarded
        static constexpr double deliveredUnit = 1.0 / 1024; ///< delivered power is counted in these shares of a shot's power
        static unsigned int Cell(double u, double v)
        {
            const auto index = [](double x) { return std::min(resolution - 1, unsigned(std::max(0.0, x) * resolution)); };
            return index(u) * resolution + index(v);
        }
        bool Uniform() const { return density.empty(); }
        /// The learned part follows the power delivered per shot in each cell; with none delivered it stays uniform.
        void Learn(const std::uint64_t* shots, const std::uint64_t* delivered)
        {
            std::vector<double> rate(cells, 0.0);
            double total = 0.0;
            for (unsigned int c = 0; c < cells; ++c)
                total += rate[c] = shots[c] ? double(delivered[c]) / double(shots[c]) : 0.0;
            if (!(total > 0.0))
                return;
            density.assign(cells, 0.0);
            rows.assign(resolution + 1, 0.0);
            columns.assign(resolution * (resolution + 1), 0.0);
            for (unsigned int r = 0; r < resolution; ++r)
            {
                double* column = &columns[r * (resolution + 1)];
                for (unsigned int c = 0; c < resolution; ++c)
                {
                    const unsigned int i = r * resolution + c;
                    density[i] = exploration + (1.0 - exploration) * cells * rate[i] / total;
                    column[c + 1] = column[c] + density[i];
                }
                rows[r + 1] = rows[r] + column[resolution];
                for (unsigned int c = 1; c <= resolution; ++c)
                    column[c] /= column[resolution];
            }
            for (unsigned int r = 1; r <= resolution; ++r)
                rows[r] /= rows[resolution];
        }
        double Density(double u, double v) const { return density.empty() ? 1.0 : density[Cell(u, v)]; }
        /// Moves (u, v) by the inverse CDF and returns the density there and its cell.
        double Warp(double& u, double& v, unsigned int& cell) const
        {
            if (density.empty())
            {
                cell = Cell(u, v);
                return 1.0;
            }
            const unsigned int r = Invert(&rows[0], u);
            cell = r * resolution + Invert(&columns[r * (resolution + 1)], v);
            return density[cell];
        }
    private:
        std::vector<double> density, rows, columns;
        static unsigned int Invert(const double* cdf, double& x)
        {
            const unsigned int i = std::min(resolution - 1, unsigned(std::upper_bound(cdf + 1, cdf + resolution, x) - (cdf + 1)));
            const double t = (x - cdf[i]) / (cdf[i + 1] - cdf[i]);
            x = std::min((i + std::min(std::max(t, 0.0), 1.0)) / resolution, std::nextafter(1.0, 0.0));
            return i;
        }
};

/// Epanechnikov weight over the disc of radius support, normalised to integrate to one.
struct PhotonKernel final
{
    static double Weight(double distance2, double support2) { return std::max(0.0, 1.0 - distance2 / support2); }
    static double Normaliser(double support2) { return 2.0 / (std::acos(-1.0) * support2); }
};

}

#endif
