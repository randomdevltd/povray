#ifndef POVRAY_CORE_PREPAREDLIGHTAXIS_H
#define POVRAY_CORE_PREPAREDLIGHTAXIS_H

#include "core/configcore.h"
#include "core/math/finite.h"
#include "core/material/preparedfield.h"
#include <algorithm>

namespace pov
{

struct PreparedLightPositionSample
{
    Vector3d position;
    DBL pdf;
};

class PreparedLightAxis final
{
    public:
        PreparedLightAxis(std::shared_ptr<const PreparedGridLayout> layout,
                          std::shared_ptr<const PreparedColourField> colour,
                          std::vector<DBL> proposalMass = {}, std::vector<DBL> nodalMass = {}) :
            layout(std::move(layout)), colour(std::move(colour)), cdfCells(BuildCellOrder()), distribution(BuildDistribution(proposalMass)),
            probabilities(BuildProbabilities(distribution)),
            nodalDistribution(BuildNodalDistribution(nodalMass)), nodalProbabilities(BuildProbabilities(nodalDistribution)),
            volume(Volume(this->layout))
        {
            if (!this->colour)
                throw std::invalid_argument("Prepared light axis requires a colour field");
        }

        PreparedLightPositionSample SamplePosition(DBL choose, DBL x, DBL y, DBL z) const
        {
            for (DBL coordinate : {choose, x, y, z})
                if (!IsFiniteNumber(coordinate) || coordinate < 0.0 || coordinate >= 1.0)
                    throw std::invalid_argument("Prepared light variates must be in [0,1)");
            Vector3d point;
            if (!nodalDistribution.empty() && choose < 0.5 && choose >= BoxWeight)
            {
                const DBL target = std::min(std::nextafter(1.0, 0.0), (choose - BoxWeight) / (0.5 - BoxWeight));
                size_t node = static_cast<size_t>(std::upper_bound(nodalDistribution.begin(), nodalDistribution.end(), target) - nodalDistribution.begin());
                node = cdfCells[std::min(node, nodalDistribution.size() - 1)];
                point = layout->low + Vector3d(TentCoordinate(node % layout->counts[X], layout->counts[X], x) * layout->size[X],
                    TentCoordinate((node / layout->counts[X]) % layout->counts[Y], layout->counts[Y], y) * layout->size[Y],
                    TentCoordinate(node / (layout->counts[X] * layout->counts[Y]), layout->counts[Z], z) * layout->size[Z]);
            }
            else if (distribution.empty() || choose < 0.5)
            {
                const DBL u = !nodalDistribution.empty() && choose < BoxWeight ? choose / BoxWeight :
                    (!nodalDistribution.empty() ? (choose - 0.5) * 2.0 : (distribution.empty() ? choose : choose * 2.0));
                point = layout->low + Vector3d(u * layout->size[X], y * layout->size[Y], z * layout->size[Z]);
            }
            else
            {
                const DBL target = (choose - 0.5) * 2.0;
                const auto found = std::upper_bound(distribution.begin(), distribution.end(), target);
                size_t cell = static_cast<size_t>(found - distribution.begin());
                if (cell == distribution.size())
                    cell = distribution.size() - 1;
                cell = cdfCells[cell];
                const size_t ix = cell % layout->counts[X];
                const size_t iy = (cell / layout->counts[X]) % layout->counts[Y];
                const size_t iz = cell / (layout->counts[X] * layout->counts[Y]);
                point = layout->low + Vector3d((ix + x) * layout->size[X] / layout->counts[X],
                                               (iy + y) * layout->size[Y] / layout->counts[Y],
                                               (iz + z) * layout->size[Z] / layout->counts[Z]);
            }
            for (int axis = X; axis <= Z; ++axis)
            {
                DBL high = layout->low[axis] + layout->size[axis];
                if (high - layout->low[axis] > layout->size[axis])
                    high = std::nextafter(high, layout->low[axis]);
                point[axis] = std::max(layout->low[axis], std::min(point[axis], high));
            }
            return {point, PositionPdf(point)};
        }

        DBL PositionPdf(const Vector3d& point) const
        {
            std::array<size_t, 3> cell;
            for (int axis = X; axis <= Z; ++axis)
            {
                const DBL delta = point[axis] - layout->low[axis];
                if (!IsFiniteNumber(delta) || delta < 0.0 || delta > layout->size[axis])
                    return 0.0;
                cell[axis] = std::min(layout->counts[axis] - 1,
                                     static_cast<size_t>((delta / layout->size[axis]) * layout->counts[axis]));
            }
            if (nodalDistribution.empty() && distribution.empty())
                return 1.0 / volume;
            const size_t index = (cell[Z] * layout->counts[Y] + cell[Y]) * layout->counts[X] + cell[X];
            if (nodalDistribution.empty())
                return (0.5 + 0.5 * probabilities[index] * layout->ValueCount()) / volume;
            const DBL masked = distribution.empty() ? 1.0 : probabilities[index] * layout->ValueCount();
            return (BoxWeight + (0.5 - BoxWeight) * NodalProbability(point) * layout->ValueCount() + 0.5 * masked) / volume;
        }

        pov_base::MathColour ColourAt(const Vector3d& point) const { return colour->Sample(point); }
        size_t NodalStorageBytes() const { return (nodalDistribution.capacity() + nodalProbabilities.capacity()) * sizeof(DBL); }

    private:
        static constexpr DBL BoxWeight = (1.0 / 1048576.0);

        static DBL TentCoordinate(size_t node, size_t count, DBL u)
        {
            if (count == 1)
                return u;
            if (node == 0)
                return (u < 0.5 ? u : 1.5 - std::sqrt(2.0 * (1.0 - u))) / count;
            if (node == count - 1)
                return 1.0 - TentCoordinate(0, count, 1.0 - u);
            return (node + 0.5 + (u < 0.5 ? std::sqrt(2.0 * u) - 1.0 : 1.0 - std::sqrt(2.0 * (1.0 - u)))) / count;
        }

        DBL NodalProbability(const Vector3d& point) const
        {
            std::array<size_t, 3> lower, upper;
            std::array<DBL, 3> fraction;
            for (int a = X; a <= Z; ++a)
            {
                const DBL t = (point[a] - layout->low[a]) * (layout->counts[a] / layout->size[a]) - 0.5;
                const DBL clamped = std::max(0.0, std::min(t, DBL(layout->counts[a] - 1)));
                lower[a] = static_cast<size_t>(clamped);
                upper[a] = std::min(lower[a] + 1, layout->counts[a] - 1);
                fraction[a] = clamped - lower[a];
            }
            DBL result = 0.0;
            for (int z = 0; z != 2; ++z)
                for (int y = 0; y != 2; ++y)
                    for (int x = 0; x != 2; ++x)
                        result += nodalProbabilities[((z ? upper[Z] : lower[Z]) * layout->counts[Y] +
                            (y ? upper[Y] : lower[Y])) * layout->counts[X] + (x ? upper[X] : lower[X])] *
                            (x ? fraction[X] : 1.0 - fraction[X]) * (y ? fraction[Y] : 1.0 - fraction[Y]) *
                            (z ? fraction[Z] : 1.0 - fraction[Z]);
            return result;
        }

        std::vector<DBL> BuildNodalDistribution(const std::vector<DBL>& masses) const
        {
            if (!masses.empty() && masses.size() != layout->ValueCount())
                throw std::invalid_argument("Prepared light nodal mass count must match grid layout");
            DBL largest = 0.0, smallest = masses.empty() ? 0.0 : masses.front();
            for (DBL mass : masses)
            {
                if (!IsFiniteNumber(mass) || mass < 0.0)
                    throw std::invalid_argument("Prepared light nodal masses must be finite and nonnegative");
                largest = std::max(largest, mass);
                smallest = std::min(smallest, mass);
            }
            if (largest == smallest)
                return {};
            long double total = 0.0L;
            for (size_t index : cdfCells)
                total += static_cast<long double>(masses[index]) / largest;
            std::vector<DBL> result;
            result.reserve(masses.size());
            long double cumulative = 0.0L;
            for (size_t index : cdfCells)
            {
                cumulative += static_cast<long double>(masses[index]) / largest;
                result.push_back(std::min(1.0, static_cast<DBL>(cumulative / total)));
            }
            result.back() = 1.0;
            return result;
        }

        static DBL Volume(const std::shared_ptr<const PreparedGridLayout>& layout)
        {
            if (!layout)
                throw std::invalid_argument("Prepared light axis requires a grid layout");
            const DBL volume = layout->size[X] * layout->size[Y] * layout->size[Z];
            if (!IsFiniteNumber(volume) || volume <= 0.0 || !IsFiniteNumber(1.0 / volume))
                throw std::invalid_argument("Prepared light axis requires a finite positive volume");
            return volume;
        }

        std::vector<size_t> BuildCellOrder() const
        {
            if (!layout)
                throw std::invalid_argument("Prepared light axis requires a grid layout");
            std::vector<size_t> order(layout->ValueCount());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            auto morton = [this](size_t index)
            {
                const size_t x = index % layout->counts[X];
                const size_t y = (index / layout->counts[X]) % layout->counts[Y];
                const size_t z = index / (layout->counts[X] * layout->counts[Y]);
                std::uint64_t key = 0;
                for (unsigned bit = 0; bit < 21; ++bit)
                {
                    key |= ((static_cast<std::uint64_t>(x) >> bit) & 1) << (3*bit);
                    key |= ((static_cast<std::uint64_t>(y) >> bit) & 1) << (3*bit + 1);
                    key |= ((static_cast<std::uint64_t>(z) >> bit) & 1) << (3*bit + 2);
                }
                return key;
            };
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
            {
                const auto ka = morton(a), kb = morton(b);
                return ka == kb ? a < b : ka < kb;
            });
            return order;
        }

        std::vector<DBL> BuildProbabilities(const std::vector<DBL>& distribution) const
        {
            if (distribution.empty())
                return {};
            std::vector<DBL> result(distribution.size());
            for (size_t i = 0; i < distribution.size(); ++i)
                result[cdfCells[i]] = distribution[i] - (i ? distribution[i - 1] : 0.0);
            return result;
        }

        std::vector<DBL> BuildDistribution(const std::vector<DBL>& masses) const
        {
            if (!layout || (!masses.empty() && masses.size() != layout->ValueCount()))
                throw std::invalid_argument("Prepared light mass count must match grid layout");
            DBL largest = 0.0;
            for (DBL mass : masses)
            {
                if (!IsFiniteNumber(mass) || mass < 0.0)
                    throw std::invalid_argument("Prepared light proposal masses must be finite and nonnegative");
                largest = std::max(largest, mass);
            }
            if (largest == 0.0)
                return {};
            long double total = 0.0L;
            for (size_t index : cdfCells)
                total += static_cast<long double>(masses[index]) / largest;
            std::vector<DBL> cdf;
            cdf.reserve(masses.size());
            long double cumulative = 0.0L;
            for (size_t index : cdfCells)
            {
                cumulative += static_cast<long double>(masses[index]) / largest;
                cdf.push_back(std::min(1.0, static_cast<DBL>(cumulative / total)));
            }
            cdf.back() = 1.0;
            return cdf;
        }

        const std::shared_ptr<const PreparedGridLayout> layout;
        const std::shared_ptr<const PreparedColourField> colour;
        const std::vector<size_t> cdfCells;
        const std::vector<DBL> distribution;
        const std::vector<DBL> probabilities;
        const std::vector<DBL> nodalDistribution;
        const std::vector<DBL> nodalProbabilities;
        const DBL volume;
};

}

#endif
