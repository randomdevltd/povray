#ifndef POVRAY_CORE_PREPAREDSOURCELAYOUT_H
#define POVRAY_CORE_PREPAREDSOURCELAYOUT_H
#include "core/configcore.h"
#include "core/math/finite.h"
#include "core/math/vector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pov
{
namespace prepared_source_detail
{
template <class Next> bool Bernoulli(long double probability, Next next)
{
    if (!(probability > 0))
        return false;
    if (probability >= 1)
        return true;
    while (probability < 0.5L)
    {
        if (next() & UINT64_C(1))
            return false;
        probability *= 2;
    }
    return (next() >> 11) * (1.0L / 9007199254740992.0L) < probability;
}
} // namespace prepared_source_detail

class PreparedSourceDomain final
{
public:
    struct Stencil
    {
        std::array<size_t, 8> nodes{};
        std::array<DBL, 8> weights{};
        size_t cell = 0;
        bool valid = false;
    };
    struct PositionSample
    {
        Vector3d position;
        DBL pdf;
    };
    struct QuadraturePoint
    {
        Vector3d position;
        DBL weight;
    };
    static PreparedSourceDomain Cartesian(Vector3d low, Vector3d size, std::array<size_t, 3> counts)
    {
        return PreparedSourceDomain(low, size, counts);
    }
    size_t CellCount() const
    {
        return corners.size();
    }
    size_t NodeCount() const
    {
        return nodes.size();
    }
    const Vector3d& Node(size_t i) const
    {
        return nodes.at(i);
    }
    const std::vector<Vector3d>& Nodes() const
    {
        return nodes;
    }
    const std::array<size_t, 8>& CellNodes(size_t i) const
    {
        return corners.at(i);
    }
    size_t StorageBytes() const
    {
        return sizeof(*this) + nodes.capacity() * sizeof(Vector3d) +
               corners.capacity() * sizeof(std::array<size_t, 8>) +
               volumes.capacity() * sizeof(DBL);
    }
    const std::array<size_t, 3>& Counts() const
    {
        return counts;
    }
    const std::array<size_t, 3>& ChartCounts() const
    {
        return counts;
    }
    std::array<QuadraturePoint, 8> CornerQuadrature(size_t cell) const
    {
        std::array<QuadraturePoint, 8> result;
        for (size_t i = 0; i < 8; ++i)
            result[i] = {Node(CellNodes(cell)[i]), CellVolume(cell) / 8};
        return result;
    }
    const Vector3d& Low() const
    {
        return origin;
    }
    const Vector3d& Size() const
    {
        return extent;
    }
    DBL CellVolume(size_t i) const
    {
        return volumes.at(i);
    }
    DBL Volume() const
    {
        return totalVolume;
    }
    Stencil SampleStencil(const Vector3d& point) const
    {
        std::array<size_t, 3> cell;
        Vector3d t;
        for (int a = X; a <= Z; ++a)
        {
            const DBL q = (point[a] - origin[a]) / extent[a];
            if (!IsFiniteNumber(q) || q < 0 || q > 1)
                return {};
            const DBL u = q * counts[a];
            cell[a] = std::min(counts[a] - 1, size_t(u));
            t[a] = u - cell[a];
        }
        Stencil s;
        s.valid = true;
        s.cell = (cell[Z] * counts[Y] + cell[Y]) * counts[X] + cell[X];
        s.nodes = corners[s.cell];
        for (int i = 0; i < 8; ++i)
            s.weights[i] =
                ((i & 1) ? t[X] : 1 - t[X]) * ((i & 2) ? t[Y] : 1 - t[Y]) * ((i & 4) ? t[Z] : 1 - t[Z]);
        return s;
    }
    PositionSample SampleUniformPosition(std::uint64_t key) const
    {
        const DBL u = Draw(key), v = Draw(key), w = Draw(key);
        return SampleUniformPosition(Vector3d(u, v, w));
    }
    PositionSample SampleUniformPosition(const Vector3d& q) const
    {
        Vector3d point = origin;
        for (int a = X; a <= Z; ++a)
            point[a] += q[a] * extent[a];
        return {point, 1 / totalVolume};
    }
    PositionSample SamplePosition(size_t cell, std::uint64_t key) const
    {
        const DBL ux = Draw(key), uy = Draw(key), uz = Draw(key);
        return SamplePosition(cell, Vector3d(ux, uy, uz));
    }
    /// The point at `offset`, each coordinate in [0,1), within a cell.
    PositionSample SamplePosition(size_t cell, const Vector3d& offset) const
    {
        if (cell >= CellCount())
            throw std::out_of_range("Prepared source cell");
        const size_t x = cell % counts[X], y = (cell / counts[X]) % counts[Y],
                     z = (cell / (counts[X] * counts[Y])) % counts[Z];
        const Vector3d q((x + offset[X]) / counts[X], (y + offset[Y]) / counts[Y], (z + offset[Z]) / counts[Z]);
        Vector3d p = origin;
        for (int a = X; a <= Z; ++a)
            p[a] += q[a] * extent[a];
        return {p, 1 / volumes[cell]};
    }
    DBL PositionPdf(size_t cell, const Vector3d& point) const
    {
        const auto s = SampleStencil(point);
        return s.valid && s.cell == cell ? 1 / volumes.at(cell) : 0;
    }
    Vector3d CellMidpoint(size_t cell) const
    {
        const size_t x = cell % counts[X], y = (cell / counts[X]) % counts[Y],
                     z = (cell / (counts[X] * counts[Y])) % counts[Z];
        const Vector3d q((x + 0.5) / counts[X], (y + 0.5) / counts[Y], (z + 0.5) / counts[Z]);
        Vector3d p = origin;
        for (int a = X; a <= Z; ++a)
            p[a] += q[a] * extent[a];
        return p;
    }

private:
    friend class PreparedSourceDistribution;
    Vector3d origin, extent;
    std::array<size_t, 3> counts;
    std::vector<DBL> volumes;
    std::vector<Vector3d> nodes;
    std::vector<std::array<size_t, 8>> corners;
    DBL totalVolume = 0;
    static std::uint64_t Next64(std::uint64_t& state)
    {
        state += UINT64_C(0x9e3779b97f4a7c15);
        auto z = state;
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
    static DBL Draw(std::uint64_t& state)
    {
        return (Next64(state) >> 11) * (1.0 / 9007199254740992.0);
    }
    PreparedSourceDomain(Vector3d origin, Vector3d extent, std::array<size_t, 3> counts)
        : origin(origin), extent(extent), counts(counts)
    {
        size_t cells = 1;
        for (int a = X; a <= Z; ++a)
        {
            if (!IsFiniteNumber(origin[a]) || !IsFiniteNumber(extent[a]) || extent[a] <= 0 ||
                !IsFiniteNumber(origin[a] + extent[a]) || this->counts[a] == 0 ||
                this->counts[a] > std::numeric_limits<int>::max() / 2 ||
                this->counts[a] > std::numeric_limits<size_t>::max() / cells)
                throw std::invalid_argument("Invalid source domain");
            const DBL magnitude = std::fabs(origin[a]) + extent[a];
            if (!IsFiniteNumber(magnitude))
                throw std::invalid_argument("Unrepresentable source bounds");
            const DBL spacing = std::nextafter(magnitude, std::numeric_limits<DBL>::infinity()) - magnitude;
            if (extent[a] / this->counts[a] <= spacing)
                throw std::invalid_argument("Unrepresentable source cell boundaries");
            cells *= this->counts[a];
        }
        const DBL v = (extent[X] / this->counts[X]) * (extent[Y] / this->counts[Y]) * (extent[Z] / this->counts[Z]);
        if (!IsFiniteNumber(v) || !(v > 0) || !IsFiniteNumber(1 / v))
            throw std::invalid_argument("Unrepresentable source cell volume");
        // nodes are numbered in the order the cells first reach them, as a lattice index finds them
        const size_t nx = this->counts[X] + 1, ny = this->counts[Y] + 1;
        std::vector<size_t> ids(nx * ny * (this->counts[Z] + 1), std::numeric_limits<size_t>::max());
        corners.reserve(cells);
        volumes.assign(cells, v);
        for (size_t z = 0; z < this->counts[Z]; ++z)
            for (size_t y = 0; y < this->counts[Y]; ++y)
                for (size_t x = 0; x < this->counts[X]; ++x)
                {
                    std::array<size_t, 8> c;
                    for (int i = 0; i < 8; ++i)
                    {
                        const size_t ix = x + bool(i & 1), iy = y + bool(i & 2), iz = z + bool(i & 4);
                        size_t& id = ids[(iz * ny + iy) * nx + ix];
                        if (id == std::numeric_limits<size_t>::max())
                        {
                            id = nodes.size();
                            const Vector3d q(DBL(ix) / this->counts[X], DBL(iy) / this->counts[Y],
                                             DBL(iz) / this->counts[Z]);
                            Vector3d p = origin;
                            for (int a = X; a <= Z; ++a)
                                p[a] += q[a] * extent[a];
                            nodes.push_back(p);
                        }
                        c[i] = id;
                    }
                    corners.push_back(c);
                }
        for (size_t cell = 0; cell < cells; ++cell)
            totalVolume += v;
        if (!IsFiniteNumber(totalVolume))
            throw std::invalid_argument("Unrepresentable source volume");
    }
};

class PreparedSourceDistribution final
{
public:
    PreparedSourceDistribution(std::shared_ptr<const PreparedSourceDomain> domain,
                               const std::vector<DBL>& masses, DBL supportWeight = 0.01)
        : domain(std::move(domain))
    {
        if (!this->domain || (!masses.empty() && masses.size() != this->domain->CellCount()) ||
            !IsFiniteNumber(supportWeight) || supportWeight <= 0 || supportWeight > 1)
            throw std::invalid_argument("Invalid prepared source distribution");
        DBL maximum = 0;
        for (DBL mass : masses)
        {
            if (!IsFiniteNumber(mass) || mass < 0)
                throw std::invalid_argument("Invalid prepared source mass");
            maximum = std::max(maximum, mass);
        }
        if (!(maximum > 0))
            return;
        long double sum = 0;
        if (maximum > 0)
            for (DBL mass : masses)
                sum += static_cast<long double>(mass) / maximum;
        leafCount = 1;
        while (leafCount < masses.size())
        {
            if (leafCount > std::numeric_limits<size_t>::max() / 4)
                throw std::invalid_argument("Prepared source tree is too large");
            leafCount *= 2;
        }
        tree.resize(leafCount * 2, 0);
        for (size_t i = 0; i < masses.size(); ++i)
        {
            const long double uniform =
                static_cast<long double>(this->domain->CellVolume(i)) / this->domain->Volume();
            const long double importance =
                maximum > 0 ? (static_cast<long double>(masses[i]) / maximum) / sum : uniform;
            tree[leafCount + i] = supportWeight * uniform + (1 - supportWeight) * importance;
            if (!(tree[leafCount + i] > 0))
                throw std::invalid_argument("Unrepresentable prepared source probability");
        }
        for (size_t i = leafCount - 1; i > 0; --i)
            tree[i] = tree[i * 2] + tree[i * 2 + 1];
    }
    const std::shared_ptr<const PreparedSourceDomain>& Domain() const
    {
        return domain;
    }
    PreparedSourceDomain::PositionSample SamplePosition(std::uint64_t key) const
    {
        if (tree.empty())
            return domain->SampleUniformPosition(key);
        size_t node = 1;
        while (node < leafCount)
        {
            size_t small = node * 2, large = small + 1;
            if (tree[small] > tree[large])
                std::swap(small, large);
            const bool selectedSmall = prepared_source_detail::Bernoulli(
                tree[small] / tree[node], [&key] { return PreparedSourceDomain::Next64(key); });
            node = selectedSmall ? small : large;
        }
        auto sample = domain->SamplePosition(node - leafCount, key);
        sample.pdf = PositionPdf(sample.position);
        return sample;
    }
    /// As SamplePosition(key), stratified: the cell by inverting the tree at u in [0,1), the point at `offset` within it.
    PreparedSourceDomain::PositionSample SamplePosition(DBL u, const Vector3d& offset) const
    {
        if (tree.empty())
            return domain->SampleUniformPosition(offset);
        size_t node = 1;
        long double v = u;
        while (node < leafCount)
        {
            const long double left = tree[node * 2] / tree[node];
            if (v < left)
            {
                node = node * 2;
                v /= left;
            }
            else
            {
                node = node * 2 + 1;
                v = (v - left) / (1 - left);
            }
            v = std::min(std::max(v, 0.0L), 1.0L - std::numeric_limits<long double>::epsilon());
        }
        auto sample = domain->SamplePosition(node - leafCount, offset);
        sample.pdf = PositionPdf(sample.position);
        return sample;
    }
    size_t StorageBytes() const
    {
        return sizeof(*this) + tree.capacity() * sizeof(long double);
    }
    DBL PositionPdf(const Vector3d& point) const
    {
        const auto stencil = domain->SampleStencil(point);
        if (tree.empty())
            return stencil.valid ? 1 / domain->Volume() : 0;
        return stencil.valid ? static_cast<DBL>(tree[leafCount + stencil.cell] / tree[1] /
                                                domain->CellVolume(stencil.cell))
                             : 0;
    }

private:
    const std::shared_ptr<const PreparedSourceDomain> domain;
    size_t leafCount = 0;
    std::vector<long double> tree;
};
} // namespace pov
#endif
