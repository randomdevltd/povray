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
#include <map>
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
        return PreparedSourceDomain(low, size, counts, {});
    }
    static PreparedSourceDomain Sphere(Vector3d centre, DBL radius, size_t angularCount,
                                       std::vector<DBL> radialBreaks)
    {
        return PreparedSourceDomain(centre, Vector3d(radius), {{angularCount, angularCount, 0}},
                                    std::move(radialBreaks));
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
               (volumes.capacity() + radii.capacity()) * sizeof(DBL);
    }
    const std::vector<DBL>& RadialBreaks() const
    {
        return radii;
    }
    const std::array<size_t, 3>& Counts() const
    {
        return counts;
    }
    const std::array<size_t, 3>& ChartCounts() const
    {
        return counts;
    }
    // Positive corner quadrature is approximate for curved cells.
    std::array<QuadraturePoint, 8> CornerQuadrature(size_t cell) const
    {
        std::array<QuadraturePoint, 8> result;
        for (size_t i = 0; i < 8; ++i)
            result[i] = {Node(CellNodes(cell)[i]), CellVolume(cell) / 8};
        return result;
    }
    DBL CellVolume(size_t i) const
    {
        return volumes.at(i);
    }
    DBL Volume() const
    {
        return totalVolume;
    }
    bool Spherical() const
    {
        return !radii.empty();
    }
    Stencil SampleStencil(const Vector3d& point) const
    {
        Vector3d q;
        int face = 0;
        if (Spherical())
        {
            const Vector3d d = (point - origin) / extent[X];
            const DBL r = d.length();
            if (!IsFiniteNumber(r) || r < radii.front() || r > radii.back())
                return {};
            int axis = X;
            for (int a = Y; a <= Z; ++a)
                if (std::fabs(d[a]) > std::fabs(d[axis]))
                    axis = a;
            face = axis * 2 + (d[axis] < 0);
            const DBL major = std::fabs(d[axis]);
            q = Vector3d(major > 0 ? (d[(axis + 1) % 3] / major + 1) * 0.5 : 0.5,
                         major > 0 ? (d[(axis + 2) % 3] / major + 1) * 0.5 : 0.5, r);
        }
        else
            for (int a = X; a <= Z; ++a)
                q[a] = (point[a] - origin[a]) / extent[a];
        std::array<size_t, 3> cell;
        Vector3d t;
        for (int a = X; a <= Z; ++a)
        {
            if (!IsFiniteNumber(q[a]) || q[a] < 0 || q[a] > 1)
                return {};
            if (Spherical() && a == Z)
            {
                cell[a] = std::min(counts[a] - 1, size_t(std::upper_bound(radii.begin(), radii.end(), q[a]) -
                                                         radii.begin() - 1));
                t[a] = (q[a] - radii[cell[a]]) / (radii[cell[a] + 1] - radii[cell[a]]);
            }
            else
            {
                const DBL u = q[a] * counts[a];
                cell[a] = std::min(counts[a] - 1, size_t(u));
                t[a] = u - cell[a];
            }
        }
        Stencil s;
        s.valid = true;
        s.cell = ((face * counts[Z] + cell[Z]) * counts[Y] + cell[Y]) * counts[X] + cell[X];
        s.nodes = corners[s.cell];
        for (int i = 0; i < 8; ++i)
            s.weights[i] =
                ((i & 1) ? t[X] : 1 - t[X]) * ((i & 2) ? t[Y] : 1 - t[Y]) * ((i & 4) ? t[Z] : 1 - t[Z]);
        return s;
    }
    PositionSample SampleUniformPosition(std::uint64_t key) const
    {
        Vector3d q;
        const DBL u = Draw(key), v = Draw(key), w = Draw(key);
        if (Spherical())
        {
            const DBL z = 1 - 2 * u, phi = 2 * std::acos(-1.0) * v;
            const DBL transverse = std::sqrt(std::max(DBL(0), 1 - z * z));
            const DBL a = radii.front(), b = radii.back();
            const DBL r = std::cbrt(a * a * a + (b - a) * (b * b + a * b + a * a) * w);
            q = Vector3d(transverse * std::cos(phi), transverse * std::sin(phi), z) * r;
        }
        else
            q = Vector3d(u, v, w);
        Vector3d point = origin;
        for (int a = X; a <= Z; ++a)
            point[a] += q[a] * extent[a];
        return {point, 1 / totalVolume};
    }
    PositionSample SamplePosition(size_t cell, std::uint64_t key) const
    {
        if (cell >= CellCount())
            throw std::out_of_range("Prepared source cell");
        const size_t x = cell % counts[X], y = (cell / counts[X]) % counts[Y],
                     z = (cell / (counts[X] * counts[Y])) % counts[Z];
        const int face = int(cell / (counts[X] * counts[Y] * counts[Z]));
        Vector3d q;
        if (!Spherical())
        {
            const DBL ux = Draw(key), uy = Draw(key), uz = Draw(key);
            q = Vector3d((x + ux) / counts[X], (y + uy) / counts[Y], (z + uz) / counts[Z]);
        }
        else
        {
            const DBL loU = 2.0 * x / counts[X] - 1, hiU = 2.0 * (x + 1) / counts[X] - 1;
            const DBL loV = 2.0 * y / counts[Y] - 1, hiV = 2.0 * (y + 1) / counts[Y] - 1;
            const DBL minU = std::max(loU, std::min(0.0, hiU)), minV = std::max(loV, std::min(0.0, hiV));
            const DBL bound = std::pow(1 + minU * minU + minV * minV, -1.5);
            DBL u, v;
            do
            {
                u = loU + (hiU - loU) * Draw(key);
                v = loV + (hiV - loV) * Draw(key);
            } while (Draw(key) * bound > std::pow(1 + u * u + v * v, -1.5));
            const DBL a = radii[z], b = radii[z + 1];
            const DBL r = std::cbrt(a * a * a + (b - a) * (b * b + a * b + a * a) * Draw(key));
            q = Direction(face, u, v) * r;
        }
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
        Vector3d q((x + 0.5) / counts[X], (y + 0.5) / counts[Y], (z + 0.5) / counts[Z]);
        if (Spherical())
            q = Direction(int(cell / (counts[X] * counts[Y] * counts[Z])), 2 * q[X] - 1, 2 * q[Y] - 1) *
                ((radii[z] + radii[z + 1]) * 0.5);
        Vector3d p = origin;
        for (int a = X; a <= Z; ++a)
            p[a] += q[a] * extent[a];
        return p;
    }

private:
    friend class PreparedSourceDistribution;
    Vector3d origin, extent;
    std::array<size_t, 3> counts;
    std::vector<DBL> radii, volumes;
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
    static Vector3d Direction(int face, DBL u, DBL v)
    {
        Vector3d d(0.0);
        const int a = face / 2;
        d[a] = (face & 1) ? -1 : 1;
        d[(a + 1) % 3] = u;
        d[(a + 2) % 3] = v;
        return d / std::sqrt(1 + u * u + v * v);
    }
    static DBL SolidAngle(DBL u, DBL v)
    {
        return std::atan2(u * v, std::sqrt(1 + u * u + v * v));
    }
    PreparedSourceDomain(Vector3d origin, Vector3d extent, std::array<size_t, 3> counts,
                         std::vector<DBL> radii)
        : origin(origin), extent(extent), counts(counts), radii(std::move(radii))
    {
        if (Spherical())
        {
            if (this->radii.size() < 2 || this->radii.front() < 0 || this->radii.back() != 1)
                throw std::invalid_argument("Invalid source radial breaks");
            for (size_t i = 0; i < this->radii.size(); ++i)
                if (!IsFiniteNumber(this->radii[i]) || (i && this->radii[i] <= this->radii[i - 1]))
                    throw std::invalid_argument("Invalid source radial breaks");
            this->counts[Z] = this->radii.size() - 1;
        }
        size_t cells = Spherical() ? 6 : 1;
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
            if (Spherical())
                for (size_t r = 1; r < this->radii.size(); ++r)
                    if ((this->radii[r] - this->radii[r - 1]) * extent[a] <= spacing)
                        throw std::invalid_argument("Unrepresentable source radial boundaries");
            cells *= this->counts[a];
        }
        std::map<std::array<int, 4>, size_t> ids;
        const int n = int(this->counts[X]);
        for (int face = 0; face < (Spherical() ? 6 : 1); ++face)
            for (size_t z = 0; z < this->counts[Z]; ++z)
                for (size_t y = 0; y < this->counts[Y]; ++y)
                    for (size_t x = 0; x < this->counts[X]; ++x)
                    {
                        std::array<size_t, 8> c;
                        for (int i = 0; i < 8; ++i)
                        {
                            const size_t ix = x + bool(i & 1), iy = y + bool(i & 2), iz = z + bool(i & 4);
                            std::array<int, 4> id{{int(ix), int(iy), int(iz), 0}};
                            Vector3d q(DBL(ix) / this->counts[X], DBL(iy) / this->counts[Y],
                                       DBL(iz) / this->counts[Z]);
                            if (Spherical())
                            {
                                const int a = face / 2;
                                id = {{0, 0, 0, int(iz)}};
                                id[a] = (face & 1) ? -n : n;
                                id[(a + 1) % 3] = 2 * int(ix) - n;
                                id[(a + 2) % 3] = 2 * int(iy) - n;
                                q = Direction(face, 2 * q[X] - 1, 2 * q[Y] - 1) * this->radii[iz];
                                if (this->radii[iz] == 0)
                                    id = {{0, 0, 0, -1}};
                            }
                            auto inserted = ids.emplace(id, nodes.size());
                            c[i] = inserted.first->second;
                            if (inserted.second)
                            {
                                Vector3d p = origin;
                                for (int a = X; a <= Z; ++a)
                                    p[a] += q[a] * extent[a];
                                nodes.push_back(p);
                            }
                        }
                        corners.push_back(c);
                        DBL v;
                        if (Spherical())
                        {
                            const DBL u0 = 2.0 * x / n - 1, u1 = 2.0 * (x + 1) / n - 1, v0 = 2.0 * y / n - 1,
                                      v1 = 2.0 * (y + 1) / n - 1;
                            const DBL omega = SolidAngle(u1, v1) - SolidAngle(u0, v1) - SolidAngle(u1, v0) +
                                              SolidAngle(u0, v0);
                            const DBL a = this->radii[z], b = this->radii[z + 1];
                            v = omega * ((b - a) * (b * b + a * b + a * a) / 3) * extent[X] * extent[X] *
                                extent[X];
                        }
                        else
                            v = (extent[X] / this->counts[X]) * (extent[Y] / this->counts[Y]) *
                                (extent[Z] / this->counts[Z]);
                        if (!IsFiniteNumber(v) || !(v > 0) || !IsFiniteNumber(1 / v))
                            throw std::invalid_argument("Unrepresentable source cell volume");
                        volumes.push_back(v);
                        totalVolume += v;
                    }
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
