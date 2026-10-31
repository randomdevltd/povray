#ifndef POVRAY_CORE_SURFACENODES_H
#define POVRAY_CORE_SURFACENODES_H

#include "core/configcore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>
#include "core/math/vector.h"

namespace pov
{

/// Points on one surface with their normals and spacings, in a k-d tree, interpolated by their k nearest in a distance
/// that grows with normal disagreement and with offset across the tangent planes, so other sheets barely count.
class SurfaceNodeIndex final
{
public:
    static constexpr unsigned kNeighbours = 8;
    static constexpr unsigned kFound = kNeighbours + 1;
    struct Weights final
    {
        unsigned count = 0;
        std::array<std::uint32_t, kFound> node;
        std::array<double, kFound> weight; ///< reproduces values linear across the tangent plane; may be negative
        std::array<double, kFound> kernel; ///< the positive kernel weights the linear ones correct
    };

    /// Nodes in any order; Source() maps each tree position back to its given index.
    SurfaceNodeIndex(const std::vector<Vector3d>& points, const std::vector<Vector3d>& normals, const std::vector<double>& spacings)
    {
        const std::size_t count = points.size();
        if (count == 0 || normals.size() != count || spacings.size() != count || count > UINT32_MAX)
            throw std::invalid_argument("Surface nodes need one normal and spacing per point");
        source.resize(count);
        std::iota(source.begin(), source.end(), 0u);
        axes.resize(count);
        Build(points, 0, count);
        nodes.resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t from = source[i];
            const Vector3d n = normals[from].normalized();
            nodes[i] = Node{points[from], {float(n[X]), float(n[Y]), float(n[Z])}, float(spacings[from])};
        }
    }

    std::size_t Size() const { return nodes.size(); }
    const std::vector<std::uint32_t>& Source() const { return source; }
    const Vector3d& Point(std::size_t i) const { return nodes[i].point; }
    Vector3d Normal(std::size_t i) const { return Vector3d(nodes[i].normal[0], nodes[i].normal[1], nodes[i].normal[2]); }
    double Spacing(std::size_t i) const { return nodes[i].spacing; }
    std::size_t StorageBytes() const { return nodes.size() * (sizeof(Node) + sizeof(std::uint8_t)); }

    /// Normalized weights of the nearest nodes at a point, a kernel corrected to fit planes; `exclude`, a tree position,
    /// leaves that node out. Readers clamp the result to their neighbours' range, where a plane overshoots.
    void Interpolate(const Vector3d& point, const Vector3d& normal, Weights& result, std::size_t exclude = SIZE_MAX) const
    {
        Found found;
        Search(point, normal, 0, nodes.size(), exclude, found);
        result.count = found.count;
        const double h2 = found.count == kFound ? found.distance[kFound - 1] : HUGE_VAL;
        double total = 0.0;
        for (unsigned i = 0; i < found.count; ++i)
        {
            const double ratio = h2 < HUGE_VAL && h2 > 0.0 ? found.distance[i] / h2 : 0.0;
            const double kernel = (1.0 - ratio) * (1.0 - ratio);
            const double alignment = std::max(0.0, dot(normal, Normal(found.node[i])));
            result.node[i] = found.node[i];
            result.weight[i] = kernel * (1.0e-6 + alignment * alignment);
            total += result.weight[i];
        }
        for (unsigned i = 0; i < found.count; ++i)
            result.kernel[i] = result.weight[i] = total > 0.0 ? result.weight[i] / total : 1.0 / found.count;
        // Moving least squares of a plane in the query's tangent coordinates, weighted by the kernel.
        const Vector3d helper = std::fabs(normal[Z]) < 0.9 ? Vector3d(0, 0, 1) : Vector3d(0, 1, 0);
        const Vector3d t1 = cross(helper, normal).normalized(), t2 = cross(normal, t1);
        double u[kFound][2], mean[2] = {0.0, 0.0};
        for (unsigned i = 0; i < found.count; ++i)
        {
            const Vector3d delta = Point(found.node[i]) - point;
            u[i][0] = dot(delta, t1);
            u[i][1] = dot(delta, t2);
            mean[0] += result.kernel[i] * u[i][0];
            mean[1] += result.kernel[i] * u[i][1];
        }
        double c[3] = {0.0, 0.0, 0.0};
        for (unsigned i = 0; i < found.count; ++i)
        {
            const double a = u[i][0] - mean[0], b = u[i][1] - mean[1];
            c[0] += result.kernel[i] * a * a;
            c[1] += result.kernel[i] * a * b;
            c[2] += result.kernel[i] * b * b;
        }
        const double ridge = 1.0e-2 * (c[0] + c[2]);
        c[0] += ridge;
        c[2] += ridge;
        const double determinant = c[0] * c[2] - c[1] * c[1];
        if (!(determinant > 0.0)) return;
        const double g[2] = {(c[2] * mean[0] - c[1] * mean[1]) / determinant, (c[0] * mean[1] - c[1] * mean[0]) / determinant};
        for (unsigned i = 0; i < found.count; ++i)
            result.weight[i] = result.kernel[i] * (1.0 - (u[i][0] - mean[0]) * g[0] - (u[i][1] - mean[1]) * g[1]);
    }

    /// The nearest node in the interpolation distance.
    std::size_t Nearest(const Vector3d& point, const Vector3d& normal) const
    {
        Found found;
        found.limit = 1;
        Search(point, normal, 0, nodes.size(), SIZE_MAX, found);
        if (!found.count) throw std::domain_error("Surface node lookup requires a finite point and normal");
        return found.node[0];
    }

private:
    struct Node final
    {
        Vector3d point;
        float normal[3];
        float spacing;
    };
    struct Found final
    {
        unsigned count = 0, limit = kFound;
        std::array<std::uint32_t, kFound> node;
        std::array<double, kFound> distance;
        double Worst() const { return count < limit ? HUGE_VAL : distance[count - 1]; }
        void Offer(std::uint32_t index, double d2)
        {
            if (!(d2 < Worst()) && !(count == limit && d2 == distance[count - 1] && index < node[count - 1]))
                return;
            unsigned at = count < limit ? count++ : count - 1;
            while (at > 0 && (distance[at - 1] > d2 || (distance[at - 1] == d2 && node[at - 1] > index)))
            {
                distance[at] = distance[at - 1];
                node[at] = node[at - 1];
                --at;
            }
            distance[at] = d2;
            node[at] = index;
        }
    };

    void Build(const std::vector<Vector3d>& points, std::size_t lo, std::size_t hi)
    {
        if (hi - lo <= 1)
        {
            if (hi > lo) axes[lo] = 0;
            return;
        }
        Vector3d low(HUGE_VAL), high(-HUGE_VAL);
        for (std::size_t i = lo; i < hi; ++i)
            for (int a = 0; a < 3; ++a)
            {
                low[a] = std::min(low[a], points[source[i]][a]);
                high[a] = std::max(high[a], points[source[i]][a]);
            }
        const Vector3d extent = high - low;
        const int axis = extent[X] >= extent[Y] ? (extent[X] >= extent[Z] ? X : Z) : (extent[Y] >= extent[Z] ? Y : Z);
        const std::size_t mid = lo + (hi - lo) / 2;
        std::nth_element(source.begin() + lo, source.begin() + mid, source.begin() + hi, [&](std::uint32_t a, std::uint32_t b)
        {
            const double pa = points[a][axis], pb = points[b][axis];
            return pa < pb || (pa == pb && a < b);
        });
        axes[mid] = std::uint8_t(axis);
        Build(points, lo, mid);
        Build(points, mid + 1, hi);
    }

    double Distance2(const Vector3d& point, const Vector3d& normal, std::size_t i) const
    {
        const Node& node = nodes[i];
        const Vector3d delta = node.point - point;
        const Vector3d n = Normal(i);
        const double spacing = node.spacing, across = dot(normal + n, delta);
        return delta.lengthSqr() + 4.0 * spacing * spacing * (1.0 - dot(normal, n)) + 16.0 * across * across;
    }

    void Search(const Vector3d& point, const Vector3d& normal, std::size_t lo, std::size_t hi, std::size_t exclude, Found& found) const
    {
        if (lo >= hi) return;
        const std::size_t mid = lo + (hi - lo) / 2;
        if (mid != exclude)
            found.Offer(std::uint32_t(mid), Distance2(point, normal, mid));
        if (hi - lo == 1) return;
        const int axis = axes[mid];
        const double offset = point[axis] - nodes[mid].point[axis];
        const bool left = offset < 0.0;
        Search(point, normal, left ? lo : mid + 1, left ? mid : hi, exclude, found);
        if (offset * offset <= found.Worst())
            Search(point, normal, left ? mid + 1 : lo, left ? hi : mid, exclude, found);
    }

    std::vector<Node> nodes;
    std::vector<std::uint32_t> source;
    std::vector<std::uint8_t> axes;
};

}
#endif
