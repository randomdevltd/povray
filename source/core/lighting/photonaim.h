#ifndef POVRAY_CORE_PHOTONAIM_H
#define POVRAY_CORE_PHOTONAIM_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/coretypes.h"

namespace pov
{

/// One piece of a photon target's bounds: a world box, or a sphere when sphereRadius is positive, with the target's weight.
struct PhotonAimBound final
{
    Vector3d low, high;
    double weight;
    std::uint32_t target;
    Vector3d sphereCentre = Vector3d(0.0);
    double sphereRadius = 0.0;
    Vector3d Centre() const { return sphereRadius > 0.0 ? sphereCentre : (low + high) * 0.5; }
    double Radius() const { return sphereRadius > 0.0 ? sphereRadius : (high - low).length() * 0.5; }
};

/// A light's photon aiming domain: a square mapped onto a cap of its directions or a rectangle of its plane, rastered
/// to the cells its targets can reach. why: doc/progressive-photons.md#aiming
class PhotonAimDomain final
{
    public:
        static constexpr unsigned int cells = 128, blocks = 16;
        static constexpr double targetShare = 0.5;

        struct Pick final
        {
            std::uint32_t cell, target;
            double x, y;
        };

        bool parallel = false;
        Vector3d centre, axis, s, t;
        double width = 2.0;                              ///< cap: one minus the cosine of its half angle
        double u0 = 0.0, v0 = 0.0, du = 1.0, dv = 1.0;    ///< plane: the rectangle in the (s, t) frame
        std::vector<float> weight;                       ///< per cell: the heaviest target weight reaching it, zero outside
        std::vector<std::uint32_t> owner;                ///< per cell: the target of that weight
        std::vector<double> mass;                        ///< per cell: the probability of a sample landing in it

        static void Frame(const Vector3d& axis, Vector3d& s, Vector3d& t)
        {
            s = cross(std::abs(axis[Y]) < 0.9 ? Vector3d(0, 1, 0) : Vector3d(1, 0, 0), axis).normalized();
            t = cross(axis, s);
        }

        /// Square (x, y) to the unit disc by the concentric map, which keeps area; returns the radius.
        static double Disc(double x, double y, double& cx, double& cy)
        {
            const double p = 2.0 * x - 1.0, q = 2.0 * y - 1.0;
            if (p == 0.0 && q == 0.0)
            {
                cx = cy = 0.0;
                return 0.0;
            }
            double rho, phi;
            if (std::abs(p) > std::abs(q)) { rho = p; phi = (M_PI / 4.0) * (q / p); }
            else { rho = q; phi = M_PI / 2.0 - (M_PI / 4.0) * (p / q); }
            cx = rho * std::cos(phi);
            cy = rho * std::sin(phi);
            return std::abs(rho);
        }

        /// The inverse of Disc for the point at radius r and angle phi.
        static void Square(double r, double phi, double& x, double& y)
        {
            if (phi < -M_PI / 4.0)
                phi += 2.0 * M_PI;
            double p, q;
            if (phi < M_PI / 4.0) { p = r; q = p * phi / (M_PI / 4.0); }
            else if (phi < 3.0 * M_PI / 4.0) { q = r; p = q * (M_PI / 2.0 - phi) / (M_PI / 4.0); }
            else if (phi < 5.0 * M_PI / 4.0) { p = -r; q = p * (phi - M_PI) / (M_PI / 4.0); }
            else { q = -r; p = q * (1.5 * M_PI - phi) / (M_PI / 4.0); }
            x = 0.5 * (p + 1.0);
            y = 0.5 * (q + 1.0);
        }

        /// The cap direction of square point (x, y): the disc radius squared is the share of the cap's solid angle.
        Vector3d Direction(double x, double y) const
        {
            double cx, cy;
            const double rho = Disc(x, y, cx, cy);
            const double deficit = rho * rho * width;
            const double sine = std::sqrt(std::max(0.0, deficit * (2.0 - deficit)));
            const Vector3d side = rho > 0.0 ? (s * cx + t * cy) / rho : s;
            return (axis * (1.0 - deficit) + side * sine).normalized();
        }

        Vector3d Point(double x, double y) const { return centre + s * (u0 + x * du) + t * (v0 + y * dv); }

        static std::uint32_t Cell(double x, double y)
        {
            const auto index = [](double v) { return std::uint32_t(std::min(double(cells - 1), std::max(0.0, std::floor(v * cells)))); };
            return index(y) * cells + index(x);
        }

        /// The cell holding direction d, false outside the cap.
        bool Locate(const Vector3d& d, std::uint32_t& cell) const
        {
            const double deficit = std::max(0.0, 1.0 - dot(d, axis));
            if (deficit > width * (1.0 + 1e-9))
                return false;
            double x, y;
            Square(std::sqrt(std::min(1.0, deficit / width)), std::atan2(dot(d, t), dot(d, s)), x, y);
            cell = Cell(x, y);
            return true;
        }

        bool Empty() const { return rowCdf.empty() || !(rowCdf.back() > 0.0); }

        /// Density of a sample in the cell, per steradian for a cap and per unit area for a plane.
        double Pdf(std::uint32_t cell) const
        {
            return mass[cell] * cells * cells / (parallel ? du * dv : 2.0 * M_PI * width);
        }

        /// The cell by its row's and then its column's cumulative mass, keeping the stratification of (a, b) in [0, 1)^2.
        Pick Sample(double a, double b) const
        {
            const double row = a * rowCdf.back();
            const std::uint32_t j = std::uint32_t(std::min<std::ptrdiff_t>(cells - 1, std::upper_bound(rowCdf.begin() + 1, rowCdf.end(), row) - rowCdf.begin() - 1));
            const auto cdf = colCdf.begin() + j * (cells + 1);
            const double col = b * cdf[cells];
            const std::uint32_t i = std::uint32_t(std::min<std::ptrdiff_t>(cells - 1, std::upper_bound(cdf + 1, cdf + cells + 1, col) - cdf - 1));
            Pick pick;
            pick.cell = j * cells + i;
            pick.target = owner[pick.cell];
            pick.x = (i + std::min(1.0 - 1e-9, std::max(0.0, (col - cdf[i]) / (cdf[i + 1] - cdf[i])))) / cells;
            pick.y = (j + std::min(1.0 - 1e-9, std::max(0.0, (row - rowCdf[j]) / (rowCdf[j + 1] - rowCdf[j])))) / cells;
            return pick;
        }

        /// A cap about light holding every bound as seen from within lightRadius of it, rastered to the cells that reach
        /// a bound, the aperture's bound if any and the spot cone if any.
        void BuildCap(const Vector3d& light, double lightRadius, const std::vector<PhotonAimBound>& bounds,
                      const PhotonAimBound* aperture, const Vector3d* spot, double spotCosine)
        {
            parallel = false;
            centre = light;
            std::vector<Cone> cones;
            Vector3d sum(0.0);
            bool all = false;
            for (const auto& b : bounds)
            {
                cones.push_back(Seen(b, light, lightRadius));
                all = all || cones.back().angle >= M_PI;
                const double sine = std::sin(cones.back().angle);
                sum += cones.back().direction * (sine * sine + 1e-12);
            }
            double half = M_PI;
            axis = Vector3d(0, 0, 1);
            if (!all && sum.length() > 1e-9)
            {
                axis = sum.normalized();
                half = 0.0;
                for (const auto& c : cones)
                    half = std::max(half, std::acos(std::max(-1.0, std::min(1.0, dot(axis, c.direction)))) + c.angle);
            }
            width = half >= M_PI ? 2.0 : 1.0 - std::cos(half);
            Frame(axis, s, t);
            const Cone gate = aperture ? Seen(*aperture, light, lightRadius) : Cone{Vector3d(0, 0, 1), M_PI, nullptr, 0.0, 0};
            const Cone beam{spot ? *spot : Vector3d(0, 0, 1), spot ? std::acos(std::max(-1.0, std::min(1.0, spotCosine))) : M_PI, nullptr, 0.0, 0};
            Reset(bounds);
            const unsigned int span = cells / blocks;
            std::vector<const Cone*> near;
            for (unsigned int bj = 0; bj < blocks; ++bj)
                for (unsigned int bi = 0; bi < blocks; ++bi)
                {
                    double blockRadius;
                    const Vector3d bc = CellCone(double(bi) / blocks, double(bj) / blocks, double(bi + 1) / blocks, double(bj + 1) / blocks, blockRadius);
                    if (!Reaches(bc, blockRadius, gate, light, lightRadius) || !Reaches(bc, blockRadius, beam, light, lightRadius))
                        continue;
                    near.clear();
                    for (const auto& c : cones)
                        if (Reaches(bc, blockRadius, c, light, lightRadius))
                            near.push_back(&c);
                    for (unsigned int j = bj * span; j < (bj + 1) * span && !near.empty(); ++j)
                        for (unsigned int i = bi * span; i < (bi + 1) * span; ++i)
                        {
                            double radius;
                            const Vector3d c = CellCone(double(i) / cells, double(j) / cells, double(i + 1) / cells, double(j + 1) / cells, radius);
                            if (!Reaches(c, radius, gate, light, lightRadius) || !Reaches(c, radius, beam, light, lightRadius))
                                continue;
                            for (const Cone* cone : near)
                                if (Reaches(c, radius, *cone, light, lightRadius))
                                    Cover(j * cells + i, cone->weight, cone->target);
                        }
                }
            Finish();
        }

        /// A rectangle of the light's plane holding every bound's shadow along direction, rastered to the cells they cover,
        /// within the aperture's shadow and the cylinder's radius if any.
        void BuildPlane(const Vector3d& light, const Vector3d& direction, const std::vector<PhotonAimBound>& bounds,
                        const PhotonAimBound* aperture, double cylinderRadius)
        {
            parallel = true;
            centre = light;
            axis = direction;
            Frame(axis, s, t);
            struct Rect { double a0, b0, a1, b1; };
            const auto shadow = [&](const PhotonAimBound& b) {
                if (b.sphereRadius > 0.0)
                {
                    const double a = dot(b.sphereCentre - light, s), c = dot(b.sphereCentre - light, t);
                    return Rect{a - b.sphereRadius, c - b.sphereRadius, a + b.sphereRadius, c + b.sphereRadius};
                }
                Rect r{HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL};
                for (int k = 0; k < 8; ++k)
                {
                    const Vector3d p((k & 1) ? b.high[X] : b.low[X], (k & 2) ? b.high[Y] : b.low[Y], (k & 4) ? b.high[Z] : b.low[Z]);
                    const double a = dot(p - light, s), c = dot(p - light, t);
                    r.a0 = std::min(r.a0, a); r.a1 = std::max(r.a1, a);
                    r.b0 = std::min(r.b0, c); r.b1 = std::max(r.b1, c);
                }
                return r;
            };
            std::vector<Rect> rects;
            Rect all{HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL};
            for (const auto& b : bounds)
            {
                rects.push_back(shadow(b));
                all.a0 = std::min(all.a0, rects.back().a0); all.a1 = std::max(all.a1, rects.back().a1);
                all.b0 = std::min(all.b0, rects.back().b0); all.b1 = std::max(all.b1, rects.back().b1);
            }
            const auto clip = [&all](const Rect& r) {
                all.a0 = std::max(all.a0, r.a0); all.a1 = std::min(all.a1, r.a1);
                all.b0 = std::max(all.b0, r.b0); all.b1 = std::min(all.b1, r.b1);
            };
            if (aperture)
                clip(shadow(*aperture));
            if (cylinderRadius > 0.0)
                clip(Rect{-cylinderRadius, -cylinderRadius, cylinderRadius, cylinderRadius});
            u0 = all.a0;
            v0 = all.b0;
            du = std::max(1e-9, all.a1 - all.a0);
            dv = std::max(1e-9, all.b1 - all.b0);
            Reset(bounds);
            for (size_t k = 0; k < rects.size() && all.a1 > all.a0 && all.b1 > all.b0; ++k)
            {
                const Rect& r = rects[k];
                const int i0 = std::max(0, int(std::floor((r.a0 - u0) / du * cells))), i1 = std::min(int(cells) - 1, int(std::floor((r.a1 - u0) / du * cells)));
                const int j0 = std::max(0, int(std::floor((r.b0 - v0) / dv * cells))), j1 = std::min(int(cells) - 1, int(std::floor((r.b1 - v0) / dv * cells)));
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const double a = u0 + (i + 0.5) * du / cells, c = v0 + (j + 0.5) * dv / cells;
                        if (cylinderRadius > 0.0 && std::sqrt(a * a + c * c) - 0.5 * std::sqrt(du * du + dv * dv) / cells > cylinderRadius)
                            continue;
                        Cover(j * cells + i, bounds[k].weight, bounds[k].target);
                    }
            }
            Finish();
        }

    private:
        /// A bound as seen from the light: the cone of directions holding it, and the box itself when it is not a sphere.
        struct Cone final
        {
            Vector3d direction;
            double angle;
            const PhotonAimBound* box;
            double weight;
            std::uint32_t target;
        };

        std::vector<double> targetWeight, targetSize, rowCdf, colCdf;
        std::vector<std::vector<std::uint32_t>> targetCells;

        static Cone Seen(const PhotonAimBound& b, const Vector3d& light, double lightRadius)
        {
            const Vector3d delta = b.Centre() - light;
            const double d = delta.length(), r = b.Radius() + lightRadius;
            if (d <= r || !std::isfinite(r))
                return Cone{Vector3d(0, 0, 1), M_PI, nullptr, b.weight, b.target};
            return Cone{delta / d, std::asin(r / d), b.sphereRadius > 0.0 ? nullptr : &b, b.weight, b.target};
        }

        /// The cone about the centre of the square [x0, x1] x [y0, y1] holding its directions, with a margin for curved edges.
        Vector3d CellCone(double x0, double y0, double x1, double y1, double& radius) const
        {
            const Vector3d c = Direction(0.5 * (x0 + x1), 0.5 * (y0 + y1));
            double widest = 0.0;
            for (int k = 0; k <= 2; ++k)
                for (int l = 0; l <= 2; ++l)
                    if (k != 1 || l != 1)
                        widest = std::max(widest, std::acos(std::max(-1.0, std::min(1.0, dot(c, Direction(x0 + (x1 - x0) * k / 2.0, y0 + (y1 - y0) * l / 2.0))))));
            radius = 1.25 * widest + 1e-6;
            return c;
        }

        /// Whether a ray from within lightRadius of light, within radius of direction c, can meet the bound.
        static bool Reaches(const Vector3d& c, double radius, const Cone& cone, const Vector3d& light, double lightRadius)
        {
            if (cone.angle >= M_PI || radius + cone.angle >= M_PI)
                return true;
            if (dot(c, cone.direction) < std::cos(radius + cone.angle))
                return false;
            if (!cone.box || radius >= M_PI / 2.0)
                return true;
            // a point of the box within radius of c lies within this distance of the ray along c
            double far = 0.0;
            for (int k = 0; k < 8; ++k)
                far = std::max(far, (Vector3d((k & 1) ? cone.box->high[X] : cone.box->low[X], (k & 2) ? cone.box->high[Y] : cone.box->low[Y],
                                              (k & 4) ? cone.box->high[Z] : cone.box->low[Z]) - light).length());
            const double grow = (far + lightRadius) * std::sin(radius) + lightRadius;
            double t0 = -lightRadius, t1 = HUGE_VAL;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double lo = cone.box->low[axis] - grow - light[axis], hi = cone.box->high[axis] + grow - light[axis];
                if (std::abs(c[axis]) < 1e-12)
                {
                    if (lo > 0.0 || hi < 0.0)
                        return false;
                    continue;
                }
                const double a = lo / c[axis], b = hi / c[axis];
                t0 = std::max(t0, std::min(a, b));
                t1 = std::min(t1, std::max(a, b));
                if (t0 > t1)
                    return false;
            }
            return true;
        }

        void Reset(const std::vector<PhotonAimBound>& bounds)
        {
            weight.assign(cells * cells, 0.0f);
            owner.assign(cells * cells, 0);
            targetWeight.clear();
            std::vector<Vector3d> low, high;
            for (const auto& b : bounds)
            {
                if (b.target >= targetWeight.size())
                {
                    targetWeight.resize(b.target + 1, 0.0);
                    low.resize(b.target + 1, Vector3d(HUGE_VAL));
                    high.resize(b.target + 1, Vector3d(-HUGE_VAL));
                }
                targetWeight[b.target] = std::max(targetWeight[b.target], b.weight);
                low[b.target] = Vector3d(std::min(low[b.target][X], b.low[X]), std::min(low[b.target][Y], b.low[Y]), std::min(low[b.target][Z], b.low[Z]));
                high[b.target] = Vector3d(std::max(high[b.target][X], b.high[X]), std::max(high[b.target][Y], b.high[Y]), std::max(high[b.target][Z], b.high[Z]));
            }
            targetSize.assign(targetWeight.size(), 0.0);
            for (size_t k = 0; k < targetSize.size(); ++k)
                targetSize[k] = (high[k] - low[k]).length();
            targetCells.assign(targetWeight.size(), std::vector<std::uint32_t>());
        }

        void Cover(std::uint32_t cell, double w, std::uint32_t target)
        {
            // equal weights go to the smaller target, so a cell's target does not depend on the order of targets
            if (float(w) > weight[cell] || (float(w) == weight[cell] && targetSize[target] < targetSize[owner[cell]]))
            {
                weight[cell] = float(w);
                owner[cell] = target;
            }
            auto& list = targetCells[target];
            if (list.empty() || list.back() != cell)
                list.push_back(cell);
        }

        /// The cell masses of both halves and the cumulative tables that sample them.
        void Finish()
        {
            double sum = 0.0, spread = 0.0;
            for (size_t k = 0; k < targetCells.size(); ++k)
            {
                std::sort(targetCells[k].begin(), targetCells[k].end());
                targetCells[k].erase(std::unique(targetCells[k].begin(), targetCells[k].end()), targetCells[k].end());
                sum += targetCells[k].empty() ? 0.0 : targetWeight[k];
            }
            for (float w : weight)
                spread += w;
            mass.assign(cells * cells, 0.0);
            rowCdf.assign(cells + 1, 0.0);
            colCdf.assign(cells * (cells + 1), 0.0);
            if (sum > 0.0 && spread > 0.0)
            {
                for (std::uint32_t c = 0; c < cells * cells; ++c)
                    mass[c] = (1.0 - targetShare) * weight[c] / spread;
                for (size_t k = 0; k < targetCells.size(); ++k)
                    for (std::uint32_t c : targetCells[k])
                        mass[c] += targetShare * targetWeight[k] / (sum * targetCells[k].size());
            }
            for (unsigned int j = 0; j < cells; ++j)
            {
                double row = 0.0;
                for (unsigned int i = 0; i < cells; ++i)
                    colCdf[j * (cells + 1) + i + 1] = row += mass[j * cells + i];
                rowCdf[j + 1] = rowCdf[j] + row;
            }
            targetCells = std::vector<std::vector<std::uint32_t>>();
        }
};

}

#endif
